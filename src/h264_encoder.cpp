#include "h264_encoder.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#include <libavutil/dict.h>
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

void H264Encoder::open(int width, int height, int fps) {
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
    ctx_->pix_fmt = AV_PIX_FMT_YUV420P;   // 兼容性最好
    ctx_->time_base = AVRational{1, fps}; // 一帧 = 1/fps 秒
    ctx_->framerate = AVRational{fps, 1};
    ctx_->bit_rate = 2000000;             // 2 Mbps
    ctx_->gop_size = fps;                 // 每秒一个关键帧
    ctx_->max_b_frames = 0;               // 不用 B 帧，简化 PTS
    ctx_->thread_count = 1;

    // tune=zerolatency：关掉 x264 的预读缓冲（默认会攒 30~40 帧才出第一个包，
    // 这就是推流端 1 秒延迟的主要来源）。代价是压缩率略低、码率控制没那么平滑。
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "tune", "zerolatency", 0);

    int ret = avcodec_open2(ctx_, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        ds::ff_die("avcodec_open2(encoder)", ret);
    }

    // 编码器输入帧：格式 yuv420p，缓冲区只分配一次，逐帧复用
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

    std::printf("encoder: libx264 %dx%d yuv420p @%dfps 2Mbps gop=%d\n",
                width, height, fps, fps);
}

bool H264Encoder::encode(const AVFrame* frame) {
    // 源格式要等第一帧才知道（yuvj422p），所以第一次编码时建转换器
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
        // 显式告诉 sws：源是 full range（JPEG），目标是 limited range（H.264）
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

    yuv420p_->pts = next_pts_++;  // PTS 必须单调递增

    int ret = avcodec_send_frame(ctx_, yuv420p_);
    if (ret < 0) {
        ds::ff_die("avcodec_send_frame", ret);
    }
    return true;
}

bool H264Encoder::next_packet(AVPacket* pkt) {
    int ret = avcodec_receive_packet(ctx_, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;  // 暂时没有新包，或编码器已彻底结束
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
