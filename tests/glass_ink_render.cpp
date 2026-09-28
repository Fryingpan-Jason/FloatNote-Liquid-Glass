// Exercise final composited pixels, not only the selected ink variable.
// Hidden windows and synthetic text; no capture, hooks, tray or personal data.
#include "../experiments/local_desktop.cpp"
#include <iostream>
#include <stdexcept>

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static LRESULT CALLBACK Fixture(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_COMMAND || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC ||
        message == WM_DRAWITEM)
        return WindowProcedure(window, message, wp, lp);
    return DefWindowProcW(window, message, wp, lp);
}
static void CheckPixels(const char* label) {
    const DWORD expected = PremultiplyPixel(kNoteText, 255);
    RenderLayeredWindow();
    GdiFlush();
    RECT host{}, editor{};
    GetWindowRect(g_window, &host); GetWindowRect(g_edit, &editor);
    OffsetRect(&editor, -host.left, -host.top);
    int samples = 0, incorrect = 0;
    for (int y = 0; y < g_textOnBlack.height; ++y) {
        for (int x = 0; x < g_textOnBlack.width; ++x) {
            const int index = y * g_textOnBlack.width + x;
            const auto black = g_textOnBlack.pixels[index] & 0xffffff;
            // Solid glyph interiors are white in both masks. Antialias edges
            // differ; both must use the current ink after composition.
            if ((black & 255) < 250) continue;
            const int sx = editor.left + x, sy = editor.top + y;
            if (sx < 0 || sy < 0 || sx >= g_surface.width || sy >= g_surface.height) continue;
            const DWORD pixel = g_surface.pixels[sy * g_surface.width + sx];
            ++samples;
            for (int shift : {0, 8, 16}) {
                if (std::abs(int((pixel >> shift) & 255) - int((expected >> shift) & 255)) > 8) {
                    ++incorrect; break;
                }
            }
        }
    }
    std::cout << label << " glyphs=" << samples << " incorrect=" << incorrect << '\n';
    Check(samples > 50, "fixture did not render enough glyph interiors");
    Check(incorrect == 0, "composited glyph pixels do not follow current ink");
}
struct GlassAutoInkTest {
    static void Disable() { g_backdrop.enabled = false; g_backdrop.mode = -1; }
    static void Seed(bool white) {
        g_backdrop.mode = 2;
        g_backdrop.enabled = g_backdrop.haveDesktop = g_backdrop.inkFrameValid = true;
        g_backdrop.inkEnabled = true;
        g_backdrop.inkTint = kBackground;
        g_backdrop.inkAlpha = MulDiv(EffectiveOpacityPercent(), 255, 100);
        g_backdrop.inkDirty = g_backdrop.inkPending = false;
        // The shader/readback itself is exercised by glass_auto_ink.cpp.
        // Keep this synthetic result inside its refresh interval while painting.
        g_backdrop.inkSubmittedAt = GlassClockMs();
        g_backdrop.inkLastShare = white ? 1 : 0;
        g_backdrop.inkDecision.Reset(white); g_backdrop.inkDecision.valid = true;
    }
    static void Paint(bool white) {
        Seed(white); SyncAdaptiveInk(); Sleep(200);
        Seed(white); SyncAdaptiveInk();
        Check(kNoteText == (white ? GlassAutoInk::Light : GlassAutoInk::Dark), "sampled ink did not settle");
        CheckPixels(white ? "sampled-dark-background" : "sampled-light-background");
    }
};
int main() {
    try {
        std::cout << std::unitbuf;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SmoothGraphicsRuntime graphics;
        g_instance = GetModuleHandleW(nullptr); g_isVisible = false;
        g_backdrop.mode = -1; g_settings.opacityPercent = 16; g_settings.fontSize = 18;
        WNDCLASSW klass{}; klass.hInstance = g_instance; klass.lpfnWndProc = Fixture;
        klass.lpszClassName = L"FloatNote.InkPixelsFixture"; RegisterClassW(&klass);
        g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, klass.lpszClassName, L"",
            WS_POPUP | WS_CLIPCHILDREN, 100, 100, 640, 300, nullptr, nullptr, g_instance, nullptr);
        Check(g_window != nullptr, "hidden fixture creation failed");
        CreateControls(g_window); g_loadingText = true;
        SetWindowTextW(g_edit, L"Background contrast 中文 ABC\r\nSecond line");
        g_loadingText = false; g_isVisible = true;
        for (bool editing : {false, true}) {
#if FLOATNOTE_VERSION_MINOR >= 2
            g_markdownEditing = editing;
#endif
            for (bool dark : {false, true}) {
                g_settings.themeColor = dark ? RGB(20, 20, 20) : RGB(245, 245, 245);
                RefreshTheme();
                CheckPixels(editing ? (dark ? "edit-dark" : "edit-light") : (dark ? "preview-dark" : "preview-light"));
            }
            g_nativeGlass = g_glassActive = true;
            GlassAutoInkTest::Paint(true);
            GlassAutoInkTest::Paint(false);
            GlassAutoInkTest::Disable();
            g_nativeGlass = g_glassActive = false;
        }
        SetWindowLongPtrW(g_edit, GWL_EXSTYLE, GetWindowLongPtrW(g_edit, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
        SetWindowPos(g_edit, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RefreshTheme();
        CheckPixels("client-edge");
        g_closing = true; g_isVisible = false; DestroyWindow(g_window); g_window = g_edit = nullptr;
        std::cout << "PASS final ink pixels in preview and editing on both theme backgrounds\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
