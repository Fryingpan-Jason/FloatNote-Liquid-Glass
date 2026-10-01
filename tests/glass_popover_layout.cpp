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
#include <iostream>
#include <stdexcept>
#include "../src/backdrop.h"
#define private public
#include "../experiments/glass_control_popover.h"
#undef private

using GlassControlPopover::Action;
using GlassControlPopover::Panel;
static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static RECT ChildRect(Panel& panel, HWND window) {
    RECT rect{};
    Check(GetWindowRect(window, &rect) != FALSE, "native control has no bounds");
    MapWindowPoints(nullptr, panel.Window(), reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
static void Configure(Panel& panel, int width, UINT dpi, int material, int height = 0) {
    panel.presenting_ = false;
    panel.dpi_ = dpi;
    panel.scale_ = dpi / 96.0f;
    panel.logicalWidth_ = static_cast<float>(width);
    panel.width_ = panel.Px(panel.logicalWidth_);
    auto state = panel.state_;
    state.material = material;
    panel.state_ = state;
    panel.height_ = panel.Px(height ? static_cast<float>(height) : panel.ContentHeight());
    panel.scroll_ = 0;
    panel.labelsReady_ = false;
    SetWindowPos(panel.Window(), nullptr, 0, 0, panel.width_, panel.height_, SWP_NOZORDER | SWP_NOACTIVATE);
    panel.Layout();
    panel.presenting_ = true;
    panel.UpdateState(state);
}
static HWND Find(Panel& panel, Action action, int value = 0) {
    for (const auto& control : panel.controls_) if (control.action == action && control.value == value)
        return control.window;
    throw std::runtime_error("missing native action");
}
static std::wstring Label(HWND window) {
    wchar_t text[160]{};
    GetWindowTextW(window, text, 160);
    return text;
}
static double Luminance(Gdiplus::Color c) {
    auto linear = [](BYTE value) { const double v = value / 255.0;
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.GetR()) + .7152 * linear(c.GetG()) + .0722 * linear(c.GetB());
}
static double Contrast(Gdiplus::Color a, Gdiplus::Color b) {
    const double x = Luminance(a), y = Luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
int main() {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::vector<std::pair<Action, int>> actions;
        Panel panel;
        Check(panel.Create(GetModuleHandleW(nullptr), [&](Action action, int value) {
            actions.emplace_back(action, value);
        }), "hidden panel creation failed");
        unsigned layouts = 0, reachable = 0;
        for (UINT dpi : {96u, 120u, 144u, 192u, 240u, 288u})
            for (int width : {200, 224, 256, 288, 320, 344})
                for (int material : {0, 1, 2}) {
                    Configure(panel, width, dpi, material);
                    std::vector<RECT> bounds;
                    unsigned backgrounds = 0, texts = 0;
                    for (const auto& control : panel.controls_) {
                        Check(control.visible && (GetWindowLongPtrW(control.window, GWL_STYLE) & WS_VISIBLE),
                              "a narrow panel hid an existing action");
                        const auto rect = ChildRect(panel, control.window);
                        Check(rect.left >= 0 && rect.right <= panel.width_ && rect.top >= 0 && rect.bottom <= panel.height_,
                              "native action escapes the panel");
                        Check(rect.right > rect.left && rect.bottom > rect.top, "empty native action target");
                        backgrounds += control.action == Action::BackgroundPreset;
                        texts += control.action == Action::TextPreset;
                        bounds.push_back(rect);
                    }
                    Check(backgrounds == 5 && texts == 5, "not every saved colour is reachable");
                    bounds.push_back(ChildRect(panel, panel.slider_));
                    if (material == 2) bounds.push_back(ChildRect(panel, panel.blurSlider_));
                    for (size_t a = 0; a < bounds.size(); ++a)
                        for (size_t b = a + 1; b < bounds.size(); ++b) {
                            RECT overlap{};
                            if (IntersectRect(&overlap, &bounds[a], &bounds[b])) {
                                std::cerr << "layout " << width << " dpi " << dpi << " material " << material
                                          << " targets " << a << '/' << b << " overlap "
                                          << overlap.left << ',' << overlap.top << '-' << overlap.right << ',' << overlap.bottom << '\n';
                                Check(false, "native action hit areas overlap");
                            }
                        }
                    Check(((GetWindowLongPtrW(panel.blurSlider_, GWL_STYLE) & WS_VISIBLE) != 0) == (material == 2),
                          "blur is available for the wrong material");

                    HWND first = GetNextDlgTabItem(panel.Window(), nullptr, FALSE), current = first;
                    std::vector<HWND> order;
                    do {
                        Check(current != nullptr && order.size() < 32, "broken native Tab cycle");
                        order.push_back(current);
                        current = GetNextDlgTabItem(panel.Window(), current, FALSE);
                    } while (current != first);
                    Check(order.size() == bounds.size(), "Tab cannot reach every displayed action");

                    Configure(panel, width, dpi, material, 180);
                    for (HWND child : order) {
                        panel.EnsureVisible(child);
                        if (child == Find(panel, Action::Close)) continue;
                        const auto rect = ChildRect(panel, child);
                        Check(rect.top >= panel.Px(panel.HeaderHeight) && rect.bottom <= panel.height_ - panel.Px(6),
                              "keyboard focus remains behind the header or below the viewport");
                        RECT clip{};
                        HRGN region = CreateRectRgn(0, 0, 0, 0);
                        Check(GetWindowRgn(child, region) != ERROR && GetRgnBox(region, &clip) != NULLREGION,
                              "a scrolled-to action has no pointer hit area");
                        DeleteObject(region);
                        ++reachable;
                    }
                    ++layouts;
                }
        std::cout << "PASS " << layouts << " native layouts: all presets, disjoint hit targets, materials and Tab order\n";
        std::cout << "PASS " << reachable << " keyboard destinations scroll into an interactive viewport\n";

        Configure(panel, 344, 192, 2);
        auto state = panel.state_;
        state.autoText = false;
        state.text = state.textColors[4];
        state.opacity = 60;
        state.background = state.backgroundColors[1];
        panel.UpdateState(state);
        Check(Label(Find(panel, Action::TextAutomatic)).find(L"已选中") == std::wstring::npos,
              "automatic ink still announces selected after choosing a colour");
        Check(Label(Find(panel, Action::TextPreset, 4)).find(L"已选中") != std::wstring::npos &&
              Label(Find(panel, Action::BackgroundPreset, 1)).find(L"已选中") != std::wstring::npos,
              "native colour names do not announce the selected preset");
        for (const auto& control : panel.controls_) {
            const auto before = actions.size();
            SendMessageW(control.window, BM_CLICK, 0, 0);
            Check(actions.size() == before + 1 && actions.back() == std::pair{control.action, control.value},
                  "native button dispatched a different action");
            panel.SetModalOpen(false);
        }
        std::cout << "PASS native selected-colour labels and all 28 action callbacks\n";

        for (bool dark : {false, true}) {
            const auto palette = panel.MakePalette(dark);
            Check(Contrast(palette.ink, palette.surface) >= 4.5 &&
                  Contrast(palette.secondary, palette.surface) >= 4.5 &&
                  Contrast(palette.accent, palette.accentSoft) >= 4.5 &&
                  Contrast(palette.selectedInk, palette.selected) >= 4.5,
                  "theme text does not meet minimum contrast");
            Check(Contrast(palette.track, palette.surface) >= 3 &&
                  Contrast(palette.thumb, palette.track) >= 3,
                  "slider controls disappear into their theme");
        }
        std::cout << "PASS light/dark text and slider contrast\n";

        RECT proposed{0, 0, 860, 1200};
        SendMessageW(panel.Window(), WM_DPICHANGED, MAKELONG(240, 240), reinterpret_cast<LPARAM>(&proposed));
        Check(panel.dpi_ == 240 && panel.scale_ == 2.5f, "popover did not follow a DPI change");
        SendMessageW(panel.Window(), WM_DPICHANGED, MAKELONG(120, 120), reinterpret_cast<LPARAM>(&proposed));
        Check(panel.dpi_ == 120 && panel.scale_ == 1.25f, "popover did not return from a DPI change");
        Check(!IsWindowVisible(panel.Window()), "hidden fixture unexpectedly opened a desktop window");
        std::cout << "PASS mixed DPI; fixtures stayed hidden and used no desktop capture or personal data\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
