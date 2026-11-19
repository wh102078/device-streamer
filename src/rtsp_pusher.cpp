#include "rtsp_pusher.h"

#include <cstdio>

#include "common.h"

namespace {

// 打印 FFmpeg 错误码，不退出进程
void warnFf(const char* what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(err, buf, sizeof(buf));
    std::fprintf(stderr, "pusher: %s failed: %s\n", what, buf);
}

}  // namespace

RtspPusher::~RtspPusher() {
    close();
}

bool RtspPusher::open(const char* url, const AVCodecContext* enc) {
    close();  // 确保从干净状态开始（支持重连）

    // 1) 按 URL 选输出格式：rtsp:// 会匹配到 RTSP muxer
    int ret = avformat_alloc_output_context2(&fmt_, nullptr, "rtsp", url);
    if (ret < 0 || fmt_ == nullptr) {
        warnFf("avformat_alloc_output_context2", ret);
        fmt_ = nullptr;
        return false;
    }

    // 2) 建一条视频流，参数从编码器复制（宽高、码流格式、extradata）
    stream_ = avformat_new_stream(fmt_, nullptr);
    if (stream_ == nullptr) {
        std::fprintf(stderr, "pusher: avformat_new_stream 失败\n");
        close();
        return false;
    }
    ret = avcodec_parameters_from_context(stream_->codecpar, enc);
    if (ret < 0) {
        warnFf("avcodec_parameters_from_context", ret);
        close();
        return false;
    }

    // 3) 告诉 muxer 编码包用的时间基（1/30），方便它换算成 RTP 的 90kHz
    stream_->time_base = enc->time_base;
    enc_time_base_ = enc->time_base;

    // 4) 握手：OPTIONS/ANNOUNCE/SETUP/RECORD 由 write_header 内部完成。
    //    RTP 走 TCP 交错（本机及虚拟机环境更稳定）。
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    ret = avformat_write_header(fmt_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        warnFf("avformat_write_header", ret);
        close();
        return false;
    }

    // 写入后 muxer 可能改过 time_base（典型是 1/90000），以最终值为准
    stream_time_base_ = stream_->time_base;
    std::printf("pusher : %s (tcp)\n", url);
    std::printf("         stream time_base=%d/%d\n",
                stream_time_base_.num, stream_time_base_.den);
    return true;
}

bool RtspPusher::write(AVPacket* pkt) {
    if (fmt_ == nullptr || stream_ == nullptr) {
        av_packet_unref(pkt);
        return false;
    }

    // 包的时间戳是按编码器时间基算的，必须先换算到 muxer 的时间基
    pkt->stream_index = stream_->index;
    av_packet_rescale_ts(pkt, enc_time_base_, stream_time_base_);

    // av_interleaved_write_frame 内部会 unref pkt：返回后 pkt->data 已释放
    int ret = av_interleaved_write_frame(fmt_, pkt);
    if (ret < 0) {
        warnFf("av_interleaved_write_frame", ret);
        return false;
    }
    return true;
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
