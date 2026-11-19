#pragma once

#include <cstddef>

extern "C" {
#include <libavcodec/avcodec.h>
}

class MJPEGDecoder {
public:
    MJPEGDecoder();
    ~MJPEGDecoder();
    MJPEGDecoder(const MJPEGDecoder&) = delete;
    MJPEGDecoder& operator=(const MJPEGDecoder&) = delete;

    // 调用方负责 AVFrame 的生命周期
    bool decode(const void* data, size_t size, AVFrame* frame);

private:
    AVCodecContext* ctx_ = nullptr;
};
