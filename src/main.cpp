// device_streamer：采集(MJPEG) → 解码(yuvj422p) → 编码(H.264) 的最小闭环。
// 抓 10 帧，把每层的中间结果存成文件，方便逐层验证。
// 这一步只跑 10 帧；持续采集 + 推流在第 4 步做。

#include <linux/videodev2.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "common.h"
#include "h264_encoder.h"
#include "mjpeg_decoder.h"
#include "v4l2_capture.h"

namespace {

constexpr const char* kDevice = "/dev/video0";
constexpr unsigned kWidth = 640;
constexpr unsigned kHeight = 480;
constexpr unsigned kFps = 30;
constexpr unsigned kFrameCount = 10;

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

// 把解码后的 AVFrame（YUV 三平面）转 RGB 存成 PPM，用来肉眼验证解码结果。
// 这不是管线必经环节，只是调试输出。
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

int main() {
    // 1) 采集
    V4L2Capture cap;
    cap.open(kDevice, kWidth, kHeight, V4L2_PIX_FMT_MJPEG);
    cap.start();

    // 2) 解码 / 编码
    MJPEGDecoder decoder;

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        std::fprintf(stderr, "error: av_frame_alloc 失败\n");
        return EXIT_FAILURE;
    }

    H264Encoder encoder;
    encoder.open(static_cast<int>(kWidth), static_cast<int>(kHeight), static_cast<int>(kFps));

    std::FILE* h264 = std::fopen("out/output.h264", "wb");
    if (h264 == nullptr) {
        std::fprintf(stderr, "fopen(out/output.h264) failed: %s\n", std::strerror(errno));
        return EXIT_FAILURE;
    }

    // 3) 主循环：抓一帧 → 存原始 jpg → 解码 → 存 ppm → 编码 → 还缓冲
    auto start_time = std::chrono::steady_clock::now();
    unsigned n = 0;
    while (n < kFrameCount) {
        CapturedFrame f;
        if (!cap.grab(&f)) {
            break;  // 连续取不到有效帧，退出
        }

        char jpg_path[64];
        std::snprintf(jpg_path, sizeof(jpg_path), "out/frame_%02u.jpg", n);
        write_file(jpg_path, f.data, f.size);

        if (decoder.decode(f.data, f.size, frame)) {
            if (n == 0) {
                std::printf("decode : %dx%d 像素格式=%s\n", frame->width, frame->height,
                            av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
            }
            char ppm_path[64];
            std::snprintf(ppm_path, sizeof(ppm_path), "out/frame_%02u.ppm", n);
            save_ppm(ppm_path, frame);

            encoder.encode(frame, h264);
        } else {
            std::printf("         !! 解码失败（第 %u 帧）\n", n);
        }

        std::printf("frame %2u  seq=%u  size=%zu\n", n, f.sequence, f.size);

        cap.release();  // 缓冲还回驱动
        ++n;
    }

    // 4) 收尾
    encoder.flush(h264);
    std::fclose(h264);

    auto end_time = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();
    std::printf("\n完成: %u 帧, 丢弃 %u 坏帧, 编码 %u 包(%u 关键帧), 耗时 %.3fs\n",
                n, cap.rejected(), encoder.packets(), encoder.keyframes(), seconds);
    std::printf("输出: out/frame_*.jpg, out/frame_*.ppm, out/output.h264\n");

    av_frame_free(&frame);
    return 0;
}
