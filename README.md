# device-streamer

设备端采集 → 编码的最小闭环（开发中）。

```
UVC 摄像头 ──V4L2(MJPEG)──► libavcodec 解码 ──► yuvj422p
                                        │
                                        ▼
                              sws 转 yuv420p
                                        │
                                        ▼
                              libx264 编码 ──► out/output.h264
```

## 当前状态

- [x] V4L2 采集（MJPEG 640x480，mmap，poll/DQBUF/QBUF）
- [x] 帧校验（SOI/EOI），坏帧丢弃重取
- [x] MJPEG 解码（libavcodec）
- [x] yuvj422p → yuv420p 转换（含 full/limited range 处理）
- [x] H.264 编码（libx264，GOP/PTS/flush）
- [x] RTSP 推流到 mediamtx（FFmpeg RTSP muxer，TCP）
- [ ] 用 RTSP 客户端拉流验证端到端
- [ ] 采集/编码统计、推流断线重连

## 构建与运行

```bash
make
./device_streamer [rtsp_url]
# 默认 rtsp://127.0.0.1:8554/dev
```

需要先启动 mediamtx（本地测试用 `tools/mediamtx/mediamtx`）。

依赖：`libavcodec`、`libavutil`、`libswscale`（`pkg-config` 查找），以及支持 libx264 的 FFmpeg。

程序抓取 10 帧后退出，输出：

- `out/frame_XX.jpg`：相机原始 JPEG
- `out/frame_XX.ppm`：解码后的画面（验证用）
- `out/output.h264`：H.264 裸码流

## 目录

```
src/
├── v4l2_capture.*     采集（V4L2）
├── mjpeg_decoder.*    解码（libavcodec）
├── h264_encoder.*     编码（libx264 + sws）
├── common.*           错误处理
└── main.cpp           串联
```
