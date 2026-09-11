#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <cstdio>
#include <cstdlib>

// Inspect the actual controller geometry without opening windows or moving the
// user's pointer. All platform/library headers are loaded before this test-only
// access switch; production API and implementation remain unchanged.
#define private public
#include "../experiments/glass_control_island.h"
#undef private

static void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using GlassControlIsland::Controller;
    int checks = 0;
    for (UINT dpi : {96u, 120u, 144u, 192u, 240u, 288u}) {
        const float scale = static_cast<float>(dpi) / 96.0f;
        for (bool below : {false, true}) {
            Controller control;
            control.dpi_ = dpi;
            control.below_ = below;
            control.noteRect_ = {100, 300, 1100, 900};
            const float edge = below ? 900.0f : 300.0f;
            control.insideCenterY_ = edge + (below ? -7.0f : 7.0f) * scale;
            control.outsideCenterY_ = edge + (below ? 13.0f : -13.0f) * scale;
            control.bounds_ = {static_cast<LONG>(600 - 45 * scale),
                               static_cast<LONG>(edge - 28 * scale),
                               static_cast<LONG>(600 + 45 * scale),
                               static_cast<LONG>(edge + 28 * scale)};
            control.visible_ = true;
            const RECT expected = control.RevealCorridor();
            const POINT parked{600, static_cast<LONG>(control.insideCenterY_)};
            const POINT gap{600, static_cast<LONG>(edge)};
            const POINT outside{expected.right + 1, parked.y};
            const LONG contentBoundary = static_cast<LONG>(std::lround(edge + (below ? -12 : 12) * scale));
            Check(below ? expected.top >= contentBoundary : expected.bottom <= contentBoundary,
                  "Reveal corridor extends into the text area");
            Check(!PtInRect(&expected, outside), "Leaving the corridor still counts as hover");
            Check(PtInRect(&expected, gap), "The travel gap does not preserve hover");

            const POINT destination{600, static_cast<LONG>(control.outsideCenterY_)};
            const RECT idleArea = control.RevealArea();
            Check(PtInRect(&idleArea, parked), "Resting hint cannot initiate reveal");
            Check(!PtInRect(&idleArea, gap) && !PtInRect(&idleArea, destination),
                  "Empty exterior space can initiate reveal");
            control.expanded_ = true;
            const RECT activeArea = control.RevealArea();
            Check(EqualRect(&activeArea, &expected), "Reveal does not activate the full stable corridor");

            // Reproduce a stationary pointer at the old hint while all actual
            // spring frames advance, settle, reverse and advance again.
            for (int frame = 0; frame < 240; ++frame) {
                control.expansion_.Step(frame < 100 || frame >= 140 ? 1.0f : 0.0f, 1.0f / 60.0f,32.0f,0.72f);
                const auto current = control.Surface();
                const RECT corridor = control.RevealArea();
                Check(EqualRect(&expected, &corridor), "Hover target moved with animation");
                Check(PtInRect(&corridor, parked), "Stationary hint pointer loses reveal provenance");
                const POINT actualSurface{600, static_cast<LONG>(std::lround(current.centerY))};
                Check(PtInRect(&corridor, actualSurface), "A spring frame leaves the stable hover corridor");
                ++checks;
            }
            control.expansion_.value = 1;
            const auto surfaces = control.VisibleSurfaceRects();
            const POINT settings{(surfaces[0].left + surfaces[0].right) / 2,
                                  (surfaces[0].top + surfaces[0].bottom) / 2};
            const POINT close{(surfaces[1].left + surfaces[1].right) / 2,
                               (surfaces[1].top + surfaces[1].bottom) / 2};
            Check(PtInRect(&expected, settings) && PtInRect(&expected, close),
                  "An actual button falls outside the reveal corridor");
            Check(settings.x - control.bounds_.left < control.SplitPixel() &&
                  close.x - control.bounds_.left >= control.SplitPixel(),
                  "Settings and close share the same native hit partition");

            // The destination is still visibly painted at the first retreat
            // frame. Its former hover area must already stop initiating reveal.
            control.expanded_ = false;
            const RECT retreatArea = control.RevealArea();
            Check(EqualRect(&retreatArea, &idleArea) && !PtInRect(&retreatArea, destination),
                  "Retreat keeps the exterior activation area alive");
            Check(PtInRect(&retreatArea, parked), "Returning to the resting hint cannot reverse retreat");

            control.inputSuppressed_ = true;
            Check(!control.ExpansionAllowed() && control.Surface().glyphAlpha == 0,
                  "Resize absorption still permits expanded button glyphs");
            control.inputSuppressed_ = false;
            control.noteTransition_ = true;
            control.transitionSource_ = control.IdleSurface();
            for (bool toFold : {false, true}) {
                control.transitionToFold_ = toFold;
                for (int i = 0; i <= 100; ++i) {
                    const float spatial = static_cast<float>(i) / 100.0f;
                    control.foldProgress_ = toFold ? spatial : 1.0f - spatial;
                    Check(std::abs(control.TransitionProgress() - spatial) < 0.00001f,
                          "Controller re-eased host spatial progress");
                    Check(!control.ExpansionAllowed() && control.Surface().glyphAlpha == 0,
                          "A note transition permits expanded button glyphs");
                }
            }
        }
    }
    std::printf("Stable hover: %d spring frames, both anchors at six DPIs; text boundary, button partitions, suppression and spatial progress passed.\n", checks);
    return 0;
}
