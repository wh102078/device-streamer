#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// H.264 编码器（libx264）：输入 AVFrame，输出 AVPacket。
// send/receive 分离：encode() 送帧，next_packet() 取包，flush() 结束输入。
class H264Encoder {
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    void open(int width, int height, int fps);

    // 转换像素格式并送进编码器。
    // timestamp_us：这一帧的采集时间（微秒），用来生成与真实时间一致的 PTS。
    bool encode(const AVFrame* frame, uint64_t timestamp_us);

    // 取出一个编码包（调用方用 av_packet_alloc 分配）。
    // 返回 false 表示暂时没有更多包（EAGAIN）或已结束（EOF）。
    bool next_packet(AVPacket* pkt);

    // 输入结束后调用：送 NULL 触发 flush，让内部缓存的帧吐出来。
    void flush();

    // 请求下一帧编码成 I 帧（关键帧）。
    // 断线重连后调用：新会话里客户端解码器是空的，必须从 I 帧开始。
    void request_keyframe() { force_keyframe_ = true; }

    // 给 muxer 用：拿编码器的宽高、帧率、extradata（SPS/PPS）等参数
    AVCodecContext* context() const { return ctx_; }

    unsigned packets() const { return packets_; }
    unsigned keyframes() const { return keyframes_; }

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* yuv420p_ = nullptr;
    SwsContext* sws_ = nullptr;
    int64_t first_ts_us_ = -1;  // 第一帧的采集时间，用作 PTS 基准
    int64_t last_pts_ = -1;     // 上一帧的 PTS，保证单调递增
    bool force_keyframe_ = false;
    unsigned packets_ = 0;
    unsigned keyframes_ = 0;
};
