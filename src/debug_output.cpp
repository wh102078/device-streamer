#include "debug_output.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <libswscale/swscale.h>
}

namespace debug_output {

void write_file(const char* path, const void* data, size_t size) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "fopen(%s) failed: %s\n", path, std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    if (std::fwrite(data, 1, size, f) != size) {
        std::fclose(f);
        std::exit(EXIT_FAILURE);
    }
    std::fclose(f);
}

void save_ppm(const char* path, const AVFrame* frame) {
    SwsContext* sws = sws_getContext(frame->width, frame->height,
                                     static_cast<AVPixelFormat>(frame->format),
                                     frame->width, frame->height,
                                     AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        std::fprintf(stderr, "sws_getContext failed\n");
        std::exit(EXIT_FAILURE);
    }

    const unsigned w = static_cast<unsigned>(frame->width);
    const unsigned h = static_cast<unsigned>(frame->height);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);

    uint8_t* dst_data[1] = {rgb.data()};
    int dst_linesize[1] = {static_cast<int>(w * 3)};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
    sws_freeContext(sws);

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "fopen(%s) failed: %s\n", path, std::strerror(errno));
        std::exit(EXIT_FAILURE);
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

}  // namespace debug_output
