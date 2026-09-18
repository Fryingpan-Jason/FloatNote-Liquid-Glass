# FloatNote 2.2.0

液态玻璃便签支持 Markdown 自动预览：点击文字编辑，点击别处回到排版后的便签。

- **自动切换**：默认显示预览；点击文字进入原文编辑，点击空白、操作控件或其他窗口后自动保存并返回预览。暂停打字不会退出编辑。
- **Markdown 排版**：支持标题、粗体、斜体、删除线、有序/无序列表、任务列表、引用、行内代码、围栏代码块和链接文字。
- **原生编辑**：保留中文输入、光标定位和撤销记录；切换时等待输入法合成结束。空便签仍可点击输入。
- **玻璃显示**：排版适配窗口宽度和字号，文字保持清晰，预览支持滚动。

内容继续按 Markdown 原文保存在 `data/note.txt`，已有普通文本便签可直接使用。点击链接或任务标记会进入编辑；本版暂不渲染表格、图片和 HTML，也不加载远程内容。Markdown 支持是常用语法子集，并非完整 CommonMark/GFM 实现。

## 下载与升级

下载 Assets 中的 ZIP，解压运行 `FloatNote.exe`：**x64 适合大多数电脑**，arm64 适合 Windows on ARM，x86 适合 32 位环境。无需额外运行库。

升级前退出旧版，替换 EXE，**保留并备份原目录的 `data` 文件夹**。发布包不含个人数据；校验值见 `SHA256SUMS.txt`。

液态玻璃主要面向支持的 Windows 11 环境。部分截图、录屏和远程共享可能看不到实时玻璃，需要时切换毛玻璃。设置和材质面板目前为中文，程序未签名。自动检查不等同于所有输入法、显卡和远程环境的实机验收。

---

## English

Markdown preview comes to the liquid-glass note: click text to edit, then click elsewhere to return to the formatted note.

- **Automatic switching**: notes start in preview. Clicking text opens source editing; clicking blank space, a control or another window saves and restores preview. Pausing typing does not leave editing.
- **Markdown formatting**: headings, bold, italic, strikethrough, ordered/unordered lists, tasks, quotes, inline/fenced code and link labels.
- **Native editing**: retains Chinese input, source-position-aware caret placement and undo history. Preview waits for IME composition to finish. Empty notes remain clickable for input.
- **Glass presentation**: text wraps to the note width, follows font-size changes and remains readable over transparent backgrounds. Preview can be scrolled.

Content remains Markdown source in `data/note.txt`, and existing plain-text notes continue to work. Clicking a link or task marker edits its source. Tables, images and HTML are not rendered; no remote content is loaded. This is a subset of common Markdown syntax, not a complete CommonMark/GFM implementation.

Download a ZIP from Assets: **x64 for most PCs**, arm64 for Windows on ARM, or x86 for 32-bit environments. Extract and run `FloatNote.exe`; no additional runtime is required.

Exit before upgrading, replace the EXE, and **keep and back up the existing `data` folder**. Packages contain no personal data. Verify downloads with `SHA256SUMS.txt`.

Liquid glass targets supported Windows 11 environments. Some capture and remote-sharing methods omit live glass; use frosted glass when needed. Settings/material panels remain in Chinese. Binaries are unsigned. Automated checks do not certify every IME, GPU or remote environment.
