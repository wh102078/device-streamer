#include "mjpeg_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"

MJPEGDecoder::MJPEGDecoder() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (codec == nullptr) {
        std::fprintf(stderr, "error: 找不到 MJPEG 解码器\n");
        std::exit(EXIT_FAILURE);
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (ctx_ == nullptr) {
        std::fprintf(stderr, "error: avcodec_alloc_context3 失败\n");
        std::exit(EXIT_FAILURE);
    }

    // MJPEG 每帧独立，单线程就够
    ctx_->thread_count = 1;

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        ds::ff_die("avcodec_open2(decoder)", ret);
    }
}

MJPEGDecoder::~MJPEGDecoder() {
    if (ctx_ != nullptr) {
        avcodec_free_context(&ctx_);
    }
}

bool MJPEGDecoder::decode(const void* data, size_t size, AVFrame* frame) {
    // av_packet_from_data 会接管并释放传入内存，不能用于 mmap 缓冲；这里拷贝一份。
    AVPacket* pkt = av_packet_alloc();
    if (pkt == nullptr) {
        return false;
    }
    if (av_new_packet(pkt, static_cast<int>(size)) < 0) {
        av_packet_free(&pkt);
        return false;
    }
    std::memcpy(pkt->data, data, size);

    int ret = avcodec_send_packet(ctx_, pkt);
    av_packet_free(&pkt);
    if (ret < 0) {
        return false;
    }

    ret = avcodec_receive_frame(ctx_, frame);
    return ret == 0;
}
