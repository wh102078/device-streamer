// device_streamer：采集 -> 解码 -> 编码 -> 推流 + 录像。
// 用法：./device_streamer [rtsp_url]；Ctrl+C 退出。

#include <linux/videodev2.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

#include "capture/capture_worker.h"
#include "common/config.h"
#include "common/debug_output.h"
#include "queue/frame_queue.h"
#include "codec/h264_encoder.h"
#include "codec/mjpeg_decoder.h"
#include "common/packet_dispatch.h"
#include "net/push_sink.h"
#include "record/record_sink.h"
#include "common/stop_flag.h"
#include "capture/v4l2_capture.h"

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

    PushSink push(url, encoder.context(), config::kPushQueue);
    if (!push.start()) {
        std::fprintf(stderr, "failed to connect to RTSP server\n");
        return EXIT_FAILURE;
    }

    AVCodecParameters* params = avcodec_parameters_alloc();
    if (params == nullptr ||
        avcodec_parameters_from_context(params, encoder.context()) < 0) {
        std::fprintf(stderr, "failed to build codec parameters\n");
        return EXIT_FAILURE;
    }

    RecordSink record(config::kRecordPath, params, encoder.context()->time_base,
                      config::kRecordQueue);
    std::string record_error;
    const bool recording = record.start(&record_error);
    avcodec_parameters_free(&params);

    if (recording) {
        std::printf("recording: %s\n", config::kRecordPath);
    } else {
        std::fprintf(stderr, "warning: recording disabled: %s\n", record_error.c_str());
    }

    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        std::fprintf(stderr, "av_packet_alloc failed\n");
        return EXIT_FAILURE;
    }

    LatestFrameQueue queue(config::kQueueCapacity);
    CaptureWorker capture_worker(capture, queue);
    capture_worker.start();

    const auto start_time = std::chrono::steady_clock::now();
    auto last_report = start_time;
    unsigned frames = 0;
    bool first_packet_reported = false;

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

        if (push.take_keyframe_request()) {
            encoder.request_keyframe();
        }

        encoder.encode(frame, q.timestamp_us);

        while (encoder.next_packet(pkt)) {
            dispatch_packet(pkt, push, recording ? &record : nullptr);
        }

        ++frames;

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds(1)) {
            const double elapsed = std::chrono::duration<double>(now - start_time).count();
            const double kbps = elapsed > 0 ? push.bytes() * 8.0 / elapsed / 1000.0 : 0.0;
            if (!first_packet_reported && push.packets() > 0) {
                first_packet_reported = true;
                std::printf("first packet: %.0f ms\n",
                            std::chrono::duration<double, std::milli>(now - start_time).count());
            }
            std::printf("stats: %u frames, %.1f fps, push %llu pkts/%.0f kbps, "
                        "record %u frames, capture_drop %u, frame_queue_drop %u, "
                        "push_queue_drop %u, reconnects %u\n",
                        frames, frames / elapsed,
                        static_cast<unsigned long long>(push.packets()), kbps,
                        record.frames(), capture.rejected(), queue.dropped(),
                        push.dropped(), push.reconnects());
            last_report = now;
        }
    }

    std::printf("\nstopping...\n");
    capture_worker.stop();

    encoder.flush();
    while (encoder.next_packet(pkt)) {
        dispatch_packet(pkt, push, recording ? &record : nullptr);
    }

    push.stop();
    if (recording) {
        record.stop();
    }

    const auto end_time = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end_time - start_time).count();
    const double avg_kbps = seconds > 0 ? push.bytes() * 8.0 / seconds / 1000.0 : 0.0;
    std::printf("summary: %u frames, %.1f fps, push %llu pkts (%.0f kbps, reconnects %u, "
                "last recovery %.0f ms), record %u frames/%llu bytes, "
                "capture_drop %u, frame_queue_drop %u, push_queue_drop %u, %.1fs\n",
                frames, seconds > 0 ? frames / seconds : 0.0,
                static_cast<unsigned long long>(push.packets()), avg_kbps,
                push.reconnects(), push.last_recovery_ms(),
                record.frames(), static_cast<unsigned long long>(record.bytes()),
                capture.rejected(), queue.dropped(), push.dropped(), seconds);

    av_packet_free(&pkt);
    av_frame_free(&frame);
    return 0;
}
