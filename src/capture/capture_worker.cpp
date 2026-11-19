#include "capture/capture_worker.h"

#include <cstring>
#include <utility>

#include "common/stop_flag.h"

CaptureWorker::CaptureWorker(V4L2Capture& capture, LatestFrameQueue& queue)
    : capture_(capture), queue_(queue) {}

CaptureWorker::~CaptureWorker() {
    stop();
}

void CaptureWorker::start() {
    thread_ = std::thread(&CaptureWorker::run, this);
}

void CaptureWorker::stop() {
    queue_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void CaptureWorker::run() {
    while (!stopflag::requested()) {
        CapturedFrame f;
        if (!capture_.grab(&f)) {
            break;
        }

        QueuedFrame q;
        q.data.resize(f.size);
        std::memcpy(q.data.data(), f.data, f.size);
        q.sequence = f.sequence;
        q.timestamp_us = f.timestamp_us;

        queue_.push(std::move(q));
        capture_.release();
    }

    queue_.stop();
}
