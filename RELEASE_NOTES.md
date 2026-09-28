# FloatNote 2.2.1

本版修复文字绘制边界、编辑点击和控制卡片外圈，并改善设置面板的动画与操作反馈。

- **文字与保存**：保留原有自动字色行为，限制字形遮罩读取范围，防止有边框或滚动条时越界；按实际读取长度保存，避免空字符填充和转换失败覆盖笔记。
- **编辑体验**：编辑时点击空格、行尾和空行可以定位光标；正文下方的空白仍可拖动便签。保留 Markdown 预览、中文输入及撤销。
- **设置面板**：修复系统圆角与自绘圆角叠加形成的外圈；改善淡入淡出、悬停和按下反馈，减少滑块闪烁，处理动画中途关闭及悬停状态清理。
- **外观规则**：保留黑白控制片与红色关闭反馈。此次未扩展背景采样范围：液态玻璃自动字色按背景采样，其余材质沿用原主题底色规则。

## 下载与升级

下载 Assets 中的 ZIP，解压运行 `FloatNote.exe`：**x64 适合大多数电脑**，arm64 适合 Windows on ARM，x86 适合 32 位环境。无需额外运行库。

升级前退出旧版，替换 EXE，**保留并备份原目录的 `data` 文件夹**。发布包不含个人数据；校验值见 `SHA256SUMS.txt`。

液态玻璃主要面向支持的 Windows 11 环境。部分截图、录屏和远程共享可能看不到实时玻璃，需要时切换毛玻璃。设置和材质面板目前为中文，程序未签名。

---

## English

This update fixes text-composition boundaries, editing hit targets and the extra outline around the settings panel, while improving interface feedback.

- **Text and persistence**: retain automatic ink behavior and bound glyph-mask reads to prevent out-of-bounds access with editor borders or scrollbars. Save only the characters actually read and protect existing notes from failed reads or conversions.
- **Editing**: spaces, line endings and blank lines accept caret placement; blank space below the text still drags the note. Markdown preview, native IME input and undo remain available.
- **Settings**: remove overlapping system/custom rounded outlines, smooth panel and hover transitions, strengthen pressed feedback, reduce slider flicker, and handle interrupted transitions and hover cleanup.
- **Appearance**: retain monochrome controls and red close-hover feedback. Background-sampling scope is unchanged: liquid-glass Auto ink samples its background; other materials retain theme-based Auto color.

Download a ZIP from Assets: **x64 for most PCs**, arm64 for Windows on ARM, or x86 for 32-bit environments. Extract and run `FloatNote.exe`; no additional runtime is required.

Exit before upgrading, replace the EXE, and **keep and back up the existing `data` folder**. Packages contain no personal data. Verify downloads with `SHA256SUMS.txt`.

Liquid glass targets supported Windows 11 environments. Some capture and remote-sharing methods omit live glass; use frosted glass when needed. Settings/material panels remain in Chinese. Binaries are unsigned.
