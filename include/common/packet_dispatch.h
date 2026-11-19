#pragma once

#include "net/push_sink.h"
#include "record/record_sink.h"

// 每个 sink 拿到自己的 AVPacket 引用；原包由调用方释放。
inline void dispatch_packet(AVPacket* pkt, PushSink& push, RecordSink* record) {
    AVPacket* for_push = av_packet_clone(pkt);
    if (for_push != nullptr) {
        push.enqueue(for_push);
    }

    if (record != nullptr) {
        AVPacket* for_record = av_packet_clone(pkt);
        if (for_record != nullptr) {
            record->enqueue(for_record);
        }
    }

    av_packet_unref(pkt);
}
