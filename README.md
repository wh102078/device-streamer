<div align="center">

# Device Streamer

**UVC 摄像头采集 → H.264 编码 → RTSP 推流（设备端）** · C++17 / V4L2 / FFmpeg / Linux

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![V4L2](https://img.shields.io/badge/Linux-V4L2-green)
![FFmpeg](https://img.shields.io/badge/FFmpeg-7.1-orange)
![License](https://img.shields.io/badge/License-MIT-yellow)

</div>

## 项目简介

Device Streamer 是一个运行在 Linux 设备端的视频推流程序：从 UVC 摄像头采集 MJPEG 帧，经解码、像素格式转换和 H.264 编码后，通过 RTSP 推送到 mediamtx，供 RTSP 客户端拉流。

面向实时监控场景，采集与编码解耦，网络异常时优先保证推流恢复，而不是缓存旧帧。

## 功能

- V4L2 mmap 流式采集（MJPEG 640×480，poll/DQBUF/QBUF）
- JPEG SOI/EOI 完整性校验，坏帧丢弃并重新入队
- MJPEG 解码（libavcodec），sws 转 yuv420p（含 full/limited range 处理）
- H.264 编码（libx264，zerolatency，GOP 30）
- 采集线程 + 容量 3 的有界队列（满时丢最旧帧），编码/推流变慢不阻塞采集
- RTSP over TCP 推流（FFmpeg RTSP muxer）
- 断线重连：1 / 2 / 4 … 上限 30 秒指数退避，重连后强制 I 帧
- 优雅退出：flush 编码器、发送 TEARDOWN；相机异常时正常退出而非硬杀进程
- 运行统计：FPS、包数、关键帧、实测码率、首包时间、采集/队列丢弃、重连次数与恢复耗时

## 技术栈

| 类别 | 使用 |
|---|---|
| 语言 | C++17 |
| 采集 | Linux V4L2（ioctl / mmap / poll） |
| 音视频 | FFmpeg 7.1（libavformat / libavcodec / libswscale / libavutil）、libx264 |
| 并发 | `std::thread`、`std::mutex`、`std::condition_variable`、`std::atomic` |
| 网络/协议 | TCP/IP、RTSP、RTP over TCP |
| 构建/调试 | Makefile、G++、ASan/UBSan、TSan |

## 构建

验证环境：

| 组件 | 版本 |
|---|---|
| 操作系统 | Ubuntu 25.10 |
| 编译器 | g++ 15.2.0（C++17） |
| FFmpeg | 7.1.1（libavformat 61 / libavcodec 61 / libswscale 8 / libavutil 59，启用 libx264） |

```bash
sudo apt install build-essential pkg-config \
                 libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
make
```

生成可执行文件 `./device_streamer`。

## 使用

先启动 RTSP 服务器（例如 [mediamtx](https://github.com/bluenviron/mediamtx)）：

```bash
mediamtx
```

再运行推流端：

```bash
./device_streamer [rtsp_url]
# 默认 rtsp://127.0.0.1:8554/dev
# Ctrl+C 退出
```

拉流验证：

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/dev
```

## 配置

采集设备、分辨率、帧率、码率、GOP、队列容量和 V4L2 缓冲数集中在 `src/config.h`（编译期常量），修改后重新编译即可。推流地址可通过命令行参数覆盖。

## 测试

```bash
make asan   # AddressSanitizer + UBSan 构建
```

- ASan/UBSan：连续推流无越界、无泄漏、无未定义行为报告；

## 性能

测试环境：VMware 虚拟机，UVC 摄像头，MJPEG 640×480 → H.264 2 Mbps / GOP 30，RTSP over TCP，无拉流客户端。

| 指标 | 实测 |
|---|---|
| 30 分钟连续推流 | RSS 51,284 → 51,312 KB，句柄数恒为 5，CPU 平均 16.7% |
| 采集/队列丢弃 | 0 / 0 |
| 首包时间（启动 → 首包写出） | 339 ms |
| 断线重连恢复（含退避等待） | 约 7.1 s（1 s + 2 s + 4 s 后成功） |
| 实测码率 | 约 1.0 Mbps（测试时相机约 15 fps） |

弱网对比（loopback 注入 2% 丢包 + 20 ms 延迟，8 秒拉流）：

| 传输 | 解码帧数 | 错误 |
|---|---|---|
| TCP | 226 | 无 |
| UDP | 155 | 出现 RTP 丢包与解码花屏 |

说明：测试环境光线不足时，摄像头会自动降低帧率。

## 目录结构

```
src/main.cpp          程序入口：初始化、采集线程、主循环、统计
src/config.h          编译期配置（设备、分辨率、码率、队列等）
src/stop_flag.h       信号处理与停止标志
src/v4l2_capture.*    V4L2 采集与坏帧过滤
src/mjpeg_decoder.*   MJPEG 解码
src/h264_encoder.*    H.264 编码与 PTS 生成
src/rtsp_pusher.*     RTSP 推流与断线重连
src/frame_queue.*     采集与编码之间的有界队列
src/debug_output.*    调试输出（首帧 JPEG/PPM）
src/common.*          错误处理
```

## 许可证

代码使用 MIT License。
