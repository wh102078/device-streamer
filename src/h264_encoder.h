#pragma once

#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// H.264 编码器（libx264）。
// 输入：任意像素格式的 AVFrame（内部用 sws 转成 yuv420p）。
// 输出：AVPacket，由调用方决定写文件还是推流。
//
// 用法（和 FFmpeg 本身一样，send/receive 分离）：
//   encoder.encode(frame);              // 送一帧
//   while (encoder.next_packet(&pkt))   // 把能取出的包取干
//       pusher.write(&pkt);
class H264Encoder {
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    void open(int width, int height, int fps);

    // 转换像素格式并送进编码器。成功返回 true。
    bool encode(const AVFrame* frame);

    // 取出一个编码包（调用方用 av_packet_alloc 分配）。
    // 返回 false 表示暂时没有更多包（EAGAIN）或已结束（EOF）。
    bool next_packet(AVPacket* pkt);

    // 输入结束后调用：送 NULL 触发 flush，让内部缓存的帧吐出来。
    void flush();

    // 给 muxer 用：拿编码器的宽高、帧率、extradata（SPS/PPS）等参数
    AVCodecContext* context() const { return ctx_; }

    unsigned packets() const { return packets_; }
    unsigned keyframes() const { return keyframes_; }

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* yuv420p_ = nullptr;
    SwsContext* sws_ = nullptr;
    int64_t next_pts_ = 0;
    unsigned packets_ = 0;
    unsigned keyframes_ = 0;
};
