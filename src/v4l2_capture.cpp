#include "v4l2_capture.h"

#include <linux/videodev2.h>

#include <poll.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"

namespace {

// ioctl 可能被信号打断返回 EINTR，必须重试。
int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

void fourcc_to_str(uint32_t f, char out[5]) {
    out[0] = static_cast<char>(f & 0xff);
    out[1] = static_cast<char>((f >> 8) & 0xff);
    out[2] = static_cast<char>((f >> 16) & 0xff);
    out[3] = static_cast<char>((f >> 24) & 0xff);
    out[4] = '\0';
}

// bytesused 非零不代表内容是完整 JPEG，需校验 SOI/EOI。
bool looks_like_jpeg(const void* data, size_t size) {
    if (size < 4) {
        return false;
    }
    const auto* p = static_cast<const unsigned char*>(data);
    return p[0] == 0xFF && p[1] == 0xD8 && p[size - 2] == 0xFF && p[size - 1] == 0xD9;
}

}  // namespace

V4L2Capture::~V4L2Capture() {
    close();
}

bool V4L2Capture::open(const char* device, unsigned width, unsigned height,
                       uint32_t pixel_format, unsigned buffer_count) {
    if (buffer_count < 2 || buffer_count > kMaxBuffers) {
        std::fprintf(stderr, "invalid buffer count: %u\n", buffer_count);
        return false;
    }
    fd_ = ::open(device, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        std::fprintf(stderr, "open(%s) failed: %s\n", device, std::strerror(errno));
        return false;
    }

    struct v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::fprintf(stderr, "VIDIOC_QUERYCAP failed: %s\n", std::strerror(errno));
        close();
        return false;
    }
    std::printf("device : %s / %s\n", cap.driver, cap.card);
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::fprintf(stderr, "error: 不是视频采集设备\n");
        close();
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::fprintf(stderr, "error: 不支持 streaming I/O\n");
        close();
        return false;
    }

    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::fprintf(stderr, "VIDIOC_S_FMT failed: %s\n", std::strerror(errno));
        close();
        return false;
    }

    char fourcc[5];
    fourcc_to_str(fmt.fmt.pix.pixelformat, fourcc);
    std::printf("format : %ux%u %s  sizeimage=%u\n",
                fmt.fmt.pix.width, fmt.fmt.pix.height, fourcc, fmt.fmt.pix.sizeimage);

    if (fmt.fmt.pix.width != width || fmt.fmt.pix.height != height) {
        std::fprintf(stderr, "warning: 驱动没有接受请求的尺寸\n");
    }
    if (fmt.fmt.pix.pixelformat != pixel_format) {
        std::fprintf(stderr, "error: 驱动没有接受请求的格式，实际是 %s\n", fourcc);
        close();
        return false;
    }

    struct v4l2_requestbuffers req{};
    req.count = buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::fprintf(stderr, "VIDIOC_REQBUFS failed: %s\n", std::strerror(errno));
        close();
        return false;
    }
    if (req.count < 2) {
        std::fprintf(stderr, "error: 驱动只给了 %u 个缓冲\n", req.count);
        close();
        return false;
    }
    buffer_count_ = req.count;

    for (unsigned i = 0; i < buffer_count_; ++i) {
        struct v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            ds::die("VIDIOC_QUERYBUF");
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd_, buf.m.offset);
        if (buffers_[i].start == MAP_FAILED) {
            ds::die("mmap");
        }
    }
    std::printf("buffers: %u 块，每块 %zu 字节\n", buffer_count_, buffers_[0].length);

    for (unsigned i = 0; i < buffer_count_; ++i) {
        if (!queue(i)) {
            close();
            return false;
        }
    }
    return true;
}

bool V4L2Capture::start() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::fprintf(stderr, "VIDIOC_STREAMON failed: %s\n", std::strerror(errno));
        return false;
    }
    streaming_ = true;
    return true;
}

void V4L2Capture::stop() {
    if (!streaming_ || fd_ < 0) {
        return;
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
        std::fprintf(stderr, "warning: VIDIOC_STREAMOFF failed: %s\n",
                     std::strerror(errno));
    }
    streaming_ = false;
    held_index_ = -1;
}

void V4L2Capture::close() {
    if (fd_ < 0) {
        return;
    }
    stop();
    for (unsigned i = 0; i < buffer_count_; ++i) {
        if (buffers_[i].start != nullptr) {
            munmap(buffers_[i].start, buffers_[i].length);
            buffers_[i].start = nullptr;
        }
    }
    ::close(fd_);
    fd_ = -1;
    buffer_count_ = 0;
}

bool V4L2Capture::wait_for_frame() {
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    for (;;) {
        int r = poll(&pfd, 1, 3000);
        if (r < 0) {
            if (errno == EINTR) {
                continue;  // 被信号打断，重试
            }
            std::fprintf(stderr, "poll failed: %s\n", std::strerror(errno));
            return false;
        }
        if (r == 0) {
            std::fprintf(stderr, "poll timeout: 3 秒内没有拿到帧\n");
            return false;
        }
        return true;
    }
}

V4L2Capture::DequeueResult V4L2Capture::dequeue(struct v4l2_buffer* buf) {
    *buf = {};
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, buf) < 0) {
        if (errno == EAGAIN) {
            return DequeueResult::Again;
        }
        std::fprintf(stderr, "VIDIOC_DQBUF failed: %s\n", std::strerror(errno));
        return DequeueResult::Error;
    }
    return DequeueResult::Ok;
}

bool V4L2Capture::queue(unsigned index) {
    struct v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::fprintf(stderr, "VIDIOC_QBUF failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

bool V4L2Capture::grab(CapturedFrame* out) {
    for (unsigned attempt = 0; attempt < kMaxGrabAttempts; ++attempt) {
        if (!wait_for_frame()) {
            return false;  // 超时或设备异常，由调用方决定退出/重连
        }

        struct v4l2_buffer buf{};
        const DequeueResult r = dequeue(&buf);
        if (r == DequeueResult::Again) {
            continue;  // 暂时没帧，回去继续等
        }
        if (r == DequeueResult::Error) {
            return false;  // 设备错误（例如被拔掉）
        }

        // 驱动标坏或不是完整 JPEG：丢弃重取
        bool bad = (buf.flags & V4L2_BUF_FLAG_ERROR) != 0
                   || !looks_like_jpeg(buffers_[buf.index].start, buf.bytesused);
        if (bad) {
            ++rejected_;
            if (!queue(buf.index)) {
                return false;
            }
            continue;
        }

        out->data = static_cast<const uint8_t*>(buffers_[buf.index].start);
        out->size = buf.bytesused;
        out->sequence = buf.sequence;
        out->index = buf.index;
        out->timestamp_us = static_cast<uint64_t>(buf.timestamp.tv_sec) * 1000000ULL +
                            static_cast<uint64_t>(buf.timestamp.tv_usec);
        held_index_ = static_cast<int>(buf.index);
        return true;
    }

    std::fprintf(stderr, "error: 连续 %u 次没取到有效帧\n", kMaxGrabAttempts);
    return false;
}

void V4L2Capture::release() {
    if (held_index_ >= 0) {
        if (!queue(static_cast<unsigned>(held_index_))) {
            std::fprintf(stderr, "warning: 归还缓冲失败\n");
        }
        held_index_ = -1;
    }
}
