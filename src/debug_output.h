#pragma once

#include <cstddef>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace debug_output {

void write_file(const char* path, const void* data, size_t size);
void save_ppm(const char* path, const AVFrame* frame);

}  // namespace debug_output
