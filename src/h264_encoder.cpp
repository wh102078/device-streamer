#include "h264_encoder.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
}

#include "common.h"

H264Encoder::~H264Encoder() {
    if (sws_ != nullptr) {
        sws_freeContext(sws_);
    }
    if (yuv420p_ != nullptr) {
        av_frame_free(&yuv420p_);
    }
    if (ctx_ != nullptr) {
        avcodec_free_context(&ctx_);
    }
}

void H264Encoder::open(int width, int height, int fps, int bitrate, unsigned gop) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        std::fprintf(stderr, "error: 找不到 H.264 编码器\n");
        std::exit(EXIT_FAILURE);
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (ctx_ == nullptr) {
        std::fprintf(stderr, "error: avcodec_alloc_context3(encoder) 失败\n");
        std::exit(EXIT_FAILURE);
    }

    ctx_->width = width;
    ctx_->height = height;
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx_->time_base = AVRational{1, fps};
    ctx_->framerate = AVRational{fps, 1};
    ctx_->bit_rate = bitrate;
    ctx_->gop_size = (gop == 0) ? fps : static_cast<int>(gop);
    ctx_->max_b_frames = 0;
    ctx_->thread_count = 1;

    // zerolatency：关闭 x264 预读缓冲以降低编码延迟，代价是压缩率略低。
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "tune", "zerolatency", 0);

    int ret = avcodec_open2(ctx_, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        ds::ff_die("avcodec_open2(encoder)", ret);
    }

    yuv420p_ = av_frame_alloc();
    if (yuv420p_ == nullptr) {
        std::fprintf(stderr, "error: av_frame_alloc(yuv420p) 失败\n");
        std::exit(EXIT_FAILURE);
    }
    yuv420p_->format = AV_PIX_FMT_YUV420P;
    yuv420p_->width = width;
    yuv420p_->height = height;
    if (av_frame_get_buffer(yuv420p_, 32) < 0) {
        std::fprintf(stderr, "error: av_frame_get_buffer 失败\n");
        std::exit(EXIT_FAILURE);
    }

    std::printf("encoder: libx264 %dx%d yuv420p @%dfps %d bps gop=%d\n",
                width, height, fps, bitrate, ctx_->gop_size);
}

bool H264Encoder::encode(const AVFrame* frame, uint64_t timestamp_us) {
    // 第一帧解码后才知道源格式
    if (sws_ == nullptr) {
        sws_ = sws_getContext(frame->width, frame->height,
                              static_cast<AVPixelFormat>(frame->format),
                              frame->width, frame->height,
                              AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws_ == nullptr) {
            std::fprintf(stderr, "error: sws_getContext 失败\n");
            return false;
        }
        // full range -> limited range
        sws_setColorspaceDetails(sws_,
                                 sws_getCoefficients(SWS_CS_ITU601), 1,
                                 sws_getCoefficients(SWS_CS_ITU601), 0,
                                 0, 1 << 16, 1 << 16);
    }

    if (av_frame_make_writable(yuv420p_) < 0) {
        return false;
    }
    sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
              yuv420p_->data, yuv420p_->linesize);

    // PTS 按采集时间换算，相机变帧率时播放速度才正确
    if (first_ts_us_ < 0) {
        first_ts_us_ = static_cast<int64_t>(timestamp_us);
    }
    const int64_t rel_us = static_cast<int64_t>(timestamp_us) - first_ts_us_;
    int64_t pts = av_rescale_q(rel_us, AVRational{1, 1000000}, ctx_->time_base);
    if (pts <= last_pts_) {
        pts = last_pts_ + 1;  // 防止两帧时间戳相同导致 PTS 不递增
    }
    last_pts_ = pts;
    yuv420p_->pts = pts;

    // 重连后请求的 I 帧：只作用于当前帧，用后复位，避免每帧都成为关键帧。
    if (force_keyframe_) {
        yuv420p_->pict_type = AV_PICTURE_TYPE_I;
        force_keyframe_ = false;
        std::printf("encoder: 强制 I 帧（重连后恢复画面）\n");
    } else {
        yuv420p_->pict_type = AV_PICTURE_TYPE_NONE;
    }

    int ret = avcodec_send_frame(ctx_, yuv420p_);
    if (ret < 0) {
        ds::ff_die("avcodec_send_frame", ret);
    }
    return true;
}

bool H264Encoder::next_packet(AVPacket* pkt) {
    int ret = avcodec_receive_packet(ctx_, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;
    }
    if (ret < 0) {
        ds::ff_die("avcodec_receive_packet", ret);
    }

    ++packets_;
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        ++keyframes_;
    }
    return true;
}

void H264Encoder::flush() {
    int ret = avcodec_send_frame(ctx_, nullptr);  // NULL = 没有更多输入
    if (ret < 0) {
        ds::ff_die("avcodec_send_frame(flush)", ret);
    }
}
