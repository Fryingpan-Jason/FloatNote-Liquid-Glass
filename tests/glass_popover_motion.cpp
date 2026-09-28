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

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
int main() {
    try {
        // No Show(): hidden controls only, no desktop capture or personal data.
        GlassControlPopover::Panel panel;
        Check(panel.Create(GetModuleHandleW(nullptr), {}), "hidden popover creation");
        const HWND a = panel.controls_[0].window, b = panel.controls_[1].window;
        panel.animated_ = false;
        panel.NoteHover(a);
        Check(panel.HoverAmount(a) == 1, "disabled motion applies hover immediately");
        panel.NoteHover(nullptr);
        Check(panel.HoverAmount(a) == 0, "disabled motion clears hover immediately");
        panel.animated_ = true;
        panel.NoteHover(a);
        panel.hoverCurrent_.amount = .6f;
        panel.NoteHover(b);
        panel.hoverCurrent_.amount = .3f;
        panel.NoteHover(a);
        Check(std::abs(panel.HoverAmount(a) - .6f) < .001f, "quick hover reversal preserves current colour");
        panel.FinishClose();
        Check(panel.hovered_ == nullptr && panel.HoverAmount(a) == 0 && panel.HoverAmount(b) == 0,
              "closing clears hover for the next opening");
        panel.target_ = {100, 100, 420, 628}; panel.scale_ = 1;
        panel.closing_ = true; panel.animationFrom_ = .4f; panel.animationTo_ = 0;
        panel.animationStart_ = GetTickCount64(); panel.TickAnimation();
        RECT bounds{}; GetWindowRect(panel.window_, &bounds);
        Check(bounds.top >= 107 && bounds.top <= 110, "closing during entry preserves slide position");
        panel.Destroy();
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
