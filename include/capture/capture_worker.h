#pragma once

#include <thread>

#include "queue/frame_queue.h"
#include "capture/v4l2_capture.h"

// 采集线程：抓帧后拷贝进帧队列，立即归还驱动缓冲。
class CaptureWorker {
public:
    CaptureWorker(V4L2Capture& capture, LatestFrameQueue& queue);
    ~CaptureWorker();
    CaptureWorker(const CaptureWorker&) = delete;
    CaptureWorker& operator=(const CaptureWorker&) = delete;

    void start();
    void stop();

private:
    void run();

    V4L2Capture& capture_;
    LatestFrameQueue& queue_;
    std::thread thread_;
};
