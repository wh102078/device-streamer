#include "rtsp_pusher.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include "common.h"

namespace {

void warnFf(const char* what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof(buf));
    std::fprintf(stderr, "pusher: %s failed: %s\n", what, buf);
}

}  // namespace

RtspPusher::~RtspPusher() {
    close();
}

bool RtspPusher::open(const char* url, const AVCodecContext* enc) {
    close();  // 支持重复调用（重连）

    int ret = avformat_alloc_output_context2(&fmt_, nullptr, "rtsp", url);
    if (ret < 0 || fmt_ == nullptr) {
        warnFf("avformat_alloc_output_context2", ret);
        fmt_ = nullptr;
        return false;
    }

    stream_ = avformat_new_stream(fmt_, nullptr);
    if (stream_ == nullptr) {
        std::fprintf(stderr, "pusher: avformat_new_stream 失败\n");
        close();
        return false;
    }
    ret = avcodec_parameters_from_context(stream_->codecpar, enc);
    if (ret < 0) {
        warnFf("avcodec_parameters_from_context", ret);
        close();
        return false;
    }

    stream_->time_base = enc->time_base;
    enc_time_base_ = enc->time_base;

    // RTP over TCP
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    ret = avformat_write_header(fmt_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        warnFf("avformat_write_header", ret);
        close();
        return false;
    }

    stream_time_base_ = stream_->time_base;
    std::printf("pusher : %s (tcp)\n", url);
    std::printf("         stream time_base=%d/%d\n",
                stream_time_base_.num, stream_time_base_.den);
    return true;
}

bool RtspPusher::write(AVPacket* pkt) {
    if (fmt_ == nullptr || stream_ == nullptr) {
        av_packet_unref(pkt);
        return false;
    }

    pkt->stream_index = stream_->index;
    av_packet_rescale_ts(pkt, enc_time_base_, stream_time_base_);

    // av_interleaved_write_frame 内部会 unref pkt
    int ret = av_interleaved_write_frame(fmt_, pkt);
    if (ret < 0) {
        warnFf("av_interleaved_write_frame", ret);
        return false;
    }
    return true;
}

bool RtspPusher::reconnect(const char* url, const AVCodecContext* enc,
                           const std::function<bool()>& stop_requested,
                           unsigned* attempts, double* recovery_ms) {
    const auto start = std::chrono::steady_clock::now();
    unsigned delay_ms = 1000;

    while (!stop_requested()) {
        ++*attempts;
        std::fprintf(stderr, "推流断开，%u ms 后重连（第 %u 次）\n", delay_ms, *attempts);

        // 分片睡眠，保证停止请求能及时生效
        for (unsigned waited = 0; waited < delay_ms; waited += 100) {
            if (stop_requested()) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (open(url, enc)) {
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (recovery_ms != nullptr) {
                *recovery_ms = ms;
            }
            std::fprintf(stderr, "推流重连成功，恢复耗时 %.0f ms\n", ms);
            return true;
        }

        delay_ms = (delay_ms * 2 > 30000) ? 30000 : delay_ms * 2;
    }
    return false;
}

void RtspPusher::close() {
    if (fmt_ == nullptr) {
        return;
    }
    av_write_trailer(fmt_);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    stream_ = nullptr;
}
