项目描述：基于 ESP32 设计网络流媒体音频播放终端，实现 HTTPS 音频流拉取、MP3 软件解码、I2S/DMA 音频输出，并内置 Web Server 支持跨平台无线控制和系统状态监控。

技术栈： C、ESP-IDF、FreeRTOS、HTTP/HTTPS、TLS/SSL、I2S/DMA、PCM5102A、PAM8403、I2C、OLED、HTML/JS

主要工作：

基于 ESP32 + FreeRTOS 设计网络音频播放终端，实现 HTTPS 音频流拉取、MP3 软件解码、I2S/DMA 音频输出和 Web 页面控制。
搭建 ESP32 + PCM5102A + PAM8403链路，ESP32 将 MP3 解码为 PCM 后，通过 I2S 输出至 PCM5102A DAC，再经 PAM8403 功放驱动扬声器播放。
动态配置采样频率声道的音频输出，使用 DMA 多缓冲机制驱动外部 DAC，支持 128kbps MP3 网络音频流稳定播放。
划分网络拉流、音频解码I2S 播放、Web 服务和 OLED 显示任务，通过缓冲队列实现网络接收、解码和播放解耦。
针对 TLS 握手和 MP3 解码内存占用高导致的 OOM 问题，优化 HTTP/TLS 缓冲区、任务栈和初始化顺序，使播放过程中最小剩余 Heap 保持在 10 KB 以上。
部署轻量级 Web Server，支持播放/暂停、URL 配置和状态查询；通过 OLED 和 5s Free Heap 心跳监控系统，连续播放 2h 未出现异常复位。
