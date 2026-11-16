#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 用 FFmpeg 的 RTSP muxer 把 H.264 编码包推给 mediamtx。
// 这里不碰 RTSP/RTP 协议细节，握手和打包都由 muxer 完成。
class RtspPusher {
public:
    RtspPusher() = default;
    ~RtspPusher();
    RtspPusher(const RtspPusher&) = delete;
    RtspPusher& operator=(const RtspPusher&) = delete;

    // 连接服务器、写 SDP（ANNOUNCE/SETUP），enc 提供宽高和 SPS/PPS
    void open(const char* url, const AVCodecContext* enc);

    // 发送一个编码包。注意：内部会接管并释放 pkt 的数据。
    void write(AVPacket* pkt);

    // 发 TEARDOWN 并释放 muxer（析构时自动调用）
    void close();

private:
    AVFormatContext* fmt_ = nullptr;
    AVStream* stream_ = nullptr;
    AVRational enc_time_base_{1, 1};
    AVRational stream_time_base_{1, 1};
};
