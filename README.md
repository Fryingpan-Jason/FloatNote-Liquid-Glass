# FloatNote — Native Liquid Glass for Windows

[English](README.en.md) · 简体中文

**Windows 原生液态玻璃便签。** 支持实时折射、弹性收放、置顶和鼠标穿透。便签与设置保存在本地，无账户、无遥测。

![FloatNote 桌面效果](docs/media/desktop.webp)

> 展示素材录自本地开发版，所示视觉与交互改进已包含在 v2.1.0 中。

**[下载最新版](https://github.com/Fryingpan-Jason/FloatNote-Liquid-Glass/releases/latest)** · [液态玻璃实现说明](docs/WINDOWS_LIQUID_GLASS.md)

## 看看效果

拖动便签，观察背景折射与文字变化。

![拖动与折射](docs/media/refraction.gif)

向下缩小便签可收纳成小条，点击恢复。下方动图约 1.43 倍速。

![收纳与恢复](docs/media/collapse-restore.gif)

<details>
<summary>外观设置</summary>

液态玻璃、毛玻璃与纯色；支持颜色、字号、模糊、置顶和鼠标穿透。

![外观设置](docs/media/settings.webp)

</details>

## 下载与使用

在发布页选择便携 ZIP，解压后运行 `FloatNote.exe`，无需安装额外运行库。

- **x64**：大多数 Intel / AMD 电脑。
- **arm64**：Windows on ARM 设备。
- **x86**：32 位环境。

选择应用 ZIP，而非 **Source code**。解压到可写目录；程序未签名，可能触发 SmartScreen。发布页提供 SHA-256 校验文件。

- 顶部显示操作按钮，右下角拖动调整大小。
- 默认预览 Markdown；点击文字编辑，点击空白、控件或其他窗口后自动保存并回到预览。支持范围见[快速开始](docs/QUICKSTART.md)。
- `Ctrl+Alt+E` 恢复编辑；`Ctrl+Alt+H` 显示/隐藏；`Ctrl+Alt+P` 切换鼠标穿透。
- `Ctrl+滚轮` 调整字号；双击托盘图标可找回便签。
- 升级时退出旧版、替换程序，**保留并备份原目录的 `data` 文件夹**。

## 兼容性

液态玻璃主要面向支持相应系统接口的 Windows 11；不支持的环境会降级。部分截图、录屏和远程共享方式可能看不到实时玻璃便签，需要时切换毛玻璃。详见[兼容性说明](docs/COMPATIBILITY.zh-CN.md)。

设置面板、关闭确认和材质参数目前为中文。屏幕背景仅在本机 GPU 上处理，不上传。

## 开发

C++/Win32、Direct3D 11、HLSL 与 Windows Graphics Capture，MIT 许可。可参考[液态玻璃实现](docs/WINDOWS_LIQUID_GLASS.md)，目前不是独立 SDK。

使用 Visual Studio C++ 工具链和 Windows SDK：

```powershell
.\build.ps1 -Architecture x64 -OutputDirectory build\x64
.\build.ps1 -Test -OutputDirectory build\tests
.\scripts\package.ps1 -Architecture x64
```

[开发说明](docs/DEVELOPMENT.md) · [第三方许可](THIRD_PARTY_NOTICES.md)
