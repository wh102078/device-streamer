#pragma once

#include <cstddef>

extern "C" {
#include <libavcodec/avcodec.h>
}

// MJPEG 解码器：一包 JPEG 字节 → 一个 AVFrame（这台相机输出 yuvj422p）。
// 解码器实例跨帧复用，不要每帧新建。
class MJPEGDecoder {
public:
    MJPEGDecoder();
    ~MJPEGDecoder();
    MJPEGDecoder(const MJPEGDecoder&) = delete;
    MJPEGDecoder& operator=(const MJPEGDecoder&) = delete;

    // 解码一帧。成功时 frame 里是解码结果；调用方负责 av_frame_alloc/free。
    bool decode(const void* data, size_t size, AVFrame* frame);

private:
    AVCodecContext* ctx_ = nullptr;
};
