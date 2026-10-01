# FloatNote 2.3.0

本版整理桌面界面的排版与操作层级，让设置更清晰，并改善浅深色主题、小屏幕和高 DPI 下的使用体验。

- **设置分组**：按“外观／文字／行为”组织选项，统一间距、字号、选中状态与键盘焦点反馈。
- **系统外观**：设置面板跟随 Windows 浅色、深色主题，并使用系统高对比度配色。滑块和数值更容易辨识。
- **小屏幕与 DPI**：完整保留预设颜色，窄面板自动换行；较短的工作区可滚动，标题与“完成”保持可用。打开的面板可随显示器 DPI 变化重新布局。
- **正文与动效**：较大便签逐渐增加留白，小便签保持文字空间；细化悬停、按下和展开反馈，继续遵守系统关闭动画的设置。
- **交互可靠性**：修复像素取整导致的分段按钮点击范围重叠，增加原生布局、键盘导航、对比度和 DPI 回归检查。

现有液态玻璃、自动文字颜色、Markdown 预览、中文输入与撤销行为继续保留。

## 下载与升级

下载 Assets 中的 ZIP，解压运行 `FloatNote.exe`：**x64 适合大多数电脑**，arm64 适合 Windows on ARM，x86 适合 32 位环境。无需额外运行库。

升级前退出旧版，替换 EXE，**保留并备份原目录的 `data` 文件夹**。发布包不含个人数据；校验值见 `SHA256SUMS.txt`。

液态玻璃主要面向支持的 Windows 11 环境。部分截图、录屏和远程共享可能看不到实时玻璃，需要时切换毛玻璃。设置和材质面板目前为中文，程序未签名。

---

## English

This update clarifies the desktop interface and settings hierarchy, with improved light/dark appearance, small-display layouts and high-DPI behavior.

- **Organized settings**: appearance, text and behavior groups share consistent spacing, typography, selection states and keyboard focus feedback.
- **Windows appearance**: settings follow the light/dark theme and use system High Contrast colors. Sliders and values are easier to distinguish.
- **Small displays and DPI**: all preset colors remain available through wrapping; short work areas scroll below a fixed header and Done action. An open panel reflows when its monitor DPI changes.
- **Text and motion**: larger notes gain gradual breathing room while small notes retain their text space. Hover, pressed and panel transitions are refined, respecting disabled system animations.
- **Reliable interaction**: consistent pixel rounding prevents overlapping segmented-button hit targets. Native layout, keyboard navigation, contrast and DPI checks cover the interface.

Liquid glass, automatic ink, Markdown preview, native IME input and undo remain available.

Download a ZIP from Assets: **x64 for most PCs**, arm64 for Windows on ARM, or x86 for 32-bit environments. Extract and run `FloatNote.exe`; no additional runtime is required.

Exit before upgrading, replace the EXE, and **keep and back up the existing `data` folder**. Packages contain no personal data. Verify downloads with `SHA256SUMS.txt`.

Liquid glass targets supported Windows 11 environments. Some capture and remote-sharing methods omit live glass; use frosted glass when needed. Settings/material panels remain in Chinese. Binaries are unsigned.
