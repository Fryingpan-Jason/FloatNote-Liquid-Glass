# FloatNote 2.1.0

液态玻璃更易读，收放与控件响应更顺畅。

- **自动字色**：根据文字区域的实际背景切换深浅字色，平滑过渡，减少反复闪烁。
- **自适应控件**：黑白控件随周围背景变化；关闭按钮保留红色悬停反馈，尺寸提示跟随正文字色。
- **玻璃光照**：根据局部背景的颜色和纹理调整曲面明暗与反光。
- **交互优化**：收放复用着色器与图形资源，加快控件出现；修复窗口较多时独立控件的层级问题。

## 下载与升级

下载 Assets 中的 ZIP，解压运行 `FloatNote.exe`：**x64 适合大多数电脑**，arm64 适合 Windows on ARM，x86 适合 32 位环境。无需额外运行库。

升级前退出旧版，替换 EXE，**保留并备份原目录的 `data` 文件夹**。发布包不含个人数据；校验值见 `SHA256SUMS.txt`。

液态玻璃主要面向支持的 Windows 11 环境。部分截图、录屏和远程共享仍可能看不到实时玻璃，需要时切换毛玻璃；本次未解决该限制。设置和材质面板目前为中文，程序未签名。

---

## English

More readable liquid glass, smoother transitions and more responsive controls.

- **Automatic text color** follows the rendered background, with smooth transitions and hysteresis to reduce flicker.
- **Adaptive monochrome controls** follow the surrounding background. Close retains red hover feedback; the resize hint follows text color.
- **Glass lighting** responds to local background color and texture.
- **Interaction improvements** reuse shaders and graphics resources during collapse/restore, reveal controls sooner, and remove a window-count limit from control stacking.

Download a ZIP from Assets: **x64 for most PCs**, arm64 for Windows on ARM, or x86 for 32-bit environments. Extract and run `FloatNote.exe`; no additional runtime is required.

Exit before upgrading, replace the EXE, and **keep and back up the existing `data` folder**. Packages contain no personal data. Verify downloads with `SHA256SUMS.txt`.

Liquid glass targets supported Windows 11 environments. Some capture and remote-sharing methods still omit live glass; use frosted glass when needed. This release does not resolve that limitation. Settings/material panels remain in Chinese. Binaries are unsigned. Automated tests do not certify every GPU or remote environment.
