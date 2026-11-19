#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 把编码后的音视频包写成文件（remux，不重新编码）。
// 线程不安全：由调用方保证同一实例只在一个线程里使用。
class MediaRecorder {
public:
    MediaRecorder() = default;
    ~MediaRecorder();
    MediaRecorder(const MediaRecorder&) = delete;
    MediaRecorder& operator=(const MediaRecorder&) = delete;

    // path 的扩展名决定容器（.mp4 / .ts）。
    // params 通常来自编码器或解封装器的 codecpar；src_time_base 是包时间戳的时间基。
    bool open(const std::string& path, const AVCodecParameters* params,
              AVRational src_time_base, std::string* error);

    // 不接管 pkt：内部克隆一份再写入，调用方可以继续使用原包。
    // 第一个关键帧到达前会跳过（避免文件开头无法解码）。
    bool write(const AVPacket* pkt);

    void close();

    bool is_open() const { return fmt_ != nullptr; }
    uint64_t bytes_written() const { return bytes_written_; }
    unsigned frames_written() const { return frames_written_; }

private:
    AVFormatContext* fmt_ = nullptr;
    AVStream* stream_ = nullptr;
    AVRational src_time_base_{1, 1};
    AVRational dst_time_base_{1, 1};
    bool waiting_keyframe_ = true;
    uint64_t bytes_written_ = 0;
    unsigned frames_written_ = 0;
};
