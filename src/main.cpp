// device_streamer：采集(MJPEG) → 解码 → 编码(H.264) → RTSP 推流。
// 用法：./device_streamer [rtsp_url]；Ctrl+C 退出。

#include <linux/videodev2.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

#include "config.h"
#include "debug_output.h"
#include "frame_queue.h"
#include "h264_encoder.h"
#include "mjpeg_decoder.h"
#include "rtsp_pusher.h"
#include "stop_flag.h"
#include "v4l2_capture.h"

int main(int argc, char** argv) {
    const char* url = (argc > 1) ? argv[1] : config::kDefaultUrl;
    stopflag::install();

    V4L2Capture capture;
    if (!capture.open(config::kDevice, config::kWidth, config::kHeight,
                      V4L2_PIX_FMT_MJPEG, config::kV4l2Buffers)) {
        std::fprintf(stderr, "failed to open capture device\n");
        return EXIT_FAILURE;
    }
    if (!capture.start()) {
        std::fprintf(stderr, "failed to start capture\n");
        return EXIT_FAILURE;
    }

    MJPEGDecoder decoder;
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        std::fprintf(stderr, "av_frame_alloc failed\n");
        return EXIT_FAILURE;
    }

    H264Encoder encoder;
    encoder.open(static_cast<int>(config::kWidth), static_cast<int>(config::kHeight),
                 static_cast<int>(config::kFps), config::kBitrate, config::kGop);

    RtspPusher pusher;
    if (!pusher.open(url, encoder.context())) {
        std::fprintf(stderr, "failed to connect to RTSP server\n");
        return EXIT_FAILURE;
    }

    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        std::fprintf(stderr, "av_packet_alloc failed\n");
        return EXIT_FAILURE;
    }

    // 采集线程：抓帧后拷贝进队列，立即归还驱动缓冲。
    LatestFrameQueue queue(config::kQueueCapacity);
    std::thread capture_thread([&capture, &queue]() {
        while (!stopflag::requested()) {
            CapturedFrame f;
            if (!capture.grab(&f)) {
                break;
            }

            QueuedFrame q;
            q.data.resize(f.size);
            std::memcpy(q.data.data(), f.data, f.size);
            q.sequence = f.sequence;
            q.timestamp_us = f.timestamp_us;

            queue.push(std::move(q));
            capture.release();
        }
        queue.stop();
    });

    auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;
    unsigned frames = 0;
    unsigned long long bytes_sent = 0;
    bool first_packet_sent = false;
    unsigned reconnects = 0;
    double last_recovery_ms = 0;

    std::printf("\nstreaming (Ctrl+C to stop)\n\n");

    while (!stopflag::requested()) {
        QueuedFrame q;
        if (!queue.pop(&q, 50)) {
            if (queue.stopped()) {
                break;
            }
            continue;
        }

        if (!decoder.decode(q.data.data(), q.data.size(), frame)) {
            continue;
        }

        if (frames == 0) {
            debug_output::write_file("out/frame_00.jpg", q.data.data(), q.data.size());
            debug_output::save_ppm("out/frame_00.ppm", frame);
            std::printf("decode: %dx%d %s\n", frame->width, frame->height,
                        av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
        }

        encoder.encode(frame, q.timestamp_us);

        while (encoder.next_packet(packet)) {
            const int packet_size = packet->size;
            if (!pusher.write(packet)) {
                av_packet_unref(packet);
                if (pusher.reconnect(url, encoder.context(),
                                     [] { return stopflag::requested(); },
                                     &reconnects, &last_recovery_ms)) {
                    encoder.request_keyframe();
                }
                break;
            }

            bytes_sent += static_cast<unsigned long long>(packet_size);
            if (!first_packet_sent) {
                first_packet_sent = true;
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start_time).count();
                std::printf("first packet: %.0f ms\n", ms);
            }
            av_packet_unref(packet);
        }

        ++frames;

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            const double elapsed = std::chrono::duration<double>(now - start_time).count();
            const double kbps = elapsed > 0 ? bytes_sent * 8.0 / elapsed / 1000.0 : 0.0;
            std::printf("stats: %u frames, %.1f fps, %u packets, %u keyframes, %.0f kbps, "
                        "capture_drop %u, queue_drop %u, backlog %zu, reconnects %u\n",
                        frames, frames / elapsed, encoder.packets(), encoder.keyframes(),
                        kbps, capture.rejected(), queue.dropped(), queue.size(), reconnects);
            last_report = now;
        }
    }

    std::printf("\nstopping...\n");
    if (capture_thread.joinable()) {
        capture_thread.join();
    }
    encoder.flush();
    while (encoder.next_packet(packet)) {
        pusher.write(packet);
        av_packet_unref(packet);
    }
    pusher.close();

    const auto end_time = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end_time - start_time).count();
    const double avg_kbps = seconds > 0 ? bytes_sent * 8.0 / seconds / 1000.0 : 0.0;
    std::printf("summary: %u frames, %.1f fps, %u packets (%u keyframes), "
                "avg %.0f kbps, capture_drop %u, queue_drop %u, "
                "reconnects %u (last recovery %.0f ms), %.1fs\n",
                frames, seconds > 0 ? frames / seconds : 0.0, encoder.packets(),
                encoder.keyframes(), avg_kbps, capture.rejected(), queue.dropped(),
                reconnects, last_recovery_ms, seconds);

    av_packet_free(&packet);
    av_frame_free(&frame);
    return 0;
}
