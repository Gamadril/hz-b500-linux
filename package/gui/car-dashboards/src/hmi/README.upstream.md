# RK3568-LVGL-CarDashboard

基于 RK3568 + LVGL v8.3 的智能汽车仪表盘系统

Smart car dashboard system based on RK3568 + LVGL v8.3

## 功能特性 / Features

- **Car Launcher** - 主界面启动器，现代化玻璃态 UI 设计
- **Dashboard** - 汽车仪表盘（车速表 0-220km/h、转速表 0-8000RPM、转向灯）
- **LED Control** - 4 路 LED 控制面板，霓虹灯效果
- **Music Player** - 音乐播放器（支持进度拖动、上/下一首）
- **Gallery** - 图片浏览器
- **Stopwatch** - 秒表计时器
- **Settings** - 系统设置页面

## 硬件平台 / Hardware

- **开发板**: RK3568 (Rockchip)
- **屏幕分辨率**: 1024 x 600
- **GPIO**: LED (120/121/123/124), Buzzer (111)

## 技术栈 / Tech Stack

- LVGL v8.3
- FreeType (中文字体渲染)
- Linux Framebuffer (/dev/fb0)
- mplayer (音频播放)

## 编译 / Build

```bash
# 1. 下载 FreeType 库到 freetype_tmp/ 目录

# 2. 交叉编译
make clean
make

# 3. 复制到开发板运行
scp demo root@<board-ip>:/Mydata/
```

## 目录结构 / Structure

```
├── main.c              # 主入口
├── mode/
│   ├── Car_Launcher.c  # 主界面 + 音乐播放器
│   ├── Car_Dashboard.c # 仪表盘界面
│   ├── LED.c           # LED 控制
│   ├── buzzer.c        # 蜂鸣器控制
│   └── Freetype_Font_Init.c  # 中文字体初始化
├── lvgl/               # LVGL 库
└── lv_drivers/         # LVGL 驱动
```

## 运行环境 / Runtime

- 字体文件: `/Mydata/simfang.ttf`
- 音乐文件: `/Mydata/1.mp3`, `2.mp3`, `3.mp3`
- 图片文件: `./gallery/1.jpg` ~ `10.jpg`

## 截图 / Screenshots

*待添加*

## License

MIT
