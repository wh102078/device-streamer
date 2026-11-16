#include "rtsp_pusher.h"

#include <cstdio>

#include "common.h"

RtspPusher::~RtspPusher() {
    close();
}

void RtspPusher::open(const char* url, const AVCodecContext* enc) {
    // 1) 按 URL 选输出格式：rtsp:// 会匹配到 RTSP muxer
    int ret = avformat_alloc_output_context2(&fmt_, nullptr, "rtsp", url);
    if (ret < 0 || fmt_ == nullptr) {
        ds::ff_die("avformat_alloc_output_context2", ret);
    }

    // 2) 建一条视频流，参数从编码器复制（宽高、码流格式、extradata）
    stream_ = avformat_new_stream(fmt_, nullptr);
    if (stream_ == nullptr) {
        std::fprintf(stderr, "error: avformat_new_stream 失败\n");
        std::exit(EXIT_FAILURE);
    }
    ret = avcodec_parameters_from_context(stream_->codecpar, enc);
    if (ret < 0) {
        ds::ff_die("avcodec_parameters_from_context", ret);
    }

    // 3) 告诉 muxer 编码包用的时间基（1/30），方便它换算成 RTP 的 90kHz
    stream_->time_base = enc->time_base;
    enc_time_base_ = enc->time_base;

    // 4) 握手：OPTIONS/ANNOUNCE/SETUP/RECORD 都在这一步内部完成。
    //    rtsp_transport=tcp：RTP 走 TCP 交错（虚拟机里比 UDP 稳）
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    ret = avformat_write_header(fmt_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        ds::ff_die("avformat_write_header", ret);
    }

    // 写入后 muxer 可能改过 time_base（典型是 1/90000），以最终值为准
    stream_time_base_ = stream_->time_base;
    std::printf("pusher : %s (tcp)\n", url);
    std::printf("         stream time_base=%d/%d\n",
                stream_time_base_.num, stream_time_base_.den);
}

void RtspPusher::write(AVPacket* pkt) {
    // 包的时间戳是按编码器时间基算的，必须先换算到 muxer 的时间基
    pkt->stream_index = stream_->index;
    av_packet_rescale_ts(pkt, enc_time_base_, stream_time_base_);

    // av_interleaved_write_frame 内部会 unref pkt：返回后 pkt->data 已释放
    int ret = av_interleaved_write_frame(fmt_, pkt);
    if (ret < 0) {
        ds::ff_die("av_interleaved_write_frame", ret);
    }
}

void RtspPusher::close() {
    if (fmt_ == nullptr) {
        return;
    }
    // 发 TEARDOWN，通知服务器结束会话
    av_write_trailer(fmt_);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    stream_ = nullptr;
}
