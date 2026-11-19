#pragma once

#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class RtspPusher {
public:
    RtspPusher() = default;
    ~RtspPusher();
    RtspPusher(const RtspPusher&) = delete;
    RtspPusher& operator=(const RtspPusher&) = delete;

    bool open(const char* url, const AVCodecContext* enc);

    // 返回 false 表示会话已不可用；pkt 的数据会被释放。
    bool write(AVPacket* pkt);

    // 指数退避重连：1s -> 2s -> 4s ... 上限 30s。recovery_ms 可为 nullptr。
    bool reconnect(const char* url, const AVCodecContext* enc,
                   const std::function<bool()>& stop_requested,
                   unsigned* attempts, double* recovery_ms);

    void close();

private:
    AVFormatContext* fmt_ = nullptr;
    AVStream* stream_ = nullptr;
    AVRational enc_time_base_{1, 1};
    AVRational stream_time_base_{1, 1};
};
