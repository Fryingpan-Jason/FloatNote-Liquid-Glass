# Development

FloatNote intentionally stays close to the Windows API. The production executable uses the static MSVC runtime and Windows system DLLs only.

For the liquid-glass renderer, source map and adaptation boundaries, start with [Implement Liquid Glass on Windows](WINDOWS_LIQUID_GLASS.md).

## Layout

- `experiments/local_desktop.cpp` — default 2.0 desktop entry; standard app identity, startup and shortcuts.
- `experiments/glass_lab.cpp` and `glass_experience.h` — shared material controls and interaction integration.
- `experiments/glass_control_*.h` — independent control/settings windows.
- `src/liquid_backdrop.h` and `monitor_backdrop_capture.h` — local GPU material and monitor capture.

- `src/main.cpp` — Win32 window, input, persistence, tray, and rendering behavior.
- `src/markdown.h` / `src/markdown_preview.h` — local Markdown parsing, source-position mapping and DirectWrite/Direct2D preview; native EDIT retains input, IME and undo.
- `src/backdrop.h` — isolated Windows Composition host-backdrop integration.
- `src/platform.h` — small Windows helpers and atomic file writes.
- `src/localization.h` — English and Simplified Chinese strings.
- `src/version.h` / `src/resources.rc` — the single product version and executable metadata.
- `tests/regression.cpp` — hidden, deterministic native behavior checks.
- `tests/glass_markdown.cpp` — automatic preview/edit switching, source persistence, undo, IME lifecycle, wrapping and transparent text composition with isolated data.
- `tests/integration.cpp` — focused compositor and interaction checks that open temporary windows.
- `tests/visuals.cpp` — shadow, tint, text antialiasing, custom text color, and settings screenshot checks.
- `tests/visual_test_support.h` — shared fixtures and compositor capture helpers.
- `build.ps1` — MSVC discovery and x64/x86/ARM64 builds.
- `scripts/package.ps1` — portable release archive creation.

## Build and test

```powershell
.\build.ps1 -Architecture x64 -OutputDirectory build\x64
.\build.ps1 -Architecture x86 -OutputDirectory build\x86
.\build.ps1 -Test -Architecture x64 -OutputDirectory build\tests-x64
```

The default test source is `tests/regression.cpp`. A focused source can be selected explicitly:

```powershell
.\build.ps1 -Test -SourceFile tests\integration.cpp -OutputDirectory build\integration
.\build.ps1 -Test -SourceFile tests\visuals.cpp -OutputDirectory build\visuals
```

The integration test opens visible fixture windows and writes screenshots under its isolated output directory. Do not point tests at a user's portable `data` directory.

Run the visual checks in an interactive Windows 11 desktop with transparency effects enabled. They require a desktop large enough to display the 800 × 500 fixture and are not part of headless CI. They use synthetic note text; keep their generated screenshots and data out of source control.

## Versioning

Update `src/version.h` once. The executable resource, package script, release filenames, and release checks read that file. Use semantic versions and create an annotated `vX.Y.Z` tag only after the corresponding commit is on `main`.

## Release

```powershell
.\scripts\package.ps1 -Architecture x64
.\scripts\package.ps1 -Architecture x86
```

The GitHub workflow builds x64, x86, and native ARM64 packages, running native and glass regression tests. A `v*` tag creates a **draft** GitHub release with the three archives and `SHA256SUMS.txt`. Verify the assets and checksums, then publish the draft. Default `build.ps1` and `package.ps1` both use the 2.0 desktop entry.

Package staging uses a fixed file allowlist: EXE, bilingual README, quick start, project license and third-party notices. Never copy a development build directory or its `data` into a release. Packaging checks the PE architecture and embedded version before producing a ZIP.

## Design constraints

- Keep note data local and portable.
- Preserve a recovery path before enabling mouse click-through.
- Never replace unreadable or unsupported note data with an error string.
- Apply transparency only to the background; text must remain legible.
- Treat glass as optional. Editing must work when composition, transparency effects, or the backdrop API is unavailable.
- Avoid polling and animation loops while the note is idle.

ARM64 CI pins Microsoft.Direct3D.WARP 1.0.20 for offscreen HLSL tests because the runner's system WARP crashes during this fixture. FLOATNOTE_TEST_WARP selects that test-only DLL. Application builds and release archives continue to use Windows system components only; the pinned test DLL is never packaged.
