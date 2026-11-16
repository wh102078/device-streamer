#pragma once

// 公共错误处理：系统调用失败用 errno，FFmpeg 失败用错误码。
namespace ds {

[[noreturn]] void die(const char* what);

[[noreturn]] void ff_die(const char* what, int err);

}  // namespace ds
