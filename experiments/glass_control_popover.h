#pragma once

// A capturable, independent control surface. It deliberately uses no screen
// capture: the note's optical renderer and the settings UI have separate lives.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "../src/backdrop.h"

namespace GlassControlPopover {

enum class Action {
    ToggleTopmost, TogglePassThrough, MaterialLiquid, MaterialAcrylic, MaterialSolid,
    BackgroundClear, BackgroundPreset, AddBackgroundColor, TextAutomatic, TextPreset, AddTextColor, Opacity, Blur,
    SmallerText, LargerText, CloseBehavior, OpenLab, HideNote, ExitApp, Close
};

struct State {
    bool pinned = false, passThrough = false, autoText = true;
    // UI material identifiers: 2 = liquid, 1 = acrylic, 0 = solid.
    int material = 2, opacity = 0, fontSize = 12;
    // Quarter units: 0..80 represents a blur strength of 0..20.
    int blur = 0;
    // Close button: 0 = ask each time, 1 = hide to tray, 2 = exit the app.
    int closeBehavior = 0;
    COLORREF background = RGB(255, 255, 255), text = RGB(20, 20, 20);
    std::array<COLORREF, 5> backgroundColors{
        RGB(248, 247, 243), RGB(220, 233, 219), RGB(221, 233, 245), RGB(243, 222, 227), RGB(38, 42, 48)};
    std::array<COLORREF, 5> textColors{
        RGB(20, 20, 20), RGB(255, 255, 255), RGB(112, 116, 123), RGB(211, 49, 59), RGB(39, 105, 205)};
};

class Panel {
public:
    Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    ~Panel() { Destroy(); }

    bool Create(HINSTANCE instance, std::function<void(Action, int)> action,
                std::function<void()> onClosed = {}) {
        if (window_) return true;
        instance_ = instance;
        action_ = std::move(action);
        onClosed_ = std::move(onClosed);
        Gdiplus::GdiplusStartupInput startup;
        if (Gdiplus::GdiplusStartup(&graphicsToken_, &startup, nullptr) != Gdiplus::Ok) return false;
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = instance;
        type.lpfnWndProc = WindowProcedure;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.lpszClassName = ClassName;
        RegisterClassExW(&type);
        // No owner: WDA_EXCLUDEFROMCAPTURE on the note cannot hide this surface.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, ClassName, L"便签控制",
                                  WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN,
                                  0, 0, 320, 528, nullptr, nullptr, instance, this);
        if (!window_) { Destroy(); return false; }
        SetWindowDisplayAffinity(window_, WDA_NONE);
        MARGINS margin{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(window_, &margin);
        const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(window_, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        // The canvas and window region already share the card radius. System
        // rounding adds a second, smaller-radius outline outside that surface.
        const DWORD roundCorners = 1; // DWMWCP_DONOTROUND
        DwmSetWindowAttribute(window_, static_cast<DWMWINDOWATTRIBUTE>(33), &roundCorners, sizeof(roundCorners));
        canvas_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                                  L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 320, 528,
                                  window_, nullptr, instance, nullptr);
        if (!canvas_) { Destroy(); return false; }
        BuildControls();
        if (!slider_ || !blurSlider_ || controls_.size() != 28 || std::any_of(controls_.begin(), controls_.end(),
                [](const Control& control) { return !control.window; })) { Destroy(); return false; }
        return true;
    }

    void Show(HWND anchorWindow, const RECT& noteRect, UINT dpi, const State& state,
              HWND extraAnchorWindow = nullptr) {
        if (!window_) return;
        anchor_ = anchorWindow;
        extraAnchor_ = extraAnchorWindow;
        dpi_ = dpi ? dpi : 96;
        scale_ = static_cast<float>(dpi_) / 96.0f;
        state_ = state;
        state_.opacity = std::clamp(state_.opacity, 0, 100);
        state_.blur = std::clamp(state_.blur, 0, 80);
        state_.fontSize = std::max(1, state_.fontSize);
        state_.closeBehavior = std::clamp(state_.closeBehavior, 0, 2);
        scroll_ = 0;
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&noteRect, MONITOR_DEFAULTTONEAREST), &monitor);
        work_ = monitor.rcWork;
        noteRect_ = noteRect;
        width_ = std::min(Px(320), std::max(Px(200), static_cast<int>(work_.right - work_.left) - Px(16)));
        height_ = std::min(Px(ContentHeight()), std::max(Px(150), static_cast<int>(work_.bottom - work_.top) - Px(16)));
        // Narrow screens shrink only horizontal spacing. Text and targets retain
        // their DPI size; short work areas scroll the body below a fixed header.
        logicalWidth_ = static_cast<float>(width_) / scale_;
        target_ = Place(noteRect);
        RECT overlap{};
        useBackdrop_ = !IntersectRect(&overlap, &target_, &noteRect) && !HighContrast();
        closing_ = false;
        modalOpen_ = false;
        ResetHoverVisuals();
        closedNotified_ = false;
        opacity_ = 255;
        mouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        SetWindowPos(window_, HWND_TOPMOST, target_.left, target_.top, width_, height_,
                     SWP_NOACTIVATE | SWP_NOREDRAW | SWP_FRAMECHANGED);
        const int diameter = Px(40);
        HRGN shape = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, diameter, diameter);
        if (shape && !SetWindowRgn(window_, shape, FALSE)) DeleteObject(shape);
        useBackdrop_ = backdrop_.Enable(window_, useBackdrop_) && useBackdrop_;
        backdrop_.Resize(width_, height_);
        presenting_ = true;
        Layout();
        labelsReady_ = false;
        UpdateState(state_);
        BOOL animations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        animated_ = animations != FALSE && !HighContrast();
        animationStart_ = GetTickCount64();
        animationFrom_ = animated_ ? 0.0f : 1.0f;
        animationTo_ = 1.0f;
        opacity_ = animated_ ? 0 : 255;
        const int travel = animated_ ? Px(14) : 0;
        // Place the first visible frame on the fade, not at the resting position.
        SetWindowPos(window_, HWND_TOPMOST, target_.left, target_.top + travel, width_, height_,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        backdrop_.SetOpacity(static_cast<float>(opacity_) / 255.0f);
        Render();
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        if (!controls_.empty()) SetFocus(controls_[0].window);
        SetTimer(window_, AnimationTimer, 16, nullptr);
        SetTimer(window_, OutsideTimer, 60, nullptr);
    }

    void UpdateState(const State& state) {
        const bool materialLayoutChanged = (state_.material == 2) != (state.material == 2);
        const bool same = state_.pinned == state.pinned && state_.passThrough == state.passThrough &&
            state_.autoText == state.autoText &&
            state_.material == state.material && state_.opacity == std::clamp(state.opacity, 0, 100) &&
            state_.blur == std::clamp(state.blur, 0, 80) &&
            state_.fontSize == state.fontSize && state_.background == state.background && state_.text == state.text &&
            state_.backgroundColors == state.backgroundColors && state_.textColors == state.textColors &&
            state_.closeBehavior == std::clamp(state.closeBehavior, 0, 2);
        state_ = state;
        state_.opacity = std::clamp(state_.opacity, 0, 100);
        state_.blur = std::clamp(state_.blur, 0, 80);
        state_.closeBehavior = std::clamp(state_.closeBehavior, 0, 2);
        if (!window_ || !presenting_) { labelsReady_ = false; return; }
        if (materialLayoutChanged && Visible()) ResizeForMaterial();
        if (same && labelsReady_) return;
        labelsReady_ = true;
        auto set = [&](Action action, const std::wstring& label) {
            for (auto& item : controls_) if (item.action == action) SetWindowTextW(item.window, label.c_str());
        };
        set(Action::ToggleTopmost, state_.pinned ? L"置顶：开启" : L"置顶：关闭");
        set(Action::TogglePassThrough, state_.passThrough ? L"鼠标穿透：开启，点击关闭" : L"鼠标穿透：关闭，点击开启");
        for (const auto& item : controls_) {
            if (item.action == Action::BackgroundPreset || item.action == Action::TextPreset) {
                const COLORREF color = item.action == Action::BackgroundPreset ?
                    state_.backgroundColors[item.value] : state_.textColors[item.value];
                const std::wstring label = std::wstring(item.action == Action::BackgroundPreset ? L"背景颜色：" : L"文字颜色：") + ColorLabel(color);
                SetWindowTextW(item.window, label.c_str());
            }
        }
        for (const auto& item : controls_) if (item.action == Action::CloseBehavior) {
            std::wstring label = L"点击关闭按钮时：";
            label += CloseBehaviors[item.value];
            label += state_.closeBehavior == item.value ? L"，已选中" : L"，点击选择";
            SetWindowTextW(item.window, label.c_str());
        }
        SendMessageW(slider_, TBM_SETPOS, TRUE, state_.opacity);
        SetWindowTextW(slider_, (L"背景遮色，" + std::to_wstring(state_.opacity) + L"%").c_str());
        SendMessageW(blurSlider_, TBM_SETPOS, TRUE, state_.blur);
        SetWindowTextW(blurSlider_, (L"模糊强度，" + BlurLabel()).c_str());
        Render();
    }

    // Keep this panel alive while an asynchronously dispatched native color
    // dialog takes focus. The caller resumes it after either OK or Cancel.
    void SetModalOpen(bool open) {
        modalOpen_ = open;
        if (!window_) return;
        if (open) KillTimer(window_, OutsideTimer);
        else if (Visible()) {
            mouseWasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            SetTimer(window_, OutsideTimer, 60, nullptr);
        }
    }

    void Close(bool animate = true) {
        if (!Visible() || closing_) return;
        KillTimer(window_, OutsideTimer);
        if (animate && animated_) {
            closing_ = true;
            animationStart_ = GetTickCount64();
            animationFrom_ = opacity_ / 255.0f;
            animationTo_ = 0.0f;
            SetTimer(window_, AnimationTimer, 16, nullptr);
        } else FinishClose();
    }

    bool Visible() const { return window_ && IsWindowVisible(window_) != FALSE && !closing_; }
    HWND Window() const { return window_; }

    bool HandleKey(const MSG& message) {
        if (!Visible() || modalOpen_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            RequestClose();
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
            HWND next = GetNextDlgTabItem(window_, GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (next) { EnsureVisible(next); SetFocus(next); }
            return true;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd != slider_ && message.hwnd != blurSlider_) {
            if (IsChild(window_, GetFocus())) SendMessageW(GetFocus(), BM_CLICK, 0, 0);
            return true;
        }
        return false;
    }

    void Destroy() {
        presenting_ = false;
        onClosed_ = {};
        action_ = {};
        if (window_) {
            KillTimer(window_, AnimationTimer);
            KillTimer(window_, OutsideTimer);
        }
        backdrop_.Close();
        if (window_) DestroyWindow(window_);
        window_ = canvas_ = slider_ = blurSlider_ = anchor_ = extraAnchor_ = nullptr;
        controls_.clear();
        labelsReady_ = false;
        surface_.Clear();
        if (font_) DeleteObject(font_);
        font_ = nullptr;
        if (graphicsToken_) Gdiplus::GdiplusShutdown(graphicsToken_);
        graphicsToken_ = 0;
    }

private:
    static constexpr wchar_t ClassName[] = L"FloatNote.GlassControlPopover";
    static constexpr UINT_PTR AnimationTimer = 41, OutsideTimer = 42, HoverTimer = 43;
    static constexpr float HeaderHeight = 54.0f;
    static constexpr std::array<const wchar_t*, 3> CloseBehaviors{L"每次询问", L"隐藏到托盘", L"退出应用"};
    struct Control {
        HWND window = nullptr;
        Action action = Action::Close;
        int value = 0;
        Gdiplus::RectF rectangle;
        bool fixed = false;
        bool visible = true;
    };
    struct Buffer {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ old = nullptr;
        BYTE* pixels = nullptr;
        int width = 0, height = 0;
        ~Buffer() { Clear(); }
        void Clear() {
            if (dc && old) SelectObject(dc, old);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            dc = nullptr; bitmap = nullptr; old = nullptr; pixels = nullptr; width = height = 0;
        }
        bool Resize(int w, int h) {
            if (dc && w == width && h == height) return true;
            Clear();
            dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (!dc || !bitmap) { Clear(); return false; }
            old = SelectObject(dc, bitmap); width = w; height = h;
            return true;
        }
    } surface_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, canvas_ = nullptr, slider_ = nullptr, blurSlider_ = nullptr, anchor_ = nullptr, extraAnchor_ = nullptr, hovered_ = nullptr;
    HFONT font_ = nullptr;
    ULONG_PTR graphicsToken_ = 0;
    NativeBackdrop backdrop_;
    std::vector<Control> controls_;
    std::function<void(Action, int)> action_;
    std::function<void()> onClosed_;
    State state_;
    RECT target_{}, work_{}, noteRect_{};
    UINT dpi_ = 96;
    float scale_ = 1.0f, logicalWidth_ = 320.0f, scroll_ = 0.0f;
    int width_ = 320, height_ = 528;
    bool useBackdrop_ = false, closing_ = false, animated_ = false, closedNotified_ = true, mouseWasDown_ = false, labelsReady_ = false, modalOpen_ = false;
    bool presenting_ = false; // Hidden panels cache state; they never submit a layered surface.
    BYTE opacity_ = 255;
    struct HoverVisual { HWND window = nullptr; float amount = 0; };
    HoverVisual hoverCurrent_{}, hoverPrevious_{};
    ULONGLONG hoverTick_ = 0;
    ULONGLONG animationStart_ = 0;
    float animationFrom_ = 0, animationTo_ = 1;

    int Px(float value) const { return static_cast<int>(std::lround(value * scale_)); }
    float ColorsTop() const { return state_.material == 2 ? 242.0f : 180.0f; }
    float ContentHeight() const { return ColorsTop() + 368.0f; }
    float OpacitySliderY() const { return ColorsTop() + 152.0f; }
    static constexpr float BlurSliderY = 202.0f;
    static std::wstring ColorLabel(COLORREF color) {
        wchar_t label[16]{};
        swprintf_s(label, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
        return label;
    }
    std::wstring BlurLabel() const {
        const int quarters = std::clamp(state_.blur, 0, 80);
        std::wstring label = std::to_wstring(quarters / 4);
        switch (quarters % 4) {
        case 1: label += L".25"; break;
        case 2: label += L".5"; break;
        case 3: label += L".75"; break;
        default: break;
        }
        return label;
    }
    void ResizeForMaterial() {
        height_ = std::min(Px(ContentHeight()), std::max(Px(150), static_cast<int>(work_.bottom - work_.top) - Px(16)));
        scroll_ = std::min(scroll_, std::max(0.0f, ContentHeight() - static_cast<float>(height_) / scale_));
        target_ = Place(noteRect_);
        SetWindowPos(window_, nullptr, target_.left, target_.top, width_, height_, SWP_NOACTIVATE | SWP_NOZORDER);
        HRGN shape = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, Px(40), Px(40));
        if (shape && !SetWindowRgn(window_, shape, FALSE)) DeleteObject(shape);
        RECT overlap{};
        useBackdrop_ = !IntersectRect(&overlap, &target_, &noteRect_) && !HighContrast();
        useBackdrop_ = backdrop_.Enable(window_, useBackdrop_) && useBackdrop_;
        backdrop_.Resize(width_, height_);
        if (state_.material != 2 && GetFocus() == blurSlider_) SetFocus(slider_);
        Layout();
    }
    bool HighContrast() const {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
               (contrast.dwFlags & HCF_HIGHCONTRASTON);
    }
    static void Path(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
        radius = std::min({radius, r.Width * .5f, r.Height * .5f});
        const float d = radius * 2;
        path.AddArc(r.X, r.Y, d, d, 180, 90);
        path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
        path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
        path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
        path.CloseFigure();
    }
    static void Round(Gdiplus::Graphics& graphics, Gdiplus::RectF r, float radius,
                      Gdiplus::Color fill, Gdiplus::Color border = Gdiplus::Color(0, 0, 0, 0)) {
        Gdiplus::GraphicsPath path;
        Path(path, r, radius);
        Gdiplus::SolidBrush brush(fill);
        graphics.FillPath(&brush, &path);
        if (border.GetA()) { Gdiplus::Pen pen(border, .65f); graphics.DrawPath(&pen, &path); }
    }
    static void Text(Gdiplus::Graphics& graphics, const std::wstring& value, Gdiplus::RectF r,
                     float size, Gdiplus::Color color, bool bold = false, bool center = false) {
        Gdiplus::FontFamily family(L"Microsoft YaHei UI");
        Gdiplus::Font font(&family, size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat format;
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        format.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush brush(color);
        graphics.DrawString(value.c_str(), static_cast<INT>(value.size()), &font, r, &format, &brush);
    }
    void BuildControls() {
        auto button = [&](Action action, const wchar_t* label, int value = 0, bool fixed = false) {
            const auto id = static_cast<INT_PTR>(5000 + controls_.size());
            HWND child = CreateWindowExW(WS_EX_LAYERED, L"BUTTON", label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 1, 1,
                window_, reinterpret_cast<HMENU>(id), instance_, nullptr);
            // Real native controls retain keyboard/accessibility semantics; their
            // almost-transparent pixels leave the antialiased canvas visible.
            if (child) {
                SetLayeredWindowAttributes(child, 0, 1, LWA_ALPHA);
                SetWindowSubclass(child, InputProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
            }
            controls_.push_back({child, action, value, {}, fixed});
        };
        button(Action::Close, L"完成，关闭便签控制", 0, true);
        button(Action::ToggleTopmost, L"置顶");
        button(Action::TogglePassThrough, L"鼠标穿透");
        button(Action::MaterialLiquid, L"材质：液态玻璃");
        button(Action::MaterialAcrylic, L"材质：毛玻璃");
        button(Action::MaterialSolid, L"材质：纯色");
        button(Action::BackgroundClear, L"背景颜色：透明");
        const wchar_t* backgrounds[]{L"背景颜色：米白", L"背景颜色：浅绿", L"背景颜色：浅蓝", L"背景颜色：浅粉", L"背景颜色：深灰"};
        for (int i = 0; i < 5; ++i) button(Action::BackgroundPreset, backgrounds[i], i);
        button(Action::AddBackgroundColor, L"添加自定义背景颜色");
        button(Action::TextAutomatic, L"文字颜色：自动");
        const wchar_t* texts[]{L"文字颜色：黑色", L"文字颜色：白色", L"文字颜色：灰色", L"文字颜色：红色", L"文字颜色：蓝色"};
        for (int i = 0; i < 5; ++i) button(Action::TextPreset, texts[i], i);
        button(Action::AddTextColor, L"添加自定义文字颜色");
        slider_ = CreateWindowExW(WS_EX_LAYERED, TRACKBAR_CLASSW, L"背景遮色",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0, 1, 1,
            window_, reinterpret_cast<HMENU>(5100), instance_, nullptr);
        if (slider_) {
            SetLayeredWindowAttributes(slider_, 0, 1, LWA_ALPHA);
            SendMessageW(slider_, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
            SendMessageW(slider_, TBM_SETPAGESIZE, 0, 10);
            SetWindowSubclass(slider_, InputProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        blurSlider_ = CreateWindowExW(WS_EX_LAYERED, TRACKBAR_CLASSW, L"模糊强度",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0, 1, 1,
            window_, reinterpret_cast<HMENU>(5101), instance_, nullptr);
        if (blurSlider_) {
            SetLayeredWindowAttributes(blurSlider_, 0, 1, LWA_ALPHA);
            SendMessageW(blurSlider_, TBM_SETRANGE, FALSE, MAKELPARAM(0, 80));
            SendMessageW(blurSlider_, TBM_SETPAGESIZE, 0, 4);
            SetWindowSubclass(blurSlider_, InputProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        button(Action::SmallerText, L"缩小字号");
        button(Action::LargerText, L"放大字号");
        for (int i = 0; i < 3; ++i) button(Action::CloseBehavior, CloseBehaviors[i], i);
        button(Action::OpenLab, L"材质实验，打开进阶调节");
        button(Action::HideNote, L"隐藏便签，可从托盘恢复");
        button(Action::ExitApp, L"退出 FloatNote");
    }

    RECT Place(const RECT& note) const {
        RECT anchor = note;
        if (IsWindow(anchor_)) GetWindowRect(anchor_, &anchor);
        RECT occupied{};
        UnionRect(&occupied, &note, &anchor);
        const int gap = Px(8), margin = Px(8);
        auto clampX = [&](int x) { return std::clamp(x, static_cast<int>(work_.left) + margin,
            std::max(static_cast<int>(work_.left) + margin, static_cast<int>(work_.right) - margin - width_)); };
        auto clampY = [&](int y) { return std::clamp(y, static_cast<int>(work_.top) + margin,
            std::max(static_cast<int>(work_.top) + margin, static_cast<int>(work_.bottom) - margin - height_)); };
        const int centered = clampX(static_cast<int>((anchor.left + anchor.right - width_) / 2));
        const int sideY = clampY(static_cast<int>(anchor.top));
        const std::array<POINT, 4> choices{{
            {centered, occupied.bottom + gap}, {centered, occupied.top - height_ - gap},
            {occupied.right + gap, sideY}, {occupied.left - width_ - gap, sideY}}};
        for (const auto& p : choices) {
            RECT candidate{p.x, p.y, p.x + width_, p.y + height_};
            if (candidate.left >= work_.left + margin && candidate.right <= work_.right - margin &&
                candidate.top >= work_.top + margin && candidate.bottom <= work_.bottom - margin) return candidate;
        }
        // If space is genuinely insufficient, minimize overlap and use the
        // opaque fallback so the note and popover never recursively blur.
        RECT best{};
        long long bestArea = LLONG_MAX;
        for (const auto& p : choices) {
            const int x = clampX(p.x), y = clampY(p.y);
            RECT candidate{x, y, x + width_, y + height_}, overlap{};
            long long area = 0;
            if (IntersectRect(&overlap, &candidate, &occupied))
                area = static_cast<long long>(overlap.right - overlap.left) * (overlap.bottom - overlap.top);
            if (area < bestArea) { best = candidate; bestArea = area; }
        }
        return best;
    }

    void Layout() {
        if (!window_) return;
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        const float inner = logicalWidth_ - 36, tile = (inner - 8) / 2, segment = inner / 3;
        const float colorsY = ColorsTop();
        const int colorCount = std::clamp(static_cast<int>((inner + 7) / 41) - 2, 1, 5);
        const float swatchStep = (inner - 34) / static_cast<float>(colorCount + 1);
        for (size_t i = 0; i < controls_.size(); ++i) {
            auto& control = controls_[i];
            control.visible = true;
            switch (control.action) {
            case Action::Close: control.rectangle = {logicalWidth_ - 70, 13, 52, 30}; break;
            case Action::ToggleTopmost: control.rectangle = {18, 60, tile, 40}; break;
            case Action::TogglePassThrough: control.rectangle = {26 + tile, 60, tile, 40}; break;
            case Action::MaterialLiquid: control.rectangle = {18, 134, segment, 34}; break;
            case Action::MaterialAcrylic: control.rectangle = {18 + segment, 134, segment, 34}; break;
            case Action::MaterialSolid: control.rectangle = {18 + 2 * segment, 134, segment, 34}; break;
            case Action::BackgroundClear: control.rectangle = {18, colorsY + 24, 34, 34}; break;
            case Action::BackgroundPreset:
                control.visible = control.value < colorCount;
                control.rectangle = {18 + (control.value + 1) * swatchStep, colorsY + 24, 34, 34}; break;
            case Action::AddBackgroundColor: control.rectangle = {logicalWidth_ - 52, colorsY + 24, 34, 34}; break;
            case Action::TextAutomatic: control.rectangle = {18, colorsY + 91, 34, 34}; break;
            case Action::TextPreset:
                control.visible = control.value < colorCount;
                control.rectangle = {18 + (control.value + 1) * swatchStep, colorsY + 91, 34, 34}; break;
            case Action::AddTextColor: control.rectangle = {logicalWidth_ - 52, colorsY + 91, 34, 34}; break;
            case Action::SmallerText: control.rectangle = {logicalWidth_ - 133, colorsY + 187, 34, 32}; break;
            case Action::LargerText: control.rectangle = {logicalWidth_ - 52, colorsY + 187, 34, 32}; break;
            case Action::CloseBehavior: control.rectangle = {18 + control.value * segment, colorsY + 257, segment, 34}; break;
            case Action::OpenLab: control.rectangle = {18, colorsY + 299, inner, 31}; break;
            case Action::HideNote: control.rectangle = {18, colorsY + 334, tile, 25}; break;
            case Action::ExitApp: control.rectangle = {26 + tile, colorsY + 334, tile, 25}; break;
            default: break;
            }
            ShowWindow(control.window, control.visible ? SW_SHOWNA : SW_HIDE);
            if (!control.visible) continue;
            const auto& r = control.rectangle;
            SetWindowPos(control.window, HWND_TOP, Px(r.X), Px(r.Y - (control.fixed ? 0 : scroll_)),
                         Px(r.Width), Px(r.Height), SWP_NOACTIVATE);
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
            ClipControl(control.window, r, control.fixed);
        }
        const Gdiplus::RectF sliderRect{15, OpacitySliderY(), logicalWidth_ - 30, 27};
        SetWindowPos(slider_, HWND_TOP, Px(sliderRect.X), Px(sliderRect.Y - scroll_),
                     Px(sliderRect.Width), Px(sliderRect.Height), SWP_NOACTIVATE);
        ClipControl(slider_, sliderRect, false);
        const Gdiplus::RectF blurRect{15, BlurSliderY, logicalWidth_ - 30, 27};
        ShowWindow(blurSlider_, state_.material == 2 ? SW_SHOWNA : SW_HIDE);
        if (state_.material == 2) {
            SetWindowPos(blurSlider_, HWND_TOP, Px(blurRect.X), Px(blurRect.Y - scroll_),
                         Px(blurRect.Width), Px(blurRect.Height), SWP_NOACTIVATE);
            ClipControl(blurSlider_, blurRect, false);
        }
        // Native Tab navigation follows the displayed vertical/horizontal order,
        // including the conditional blur slider and compact palette rows.
        std::vector<std::pair<HWND, Gdiplus::RectF>> tabOrder;
        for (const auto& control : controls_) if (control.visible) tabOrder.emplace_back(control.window, control.rectangle);
        tabOrder.emplace_back(slider_, sliderRect);
        if (state_.material == 2) tabOrder.emplace_back(blurSlider_, blurRect);
        std::sort(tabOrder.begin(), tabOrder.end(), [](const auto& left, const auto& right) {
            if (left.second.Y != right.second.Y) return left.second.Y < right.second.Y;
            return left.second.X < right.second.X;
        });
        for (auto entry = tabOrder.rbegin(); entry != tabOrder.rend(); ++entry)
            SetWindowPos(entry->first, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(canvas_, HWND_BOTTOM, 0, 0, width_, height_, SWP_NOACTIVATE);
    }
    void ClipControl(HWND control, const Gdiplus::RectF& rectangle, bool fixed) {
        if (!control) return;
        const float y = rectangle.Y - (fixed ? 0 : scroll_);
        const float top = fixed ? 0 : HeaderHeight;
        const float bottom = static_cast<float>(height_) / scale_ - 6;
        const int clipTop = Px(std::max(0.0f, top - y));
        const int clipBottom = Px(std::clamp(bottom - y, 0.0f, rectangle.Height));
        HRGN clip = CreateRectRgn(0, clipTop, Px(rectangle.Width), clipBottom);
        if (clip && !SetWindowRgn(control, clip, FALSE)) DeleteObject(clip);
        // Keep offscreen inputs in the native tab order; focusing one scrolls it
        // into view. An empty clip removes its pointer hit area.
    }
    void EnsureVisible(HWND child) {
        if (child == slider_) { ScrollInto(OpacitySliderY(), OpacitySliderY() + 27); return; }
        if (child == blurSlider_) { ScrollInto(BlurSliderY, BlurSliderY + 27); return; }
        for (const auto& control : controls_) if (control.window == child && !control.fixed) {
            ScrollInto(control.rectangle.Y, control.rectangle.GetBottom()); return;
        }
    }
    void ScrollInto(float top, float bottom) {
        const float viewBottom = static_cast<float>(height_) / scale_ - 8;
        float desired = scroll_;
        if (top - scroll_ < HeaderHeight + 4) desired = top - HeaderHeight - 4;
        if (bottom - desired > viewBottom) desired = bottom - viewBottom;
        Scroll(desired);
    }
    void Scroll(float value) {
        const float maximum = std::max(0.0f, ContentHeight() - static_cast<float>(height_) / scale_);
        value = std::clamp(value, 0.0f, maximum);
        if (std::abs(value - scroll_) < .1f) return;
        scroll_ = value; Layout(); Render();
    }

    bool Selected(const Control& item) const {
        switch (item.action) {
        case Action::ToggleTopmost: return state_.pinned;
        case Action::TogglePassThrough: return state_.passThrough;
        case Action::MaterialLiquid: return state_.material == 2;
        case Action::MaterialAcrylic: return state_.material == 1;
        case Action::MaterialSolid: return state_.material == 0;
        case Action::CloseBehavior: return state_.closeBehavior == item.value;
        case Action::BackgroundClear: return state_.opacity == 0;
        case Action::BackgroundPreset: return state_.opacity > 0 && state_.background == state_.backgroundColors[item.value];
        case Action::TextAutomatic: return state_.autoText;
        case Action::TextPreset: return !state_.autoText && state_.text == state_.textColors[item.value];
        default: return false;
        }
    }
    static Gdiplus::Color FadeColor(Gdiplus::Color from, Gdiplus::Color to, float amount) {
        amount = std::clamp(amount, 0.0f, 1.0f);
        auto channel = [&](BYTE start, BYTE end) {
            return static_cast<BYTE>(std::lround(start + (end - start) * amount));
        };
        return Gdiplus::Color(channel(from.GetA(), to.GetA()), channel(from.GetR(), to.GetR()),
                              channel(from.GetG(), to.GetG()), channel(from.GetB(), to.GetB()));
    }
    float HoverAmount(HWND window) const {
        float amount = 0;
        if (window && window == hoverCurrent_.window)
            amount = std::max(amount, hoverCurrent_.amount);
        if (window && window == hoverPrevious_.window)
            amount = std::max(amount, hoverPrevious_.amount);
        return amount;
    }
    void NoteHover(HWND window) {
        if (hovered_ == window)
            return;
        const float amount = HoverAmount(window);
        if (hoverCurrent_.window != window) {
            if (hoverCurrent_.amount > 0.01f)
                hoverPrevious_ = hoverCurrent_;
            hoverCurrent_ = {window, amount};
        }
        hovered_ = window;
        if (!animated_) {
            hoverCurrent_ = {window, window ? 1.0f : 0.0f};
            hoverPrevious_ = {};
            KillTimer(window_, HoverTimer);
            Render();
            return;
        }
        hoverTick_ = GetTickCount64();
        if (window_)
            SetTimer(window_, HoverTimer, 16, nullptr);
    }
    void ResetHoverVisuals() {
        if (window_) KillTimer(window_, HoverTimer);
        hovered_ = nullptr;
        hoverCurrent_ = hoverPrevious_ = {};
    }
    void TickHover() {
        const auto now = GetTickCount64();
        const float amount = 1.0f - std::exp(-static_cast<float>(now - hoverTick_) / 42.0f);
        hoverTick_ = now;
        auto step = [amount](HoverVisual& item, float target) {
            item.amount += (target - item.amount) * amount;
            if (std::abs(item.amount - target) < 0.02f)
                item.amount = target;
        };
        step(hoverCurrent_, hovered_ ? 1.0f : 0.0f);
        step(hoverPrevious_, 0.0f);
        Render();
        if ((!hovered_ || hoverCurrent_.amount >= 1.0f) && hoverPrevious_.amount <= 0.0f)
            KillTimer(window_, HoverTimer);
    }
    void DrawControl(Gdiplus::Graphics& graphics, const Control& item) {
        using namespace Gdiplus;
        auto r = item.rectangle;
        const float hoverAmount = HoverAmount(item.window);
        const bool hover = hoverAmount > 0.04f;
        const bool pressed = (SendMessageW(item.window, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        const bool selected = Selected(item);
        const Color ink(255, 31, 35, 43), blue(255, 30, 105, 215), secondary(255, 100, 107, 117);
        const bool swatch = item.action == Action::BackgroundClear || item.action == Action::BackgroundPreset ||
                            item.action == Action::TextAutomatic || item.action == Action::TextPreset ||
                            item.action == Action::AddBackgroundColor || item.action == Action::AddTextColor;
        if (pressed) { r.Inflate(-.8f, -.8f); }
        if (swatch) {
            RectF disc(r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
            const bool add = item.action == Action::AddBackgroundColor || item.action == Action::AddTextColor;
            COLORREF color = RGB(250, 251, 252);
            if (item.action == Action::BackgroundPreset) color = state_.backgroundColors[item.value];
            if (item.action == Action::TextPreset) color = state_.textColors[item.value];
            // The add target lives directly on the same panel material as the
            // palette. Its center has no independently filled section/card.
            Round(graphics, disc, disc.Width / 2, add ? Color(static_cast<BYTE>(std::lround(24 * hoverAmount)), 45, 105, 185) :
                Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), Color(65, 80, 88, 100));
            if (add) {
                Pen plus(FadeColor(secondary, blue, hoverAmount), 1.5f); plus.SetStartCap(LineCapRound); plus.SetEndCap(LineCapRound);
                const float cx = disc.X + disc.Width / 2, cy = disc.Y + disc.Height / 2;
                graphics.DrawLine(&plus, cx - 5, cy, cx + 5, cy);
                graphics.DrawLine(&plus, cx, cy - 5, cx, cy + 5);
            } else if (item.action == Action::BackgroundClear) {
                Pen slash(Color(255, 139, 148, 158), 1.1f);
                graphics.DrawLine(&slash, disc.X + 5, disc.GetBottom() - 5, disc.GetRight() - 5, disc.Y + 5);
            } else if (item.action == Action::TextAutomatic) Text(graphics, L"A", disc, 13, ink, true, true);
            if (selected || hover || pressed) {
                Pen outline(selected ? blue : Color(static_cast<BYTE>(std::lround(90 * std::max(hoverAmount, pressed ? 1.0f : 0.0f))), 90, 110, 135), selected ? 1.8f : 1.0f);
                graphics.DrawEllipse(&outline, r.X + .8f, r.Y + .8f, r.Width - 1.6f, r.Height - 1.6f);
            }
        } else if (item.action == Action::ToggleTopmost || item.action == Action::TogglePassThrough) {
            Round(graphics, r, 11, selected ? Color(235, 222, 236, 255) : Color(150, 255, 255, 255),
                  selected ? Color(100, 92, 149, 234) : Color(35, 90, 100, 113));
            const Color icon = selected ? blue : secondary;
            Pen line(icon, 1.55f); line.SetStartCap(LineCapRound); line.SetEndCap(LineCapRound); line.SetLineJoin(LineJoinRound);
            const bool showIcon = r.Width >= 115;
            const float x = r.X + 16, y = r.Y + 19;
            if (showIcon && item.action == Action::ToggleTopmost) {
                graphics.DrawLine(&line, x - 5, y - 6, x + 5, y - 6);
                graphics.DrawLine(&line, x - 3, y - 6, x - 3, y);
                graphics.DrawLine(&line, x + 3, y - 6, x + 3, y);
                graphics.DrawLine(&line, x - 3, y, x - 6, y + 4);
                graphics.DrawLine(&line, x - 6, y + 4, x + 6, y + 4);
                graphics.DrawLine(&line, x + 6, y + 4, x + 3, y);
                graphics.DrawLine(&line, x, y + 4, x, y + 10);
            } else if (showIcon) {
                const PointF points[]{{x - 5, y - 7}, {x - 5, y + 8}, {x - 1, y + 4}, {x + 3, y + 10}, {x + 6, y + 8}, {x + 2, y + 2}, {x + 8, y + 2}};
                graphics.DrawPolygon(&line, points, 7);
            }
            const float inset = showIcon ? 31.0f : 10.0f;
            Text(graphics, item.action == Action::ToggleTopmost ? L"置顶" : L"鼠标穿透",
                 {r.X + inset, r.Y, r.Width - inset - 27, r.Height}, showIcon ? 11.0f : 9.8f, ink, true);
            Text(graphics, selected ? L"开" : L"关", {r.GetRight() - 28, r.Y, 23, r.Height}, 10, selected ? blue : secondary, false, true);
            if (hoverAmount > 0.01f)
                Round(graphics, r, 11, Color(static_cast<BYTE>(std::lround(28 * hoverAmount)), 45, 105, 185));
            if (pressed)
                Round(graphics, r, 11, Color(78, 45, 105, 185));
        } else if (item.action == Action::MaterialLiquid || item.action == Action::MaterialAcrylic ||
                   item.action == Action::MaterialSolid || item.action == Action::CloseBehavior) {
            if (selected) { r.Inflate(-2, -3); Round(graphics, r, 9, Color(255, 255, 255, 255), Color(28, 80, 90, 105)); }
            else if (pressed) Round(graphics, r, 9, Color(168, 198, 214, 234));
            else if (hoverAmount > 0.01f)
                Round(graphics, r, 9, Color(static_cast<BYTE>(std::lround(96 * hoverAmount)), 255, 255, 255));
            const wchar_t* label = item.action == Action::CloseBehavior ? CloseBehaviors[item.value] :
                item.action == Action::MaterialLiquid ? L"液态玻璃" : item.action == Action::MaterialAcrylic ? L"毛玻璃" : L"纯色";
            const float size = item.action == Action::CloseBehavior && logicalWidth_ < 280 ? 10.0f : 11.5f;
            Text(graphics, label, r, size, selected ? ink : secondary, selected, true);
        } else {
            std::wstring label;
            Color color = ink;
            switch (item.action) {
            case Action::Close: label = L"完成"; color = blue; break;
            case Action::SmallerText: label = L"A−"; break;
            case Action::LargerText: label = L"A+"; break;
            case Action::OpenLab: label = L"材质实验"; color = blue; break;
            case Action::HideNote: label = L"隐藏便签"; color = secondary; break;
            case Action::ExitApp: label = L"退出"; color = secondary; break;
            default: break;
            }
            const bool tile = item.action == Action::SmallerText || item.action == Action::LargerText;
            const Color idle(tile ? 155 : 0, 255, 255, 255);
            const Color hot(150, 214, 226, 242);
            const Color down(215, 168, 190, 220);
            if (tile || hoverAmount > 0.01f || pressed)
                Round(graphics, r, 10, pressed ? down : FadeColor(idle, hot, hoverAmount),
                      tile ? Color(28, 90, 100, 115) : Color(0, 0, 0, 0));
            Text(graphics, label, r, item.action == Action::Close ? 12.0f : 11.5f, color,
                 item.action == Action::Close, true);
        }
        if (GetFocus() == item.window) {
            GraphicsPath path; Path(path, item.rectangle, swatch ? 17.0f : 10.0f);
            Pen focus(Color(220, 44, 112, 222), 1.2f); focus.SetDashStyle(DashStyleDot);
            graphics.DrawPath(&focus, &path);
        }
    }

    void DrawSlider(Gdiplus::Graphics& graphics, HWND slider, float y) {
        using namespace Gdiplus;
        // Match the native channel/thumb rather than estimating its geometry,
        // so custom drawing, mouse interaction and keyboard stepping agree.
        RECT channel{}, thumb{};
        SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
        SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        const float left = 15 + static_cast<float>(channel.left) / scale_;
        const float right = 15 + static_cast<float>(channel.right) / scale_;
        const float cy = y + static_cast<float>(thumb.top + thumb.bottom) / (2 * scale_);
        const float tx = 15 + static_cast<float>(thumb.left + thumb.right) / (2 * scale_);
        Round(graphics, {left, cy - 2, std::max(1.0f, right - left), 4}, 2, Color(65, 132, 143, 162));
        if (tx > left) Round(graphics, {left, cy - 2, tx - left, 4}, 2, Color(255, 65, 121, 218));
        Round(graphics, {tx - 7, cy - 6.5f, 14, 14}, 7, Color(40, 52, 60, 76));
        Round(graphics, {tx - 7, cy - 7.5f, 14, 14}, 7, Color(255, 255, 255, 255), Color(45, 75, 88, 106));
        if (GetFocus() == slider) {
            Pen focus(Color(230, 44, 112, 222), 1.1f);
            graphics.DrawEllipse(&focus, tx - 9, cy - 9.5f, 18.0f, 18.0f);
        }
    }

    void Render() {
        if (!presenting_ || !canvas_ || !surface_.Resize(width_, height_)) return;
        using namespace Gdiplus;
        ZeroMemory(surface_.pixels, static_cast<size_t>(width_) * height_ * 4);
        Bitmap bitmap(width_, height_, width_ * 4, PixelFormat32bppPARGB, surface_.pixels);
        {
            Graphics graphics(&bitmap);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
            graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            graphics.ScaleTransform(scale_, scale_);
            const float h = static_cast<float>(height_) / scale_;
            Round(graphics, {.5f, .5f, logicalWidth_ - 1, h - 1}, 19.5f,
                  Color(useBackdrop_ ? 227 : 255, 244, 246, 249), Color(170, 255, 255, 255));
            const Color ink(255, 31, 35, 43), secondary(255, 103, 111, 123);
            const auto body = graphics.Save();
            graphics.SetClip(RectF(5, HeaderHeight, logicalWidth_ - 10, h - HeaderHeight - 6));
            graphics.TranslateTransform(0, -scroll_);
            const float colorsY = ColorsTop();
            Round(graphics, {18, 134, logicalWidth_ - 36, 34}, 11, Color(30, 109, 121, 141));
            Round(graphics, {18, colorsY + 257, logicalWidth_ - 36, 34}, 11, Color(30, 109, 121, 141));
            Text(graphics, L"材质", {18, 108, logicalWidth_ - 36, 22}, 10.5f, secondary, true);
            if (state_.material == 2) {
                Text(graphics, L"模糊强度", {18, 180, logicalWidth_ - 104, 21}, 10.5f, secondary, true);
                Text(graphics, BlurLabel(), {logicalWidth_ - 68, 180, 50, 21}, 10.5f, secondary, false, true);
                DrawSlider(graphics, blurSlider_, BlurSliderY);
            }
            Text(graphics, L"背景颜色", {18, colorsY, logicalWidth_ - 36, 21}, 10.5f, secondary, true);
            Text(graphics, L"文字颜色", {18, colorsY + 67, logicalWidth_ - 36, 21}, 10.5f, secondary, true);
            Text(graphics, L"背景遮色", {18, colorsY + 130, logicalWidth_ - 104, 21}, 10.5f, secondary, true);
            Text(graphics, std::to_wstring(state_.opacity) + L"%", {logicalWidth_ - 68, colorsY + 130, 50, 21}, 10.5f, secondary, false, true);
            Text(graphics, L"字号", {18, colorsY + 187, logicalWidth_ - 155, 32}, 11.5f, ink);
            Text(graphics, std::to_wstring(state_.fontSize), {logicalWidth_ - 99, colorsY + 187, 47, 32}, 12, ink, false, true);
            Text(graphics, L"点击关闭按钮时", {18, colorsY + 231, logicalWidth_ - 36, 22}, 10.5f, secondary, true);
            DrawSlider(graphics, slider_, OpacitySliderY());
            for (const auto& control : controls_) if (!control.fixed && control.visible) DrawControl(graphics, control);
            graphics.Restore(body);
            Text(graphics, L"便签控制", {18, 10, logicalWidth_ - 96, 36}, 15, ink, true);
            for (const auto& control : controls_) if (control.fixed) DrawControl(graphics, control);
            if (ContentHeight() > h) {
                const float rail = h - HeaderHeight - 20, maximum = ContentHeight() - h;
                const float length = std::max(24.0f, rail * (h - HeaderHeight) / (ContentHeight() - HeaderHeight));
                const float y = HeaderHeight + 8 + (rail - length) * scroll_ / maximum;
                Round(graphics, {logicalWidth_ - 6, y, 2.5f, length}, 1.25f, Color(75, 100, 111, 128));
            }
        }
        POINT source{0, 0};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, opacity_, AC_SRC_ALPHA};
        UpdateLayeredWindow(canvas_, nullptr, nullptr, &size, surface_.dc, &source, 0, &blend, ULW_ALPHA);
    }

    void RequestClose() {
        // Make visibility change before notifying the app; a callback is allowed
        // to update the entry button or destroy this panel.
        Close();
        if (action_) action_(Action::Close, 0);
    }
    void FinishClose() {
        presenting_ = false;
        ResetHoverVisuals();
        KillTimer(window_, AnimationTimer);
        KillTimer(window_, OutsideTimer);
        ShowWindow(window_, SW_HIDE);
        backdrop_.Enable(window_, false);
        closing_ = false;
        modalOpen_ = false;
        if (!closedNotified_) {
            closedNotified_ = true;
            auto callback = onClosed_;
            if (callback) callback();
        }
    }
    void TickAnimation() {
        const float duration = closing_ ? 170.0f : 210.0f;
        const float time = animated_ ? std::clamp(static_cast<float>(GetTickCount64() - animationStart_) / duration, 0.0f, 1.0f) : 1.0f;
        const float eased = 1 - std::pow(1 - time, 3.0f);
        const float progress = animationFrom_ + (animationTo_ - animationFrom_) * eased;
        opacity_ = static_cast<BYTE>(std::clamp(std::lround(progress * 255), 0L, 255L));
        const int travel = animated_ ? Px(14) : 0;
        const float remaining = 1.0f - progress;
        SetWindowPos(window_, nullptr, target_.left, target_.top + static_cast<int>(std::lround(travel * remaining)),
                     0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        backdrop_.SetOpacity(static_cast<float>(opacity_) / 255.0f);
        Render();
        if (time >= 1) {
            KillTimer(window_, AnimationTimer);
            if (closing_) FinishClose();
        }
    }
    void CheckOutside() {
        if (modalOpen_) return;
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (down && !mouseWasDown_) {
            POINT point{}; GetCursorPos(&point);
            RECT bounds{}; GetWindowRect(window_, &bounds);
            RECT anchor{}; if (IsWindow(anchor_)) GetWindowRect(anchor_, &anchor);
            RECT extraAnchor{}; if (IsWindow(extraAnchor_)) GetWindowRect(extraAnchor_, &extraAnchor);
            if (!PtInRect(&bounds, point) && !PtInRect(&anchor, point) && !PtInRect(&extraAnchor, point)) {
                mouseWasDown_ = down; RequestClose(); return;
            }
        }
        mouseWasDown_ = down;
    }
    void Invoke(HWND source) {
        for (const auto& control : controls_) if (control.window == source) {
            const auto action = control.action; const int value = control.value;
            if (action == Action::Close) { RequestClose(); return; }
            if (action == Action::OpenLab || action == Action::HideNote || action == Action::ExitApp) Close(false);
            if (action == Action::AddBackgroundColor || action == Action::AddTextColor) SetModalOpen(true);
            auto callback = action_;
            if (callback) callback(action, value);
            else SetModalOpen(false);
            return;
        }
    }

    static LRESULT CALLBACK InputProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                            UINT_PTR, DWORD_PTR reference) {
        auto* self = reinterpret_cast<Panel*>(reference);
        if ((window == self->slider_ || window == self->blurSlider_) &&
            (message == WM_ERASEBKGND || message == WM_PAINT)) {
            if (message == WM_ERASEBKGND)
                return 1;
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            self->NoteHover(window);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
        } else if (message == WM_MOUSELEAVE) {
            if (self->hovered_ == window) self->NoteHover(nullptr);
        } else if (message == WM_SETFOCUS) {
            self->EnsureVisible(window);
            self->Render();
        } else if (message == WM_MOUSEWHEEL && window != self->slider_ && window != self->blurSlider_) {
            self->Scroll(self->scroll_ - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 36);
            return 0;
        } else if (message == WM_KEYDOWN && wp == VK_ESCAPE && !self->modalOpen_) {
            self->RequestClose(); return 0;
        }
        const LRESULT result = DefSubclassProc(window, message, wp, lp);
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_KEYDOWN ||
            message == WM_KEYUP || message == WM_KILLFOCUS || message == WM_CAPTURECHANGED || message == BM_SETSTATE)
            self->Render();
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, InputProcedure, 1);
        return result;
    }
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        Panel* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_NCCALCSIZE: return 0;
        case WM_NCHITTEST: return HTCLIENT;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
            FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(window, &paint); return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) self->Invoke(reinterpret_cast<HWND>(lp));
            return 0;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) == self->slider_) {
                self->state_.opacity = static_cast<int>(SendMessageW(self->slider_, TBM_GETPOS, 0, 0));
                const int value = self->state_.opacity;
                SetWindowTextW(self->slider_, (L"背景遮色，" + std::to_wstring(value) + L"%").c_str());
                self->Render();
                auto callback = self->action_;
                if (callback) callback(Action::Opacity, value);
            } else if (reinterpret_cast<HWND>(lp) == self->blurSlider_ && self->state_.material == 2) {
                self->state_.blur = static_cast<int>(SendMessageW(self->blurSlider_, TBM_GETPOS, 0, 0));
                const int value = self->state_.blur;
                SetWindowTextW(self->blurSlider_, (L"模糊强度，" + self->BlurLabel()).c_str());
                self->Render();
                auto callback = self->action_;
                if (callback) callback(Action::Blur, value);
            }
            return 0;
        case WM_MOUSEWHEEL:
            self->Scroll(self->scroll_ - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 36);
            return 0;
        case WM_TIMER:
            if (wp == AnimationTimer) self->TickAnimation();
            if (wp == OutsideTimer) self->CheckOutside();
            if (wp == HoverTimer) self->TickHover();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && self->Visible() && !self->modalOpen_) {
                HWND next = reinterpret_cast<HWND>(lp);
                const bool anchor = self->anchor_ && (next == self->anchor_ || IsChild(self->anchor_, next));
                const bool extraAnchor = self->extraAnchor_ &&
                    (next == self->extraAnchor_ || IsChild(self->extraAnchor_, next));
                if (!anchor && !extraAnchor && !IsChild(window, next))
                    self->RequestClose();
            }
            return 0;
        case WM_CLOSE: if (!self->modalOpen_) self->RequestClose(); return 0;
        case WM_SIZE:
            if (self->canvas_) self->backdrop_.Resize(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_DESTROY:
            KillTimer(window, AnimationTimer); KillTimer(window, OutsideTimer); return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
};

} // namespace GlassControlPopover
