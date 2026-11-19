#pragma once

#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 用 FFmpeg 的 RTSP muxer 把 H.264 编码包推给 mediamtx。
// 这里不碰 RTSP/RTP 协议细节，握手和打包都由 muxer 完成。
//
// 失败不退出进程：open/write 返回 false，由调用方决定重连还是放弃。
class RtspPusher {
public:
    RtspPusher() = default;
    ~RtspPusher();
    RtspPusher(const RtspPusher&) = delete;
    RtspPusher& operator=(const RtspPusher&) = delete;

    // 连接服务器、写 SDP（ANNOUNCE/SETUP），enc 提供宽高和 SPS/PPS。
    // 返回 false 表示连接失败（调用方可以稍后重试 open）。
    bool open(const char* url, const AVCodecContext* enc);

    // 发送一个编码包。返回 false 表示发送失败，会话已不可用。
    // 注意：无论成功失败，pkt 的数据都会被释放。
    bool write(AVPacket* pkt);

    // 断线后指数退避重连：1s → 2s → 4s … 上限 30s。
    // stop_requested 返回 true 时中止；attempts 累加尝试次数；
    // recovery_ms 返回从开始重连到成功的耗时（可为 nullptr）。
    bool reconnect(const char* url, const AVCodecContext* enc,
                   const std::function<bool()>& stop_requested,
                   unsigned* attempts, double* recovery_ms);

    // 发 TEARDOWN 并释放 muxer（析构时自动调用，可重复调用）。
    void close();

private:
    AVFormatContext* fmt_ = nullptr;
    AVStream* stream_ = nullptr;
    AVRational enc_time_base_{1, 1};
    AVRational stream_time_base_{1, 1};
};
