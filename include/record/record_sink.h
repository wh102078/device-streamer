#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "record/media_recorder.h"
#include "queue/packet_queue.h"

// 录像线程：队列满时阻塞生产者，保证录制不丢帧。
class RecordSink {
public:
    RecordSink(std::string path, const AVCodecParameters* params,
               AVRational time_base, std::size_t capacity);
    ~RecordSink();
    RecordSink(const RecordSink&) = delete;
    RecordSink& operator=(const RecordSink&) = delete;

    bool start(std::string* error);
    void enqueue(AVPacket* pkt);
    void stop();

    unsigned frames() const { return frames_.load(); }
    uint64_t bytes() const { return bytes_.load(); }
    unsigned dropped() const { return queue_.dropped(); }
    unsigned write_errors() const { return write_errors_.load(); }

private:
    void run();

    std::string path_;
    const AVCodecParameters* params_;
    AVRational time_base_;
    PacketQueue queue_;
    MediaRecorder recorder_;
    std::thread thread_;
    std::atomic<unsigned> frames_{0};
    std::atomic<uint64_t> bytes_{0};
    std::atomic<unsigned> write_errors_{0};
};
