<div align="center">

# Device Streamer

**UVC 摄像头采集 -> H.264 编码 -> RTSP 推流 + 本地录像（设备端）** · C++17 / V4L2 / FFmpeg / Linux

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![V4L2](https://img.shields.io/badge/Linux-V4L2-green)
![FFmpeg](https://img.shields.io/badge/FFmpeg-7.1-orange)
![License](https://img.shields.io/badge/License-MIT-yellow)

</div>

## 项目简介

Device Streamer 是一个运行在 Linux 设备端的视频推流与录像程序：从 UVC 摄像头采集 MJPEG 帧，经解码、像素格式转换和 H.264 编码后，同时推送到 mediamtx 并写入本地 MP4。

编码后的每个包通过引用计数分发给推流和录像两个独立线程：网络中断只影响推流，录像继续；录像不丢帧，推流队列满时丢旧帧以保持实时性。

## 功能

- V4L2 mmap 流式采集（MJPEG 640×480，poll/DQBUF/QBUF）
- JPEG SOI/EOI 完整性校验，坏帧丢弃并重新入队
- MJPEG 解码（libavcodec），sws 转 yuv420p（含 full/limited range 处理）
- H.264 编码（libx264，zerolatency，GOP 30）
- 采集线程 + 容量 3 的有界队列（满时丢最旧帧），编码/推流变慢不阻塞采集
- RTSP over TCP 推流（FFmpeg RTSP muxer）
- 断线重连：1 / 2 / 4 ... 上限 30 秒指数退避，重连后强制 I 帧
- 本地录像：编码包旁路 remux 为 fragmented MP4，不重新编码；断网期间继续录制
- 推流/录像双 sink 线程：推流队列满时丢旧帧，录像队列满时阻塞，录像不丢帧
- 优雅退出：flush 编码器、发送 TEARDOWN、写 MP4 trailer；相机异常时正常退出
- 运行统计：FPS、包数、实测码率、首包时间、录像帧数、采集/帧队列/推流队列丢弃、重连次数与恢复耗时

## 技术栈

| 类别 | 使用 |
|---|---|
| 语言 | C++17 |
| 采集 | Linux V4L2（ioctl / mmap / poll） |
| 音视频 | FFmpeg 7.1（libavformat / libavcodec / libswscale / libavutil）、libx264 |
| 并发 | `std::thread`、`std::mutex`、`std::condition_variable`、`std::atomic` |
| 网络/协议 | TCP/IP、RTSP、RTP over TCP |
| 构建/调试 | Makefile、G++、ASan/UBSan |

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

采集设备、分辨率、帧率、码率、GOP、队列容量、V4L2 缓冲数和录像路径集中在 `src/config.h`（编译期常量），修改后重新编译即可。推流地址可通过命令行参数覆盖。

录像默认开启（`out/record.mp4`），打开失败时只禁用录像，推流继续。

## 测试

```bash
make asan   # AddressSanitizer + UBSan 构建
```

- ASan/UBSan：连续推流无越界、无泄漏、无未定义行为报告；

## 性能

测试环境：VMware 虚拟机，UVC 摄像头，MJPEG 640×480 -> H.264 2 Mbps / GOP 30，RTSP over TCP，无拉流客户端。本次 30 分钟长稳录像关闭，补光后相机约 29.3 fps。

| 指标 | 实测 |
|---|---|
| 30 分钟连续推流 | 52,923 帧 / 29.3 fps；RSS 51,128 -> 52,088 KB（前 10 分钟增长约 1 MB 后稳定）；句柄数恒为 5；CPU 平均 76.2%（单核） |
| 采集 / 帧队列 / 推流队列丢弃 | 9 / 115 / 0（合计 <0.3%） |
| 首包时间（进程启动 -> 首包写出） | 230–321 ms（3 次：230 / 233 / 321 ms） |
| 断线重连恢复（含退避等待） | 约 7.1 s（1 s + 2 s + 4 s 后成功） |
| 实测码率 | 约 1.96 Mbps（29.3 fps） |

弱网对比（loopback 注入 2% 丢包 + 20 ms 延迟，8 秒拉流）：

| 传输 | 解码帧数 | 错误 |
|---|---|---|
| TCP | 226 | 无 |
| UDP | 155 | 出现 RTP 丢包与解码花屏 |

双 sink 与录像（早期 8 秒 / 断网测试，未随本次 30 分钟重测）：

| 场景 | 结果 |
|---|---|
| 正常 8 秒 | 29.5 fps，录像 232 帧，采集/帧队列/推流队列丢弃均为 0 |
| 断网 5 秒 | 主循环仍为 29.5 fps；录像继续写入；推流队列丢弃 204（预期） |
| 恢复 | 重连 3 次、恢复耗时 7.1 s，推流继续 |
| 整段 17 秒 | 录像 504 帧 / 17.0 s，完整解码通过（断网期间的帧不缺失） |

说明：30 分钟长稳为录像关闭；录像开启会多一个 MP4 文件句柄。测试环境光线不足时，摄像头会自动降低帧率。

## 目录结构

```
include/
├── common/   common.h  config.h  debug_output.h  stop_flag.h  packet_dispatch.h
├── queue/    frame_queue.h  packet_queue.h
├── capture/  v4l2_capture.h  capture_worker.h
├── codec/    mjpeg_decoder.h  h264_encoder.h
├── net/      rtsp_pusher.h  push_sink.h
└── record/   media_recorder.h  record_sink.h

src/
├── main.cpp
├── common/   capture/   codec/   net/   queue/   record/   对应的 .cpp
```

## 许可证

代码使用 MIT License。
