#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class H264Encoder {
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    void open(int width, int height, int fps, int bitrate, unsigned gop);

    bool encode(const AVFrame* frame, uint64_t timestamp_us);
    bool next_packet(AVPacket* pkt);
    void flush();

    // 下一帧强制为 I 帧（重连后使用）
    void request_keyframe() { force_keyframe_ = true; }

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
