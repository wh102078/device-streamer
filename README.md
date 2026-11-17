# device-streamer

UVC 摄像头采集 → H.264 编码 → RTSP 推流（设备端）。

```
UVC 摄像头 ──V4L2(MJPEG)──► 解码 ──► yuvj422p ──► yuv420p ──► libx264 ──► RTSP ──► mediamtx
```

## 功能

- V4L2 流式采集（MJPEG 640×480，mmap，poll/DQBUF/QBUF）
- 帧完整性校验（JPEG SOI/EOI），坏帧丢弃并重新入队
- MJPEG 解码（libavcodec）
- 像素格式转换：yuvj422p → yuv420p（含 full/limited range 处理）
- H.264 编码（libx264，zerolatency，GOP 30）
- 采集线程 + 有界帧队列（容量 3，满时丢最旧帧），编码/推流慢时不阻塞采集
- RTSP 推流（TCP，FFmpeg RTSP muxer）

## 构建

```bash
make
```

依赖 FFmpeg（`libavcodec`、`libavformat`、`libavutil`、`libswscale`，需启用 libx264），通过 `pkg-config` 查找。

## 运行

先启动 RTSP 服务器（例如 mediamtx），再运行：

```bash
./device_streamer [rtsp_url]
# 默认 rtsp://127.0.0.1:8554/dev
# Ctrl+C 退出
```

拉流验证：

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/dev
```

## 调试输出

程序会把第一帧存到 `out/frame_00.jpg` 和 `out/frame_00.ppm`，便于确认画面和颜色。

## 目录

```
src/
├── v4l2_capture.*     采集（V4L2）
├── mjpeg_decoder.*    解码（libavcodec）
├── h264_encoder.*     编码（libx264 + sws）
├── rtsp_pusher.*      推流（libavformat RTSP muxer）
├── common.*           错误处理
└── main.cpp           串联
```
