## 步骤0：配置CSI摄像头引脚
```bash
sudo /opt/nvidia/jetson-io/jetson-io.py
```

- 按方向键下选择Configure Jetson 24pin CSI Connector
- 选择Configure for compatible hardware 
- 按方向键下选择Camera IMX219 Dual
- 选择Save pin changes
- 按方向键下选择Save and reboot to reconfigure pins

- *重启以后进行：sudo ls /dev/video 就能找到video0*

---
## 步骤1：检查仓库并尝试直接操作相机

看起来我们的Jetson设备上可能有一些仓库配置问题。让我们尝试不同的方式来诊断：

```bash
# 更新仓库信息
sudo apt update

# 检查仓库源配置
cat /etc/apt/sources.list

# 尝试安装v4l-utils (注意是v4l而不是v4l2)
sudo apt install -y v4l-utils
```

同时，让我们检查GStreamer的可用性和尝试最简单的相机测试:

```bash
# 检查GStreamer版本
gst-launch-1.0 --version

# 尝试列出GStreamer插件
gst-inspect-1.0 | grep -E 'v4l|nvargus|nvcamera'
```

---

## 步骤2: 测试基本的GStreamer相机捕获

我们有多个GStreamer插件可用于相机捕获。由于这是Jetson设备上的IMX219 CSI相机，我们有两个主要选择：

1. `nvarguscamerasrc` - NVIDIA专为CSI相机优化的接口
2. `v4l2src` - 标准Linux视频接口

让我们首先尝试使用`nvarguscamerasrc`捕获一张JPEG图像：

```bash
# 使用nvarguscamerasrc捕获单帧并保存为JPEG
gst-launch-1.0 nvarguscamerasrc num-buffers=1 ! 'video/x-raw(memory:NVMM),width=1920,height=1080,format=NV12' ! nvjpegenc ! filesink location=test_argus.jpg
```

如果这个不成功，我们再尝试标准的v4l2接口：

```bash
# 使用v4l2src捕获单帧并保存为JPEG
gst-launch-1.0 v4l2src device=/dev/video0 num-buffers=1 ! 'video/x-raw,width=1280,height=720' ! nvjpegenc ! filesink location=test_v4l2.jpg
```

---
## 步骤3: 录制视频

既然我们已经成功捕获了静态图像，让我们尝试录制一段视频。我们将使用相同的相机源，但改为H.264编码并保存为MP4文件。

### 使用nvarguscamerasrc录制视频

```bash
# 录制5秒钟的高清视频
gst-launch-1.0 nvarguscamerasrc ! 'video/x-raw(memory:NVMM),width=1920,height=1080,framerate=30/1' ! nvv4l2h264enc bitrate=8000000 ! h264parse ! qtmux ! filesink location=test_video.mp4 -e
```

你可以按Ctrl+C提前停止录制，或者让它运行预设的时间（默认会一直运行直到手动停止）。

### 如果你想设置固定时长

```bash
# 精确录制10秒钟的视频
gst-launch-1.0 nvarguscamerasrc num-buffers=300 ! 'video/x-raw(memory:NVMM),width=1920,height=1080,framerate=30/1' ! nvv4l2h264enc bitrate=8000000 ! h264parse ! qtmux ! filesink location=test_video_10sec.mp4
```

此命令会精确录制10秒（在30fps下，300帧 = 10秒）。

### 改变分辨率和质量

你也可以调整分辨率和比特率来改变视频质量：

```bash
# 录制720p视频，较低比特率
gst-launch-1.0 nvarguscamerasrc ! 'video/x-raw(memory:NVMM),width=1280,height=720,framerate=30/1' ! nvv4l2h264enc bitrate=4000000 ! h264parse ! qtmux ! filesink location=test_video_720p.mp4 -e
```

---