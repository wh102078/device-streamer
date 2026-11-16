#pragma once

#include <cstdint>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// H.264 编码器（libx264）。
// 输入：任意像素格式的 AVFrame（内部用 sws 转成 yuv420p）。
// 输出：把编码后的 AVPacket 直接写进 FILE（裸 H.264 bitstream）。
class H264Encoder {
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    void open(int width, int height, int fps);

    // 编码一帧并写出所有可用包。成功返回 true。
    bool encode(const AVFrame* frame, std::FILE* out);

    // 输入结束后调用：把编码器内部缓存的帧全部吐出来，否则最后几帧会丢。
    void flush(std::FILE* out);

    unsigned packets() const { return packets_; }
    unsigned keyframes() const { return keyframes_; }

private:
    void drain(std::FILE* out);

    AVCodecContext* ctx_ = nullptr;
    AVFrame* yuv420p_ = nullptr;
    SwsContext* sws_ = nullptr;
    int64_t next_pts_ = 0;
    unsigned packets_ = 0;
    unsigned keyframes_ = 0;
};
