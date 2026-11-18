// device_streamer：采集(MJPEG) → 解码 → 编码(H.264) → RTSP 推流到 mediamtx。
// 用法：
//   ./device_streamer [rtsp_url]
//   默认 rtsp://127.0.0.1:8554/dev；Ctrl+C 优雅退出。

#include <linux/videodev2.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "common.h"
#include "frame_queue.h"
#include "h264_encoder.h"
#include "mjpeg_decoder.h"
#include "rtsp_pusher.h"
#include "v4l2_capture.h"

namespace {

constexpr const char* kDevice = "/dev/video0";
constexpr const char* kDefaultUrl = "rtsp://127.0.0.1:8554/dev";
constexpr unsigned kWidth = 640;
constexpr unsigned kHeight = 480;
constexpr unsigned kFps = 30;

// 信号处理函数里只能访问 sig_atomic_t 这类最简单的东西，不能调 printf/malloc
volatile sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

void write_file(const char* path, const void* data, size_t size) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "fopen(%s) failed: %s\n", path, std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    if (std::fwrite(data, 1, size, f) != size) {
        std::fclose(f);
        ds::die("fwrite");
    }
    std::fclose(f);
}

// 推流断开后的重连：指数退避 1s → 2s → 4s … 上限 30s。
// 等待期间按 100ms 分片睡眠，保证 Ctrl+C 能及时生效。
// 返回 false 表示用户要求停止。
bool reconnect_pusher(RtspPusher& pusher, const char* url, AVCodecContext* enc,
                      unsigned* attempts) {
    unsigned delay_ms = 1000;
    while (!g_stop) {
        ++*attempts;
        std::fprintf(stderr, "推流断开，%u ms 后重连（第 %u 次）\n", delay_ms, *attempts);
        for (unsigned waited = 0; waited < delay_ms && !g_stop; waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (g_stop) {
            break;
        }
        if (pusher.open(url, enc)) {
            std::fprintf(stderr, "推流重连成功\n");
            return true;
        }
        delay_ms = (delay_ms * 2 > 30000) ? 30000 : delay_ms * 2;
    }
    return false;
}

// 调试用：把解码后的第一帧转 RGB 存成 PPM，方便肉眼确认画面
void save_ppm(const char* path, const AVFrame* frame) {
    SwsContext* sws = sws_getContext(frame->width, frame->height,
                                     static_cast<AVPixelFormat>(frame->format),
                                     frame->width, frame->height,
                                     AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        std::fprintf(stderr, "error: sws_getContext(ppm) 失败\n");
        std::exit(EXIT_FAILURE);
    }

    const unsigned w = static_cast<unsigned>(frame->width);
    const unsigned h = static_cast<unsigned>(frame->height);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);

    uint8_t* dst_data[1] = {rgb.data()};
    int dst_linesize[1] = {static_cast<int>(w * 3)};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
    sws_freeContext(sws);

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "fopen(%s) failed: %s\n", path, std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    const char* url = (argc > 1) ? argv[1] : kDefaultUrl;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    // 1) 采集
    V4L2Capture cap;
    cap.open(kDevice, kWidth, kHeight, V4L2_PIX_FMT_MJPEG);
    cap.start();

    // 2) 解码 / 编码 / 推流
    MJPEGDecoder decoder;

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        std::fprintf(stderr, "error: av_frame_alloc 失败\n");
        return EXIT_FAILURE;
    }

    H264Encoder encoder;
    encoder.open(static_cast<int>(kWidth), static_cast<int>(kHeight), static_cast<int>(kFps));

    RtspPusher pusher;
    unsigned reconnects = 0;
    if (!pusher.open(url, encoder.context())) {
        std::fprintf(stderr, "error: 初始连接推流服务器失败（服务器是否已启动？）\n");
        return EXIT_FAILURE;
    }

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        std::fprintf(stderr, "error: av_packet_alloc 失败\n");
        return EXIT_FAILURE;
    }

    // 3) 采集线程：只管抓帧，放进队列
    //    队列容量 3，满了丢最旧。这样编码/网络变慢时，采集线程不会被拖住。
    LatestFrameQueue queue(3);
    std::thread capture_thread([&cap, &queue]() {
        while (!g_stop) {
            CapturedFrame f;
            if (!cap.grab(&f)) {
                break;  // 连续取不到有效帧
            }

            // V4L2 的 mmap 缓冲 release 后随时会被驱动覆盖，
            // 所以跨线程传递前必须拷贝一份（一帧约 45KB，开销可忽略）。
            QueuedFrame q;
            q.data.resize(f.size);
            std::memcpy(q.data.data(), f.data, f.size);
            q.sequence = f.sequence;

            queue.push(std::move(q));
            cap.release();  // 数据已拷贝，立刻把缓冲还给驱动
        }

        // 采集线程结束（Ctrl+C 或设备异常）时终止队列，唤醒主线程；
        // 否则主线程只会在 pop 超时后继续空转，进程退不出去
        queue.stop();
    });

    // 4) 主循环：从队列取帧 → 解码 → 编码 → 推流
    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;
    unsigned frames = 0;

    std::printf("\n开始推流（Ctrl+C 退出）\n\n");

    while (!g_stop) {
        QueuedFrame q;
        if (!queue.pop(&q, 50)) {
            if (queue.stopped()) {
                break;  // 采集线程已结束，没有更多帧
            }
            continue;  // 只是暂时为空，回去检查 g_stop
        }

        if (!decoder.decode(q.data.data(), q.data.size(), frame)) {
            continue;
        }

        // 只保存第一帧，供肉眼验证；持续推流时不再逐帧写文件
        if (frames == 0) {
            write_file("out/frame_00.jpg", q.data.data(), q.data.size());
            save_ppm("out/frame_00.ppm", frame);
            std::printf("decode : %dx%d 像素格式=%s\n", frame->width, frame->height,
                        av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
        }

        encoder.encode(frame);

        // 编码器有延迟：一次 send 可能出 0 个、1 个或多个包，全部推出去
        while (encoder.next_packet(pkt)) {
            if (!pusher.write(pkt)) {
                av_packet_unref(pkt);
                // 网络断开或服务器重启：重连期间这一帧丢弃，
                // 采集线程继续工作，队列满了会丢旧帧，保证画面实时性
                if (reconnect_pusher(pusher, url, encoder.context(), &reconnects)) {
                    // 新会话里客户端解码器是空的，下一帧强制 I 帧，
                    // 否则要等下一个自然 GOP（最多 1 秒）才恢复画面
                    encoder.request_keyframe();
                }
                break;
            }
            av_packet_unref(pkt);
        }

        ++frames;

        // 每秒打印一次状态
        auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            std::printf("streaming: %u 帧, %.1f fps, %u 包, %u 关键帧, "
                        "采集丢弃 %u, 队列丢弃 %u, 积压 %zu\n",
                        frames, frames / elapsed, encoder.packets(),
                        encoder.keyframes(), cap.rejected(), queue.dropped(),
                        queue.size());
            last_report = now;
        }
    }

    // 4) 收尾：先让采集线程退出并回收，再 flush 编码器，最后断开 RTSP
    std::printf("\n停止中...\n");
    if (capture_thread.joinable()) {
        capture_thread.join();
    }
    encoder.flush();
    while (encoder.next_packet(pkt)) {
        pusher.write(pkt);  // 收尾阶段发送失败就放弃，不再重连
        av_packet_unref(pkt);
    }
    pusher.close();

    auto end_time = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();
    std::printf("结束: %u 帧, %.1f fps, %u 包(%u 关键帧), 采集丢弃 %u, 队列丢弃 %u, "
                "重连 %u 次, 总时长 %.1fs\n",
                frames, seconds > 0 ? frames / seconds : 0.0, encoder.packets(),
                encoder.keyframes(), cap.rejected(), queue.dropped(), reconnects,
                seconds);

    av_packet_free(&pkt);
    av_frame_free(&frame);
    return 0;
}
