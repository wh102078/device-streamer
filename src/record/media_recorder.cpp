#include "record/media_recorder.h"

#include <cstdio>
#include <cstring>

namespace {

bool isMp4(const std::string& path) {
    return path.size() > 4 && path.compare(path.size() - 4, 4, ".mp4") == 0;
}

}  // namespace

MediaRecorder::~MediaRecorder() {
    close();
}

bool MediaRecorder::open(const std::string& path, const AVCodecParameters* params,
                         AVRational src_time_base, std::string* error) {
    close();

    int ret = avformat_alloc_output_context2(&fmt_, nullptr, nullptr, path.c_str());
    if (ret < 0 || fmt_ == nullptr) {
        if (error != nullptr) {
            *error = "avformat_alloc_output_context2 failed";
        }
        fmt_ = nullptr;
        return false;
    }

    stream_ = avformat_new_stream(fmt_, nullptr);
    if (stream_ == nullptr) {
        if (error != nullptr) {
            *error = "avformat_new_stream failed";
        }
        close();
        return false;
    }

    ret = avcodec_parameters_copy(stream_->codecpar, params);
    if (ret < 0) {
        if (error != nullptr) {
            *error = "avcodec_parameters_copy failed";
        }
        close();
        return false;
    }

    src_time_base_ = src_time_base;
    stream_->time_base = src_time_base;

    AVDictionary* options = nullptr;
    if (isMp4(path)) {
        // 分段 MP4：异常退出后文件仍可播放，不依赖 trailer
        av_dict_set(&options, "movflags",
                    "+frag_keyframe+empty_moov+default_base_moof", 0);
    }

    if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE, nullptr, &options);
        if (ret < 0) {
            if (error != nullptr) {
                *error = "avio_open2 failed";
            }
            av_dict_free(&options);
            close();
            return false;
        }
    }

    ret = avformat_write_header(fmt_, &options);
    av_dict_free(&options);
    if (ret < 0) {
        if (error != nullptr) {
            *error = "avformat_write_header failed";
        }
        close();
        return false;
    }

    dst_time_base_ = stream_->time_base;
    waiting_keyframe_ = true;
    bytes_written_ = 0;
    frames_written_ = 0;
    return true;
}

bool MediaRecorder::write(const AVPacket* pkt) {
    if (fmt_ == nullptr || stream_ == nullptr || pkt == nullptr) {
        return false;
    }

    if (waiting_keyframe_) {
        if ((pkt->flags & AV_PKT_FLAG_KEY) == 0) {
            return true;  // 丢弃关键帧之前的数据
        }
        waiting_keyframe_ = false;
    }

    AVPacket* copy = av_packet_clone(pkt);
    if (copy == nullptr) {
        return false;
    }

    copy->stream_index = stream_->index;
    av_packet_rescale_ts(copy, src_time_base_, dst_time_base_);

    const int size = copy->size;
    const int ret = av_interleaved_write_frame(fmt_, copy);
    av_packet_free(&copy);
    if (ret < 0) {
        return false;
    }

    bytes_written_ += static_cast<uint64_t>(size);
    ++frames_written_;
    return true;
}

void MediaRecorder::close() {
    if (fmt_ == nullptr) {
        return;
    }
    if (fmt_->pb != nullptr) {
        av_write_trailer(fmt_);
        avio_closep(&fmt_->pb);
    }
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    stream_ = nullptr;
}
