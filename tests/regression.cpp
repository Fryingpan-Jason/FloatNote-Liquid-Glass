// Native regression checks. All windows stay hidden; all data is isolated.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}

LRESULT CALLBACK TestProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    // Do not register a tray icon, hooks, or hotkeys during headless checks.
    if (message == WM_COMMAND || message == WM_CLOSE || message == WM_GETMINMAXINFO || message == WM_PRINTCLIENT ||
        message == WM_DRAWITEM || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC)
        return WindowProcedure(window, message, wp, lp);
    return DefWindowProcW(window, message, wp, lp);
}

int main() {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        g_instance = GetModuleHandleW(nullptr);
        g_dataDirectory = ExecutableDirectory() / L"regression-data" / std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(g_dataDirectory);
        g_notePath = g_dataDirectory / L"note.txt";
        g_settingsPath = g_dataDirectory / L"settings.ini";
        WNDCLASSW klass{};
        klass.hInstance = g_instance;
        klass.lpfnWndProc = TestProcedure;
        klass.lpszClassName = L"FloatNote.HiddenRegression";
        RegisterClassW(&klass);
        g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, klass.lpszClassName, L"Hidden regression",
                                   WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1000, 600, nullptr, nullptr, g_instance, nullptr);
        g_isVisible = false;
        CreateControls(g_window);

        Check(NormalizeNewlines(L"a\nb\r\nc\rd") == L"a\r\nb\r\nc\r\nd", "LF/CRLF/CR import");
        const std::wstring expected = L"中文 English 123\r\n第二行 😀\r\n";
        SetWindowTextW(g_edit, expected.c_str());
        Check(g_dirty, "UIA/WM_SETTEXT marks note dirty");
        SavePendingNote();
        Check(!g_dirty && !g_saveFailed && ReadBytes(g_notePath) == WideToUtf8(expected), "UTF-8 atomic save");
        g_loadingText = true;
        SetWindowTextW(g_edit, L"different unsaved buffer");
        g_loadingText = false;
        LoadNote();
        Check(EditorText() == expected, "saved multiline note reload");

        HANDLE lock = CreateFileW(g_notePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(lock != INVALID_HANDLE_VALUE, "create save-failure fixture");
        SetWindowTextW(g_edit, L"replacement");
        SavePendingNote();
        Check(g_saveFailed && g_dirty && ReadBytes(g_notePath) == WideToUtf8(expected),
              "failed replacement preserves file and dirty buffer");
        CloseHandle(lock);
        SavePendingNote();
        Check(!g_dirty && !g_saveFailed && ReadBytes(g_notePath) == "replacement", "save retries after file unlock");

        g_settings.topmost = true;
        ApplyInteractionMode(true);
        const auto passFlags = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        Check(!(GetWindowLongPtrW(g_window, GWL_EXSTYLE) & passFlags), "pinned note stays interactive");
        Check((GetWindowLongPtrW(g_edit, GWL_STYLE) & WS_VISIBLE) != 0, "editor remains available");
        SetOpacityPercent(70);
        g_isVisible = true;
        RenderLayeredWindow();
        g_isVisible = false;
        Check(g_surface.pixels && (g_surface.pixels[50 * g_surface.width + 10] >> 24) == 179,
              "70 percent applies to background pixel alpha");
        bool opaqueText = false;
        const DWORD textPixel = PremultiplyPixel(kText, 255);
        for (int i = 0; i < g_surface.width * g_surface.height; ++i)
            if (g_surface.pixels[i] == textPixel)
                opaqueText = true;
        Check(opaqueText, "native text remains fully opaque over translucent background");
        g_settings.topmost = false;
        ApplyInteractionMode();
        Check(!(GetWindowLongPtrW(g_window, GWL_EXSTYLE) & passFlags), "unpinned note stays interactive");
        Check(!IsWindowVisible(g_window), "settings never reveal an explicitly hidden note");
        RECT originalBounds{}, zoomBounds{};
        GetWindowRect(g_window, &originalBounds);
        const int initialFont = g_settings.fontSize;
        SendMessageW(g_edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
        GetWindowRect(g_window, &zoomBounds);
        Check(g_settings.fontSize == initialFont + 1 && EqualRect(&originalBounds, &zoomBounds),
              "Ctrl+wheel enlarges font without resizing note");
        SendMessageW(g_edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -WHEEL_DELTA), 0);
        Check(g_settings.fontSize == initialFont, "Ctrl+wheel restores font size");
        SendMessageW(g_edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        Check(g_settings.fontSize == initialFont, "ordinary wheel does not zoom");
        Check(!(GetWindowLongPtrW(g_window, GWL_STYLE) & (WS_THICKFRAME | WS_CAPTION | WS_MAXIMIZEBOX)),
              "no system frame or double-click maximize");
        Check(HitTestWindow(g_window, MAKELPARAM(1, 1)) == HTCLIENT, "window edges never enter system resize");
        Check(IsWindow(g_grip) && IsWindow(g_pill), "explicit resize grip and pill available");
        bool sliderMapping = true;
        for (int value = 0; value <= 100; ++value)
            sliderMapping =
                sliderMapping && SliderValueFromPixel(SliderPixelFromValue(value, 25, 275), 25, 275) == value;
        Check(sliderMapping, "slider drawing and input share exact 0-100 percent mapping");
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"Test opacity", WS_CHILD | TBS_NOTICKS, 0, 0, 300, 60,
                                      g_window, nullptr, g_instance, nullptr);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SetWindowSubclass(slider, SliderProcedure, 1, 0);
        SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 30));
        SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(150, 30));
        Check(g_settings.opacityPercent == 50, "clicking drawn track midpoint applies 50 percent");
        SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 30));
        SendMessageW(slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(299, 30));
        SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(299, 30));
        g_isVisible = true;
        RenderLayeredWindow();
        g_isVisible = false;
        Check(g_settings.opacityPercent == 100 && (g_surface.pixels[50 * g_surface.width + 10] >> 24) == 255,
              "dragging track right reaches actual full background opacity");
        DestroyWindow(slider);
        g_pointerWindow = originalBounds;
        ResizeWindowFromPointerDelta(-25, -15);
        RECT resized{};
        GetWindowRect(g_window, &resized);
        Check(resized.right - resized.left == originalBounds.right - originalBounds.left - 25 &&
                  resized.bottom - resized.top == originalBounds.bottom - originalBounds.top - 15,
              "bottom-right grip resizes both dimensions");
        g_pointerWindow = resized;
        MoveWindowFromPointerDelta(30, 20);
        RECT moved{};
        GetWindowRect(g_window, &moved);
        Check(moved.left == resized.left + 30 && moved.top == resized.top + 20 &&
                  moved.right - moved.left == resized.right - resized.left,
              "pill drag moves without resizing");
        SetWindowPos(g_window, nullptr, 0, 0, originalBounds.right - originalBounds.left,
                     originalBounds.bottom - originalBounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowTextW(g_edit, L"Hello\r\n\r\n世界");
        const LRESULT glyph = SendMessageW(g_edit, EM_POSFROMCHAR, 0, 0);
        const POINT glyphPoint{GET_X_LPARAM(glyph) + 2, GET_Y_LPARAM(glyph) + 3};
        Check(IsTextPoint(g_edit, glyphPoint), "text hit test recognizes visible characters");
        RECT editorBounds{};
        GetClientRect(g_edit, &editorBounds);
        const POINT blank{editorBounds.right - 6, editorBounds.bottom - 6};
        Check(!IsTextPoint(g_edit, blank), "blank editor area is not treated as text");
        SendMessageW(g_edit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(glyphPoint.x, glyphPoint.y));
        Check(!g_pointerDown, "pressing text stays in native editing and selection");
        SendMessageW(g_edit, WM_LBUTTONUP, 0, MAKELPARAM(glyphPoint.x, glyphPoint.y));
        GetWindowRect(g_window, &originalBounds);
        g_pointerWindow = originalBounds;
        MoveWindowFromPointerDelta(-23, -13);
        GetWindowRect(g_window, &moved);
        Check(moved.left == originalBounds.left - 23 && moved.top == originalBounds.top - 13 &&
                  moved.right - moved.left == originalBounds.right - originalBounds.left &&
                  EditorText() == L"Hello\r\n\r\n世界",
              "dragging blank text area moves window without changing content or size");
        SetWindowTextW(g_edit, L"");
        Check(IsTextPoint(g_edit, {10, 10}), "empty notes remain clickable for input");
        HDC paintDc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 80;
        info.bmiHeader.biHeight = -24;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(paintDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HGDIOBJ previousBitmap = SelectObject(paintDc, bitmap);
        RECT pillRect{0, 0, 80, 24};
        FillRect(paintDc, &pillRect, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SmoothRoundRect(paintDc, pillRect, 12.0f, RGB(44, 44, 44));
        int blendedPixels = 0;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 80; ++x) {
                const int channel = GetRValue(GetPixel(paintDc, x, y));
                if (channel > 44 && channel < 255)
                    ++blendedPixels;
            }
        Check(blendedPixels > 0, "pill edge contains antialiased coverage pixels");
        SelectObject(paintDc, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(paintDc);
        std::wstring longNote;
        for (int line = 0; line < 50; ++line)
            longNote += L"Scroll test line\r\n";
        SetWindowTextW(g_edit, longNote.c_str());
        SendMessageW(g_edit, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        Check(SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0) > 0,
              "ordinary wheel scrolls overflowing text without a visible scrollbar");
        SetWindowTextW(g_edit, L"");
        SavePendingNote();
        LoadNote();
        Check(EditorText().empty() && ReadBytes(g_notePath).empty(), "empty note survives save/reload");

        Check(AtomicWrite(g_notePath, std::string("\xef\xbb\xbf") + WideToUtf8(expected)), "create UTF-8 BOM fixture");
        LoadNote();
        Check(EditorText() == expected && !g_loadFailed, "UTF-8 BOM is not inserted into the editor");
        HANDLE readLock =
            CreateFileW(g_notePath.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(readLock != INVALID_HANDLE_VALUE, "create unreadable-note fixture");
        LoadNote();
        Check(g_loadFailed && (GetWindowLongPtrW(g_edit, GWL_STYLE) & ES_READONLY) && !SaveNote(),
              "read failure cannot turn an error message into a replacement note");
        CloseHandle(readLock);
        LoadNote();
        Check(!g_loadFailed && EditorText() == expected, "original note loads after the read lock is released");
        Check(AtomicWrite(g_notePath, std::string("a\0b", 3)), "create unsupported encoding fixture");
        LoadNote();
        Check(g_loadFailed && !SaveNote() && ReadBytes(g_notePath) == std::string("a\0b", 3),
              "embedded NUL data is protected from truncation");
        const std::string oversized(4 * 1024 * 1024 + 1, 'x');
        Check(AtomicWrite(g_notePath, oversized), "create oversized note fixture");
        LoadNote();
        Check(g_loadFailed && !SaveNote() && std::filesystem::file_size(g_notePath) == oversized.size(),
              "oversized notes remain intact and are not edited");
        Check(AtomicWrite(g_notePath, WideToUtf8(expected)), "restore readable fixture");
        LoadNote();

        bool contrastOk = true;
        for (int red = 0; red <= 255; red += 17)
            for (int green = 0; green <= 255; green += 17)
                for (int blue = 0; blue <= 255; blue += 17) {
                    const auto background = RGB(red, green, blue);
                    const double a = Luminance(background), b = Luminance(ContrastText(background));
                    contrastOk &= (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05) >= 4.5;
                }
        Check(contrastOk, "4096 custom colors select readable text with at least 4.5:1 contrast on opaque backgrounds");
        g_settings.language = UiLanguage::Chinese;
        Check(std::wstring(Text().settingsTitle) == L"便签设置" && std::wstring(Text().exit) == L"退出",
              "explicit Chinese localization resolves without changing note content");
        g_settings.language = UiLanguage::English;
        Check(std::wstring(Text().settingsTitle) == L"Note settings" && std::wstring(Text().exit) == L"Exit",
              "explicit English localization resolves without changing note content");
        g_settings.themeColor = RGB(123, 47, 173);
        g_settings.opacityPercent = 63;
        g_settings.glass = false;
        g_settings.passThrough = true;
        SaveSettings();
        const auto settingsBefore = ReadBytes(g_settingsPath);
        g_settings = Settings{};
        LoadSettings();
        Check(g_settings.themeColor == RGB(123, 47, 173) && g_settings.opacityPercent == 63 && !g_settings.glass &&
                  g_settings.passThrough && g_settings.language == UiLanguage::English,
              "theme, alpha, glass, pass-through and language round-trip together");
        HANDLE settingsLock = CreateFileW(g_settingsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(settingsLock != INVALID_HANDLE_VALUE, "create settings failure fixture");
        g_settings.themeColor = RGB(1, 2, 3);
        SaveSettings();
        Check(g_settingsFailed && ReadBytes(g_settingsPath) == settingsBefore,
              "failed settings replacement preserves the complete previous preferences");
        CloseHandle(settingsLock);
        SaveSettings();
        Check(!g_settingsFailed, "settings save retries after permission is restored");
        g_settings.glass = false;
        SetOpacityPercent(67);
        g_settings.glass = true;
        ApplyVisuals(g_window);
        SetOpacityPercent(12);
        Check(EffectiveOpacityPercent() == 12 && g_settings.opacityPercent == 12,
              "glass uses the same adjustable background opacity as ordinary mode");
        g_settings.glass = false;
        ApplyVisuals(g_window);
        Check(EffectiveOpacityPercent() == 12, "background opacity survives the glass round trip");
        SetOpacityPercent(0);
        g_isVisible = true;
        RenderLayeredWindow();
        g_isVisible = false;
        bool coverageLevels[256]{};
        for (int i = 0; i < g_surface.width * g_surface.height; ++i)
            coverageLevels[g_surface.pixels[i] >> 24] = true;
        int partialLevels = 0;
        for (int alpha = 2; alpha < 255; ++alpha)
            partialLevels += coverageLevels[alpha];
        Check(partialLevels > 0, "composited pill retains fractional-alpha edge coverage");
        bool premultiplied = true;
        for (int i = 0; i < g_surface.width * g_surface.height; ++i) {
            const DWORD pixel = g_surface.pixels[i], alpha = pixel >> 24;
            premultiplied &= ((pixel >> 16) & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && (pixel & 255) <= alpha;
        }
        Check(premultiplied, "edge pixels remain valid premultiplied colors without bright halos");
        g_settings.themeColor = RGB(248, 247, 243);
        g_settings.passThrough = false;
        g_settings.glass = true;
        g_settings.opacityPercent = 40;
        RefreshTheme();
        ApplyInteractionMode();
        g_highContrast = true;
        ApplyVisuals(g_window);
        g_isVisible = true;
        RenderLayeredWindow();
        g_isVisible = false;
        Check(!g_glassActive && g_settings.glass && (g_surface.pixels[50 * g_surface.width + 10] >> 24) == 255,
              "high contrast suppresses blur and transparency without erasing user preferences");
        RefreshTheme();
        ApplyVisuals(g_window);
        Check(g_settings.opacityPercent == 40 && g_settings.glass,
              "leaving accessibility fallback retains chosen effects");

        RECT negative = ConstrainToWorkArea({-5000, -50, -4400, 270}, {-1920, 0, 0, 1080});
        Check(negative.left == -1920 && negative.top == 0 && negative.right - negative.left == 600,
              "off-screen notes recover on a monitor with negative coordinates");
        RECT oversizedRect = ConstrainToWorkArea({1900, 900, 6900, 5900}, {0, 0, 1920, 1040});
        Check(oversizedRect.left == 0 && oversizedRect.top == 0 && oversizedRect.right == 1920 &&
                  oversizedRect.bottom == 1040,
              "smaller replacement displays fit the whole note into their work area");

        SetWindowTextW(g_edit, L"hello\r\nnext");
        GetWindowRect(g_window, &originalBounds);
        POINT lineEnd{static_cast<LONG>(editorBounds.right - 10), 5};
        MapWindowPoints(g_edit, g_window, &lineEnd, 1);
        SendMessageW(g_window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(lineEnd.x, lineEnd.y));
        SendMessageW(g_window, WM_LBUTTONUP, 0, MAKELPARAM(lineEnd.x, lineEnd.y));
        DWORD selectionStart = 0, selectionEnd = 0;
        SendMessageW(g_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                     reinterpret_cast<LPARAM>(&selectionEnd));
        GetWindowRect(g_window, &moved);
        Check(EqualRect(&originalBounds, &moved) && !g_markdownEditing && EditorText()==L"hello\r\nnext",
              "clicking blank line space returns to preview; only dragging moves the note");

        std::wstring largeNote;
        for (int line = 0; line < 4500; ++line)
            largeNote += L"0123456789abcdefghij\r\n";
        SetWindowTextW(g_edit, largeNote.c_str());
        SendMessageW(g_edit, EM_LINESCROLL, 0, 3500);
        const int visibleLine = static_cast<int>(SendMessageW(g_edit, EM_GETFIRSTVISIBLELINE, 0, 0));
        const int visibleIndex = static_cast<int>(SendMessageW(g_edit, EM_LINEINDEX, visibleLine, 0));
        const auto visiblePos = SendMessageW(g_edit, EM_POSFROMCHAR, visibleIndex, 0);
        Check(visibleIndex > 65535 && IsTextPoint(g_edit, {GET_X_LPARAM(visiblePos) + 2, GET_Y_LPARAM(visiblePos) + 3}),
              "text hit testing still works beyond the 16-bit character index boundary");
        const auto* cache = g_hitText.data();
        for (int i = 0; i < 100; ++i)
            IsTextPoint(g_edit, {GET_X_LPARAM(visiblePos) + 2, GET_Y_LPARAM(visiblePos) + 3});
        Check(g_hitText.data() == cache && !g_hitTextDirty,
              "pointer movement reuses the text cache instead of copying a large note");
        SetWindowTextW(g_edit, expected.c_str());
        SendMessageW(g_window, WM_CLOSE, 0, 0);
        Check(!IsWindow(g_window) && ReadBytes(g_notePath) == WideToUtf8(expected),
              "close saves before child destruction");
        Check(!SaveNote(), "destroyed editor cannot overwrite a saved note");
        std::cout << "All native regression checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
