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

#include "../src/glass_control_appearance.h"
#define private public
#include "../experiments/glass_control_island.h"
#undef private

using GlassControlIsland::Controller;
using GlassControlIsland::Detail::DotImpulse;

static void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

int main() {
    DotImpulse pulse;
    pulse.Kick();
    Check(pulse.active && pulse.value == 0, "Impact jumps immediately instead of starting at the landed radius");
    float peak = 0, trough = 0;
    int peakMs = 0;
    for (int ms = 1; ms <= 300; ++ms) {
        pulse.Step(0.001f);
        if (pulse.value > peak) { peak = pulse.value; peakMs = ms; }
        trough = std::min(trough, pulse.value);
    }
    Check(peak >= 0.59f && peak <= 0.61f && peakMs >= 40 && peakMs <= 55,
          "Arrival overshoot lost its fast 60 percent peak");
    Check(trough >= -0.085f && trough <= -0.065f, "Arrival recoil is missing or too large");
    Check(!pulse.active && pulse.value == 0, "Impact does not settle within 300 ms");
    DotImpulse smooth, uneven;
    smooth.Kick(); uneven.Kick();
    for (int i = 0; i < 170; ++i) smooth.Step(0.001f);
    for (float dt : {0.017f, 0.046f, 0.011f, 0.096f}) uneven.Step(dt);
    Check(Near(smooth.value, uneven.value), "Impact depends on frame cadence");
    uneven.Step(2.0f);
    Check(!uneven.active && uneven.value == 0, "A late frame leaves a stale impact running");

    Controller control;
    Check(!control.SetFoldState(true, 1, false) && !control.dotImpulse_.active,
          "Loading a folded note replays an arrival impact");
    control.SetFoldState(false, 0, false);
    control.BeginNoteTransition(true);
    control.SetFoldState(true, 1, true);
    Check(!control.dotImpulse_.active, "The last in-flight endpoint triggers impact too early");
    Check(control.SetFoldState(true, 1, false) && control.dotImpulse_.active,
          "Completed absorption does not trigger impact");
    control.dotImpulse_.Step(0.046f);
    const float elapsed = control.dotImpulse_.elapsed;
    for (int i = 0; i < 8; ++i)
        Check(!control.SetFoldState(true, 1, false) && control.dotImpulse_.elapsed == elapsed,
              "Repeated settled updates restart the impact");

    int geometryFrames = 0;
    for (UINT dpi : {96u, 120u, 144u, 192u, 240u, 288u}) {
        control.dpi_ = dpi;
        const float scale = static_cast<float>(dpi) / 96.0f;
        for (const RECT sample : {RECT{100, 200, 600, 400}, RECT{100, 0, 600, 100}}) {
            const auto anchor = Controller::Place(sample, dpi);
            Check(Near(anchor.foldedCenterY, anchor.insideCenterY),
                  "Absorption endpoint is offset from the resting hint");
            const auto target = control.FoldedDotBounds(sample, dpi);
            Check(Near(target.centerY, anchor.insideCenterY),
                  "Host absorption target disagrees with the displayed center");
            Check(anchor.bounds.top <= anchor.foldedCenterY - 9.34f * scale &&
                  anchor.bounds.bottom >= anchor.foldedCenterY + 9.34f * scale,
                  "Anchored impact or its input fringe is clipped by the raster");
        }
        control.bounds_ = {100, 100, 100 + MulDiv(90, dpi, 96), 300};
        control.insideCenterY_ = control.foldedCenterY_ = 200;
        control.visible_ = true;
        for (bool below : {false, true}) {
            control.below_ = below;
            for (float hover : {0.0f, 1.0f, 1.04f}) {
                control.foldedHover_.value = hover;
                control.dotImpulse_.Clear();
                const auto baseline = control.CurrentDot();
                Check(Near(baseline.centerY, control.IdleSurface().centerY),
                      "Hover displaces the folded surface from the resting hint");
                const auto baselineSurface = control.Surface();
                const auto baselineReveal = control.RevealCorridor();
                control.dotImpulse_.Kick();
                for (int ms = 0; ms <= 280; ++ms) {
                    const auto dot = control.CurrentDot();
                    const auto surface = control.Surface();
                    const auto painted = control.VisibleSurfaceRects();
                    const auto reveal = control.RevealCorridor();
                    Check(Near(dot.centerX, baseline.centerX) && Near(dot.centerY, baseline.centerY),
                          "Impact moves the white dot's center");
                    Check(dot.radius <= (surface.height * 0.5f - 0.99f) * scale,
                          "White dot leaves the black bar");
                    Check(EqualRect(&baselineReveal, &reveal), "Impact moves the stable reveal corridor");
                    Check(painted[0].left >= control.bounds_.left && painted[0].right <= control.bounds_.right &&
                          painted[0].top >= control.bounds_.top && painted[0].bottom <= control.bounds_.bottom,
                          "Inflated capsule is clipped by its raster");
                    if (ms == 46) {
                        Check(surface.height > baselineSurface.height + 3.5f &&
                              surface.width > baselineSurface.width + 2.3f,
                              "Arrival impact does not visibly inflate the black capsule");
                    }
                    control.dotImpulse_.Step(0.001f);
                    ++geometryFrames;
                }
            }
        }
    }

    control.dotImpulse_.Kick();
    control.dotImpulse_.Step(0.046f);
    const auto beforeRestore = control.CurrentDot();
    const auto surfaceBeforeRestore = control.Surface();
    control.SetInputSuppressed(true);
    Check(Near(beforeRestore.radius, control.CurrentDot().radius) && control.dotImpulse_.active,
          "The input guard clears the dot before restore captures it");
    control.BeginNoteTransition(false);
    Check(!control.dotImpulse_.active, "Restore does not stop the decorative impact");
    Check(!control.SetFoldState(false, 1, true), "Restore start falsely reports an absorption");
    const auto restoreSource = control.CurrentDot();
    Check(Near(control.Surface().width, surfaceBeforeRestore.width) &&
          Near(control.Surface().height, surfaceBeforeRestore.height),
          "Interrupting impact with restore jumps from the inflated capsule");
    Check(Near(restoreSource.centerX, beforeRestore.centerX) &&
          Near(restoreSource.centerY, beforeRestore.centerY) && Near(restoreSource.radius, beforeRestore.radius),
          "Interrupting impact with restore jumps from the displayed dot");
    control.SetFoldState(false, 0, true);
    Check(!control.SetFoldState(false, 0, false) && !control.dotImpulse_.active,
          "Restore completion wrongly kicks a white dot");

    control.SetFoldState(false, 0, false);
    control.BeginNoteTransition(true);
    control.SetFoldState(true, 1, true);
    control.SetFoldState(true, 1, false);
    control.inputSuppressed_ = true;
    control.lastMotionTick_ = GetTickCount64() - 46;
    control.MotionFrame();
    Check(control.dotImpulse_.value > 0.5f && control.dotImpulse_.active,
          "The release guard prevents already-completed arrival feedback");
    control.Destroy();
    Check(!control.dotImpulse_.active && control.dotImpulse_.value == 0, "Destroy leaves impact state alive");

    // Create only hidden native providers: this checks real hide/reduced-motion
    // lifecycle without showing an app, moving the pointer, or changing a setting.
    Controller hidden;
    Check(hidden.Create(GetModuleHandleW(nullptr), [] {}, [](int, int) {}), "Cannot create hidden test providers");
    hidden.dotImpulse_.Kick();
    hidden.Update(RECT{100, 100, 500, 400}, 96, false, false, false, false);
    Check(!hidden.dotImpulse_.active, "Hiding the controller leaves impact running");
    hidden.visible_ = true; // Internal state only; HWNDs remain hidden.
    hidden.reduceMotion_ = true;
    hidden.dotImpulse_.Kick();
    hidden.Animate();
    Check(!hidden.dotImpulse_.active && hidden.dotImpulse_.value == 0,
          "Reduced motion does not clear an existing impact");
    hidden.BeginNoteTransition(true);
    hidden.SetFoldState(true, 1, true);
    hidden.SetFoldState(true, 1, false);
    Check(!hidden.dotImpulse_.active, "Reduced motion triggers a new impact");
    Check(!IsWindowVisible(hidden.Window()) && !IsWindowVisible(hidden.CloseWindow()),
          "Lifecycle test accidentally showed a native window");
    std::printf("Dot impulse: peak +%.1f%% at %d ms, recoil %.1f%%; %d geometry frames and lifecycle checks passed.\n",
                peak * 100, peakMs, trough * 100, geometryFrames);
    return 0;
}
