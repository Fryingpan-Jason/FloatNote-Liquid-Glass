#include "visual_test_support.h"
int main() {
    HWND fixture = nullptr;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        g_instance = GetModuleHandleW(nullptr);
        g_dataDirectory = ExecutableDirectory() / L"integration-data" / std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(g_dataDirectory);
        g_notePath = g_dataDirectory / L"note.txt";
        g_settingsPath = g_dataDirectory / L"settings.ini";
        Require(AtomicWrite(g_notePath, WideToUtf8(L"背景透明，文字清晰。\r\nWindows 毛玻璃 · FloatNote")),
                "isolated fixture note");
        WNDCLASSW bg{};
        bg.hInstance = g_instance;
        bg.lpfnWndProc = FixtureProc;
        bg.lpszClassName = L"FloatNote.QABackground";
        RegisterClassW(&bg);
        fixture = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, bg.lpszClassName, L"FloatNote visual fixture",
                                  WS_POPUP, 2200, 200, 720, 400, nullptr, nullptr, g_instance, nullptr);
        ShowWindow(fixture, SW_SHOWNOACTIVATE);
        UpdateWindow(fixture);
        WNDCLASSW note{};
        note.hInstance = g_instance;
        note.lpfnWndProc = NoteProc;
        note.lpszClassName = L"FloatNote.QACompositor";
        note.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&note);
        g_settings = Settings{};
        g_settings.width = 640;
        g_settings.height = 320;
        g_settings.opacityPercent = 40;
        g_settings.topmost = true;
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, note.lpszClassName, L"FloatNote visual check",
                        WS_POPUP | WS_CLIPCHILDREN, 2240, 240, 640, 320, fixture, nullptr, g_instance, nullptr);
        g_isVisible = true;
        ApplyInteractionMode();
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
        PumpFor(300);
        const auto plain = Capture(g_dataDirectory / L"01-transparent.png");
        g_settings.glass = true;
        ApplyVisuals(g_window);
        PumpFor(300);
        const auto glass = Capture(g_dataDirectory / L"02-glass.png");
        if (GetEnvironmentVariableW(L"FLOATNOTE_CORNER_CHECK", nullptr, 0)) {
            auto verifyCorners = [&](const std::vector<DWORD>& captured) {
                for (POINT point : {POINT{2, 2}, POINT{637, 2}, POINT{2, 317}, POINT{637, 317}}) {
                    const auto a = plain[point.y * 640 + point.x] & 0xffffff,
                               b = captured[point.y * 640 + point.x] & 0xffffff;
                    std::cout << "CORNER " << point.x << ',' << point.y << " plain=" << a << " glass=" << b << '\n';
                    int strongest = 0;
                    for (int shift : {8, 16})
                        if (((a >> shift) & 255) > ((a >> strongest) & 255))
                            strongest = shift;
                    const double shadow = double((b >> strongest) & 255) / std::max<DWORD>(1, (a >> strongest) & 255);
                    bool untouched = shadow >= 0.65 && shadow <= 1.02;
                    for (int shift : {0, 8, 16})
                        untouched &= std::abs(double((b >> shift) & 255) - ((a >> shift) & 255) * shadow) <= 2.0;
                    Require(untouched, "rounded corners retain backdrop colors with only neutral system shadow");
                }
            };
            verifyCorners(glass);
            const auto left = glass[210 * 640 + 110], right = glass[210 * 640 + 510];
            Require(int((left >> 16) & 255) - int((right >> 16) & 255) > 20 && int(right & 255) - int(left & 255) > 20,
                    "rounded glass still shows real background colors");
            Require(StripeContrast(glass, 640) < StripeContrast(plain, 640) * 0.6,
                    "rounded glass still blurs background detail");
            SetPassThrough(true);
            PumpFor(150);
            auto passing = Capture(g_dataDirectory / L"03-rounded-pass-through.png");
            verifyCorners(passing);
            Require(WindowFromPoint({2320, 420}) == fixture, "rounded glass still passes mouse input through");
            SetPassThrough(false);
            g_settings.glass = false;
            ApplyInteractionMode();
            SetOpacityPercent(55);
            PumpFor(150);
            Require(!g_nativeGlass && !g_glassActive && g_settings.opacityPercent == 55,
                    "ordinary transparency returns after rounded glass");
            Capture(g_dataDirectory / L"04-restored-normal.png");
            std::wcout << L"Corner evidence: " << g_dataDirectory << L'\n';
            SendMessageW(g_window, WM_CLOSE, 0, 0);
            DestroyWindow(fixture);
            return 0;
        }
        std::cout << "glassActive=" << g_glassActive << " plainStripeContrast=" << StripeContrast(plain, 640)
                  << " glassStripeContrast=" << StripeContrast(glass, 640) << '\n';
        if (SystemTransparencyEnabled() && !SystemHighContrast() && !SystemEnergySaver() &&
            !GetSystemMetrics(SM_REMOTESESSION)) {
            Require(g_glassActive, "native blur accepted by compositor");
            Require(StripeContrast(glass, 640) < StripeContrast(plain, 640) * 0.6,
                    "glass visibly blurs underlying stripe edges");
            const auto left = glass[210 * 640 + 110], right = glass[210 * 640 + 510];
            std::cout << "glassLeftRGB=" << ((left >> 16) & 255) << ',' << ((left >> 8) & 255) << ',' << (left & 255)
                      << " glassRightRGB=" << ((right >> 16) & 255) << ',' << ((right >> 8) & 255) << ','
                      << (right & 255) << '\n';
            Require(int((left >> 16) & 255) - int((right >> 16) & 255) > 20 && int(right & 255) - int(left & 255) > 20,
                    "glass retains distinct colors of real content behind both halves");
        }
        SetOpacityPercent(100);
        PumpFor(80);
        Require(g_settings.opacityPercent == 100 && EffectiveOpacityPercent() == 100,
                "glass permits full theme-color coverage");
        g_settings.glass = false;
        ApplyVisuals(g_window);
        Require(EffectiveOpacityPercent() == 100, "normal mode retains the same background opacity");
        SetOpacityPercent(100);
        PumpFor(150);
        const auto opaque = Capture(g_dataDirectory / L"03-opaque.png");
        Require(StripeContrast(opaque, 640) < 0.2, "100 percent background hides the underlying pattern");
        Require(opaque[210 * 640 + 110] != glass[210 * 640 + 110], "opacity changes actual backdrop contribution");
        POINT hit{2240 + 80, 240 + 180};
        Require(WindowFromPoint(hit) == g_window, "interactive blank background belongs to the note");
        SetPassThrough(true);
        PumpFor(120);
        Require(WindowFromPoint(hit) == fixture, "Windows hit testing passes through the complete note");
        Require((GetWindowLongPtrW(g_window, GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) ==
                    (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE),
                "pass-through does not activate or intercept input");
        SetPassThrough(false);
        PumpFor(120);
        Require(WindowFromPoint(hit) == g_window, "switching off pass-through restores interaction");
        g_settings.glass = true;
        SetOpacityPercent(0);
        PumpFor(150);
        Capture(g_dataDirectory / L"05-zero-glass.png");
        Require((g_surface.pixels[210 * 640 + 110] >> 24) == 1 && WindowFromPoint(hit) == g_window,
                "zero tint keeps an imperceptible input surface in editing mode");
        SetPassThrough(true);
        PumpFor(120);
        Require((g_surface.pixels[210 * 640 + 110] >> 24) == 0 && WindowFromPoint(hit) == fixture,
                "zero tint becomes exact zero alpha in pass-through mode");
        SetPassThrough(false);
        g_settings.glass = false;
        SetOpacityPercent(0);
        ApplyVisuals(g_window);
        PumpFor(120);
        const auto zeroPlain = Capture(g_dataDirectory / L"06-zero-plain.png");
        Require(StripeContrast(zeroPlain, 640) > StripeContrast(plain, 640),
                "zero tint reveals more underlying detail than 40 percent tint");
        SetOpacityPercent(55);
        PumpFor(120);
        SendMessageW(g_pill, BM_CLICK, 0, 0);
        PumpFor(120);
        Require(IsWindowVisible(g_menu) && !g_pointerDown,
                "accessible pill activation opens settings without capturing a drag");
        CaptureWindow(g_menu, g_dataDirectory / L"07-settings.png");
        const int chosenOpacity = g_settings.opacityPercent;
        SendMessageW(g_menu, WM_COMMAND, kMenuThemeBase, 0);
        PumpFor(80);
        Require(g_settings.themeColor == kThemeColors[0] && g_settings.opacityPercent == chosenOpacity,
                "warm theme button changes color without changing opacity");
        RECT normalMenu{};
        GetWindowRect(g_menu, &normalMenu);
        SendMessageW(g_menu, WM_COMMAND, kMenuGlass, 0);
        PumpFor(100);
        RECT glassMenu{};
        GetWindowRect(g_menu, &glassMenu);
        Require(g_settings.glass && IsWindowVisible(g_slider) && EffectiveOpacityPercent() == chosenOpacity &&
                    g_settings.opacityPercent == chosenOpacity,
                "glass mode retains the usable background opacity slider");
        Require(glassMenu.bottom - glassMenu.top == normalMenu.bottom - normalMenu.top,
                "settings layout stays stable when switching glass modes");
        CaptureWindow(g_menu, g_dataDirectory / L"09-glass-settings.png");
        SendMessageW(g_menu, WM_COMMAND, kMenuGlass, 0);
        PumpFor(100);
        Require(!g_settings.glass && IsWindowVisible(g_slider) && g_settings.opacityPercent == chosenOpacity,
                "switching back restores the slider and the exact prior value");
        SetFocus(g_slider);
        PostMessageW(g_slider, WM_KEYDOWN, VK_TAB, 0);
        PumpFor(60);
        Require(GetFocus() == g_menuGlass, "Tab moves within settings instead of behind the popup");
        SetFocus(g_slider);
        SendMessageW(g_slider, WM_KEYDOWN, VK_HOME, 0);
        PumpFor(60);
        Require(g_settings.opacityPercent == 0, "keyboard Home reaches zero background tint");
        SendMessageW(g_slider, WM_KEYDOWN, VK_END, 0);
        PumpFor(60);
        Require(g_settings.opacityPercent == 100, "keyboard End reaches full background tint");
        SetWindowLongPtrW(g_menu, GWL_STYLE, GetWindowLongPtrW(g_menu, GWL_STYLE) | WS_VSCROLL);
        SetWindowPos(g_menu, nullptr, 0, 0,
                     ScaleForDpi(g_window, 280) + GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(g_window)), 320,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SetFocus(g_menuAutostart);
        PostMessageW(g_menuAutostart, WM_KEYDOWN, VK_TAB, 0);
        PumpFor(80);
        RECT popup{}, visibleButton{};
        GetClientRect(g_menu, &popup);
        GetWindowRect(g_menuHide, &visibleButton);
        MapWindowPoints(nullptr, g_menu, reinterpret_cast<POINT*>(&visibleButton), 2);
        Require(g_menuScroll > 0 && GetFocus() == g_menuHide && visibleButton.top >= 0 &&
                    visibleButton.bottom <= popup.bottom,
                "small settings viewport scrolls focused controls fully into view");
        CaptureWindow(g_menu, g_dataDirectory / L"08-compact-settings.png");
        PostMessageW(g_slider, WM_KEYDOWN, VK_ESCAPE, 0);
        PumpFor(280);
        Require(!IsWindowVisible(g_menu) && IsWindow(g_window), "Escape closes settings without closing the note");
        BeginMarkdownEditing();
        const auto originalText = EditorText();
        SendMessageW(g_edit, EM_SETSEL, 0, -1);
        SendMessageW(g_edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"中文输入与撤销 😀"));
        PumpFor(80);
        Require(EditorText() == L"中文输入与撤销 😀",
                "native edit accepts a Unicode replacement without losing surrogate pairs");
        SendMessageW(g_edit, EM_UNDO, 0, 0);
        PumpFor(80);
        Require(EditorText() == originalText, "native undo restores the complete preceding text");
        SetFocus(nullptr);
        g_settings.glass = true;
        g_settings.themeColor = RGB(38, 42, 48);
        RefreshTheme();
        ApplyVisuals(g_window);
        PumpFor(150);
        Capture(g_dataDirectory / L"04-dark-glass.png");
        const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int i = 0; i < 60; ++i) {
            g_settings.themeColor = kThemeColors[i % 5];
            RefreshTheme();
            ChangeFontSize(i % 2 ? -1 : 1);
            SetWindowPos(g_window, nullptr, 0, 0, 640 + i % 2 * 40, 320 + i % 2 * 20,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            PumpFor(2);
        }
        PumpFor(300);
        Require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore + 3,
                "theme/font/resize stress does not leak GDI objects");
        Require(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore + 3,
                "theme/font/resize stress does not leak USER objects");
        // Drain deferred persistence before measuring idle; a legitimate final
        // save can update the status pill and must not be mistaken for a loop.
        SavePendingNote();
        KillTimer(g_window, 2);
        SaveSettings();
        SetFocus(nullptr);
        KillTimer(g_window, kCaretTimer);
        DwmFlush();
        PumpFor(300);
        const auto renderedBefore = g_renderCount;
        const auto before = CpuTicks();
        PumpFor(2000);
        const auto elapsed = CpuTicks() - before;
        std::cout << "idleCpuMilliseconds=" << elapsed / 10000.0 << " idleRenders=" << g_renderCount - renderedBefore
                  << " GDI=" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << '\n';
        Require(g_renderCount == renderedBefore, "unfocused idle note has no repaint loop");
        Require(elapsed < 1500000, "idle note uses less than 150 ms CPU over 2 seconds");
        SendMessageW(g_window, WM_CLOSE, 0, 0);
        DestroyWindow(fixture);
        CoUninitialize();
        std::wcout << L"Evidence: " << g_dataDirectory << L'\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (IsWindow(g_window))
            DestroyWindow(g_window);
        if (IsWindow(fixture))
            DestroyWindow(fixture);
        return 1;
    }
}
