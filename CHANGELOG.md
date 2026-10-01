# Changelog

All notable changes are documented here. FloatNote follows [Semantic Versioning](https://semver.org/).

## [2.3.0] - 2026-10-01

### Added

- Settings follow Windows light/dark appearance and system High Contrast colors.
- Hidden native settings-layout coverage for complete palettes, keyboard navigation, contrast and DPI changes.

### Improved

- Group settings into appearance, text and behavior with consistent spacing, typography, selection and focus feedback.
- Wrap every saved color and stack longer choices on narrow work areas; keep the header available while scrolling.
- Add gradual breathing room to larger notes while retaining the usable text height of small and shallow notes.
- Make sliders easier to see, refine hover/pressed feedback, and shorten panel transitions while honoring disabled motion.

### Fixed

- Round shared control edges consistently to prevent overlapping native hit targets.
- Reflow an open settings panel when its monitor DPI changes and refresh its appearance when Windows settings change.
- Announce selected materials and colors through native accessible control names.

## [2.2.1] - 2026-09-28

### Fixed

- Bound glyph-mask reads to the editor client buffer while retaining text placement and automatic ink behavior.
- Avoid saving NUL padding or replacing a note after a failed text read or encoding conversion.
- Keep spaces, line endings and blank lines editable without starting a window drag.
- Remove the duplicate system-rounded outline around the custom settings panel.

### Improved

- Smooth settings-panel transitions and hover feedback, stronger pressed states and buffered slider painting.
- Preserve transition position when closing during entry; clear hover state on close and honor disabled animations.
- Add final-pixel ink and settings-animation regression coverage. Background-sampling scope is unchanged.

## [2.2.0] - 2026-09-18

### Added

- Automatic Markdown preview with source-position-aware click-to-edit, native IME and undo preservation.
- Common Markdown formatting, source-preserving persistence, wrapping and preview scrolling.

## [2.1.0] - 2026-09-13

### Added

- Automatic liquid-glass text color based on the rendered background, with smooth transitions and protection against rapid switching.
- Monochrome controls that adapt to the surrounding background, retaining red feedback when hovering over close.
- Local background analysis for glass shoulder lighting and reflections.

### Improved

- Reuse shaders and graphics resources across collapse/restore transitions; reveal controls more promptly.
- Keep independent controls above the note without a window-count limit.
- Match the resize hint to the note text color and its transition.
- Add product screenshots and short demonstrations to the bilingual README.

### Limits

- Live glass may remain absent from some screen-capture and remote-sharing methods; use frosted glass when necessary.
- Settings and material controls remain in Chinese.

## [2.0.0] - 2026-09-11

### Added

- Liquid-glass rendering with continuous edge refraction, adaptive reflection, softness and dispersion controls.
- Compact expandable controls, redesigned settings, custom color palettes and a remembered close action.
- Animated note storage/restoration with stable pointer activation regions and aligned arrival feedback.

### Changed

- Default builds now produce the 2.0 desktop interface with standard single-instance, startup and global shortcuts.
- Portable archives include a quick-start guide and third-party material licenses, with version/architecture/content checks.
- Private local checkpoints and research artifacts are excluded from the public release tree and history.

### Fixed

- Pin preference and actual startup stacking agree.
- Prevent hover oscillation, unintended activation above the note, clipped small-window text, and an unpositioned startup settings window.
- Preserve native text in off-screen rendering and caret routing before a parent window is shown.

### Limits

- New settings and material controls currently use Chinese. Liquid-glass visibility and fallback depend on Windows capture/composition support.

## [1.1.0] - 2026-09-09

### Added

- Optional window shadow while retaining rounded glass.
- Custom text colors with a setting to return to automatic theme-based text color.

### Changed

- Glass and standard mode share adjustable 0–100% background opacity without fading text.

### Fixed

- Remove light fringes around text over dark backgrounds using independent grayscale coverage masks, preserving native selection colors.

## [1.0.0] - 2026-09-09

### Added

- Portable single-note Windows application with atomic UTF-8 saving.
- Native rounded glass on supported Windows 11 systems and 0–100% standard background opacity.
- Always-on-top, mouse click-through, tray controls, global shortcuts, and per-user startup shortcut.
- Five theme presets, custom RGB colors, and readable foreground selection.
- English and Simplified Chinese UI with automatic Windows-language detection and a manual selector.
- x64, x86, and ARM64 build targets.

### Reliability

- Protect unreadable, unsupported, oversized, and locked note files from accidental replacement.
- Recover off-screen windows after monitor changes and rescale for per-monitor DPI.
- Fall back from glass in High Contrast, Energy Saver, Remote Desktop, disabled transparency, or unsupported compositor environments.
- Avoid idle repaint loops and reuse the layered drawing buffer.
