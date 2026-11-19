#pragma once

#include <cstddef>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace debug_output {

// 把原始字节写文件（用于保存第一帧 JPEG）。
void write_file(const char* path, const void* data, size_t size);

// 把解码后的帧转 RGB 并保存为 PPM（用于确认画面和颜色）。
void save_ppm(const char* path, const AVFrame* frame);

}  // namespace debug_output
