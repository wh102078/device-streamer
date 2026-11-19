#pragma once

#include <csignal>

// 进程级停止标志。信号处理函数只写 sig_atomic_t，线程通过 requested() 读取。
namespace stopflag {

inline volatile std::sig_atomic_t g_stop = 0;

inline void request_stop(int) {
    g_stop = 1;
}

inline void install() {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
}

inline bool requested() {
    return g_stop != 0;
}

}  // namespace stopflag
