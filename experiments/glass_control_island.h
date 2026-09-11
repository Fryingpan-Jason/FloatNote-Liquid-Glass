#pragma once

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

namespace GlassControlIsland {

namespace Detail {
// Closed-form damped oscillator: pointer reversals preserve velocity, while an
// arbitrarily late frame remains stable. The small overshoot gives the surface
// elasticity without changing its anchored edge or its hit-test footprint.
struct Spring {
    float value = 0, velocity = 0;
    bool Step(float target, float seconds, float frequency=22.0f, float damping=0.74f) {
        const float dt = std::clamp(seconds, 0.0f, 1.0f);
        const float decayRate = frequency * damping;
        const float dampedFrequency = frequency * std::sqrt(1.0f - damping * damping);
        const float displacement = value - target;
        const float sineWeight = (velocity + decayRate * displacement) / dampedFrequency;
        const float decay = std::exp(-decayRate * dt);
        const float cosine = std::cos(dampedFrequency * dt), sine = std::sin(dampedFrequency * dt);
        value = target + decay * (displacement * cosine + sineWeight * sine);
        velocity = decay * ((-decayRate * displacement + dampedFrequency * sineWeight) * cosine +
                           (-decayRate * sineWeight - dampedFrequency * displacement) * sine);
        if (std::abs(value - target) < 0.0005f && std::abs(velocity) < 0.012f) {
            value = target;
            velocity = 0;
            return false;
        }
        return true;
    }
};

// A one-shot velocity impulse, separate from the note's spatial animation and
// from pointer hover. Its analytic solution starts at zero displacement, gives
// the arriving dot one fast overshoot and a smaller return, then settles.
struct DotImpulse {
    float value = 0, elapsed = 0;
    bool active = false;
    void Clear() { value = elapsed = 0; active = false; }
    void Kick() { value = elapsed = 0; active = true; }
    bool Step(float seconds) {
        if (!active) return false;
        constexpr float frequency = 26.0f, damping = 0.55f;
        constexpr float decayRate = frequency * damping;
        constexpr float duration = 0.280f;
        const float dampedFrequency = frequency * std::sqrt(1.0f - damping * damping);
        elapsed += std::max(0.0f, seconds);
        if (elapsed >= duration) { Clear(); return false; }
        // Normalize the first peak to +60% radius (about 46 ms). The following
        // undershoot is about -7.6%; the final residual is below half a percent.
        const float peakTime = std::atan(dampedFrequency / decayRate) / dampedFrequency;
        const float amplitude = 0.60f / (std::exp(-decayRate * peakTime) *
                                         std::sin(dampedFrequency * peakTime));
        value = amplitude * std::exp(-decayRate * elapsed) * std::sin(dampedFrequency * elapsed);
        return true;
    }
};

inline float Smooth(float low, float high, float value) {
    const float p = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return p * p * (3.0f - 2.0f * p);
}

inline bool Fade(float& value, float target, float seconds) {
    value = target + (value - target) * std::exp(-std::max(seconds, 0.0f) / 0.052f);
    if (std::abs(value - target) < 0.002f) { value = target; return false; }
    return true;
}
} // namespace Detail

// UI-thread only. This HWND intentionally has no owner: neither capture exclusion
// nor the note's click-through style should make the recovery control disappear.
// Drag callbacks receive TOTAL screen-pixel displacement from pointer-down, never
// incremental movement. Snapshot the note rect before the first onDragMove, move
// relative to that snapshot, and clear/save it in onDragEnd. A completed drag
// invokes onDragEnd even if another window takes pointer capture.
class Controller {
public:
    struct DotGeometry {
        float centerX = 0, centerY = 0, radius = 0;
        RECT PixelBounds() const {
            return {static_cast<LONG>(std::lround(centerX - radius)),
                    static_cast<LONG>(std::lround(centerY - radius)),
                    static_cast<LONG>(std::lround(centerX + radius)),
                    static_cast<LONG>(std::lround(centerY + radius))};
        }
    };
    Controller() = default;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    ~Controller() { Destroy(); }

    bool Create(HINSTANCE instance, std::function<void()> onToggle,
                std::function<void(int dx, int dy)> onDragEnd,
                std::function<void(int dx, int dy)> onDragMove = {},
                std::function<void()> onClose = {}) {
        Destroy();
        onToggle_ = std::move(onToggle);
        onDragEnd_ = std::move(onDragEnd);
        onDragMove_ = std::move(onDragMove);
        onClose_ = std::move(onClose);
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&graphicsToken_, &input, nullptr) != Gdiplus::Ok)
            return false;
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&common);
        // Keep a genuine BUTTON accessibility provider and BM_CLICK contract.
        // Painting and pointer capture are replaced, but its accessible name and
        // default action remain available to Windows automation and assistive UI.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                  L"BUTTON", label_.c_str(), WS_POPUP | BS_PUSHBUTTON,
                                  0, 0, 76, 28, nullptr, nullptr, instance, nullptr);
        if (!window_ || !SetWindowSubclass(window_, Procedure, kSubclassId,
                                          reinterpret_cast<DWORD_PTR>(this))) {
            Destroy();
            return false;
        }
        SetWindowDisplayAffinity(window_, WDA_NONE);
        // A separate real BUTTON gives close its own accessible name and action.
        // It has no owner and is only shown once the close glyph is visible.
        closeWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                      L"BUTTON", closeLabel_.c_str(), WS_POPUP | BS_PUSHBUTTON,
                                      0, 0, 29, 28, nullptr, nullptr, instance, nullptr);
        if (!closeWindow_ || !SetWindowSubclass(closeWindow_, Procedure, kSubclassId,
                                               reinterpret_cast<DWORD_PTR>(this))) {
            Destroy();
            return false;
        }
        SetWindowDisplayAffinity(closeWindow_, WDA_NONE);
        // Accessible button names stay available; native tooltips intentionally
        // do not exist, so no popup can become trapped behind a topmost note.
        ReadMotionPreference();
        return true;
    }

    void SetCloseLabel(std::wstring label) {
        closeLabel_ = std::move(label);
        if (!closeWindow_) return;
        SetWindowTextW(closeWindow_, closeLabel_.c_str());
    }

    void SetLabel(std::wstring label) {
        label_ = std::move(label);
        if (!window_)
            return;
        SetWindowTextW(window_, label_.c_str());
    }

    void SetInputSuppressed(bool suppressed) {
        if (inputSuppressed_ == suppressed) return;
        inputSuppressed_ = suppressed;
        if (suppressed) {
            // The resize gesture owns the whole absorption. Discard the hover
            // bridge/menu provenance before capture is transferred here; that
            // transfer is not a request to reveal settings or close.
            ClearExpansion();
            if (window_) KillTimer(window_, kMotionTimer);
            motionRunning_ = false;
            // Keep both BUTTON providers alive, but consume the release from an
            // absorption drag without treating it as a fresh restore or close.
            FinishClosePointer(false);
            FinishPointer(false);
            SyncCloseVisibility();
            Render();
        }
        // Preserve the hovered dot spring until Update snapshots the restore
        // origin. Clearing expansion only affects the normal note's controls.
        // Releasing suppression must also restart motion when hover itself
        // stayed unchanged throughout the absorption/release guard.
        if (!suppressed) { PollHover(); Animate(); }
    }

    void Update(const RECT& noteRect, UINT dpi, bool visible, bool topmost,
                bool passThrough, bool menuOpen, HWND noteWindow = nullptr, int cornerRadiusPx = 0,
                bool folded = false, float foldProgress = 1.0f, bool noteTransition = false) {
        if (!window_)
            return;
        const UINT newDpi = dpi ? dpi : 96;
        const float nextFoldProgress = std::clamp(foldProgress, 0.0f, 1.0f);
        const bool transitionStarted = noteTransition && !noteTransition_;
        if (transitionStarted) {
            BeginNoteTransition(!folded_);
        }
        const bool effectiveMenuOpen = menuOpen && !noteTransition && !inputSuppressed_;
        const bool appearanceChanged = dpi_ != newDpi || passThrough_ != passThrough ||
                                       menuOpen_ != effectiveMenuOpen || folded_ != folded ||
                                       foldProgress_ != nextFoldProgress || noteTransition_ != noteTransition;
        const bool foldChanged = folded_ != folded;
        const bool absorptionCompleted = SetFoldState(folded, nextFoldProgress, noteTransition);
        dpi_ = newDpi;
        passThrough_ = passThrough;
        menuOpen_ = effectiveMenuOpen;
        // Retain the API for the host, but note/body hover and note capture are
        // deliberately no longer expansion sources, including click-through.
        (void)noteWindow;
        noteRect_ = noteRect;
        cornerRadiusPx_ = std::max(0, cornerRadiusPx);
        const bool previousBelow = below_;
        const auto anchor = Place(noteRect, dpi_);
        below_ = anchor.below;
        insideCenterY_ = anchor.insideCenterY;
        outsideCenterY_ = anchor.outsideCenterY;
        foldedCenterY_ = anchor.foldedCenterY;
        const RECT next = TransitionBounds(anchor.bounds);
        const bool geometryChanged = !EqualRect(&next, &bounds_);
        const bool zChanged = !hasZState_ || topmost_ != topmost;
        const bool visibilityChanged = visible_ != visible;
        bounds_ = next;
        topmost_ = topmost;
        hasZState_ = true;
        visible_ = visible;
        if (geometryChanged || zChanged || visibilityChanged) {
            UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            if (!geometryChanged)
                flags |= SWP_NOMOVE | SWP_NOSIZE;
            if (!zChanged)
                flags |= SWP_NOZORDER;
            if (visibilityChanged)
                flags |= visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
            SetWindowPos(window_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         next.left, next.top, next.right - next.left, next.bottom - next.top, flags);
            SetWindowPos(closeWindow_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         next.left + SplitPixel(), next.top,
                         next.right - next.left - SplitPixel(), next.bottom - next.top,
                         (flags & ~(SWP_SHOWWINDOW | SWP_HIDEWINDOW)));
        }
        if (!visible) {
            KillTimer(window_, kHoverTimer);
            KillTimer(window_, kMotionTimer);
            motionRunning_ = false;
            expansion_ = {};
            foldedHover_ = {};
            dotImpulse_.Clear();
            settingsTint_ = closeTint_ = 0;
            hover_ = false;
            expanded_ = false;
            closeHover_ = false;
            keepExpandedUntil_ = 0;
            FinishClosePointer(false);
            SyncCloseVisibility();
            FinishPointer(false);
            return;
        }
        if ((foldChanged && folded_) || transitionStarted) {
            expanded_ = false;
            closeHover_ = false;
            FinishClosePointer(false);
            SyncCloseVisibility();
            Animate();
        }
        if (transitionStarted) {
            // The restore source has already captured the currently drawn dot.
            // Absorption always starts from the clean hint, never button glyphs.
            ClearExpansion();
            foldedHover_.value = foldedHover_.velocity = 0;
        }
        if (absorptionCompleted) Animate();
        if (visibilityChanged) SetTimer(window_, kHoverTimer, 32, nullptr);
        PollHover();
        if (geometryChanged || appearanceChanged || visibilityChanged || previousBelow != below_)
            Render();
    }

    void Destroy() {
        closePointerDown_ = false;
        if (closeWindow_) {
            if (GetCapture() == closeWindow_) ReleaseCapture();
            DestroyWindow(closeWindow_);
            closeWindow_ = nullptr;
        }
        if (window_) {
            // Destroying a control is not a request to move or open its note.
            pointerDown_ = false;
            if (GetCapture() == window_)
                ReleaseCapture();
            KillTimer(window_, kHoverTimer);
            KillTimer(window_, kMotionTimer);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (graphicsToken_) {
            Gdiplus::GdiplusShutdown(graphicsToken_);
            graphicsToken_ = 0;
        }
        bounds_ = {};
        noteRect_ = {};
        visible_ = hasZState_ = hover_ = closeHover_ = dragging_ = expanded_ = below_ = false;
        closeVisible_ = false;
        folded_ = motionRunning_ = inputSuppressed_ = noteTransition_ = false;
        foldProgress_ = 1.0f;
        expansion_ = {};
        foldedHover_ = {};
        dotImpulse_.Clear();
        settingsTint_ = closeTint_ = 0;
        keepExpandedUntil_ = 0;
        onToggle_ = {};
        onDragEnd_ = {};
        onDragMove_ = {};
        onClose_ = {};
    }

    HWND Window() const { return window_; }
    HWND CloseWindow() const { return closeWindow_; }
    RECT Bounds() const { return bounds_; }
    bool Expanded() const { return expanded_; }
    POINT DotCenter() const {
        const auto dot = CurrentDot();
        return {static_cast<LONG>(std::lround(dot.centerX)), static_cast<LONG>(std::lround(dot.centerY))};
    }
    DotGeometry CurrentDot() const {
        const auto surface = Surface();
        const float scale = static_cast<float>(dpi_) / 96.0f;
        return {static_cast<float>(bounds_.left + bounds_.right) * 0.5f,
                surface.centerY, surface.dotRadius * scale};
    }
    DotGeometry FoldedDotBounds(const RECT& note, UINT dpi) const {
        const auto anchor = Place(note, dpi ? dpi : 96);
        return {static_cast<float>(anchor.bounds.left + anchor.bounds.right) * 0.5f,
                anchor.foldedCenterY, 2.0f * static_cast<float>(dpi ? dpi : 96) / 96.0f};
    }
    std::array<RECT, 2> VisibleSurfaceRects() const {
        if (!visible_) return {};
        const auto surface = Surface();
        const float scale = static_cast<float>(dpi_) / 96.0f;
        const float centerX = static_cast<float>(bounds_.left + bounds_.right) * 0.5f;
        const auto rectangle = [scale, &surface](float left, float right) {
            return RECT{static_cast<LONG>(std::floor(left)),
                        static_cast<LONG>(std::floor(surface.centerY - surface.height * scale * 0.5f)),
                        static_cast<LONG>(std::ceil(right)),
                        static_cast<LONG>(std::ceil(surface.centerY + surface.height * scale * 0.5f))};
        };
        if (surface.separation < 0.999f)
            return {rectangle(centerX - surface.width * scale * 0.5f,
                              centerX + surface.width * scale * 0.5f), RECT{}};
        return {rectangle(centerX - 36.0f * scale, centerX + 8.0f * scale),
                rectangle(centerX + 10.0f * scale, centerX + 36.0f * scale)};
    }

private:
    static constexpr UINT_PTR kSubclassId = 0x474349;
    static constexpr UINT_PTR kHoverTimer = 1;
    static constexpr UINT_PTR kMotionTimer = 2;
    HWND window_ = nullptr;
    HWND closeWindow_ = nullptr;
    ULONG_PTR graphicsToken_ = 0;
    std::wstring label_ = L"便签控制";
    std::wstring closeLabel_ = L"关闭便签";
    std::function<void()> onToggle_;
    std::function<void(int, int)> onDragEnd_;
    std::function<void(int, int)> onDragMove_;
    std::function<void()> onClose_;
    RECT bounds_{};
    RECT noteRect_{};
    int cornerRadiusPx_ = 0;
    UINT dpi_ = 96;
    bool visible_ = false, topmost_ = false, hasZState_ = false;
    bool passThrough_ = false, menuOpen_ = false, hover_ = false;
    bool pointerDown_ = false, dragging_ = false, expanded_ = false, below_ = false;
    bool closeHover_ = false, closePointerDown_ = false, closeVisible_ = false;
    bool settingsDownFromIdle_ = false;
    bool folded_ = false, highContrast_ = false, reduceMotion_ = false, motionRunning_ = false;
    bool inputSuppressed_ = false;
    Detail::Spring expansion_;
    Detail::Spring foldedHover_;
    Detail::DotImpulse dotImpulse_;
    float settingsTint_ = 0, closeTint_ = 0;
    float foldProgress_ = 1.0f;
    bool noteTransition_ = false, transitionToFold_ = false;
    float insideCenterY_ = 0, outsideCenterY_ = 0, foldedCenterY_ = 0;
    struct SurfaceGeometry {
        float centerY = 0; // screen pixels, identical to the host morph endpoint
        float width = 72, height = 4, separation = 0, glyphAlpha = 0;
        float dotAlpha = 0, dotRadius = 0;
    };
    SurfaceGeometry transitionSource_{};
    RECT transitionSourceBounds_{};
    ULONGLONG lastMotionTick_ = 0;
    POINT pointerStart_{}, dragDelta_{};
    ULONGLONG keepExpandedUntil_ = 0;

    int Scale(int dip) const { return MulDiv(dip, static_cast<int>(dpi_), 96); }
    int SplitPixel() const {
        return static_cast<int>(std::lround(static_cast<float>(bounds_.right - bounds_.left) * 0.5f +
                                           9.0f * static_cast<float>(dpi_) / 96.0f));
    }

    struct AnchorGeometry {
        RECT bounds{};
        float insideCenterY = 0, outsideCenterY = 0, foldedCenterY = 0;
        bool below = false;
    };

    static AnchorGeometry Place(const RECT& note, UINT dpi) {
        const float scale = static_cast<float>(dpi) / 96.0f;
        const int width = std::max(1, MulDiv(90, static_cast<int>(dpi), 96));
        MONITORINFO monitor{sizeof(monitor)};
        RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (GetMonitorInfoW(MonitorFromRect(&note, MONITOR_DEFAULTTONEAREST), &monitor))
            work = monitor.rcWork;
        AnchorGeometry result;
        result.below = note.top - 28.0f * scale < work.top && note.bottom + 28.0f * scale <= work.bottom;
        if (result.below) {
            result.insideCenterY = note.bottom - 7.0f * scale;
            result.outsideCenterY = note.bottom + 13.0f * scale;
        } else {
            result.insideCenterY = note.top + 7.0f * scale;
            result.outsideCenterY = note.top - 13.0f * scale;
        }
        // Folding changes size around the resting hint's center, not position.
        result.foldedCenterY = result.insideCenterY;
        const int x = std::clamp(static_cast<int>(note.left + (note.right - note.left - width) / 2),
                                 static_cast<int>(work.left), std::max(static_cast<int>(work.left),
                                 static_cast<int>(work.right) - width));
        // Leave room for the arrival/hover inflation and its input fringe.
        // This is raster padding only; activation geometry stays unchanged.
        const int top = static_cast<int>(std::floor(std::min(result.insideCenterY - 10.0f * scale,
                                                           result.outsideCenterY - 15.0f * scale)));
        const int bottom = static_cast<int>(std::ceil(std::max(result.insideCenterY + 10.0f * scale,
                                                              result.outsideCenterY + 15.0f * scale)));
        result.bounds = {x, top, x + width, bottom};
        return result;
    }

    SurfaceGeometry IdleSurface() const {
        return {insideCenterY_, IdleWidth(), 4, 0, 0, 0, 0};
    }

    void ClearExpansion() {
        expanded_ = hover_ = closeHover_ = menuOpen_ = false;
        keepExpandedUntil_ = 0;
        expansion_ = {};
        settingsTint_ = closeTint_ = 0;
    }

    void BeginNoteTransition(bool toFold) {
        // A resize can cross the threshold while the hover retreat still has
        // velocity. Morphing its split surface would leave settings/X inside
        // the dot. Use only the inside hint for absorption. Conversely, restore
        // must preserve the exact hovered dot and its current inflation.
        transitionSource_ = toFold ? IdleSurface() : Surface();
        transitionSourceBounds_ = bounds_;
        transitionToFold_ = toFold;
        // A restore may interrupt the impact at its largest radius. Capture
        // that exact displayed dot before removing the decorative impulse.
        dotImpulse_.Clear();
    }

    bool SetFoldState(bool folded, float progress, bool transitioning) {
        // Reaching progress=1 still presents a final note frame. Only handoff
        // out of that transition means the note has really landed in the dot.
        const bool absorbed = noteTransition_ && !transitioning && transitionToFold_ && folded;
        folded_ = folded;
        foldProgress_ = progress;
        noteTransition_ = transitioning;
        if (absorbed && !reduceMotion_) dotImpulse_.Kick();
        return absorbed;
    }

    bool ExpansionAllowed() const {
        return !folded_ && !noteTransition_ && !inputSuppressed_;
    }

    SurfaceGeometry Surface() const {
        const float idleWidth = IdleWidth();
        if (noteTransition_) {
            const SurfaceGeometry destination = transitionToFold_ ?
                SurfaceGeometry{foldedCenterY_, 82, 9, 0, 0, 1, 2} :
                SurfaceGeometry{insideCenterY_, idleWidth, 4, 0, 0, 0, 0};
            const float p = TransitionProgress();
            const auto mix = [p](float a, float b) { return a + (b - a) * p; };
            return {mix(transitionSource_.centerY, destination.centerY),
                    mix(transitionSource_.width, destination.width),
                    mix(transitionSource_.height, destination.height),
                    mix(transitionSource_.separation, destination.separation),
                    mix(transitionSource_.glyphAlpha, destination.glyphAlpha),
                    mix(transitionSource_.dotAlpha, destination.dotAlpha),
                    mix(transitionSource_.dotRadius, destination.dotRadius)};
        }
        if (folded_) {
            const float hover = std::clamp(foldedHover_.value, 0.0f, 1.04f);
            // The arrival impulse inflates the whole capsule around the dot.
            // Emphasize height (+3.6 DIP at the peak), keeping horizontal
            // growth (+2.4 DIP) inside the raster without moving the reveal area.
            const float height = 9.0f + 2.0f * hover + 6.0f * dotImpulse_.value;
            const float width = 82.0f + 4.0f * hover + 4.0f * dotImpulse_.value;
            const float radius = std::min((2.0f + 0.4f * hover) * (1.0f + dotImpulse_.value),
                                          height * 0.5f - 1.0f);
            return {foldedCenterY_,
                    width, height, 0, 0, 1, radius};
        }
        if (inputSuppressed_) return IdleSurface();
        const float p = std::clamp(expansion_.value, 0.0f, 1.04f);
        return {insideCenterY_ + (outsideCenterY_ - insideCenterY_) * p,
                idleWidth + (72.0f - idleWidth) * p, 4.0f + 18.0f * p, Detail::Smooth(0.22f, 0.94f, p),
                Detail::Smooth(0.70f, 0.99f, p), 0, 0};
    }

    float TransitionProgress() const {
        const float p = transitionToFold_ ? foldProgress_ : 1.0f - foldProgress_;
        // The host supplies spatial progress, already eased by the note's
        // animation curve. Applying a second curve would separate the dot from
        // the body's destination, especially during the quick final approach.
        return std::clamp(p, 0.0f, 1.0f);
    }

    RECT TransitionBounds(const RECT& target) const {
        if (!noteTransition_) return target;
        const float p = TransitionProgress();
        if (p <= 0.0f) return transitionSourceBounds_;
        if (p >= 1.0f) return target;
        // A restored note can be constrained to a different screen position.
        // Move the raster with the exact same eased progress as its contents,
        // rather than placing it at the destination and clipping the old dot.
        const auto mix = [p](LONG a, LONG b) {
            return static_cast<LONG>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - a) * p));
        };
        return {mix(transitionSourceBounds_.left, target.left),
                mix(transitionSourceBounds_.top, target.top),
                mix(transitionSourceBounds_.right, target.right),
                mix(transitionSourceBounds_.bottom, target.bottom)};
    }

    float IdleWidth() const {
        // A maximally rounded, narrow note has very little horizontal room in
        // its top padding. Shorten only the hint to fit that contour; the two
        // exterior buttons retain their full targets and the hidden bar stays
        // wider. This avoids painting beyond a circular note's shoulders.
        const float scale = static_cast<float>(dpi_) / 96.0f;
        const float width = static_cast<float>(noteRect_.right - noteRect_.left) / scale;
        const float height = static_cast<float>(noteRect_.bottom - noteRect_.top) / scale;
        const float radius = std::min(static_cast<float>(cornerRadiusPx_) / scale, std::min(width, height) * 0.5f);
        const float inset = radius > 5.0f ? radius - std::sqrt(std::max(0.0f, radius * radius -
                                                                     (radius - 5.0f) * (radius - 5.0f))) : 0.0f;
        return std::clamp(width - 2.0f * inset - 4.0f, 12.0f, 72.0f);
    }

    bool BelongsTo(HWND target, HWND root) const {
        return target && root && (target == root || IsChild(root, target));
    }

    RECT RevealCorridor() const {
        // Once revealed, hover provenance must not move with the morphing pixels. This fixed
        // rectangle joins the inside hint, its travel gap and both exterior
        // buttons; it ends at 12 DIP inside the note's padding, above the EDIT.
        // The bottom-anchored version is the exact vertical mirror.
        const float scale = static_cast<float>(dpi_) / 96.0f;
        const float centerX = static_cast<float>(bounds_.left + bounds_.right) * 0.5f;
        return {static_cast<LONG>(std::floor(centerX - 40.0f * scale)),
                static_cast<LONG>(std::floor(std::min(insideCenterY_ - 5.0f * scale,
                                                     outsideCenterY_ - 15.0f * scale))),
                static_cast<LONG>(std::ceil(centerX + 40.0f * scale)),
                static_cast<LONG>(std::ceil(std::max(insideCenterY_ + 5.0f * scale,
                                                    outsideCenterY_ + 15.0f * scale)))};
    }

    RECT RevealArea() const {
        if (expanded_) return RevealCorridor();
        // Only the resting hint and a small margin can initiate reveal.
        // The destination/gap become active after reveal begins, and stop
        // being activation targets as soon as the retreat begins.
        const float scale = static_cast<float>(dpi_) / 96.0f;
        const float centerX = static_cast<float>(bounds_.left + bounds_.right) * 0.5f;
        const float halfWidth = (IdleWidth() * 0.5f + 4.0f) * scale;
        return {static_cast<LONG>(std::floor(centerX - halfWidth)),
                static_cast<LONG>(std::floor(insideCenterY_ - 5.0f * scale)),
                static_cast<LONG>(std::ceil(centerX + halfWidth)),
                static_cast<LONG>(std::ceil(insideCenterY_ + 5.0f * scale))};
    }

    bool OverRevealCorridor(POINT point, HWND target) const {
        if (!ExpansionAllowed()) return false;
        const RECT corridor = RevealArea();
        if (!PtInRect(&corridor, point) || !target) return false;
        if (BelongsTo(target, window_) || BelongsTo(target, closeWindow_)) return true;
        // Sense the transparent gap without giving it alpha or an input HWND:
        // clicks in the gap still reach the note/background normally. A window
        // actually above our controller blocks reveal, including other apps'
        // popovers. Only inspect z order while inside this small corridor.
        const HWND targetRoot = GetAncestor(target, GA_ROOT);
        if (!targetRoot) return false;
        HWND above = GetWindow(window_, GW_HWNDPREV);
        // Bound traversal if another process keeps rearranging/destroying its
        // windows concurrently. In that exceptional case, fail closed.
        for (unsigned visited = 0; above && visited < 256;
             ++visited, above = GetWindow(above, GW_HWNDPREV)) {
            if (above == targetRoot) return false;
        }
        return !above;
    }

    void PollHover() {
        if (!window_ || !visible_) return;
        if (inputSuppressed_ || noteTransition_) {
            const bool changed = expanded_ || hover_ || closeHover_ || menuOpen_ ||
                                 expansion_.value != 0 || expansion_.velocity != 0;
            ClearExpansion();
            SyncCloseVisibility();
            if (changed) Render();
            return;
        }
        const ULONGLONG now = GetTickCount64();
        POINT point{};
        bool overControl = false;
        bool overReveal = false;
        const bool previousCloseHover = closeHover_;
        const bool previousHover = hover_;
        closeHover_ = false;
        if (GetCursorPos(&point)) {
            const HWND target = WindowFromPoint(point);
            // Also require geometry: synthetic button messages and a stale HWND
            // hit must never reveal the control when the pointer is elsewhere.
            overControl = PtInRect(&bounds_, point) &&
                          (BelongsTo(target, window_) || BelongsTo(target, closeWindow_));
            closeHover_ = closeVisible_ && overControl && BelongsTo(target, closeWindow_);
            overReveal = OverRevealCorridor(point, target);
        }
        hover_ = overControl;
        const HWND capture = GetCapture();
        // Painted pixels still drive each button's tint, but cannot bypass the
        // smaller activation area while an exterior surface is retreating.
        const bool held = pointerDown_ || closePointerDown_ || menuOpen_ || overReveal ||
                          BelongsTo(capture, window_) || BelongsTo(capture, closeWindow_);
        if (held) keepExpandedUntil_ = now + 180;
        const bool next = ExpansionAllowed() && (held || (expanded_ && now < keepExpandedUntil_));
        if (expanded_ != next) {
            expanded_ = next;
            SyncCloseVisibility();
            Animate();
            Render();
        } else if (previousCloseHover != closeHover_ || previousHover != hover_) Animate();
    }

    void SyncCloseVisibility() {
        // A close button becomes actionable only when its own lobe and glyph
        // have appeared. During collapse, the parent paints that region again.
        const bool show = visible_ && expanded_ && ExpansionAllowed() && expansion_.value >= 0.84f;
        if (!closeWindow_ || closeVisible_ == show) return;
        closeVisible_ = show;
        ShowWindow(closeWindow_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    void ReadMotionPreference() {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        highContrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                        (contrast.dwFlags & HCF_HIGHCONTRASTON);
        BOOL clientAnimation = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientAnimation, 0);
        reduceMotion_ = highContrast_ || !clientAnimation;
        if (reduceMotion_) dotImpulse_.Clear();
    }

    void Animate() {
        if (!window_ || !visible_) return;
        if (reduceMotion_) {
            dotImpulse_.Clear();
            expansion_.value = expanded_ && ExpansionAllowed() ? 1.0f : 0.0f;
            expansion_.velocity = 0;
            foldedHover_.value = folded_ && !noteTransition_ && !inputSuppressed_ && hover_ ? 1.0f : 0.0f;
            foldedHover_.velocity = 0;
            settingsTint_ = hover_ && !closeHover_ && !inputSuppressed_ && !noteTransition_ ? 1.0f : 0.0f;
            closeTint_ = closeHover_ && ExpansionAllowed() ? 1.0f : 0.0f;
            KillTimer(window_, kMotionTimer);
            motionRunning_ = false;
            SyncCloseVisibility();
            Render();
        } else if (!motionRunning_) {
            motionRunning_ = true;
            lastMotionTick_ = GetTickCount64();
            // 17 ms limits this paint timer to fewer than 60 frames per second.
            // The timer stops once hover, tint and the arrival impulse settle.
            SetTimer(window_, kMotionTimer, 17, nullptr);
        }
    }

    void MotionFrame() {
        const ULONGLONG now = GetTickCount64();
        const float seconds = static_cast<float>(now - lastMotionTick_) / 1000.0f;
        lastMotionTick_ = now;
        // A faster, more pronounced rise and a short elastic settle. Retain
        // velocity when the pointer reverses instead of restarting an easing.
        bool active = expansion_.Step(expanded_ && ExpansionAllowed() ? 1.0f : 0.0f, seconds, 32.0f, 0.72f);
        active |= foldedHover_.Step(folded_ && !noteTransition_ && !inputSuppressed_ && hover_ ? 1.0f : 0.0f, seconds);
        active |= Detail::Fade(settingsTint_, hover_ && !closeHover_ && !inputSuppressed_ && !noteTransition_ ? 1.0f : 0.0f, seconds);
        active |= Detail::Fade(closeTint_, closeHover_ && ExpansionAllowed() ? 1.0f : 0.0f, seconds);
        // The release guard suppresses clicks, not the already completed
        // absorption's visual feedback; this never delays note input release.
        active |= dotImpulse_.Step(seconds);
        SyncCloseVisibility();
        Render();
        if (!active) {
            KillTimer(window_, kMotionTimer);
            motionRunning_ = false;
        }
    }

    static void RoundPath(Gdiplus::GraphicsPath& path, float x, float y,
                          float width, float height, float radius) {
        const float diameter = std::min(radius * 2, std::min(width, height));
        path.AddArc(x, y, diameter, diameter, 180, 90);
        path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
        path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0, 90);
        path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
    }

    static void SurfacePath(Gdiplus::GraphicsPath& path, float top, float height, float separation) {
        const float radius = height * 0.5f;
        if (separation < 0.001f) {
            RoundPath(path, 2, top, 72, height, radius);
            return;
        }
        if (separation >= 0.999f) {
            RoundPath(path, 2, top, 44, height, radius);
            RoundPath(path, 48, top, 26, height, radius);
            return;
        }
        // One continuous closed outline develops a waist before separating.
        // The neck closes from both sides rather than revealing two unrelated
        // rounded rectangles. Controls are the quarter-circle Bezier handles.
        const float bottom = top + height;
        const float neck = radius * separation;
        const float inset = radius * separation;
        constexpr float k = 0.55228475f;
        path.AddLine(2 + radius, top, 46 - inset, top);
        path.AddBezier(46 - inset, top, 46 - inset * (1 - k), top,
                       46.0f, top + neck * (1 - k), 46.0f, top + neck);
        path.AddLine(46.0f, top + neck, 48.0f, top + neck);
        path.AddBezier(48.0f, top + neck, 48.0f, top + neck * (1 - k),
                       48 + inset * (1 - k), top, 48 + inset, top);
        path.AddLine(48 + inset, top, 74 - radius, top);
        path.AddArc(74 - height, top, height, height, 270, 180);
        path.AddLine(74 - radius, bottom, 48 + inset, bottom);
        path.AddBezier(48 + inset, bottom, 48 + inset * (1 - k), bottom,
                       48.0f, bottom - neck * (1 - k), 48.0f, bottom - neck);
        path.AddLine(48.0f, bottom - neck, 46.0f, bottom - neck);
        path.AddBezier(46.0f, bottom - neck, 46.0f, bottom - neck * (1 - k),
                       46 - inset * (1 - k), bottom, 46 - inset, bottom);
        path.AddLine(46 - inset, bottom, 2 + radius, bottom);
        path.AddArc(2.0f, top, height, height, 90.0f, 180.0f);
        path.CloseFigure();
    }

    static Gdiplus::Color Mix(Gdiplus::Color a, Gdiplus::Color b, float amount) {
        const auto channel = [amount](BYTE x, BYTE y) {
            return static_cast<BYTE>(std::lround(x + (y - x) * std::clamp(amount, 0.0f, 1.0f)));
        };
        return Gdiplus::Color(channel(a.GetA(), b.GetA()), channel(a.GetR(), b.GetR()),
                              channel(a.GetG(), b.GetG()), channel(a.GetB(), b.GetB()));
    }

    void Render() {
        if (!window_ || !visible_ || !graphicsToken_)
            return;
        RenderSurface(window_, false);
        if (closeVisible_ && closeWindow_) RenderSurface(closeWindow_, true);
    }

    void RenderSurface(HWND target, bool close) {
        const int splitPixel = SplitPixel();
        const float rasterScale = static_cast<float>(dpi_) / 96.0f;
        const float originDip = static_cast<float>(bounds_.right - bounds_.left) / rasterScale * 0.5f - 38.0f;
        const float splitDip = static_cast<float>(splitPixel) / rasterScale - originDip;
        const int offset = close ? splitPixel : 0;
        const int width = static_cast<int>(bounds_.right - bounds_.left) - offset;
        const int height = static_cast<int>(bounds_.bottom - bounds_.top);
        if (width <= 0 || height <= 0)
            return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !dib || !pixels) {
            if (dib) DeleteObject(dib);
            if (dc) DeleteDC(dc);
            return;
        }
        const HGDIOBJ old = SelectObject(dc, dib);
        {
            // GDI+ writes premultiplied BGRA directly, including edge coverage.
            // Do not re-premultiply these pixels after drawing.
            Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
                                    static_cast<BYTE*>(pixels));
            Gdiplus::Graphics graphics(&bitmap);
            // The HWND spans inside and outside the note, but those unused
            // pixels must be genuinely transparent to native EDIT hit testing.
            // Only a 2 DIP enlargement of the visible shape receives alpha 1.
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.ScaleTransform(rasterScale, rasterScale);
            // Both rasters share exactly the same physical origin and split;
            // fractional DPI cannot shift one half of the connecting neck.
            if (close) graphics.TranslateTransform(-static_cast<float>(splitPixel) / rasterScale, 0);
            graphics.TranslateTransform(originDip, 0);
            if (!close && closeVisible_) {
                graphics.SetClip(Gdiplus::RectF(-originDip, 0, splitDip + originDip,
                                                static_cast<float>(height) / rasterScale));
            }
            const auto surface = Surface();
            const float surfaceHeight = surface.height;
            const float centerY = (surface.centerY - static_cast<float>(bounds_.top)) / rasterScale;
            const float top = centerY - surfaceHeight * 0.5f;
            const float separation = surface.separation;
            const float glyphAlpha = surface.glyphAlpha;
            Gdiplus::GraphicsPath hitPath;
            RoundPath(hitPath, 38.0f - surface.width * 0.5f - 2.0f, top - 2.0f,
                      surface.width + 4.0f, surfaceHeight + 4.0f, surfaceHeight * 0.5f + 2.0f);
            Gdiplus::SolidBrush hitTarget(Gdiplus::Color(1, 0, 0, 0));
            graphics.FillPath(&hitTarget, &hitPath);
            const bool pressed = close ? (closePointerDown_ && closeHover_) : (pointerDown_ && !dragging_);
            // Hidden press must not contract the dot after the host samples it
            // as the restore origin. Its hover spring supplies pressed feedback.
            const float contraction = pressed && !folded_ && !noteTransition_ ? 0.94f : 1.0f;
            const float centerX = close ? 61.0f : (closeVisible_ ? 24.0f : 38.0f);
            graphics.TranslateTransform(centerX, centerY);
            graphics.ScaleTransform(contraction, contraction);
            graphics.TranslateTransform(-centerX, -centerY);
            graphics.TranslateTransform(38.0f, centerY);
            graphics.ScaleTransform(surface.width / 72.0f, 1.0f);
            graphics.TranslateTransform(-38.0f, -centerY);
            const float materialWeight = std::max(glyphAlpha, folded_ ? foldProgress_ : 0.0f);
            Gdiplus::Color background = Mix(Gdiplus::Color(248, 64, 66, 70),
                                              Gdiplus::Color(248, 48, 50, 54), materialWeight);
            Gdiplus::Color settingsBackground = Mix(background, Gdiplus::Color(252, 72, 75, 80), settingsTint_);
            Gdiplus::Color closeBackground = Mix(background, Gdiplus::Color(255, 220, 52, 65), closeTint_);
            closeBackground = Mix(settingsBackground, closeBackground, separation);
            Gdiplus::Color foreground(255, 245, 247, 251);
            Gdiplus::Color closeForeground = Mix(Gdiplus::Color(255, 255, 112, 113),
                                                   Gdiplus::Color(255, 255, 255, 255), closeTint_);
            if (highContrast_) {
                const COLORREF bg = GetSysColor(COLOR_BTNFACE), fg = GetSysColor(COLOR_BTNTEXT);
                settingsBackground = Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg));
                closeBackground = settingsBackground;
                foreground = closeForeground = Gdiplus::Color(255, GetRValue(fg), GetGValue(fg), GetBValue(fg));
                if (closeHover_) {
                    const COLORREF cb = GetSysColor(COLOR_HIGHLIGHT), cf = GetSysColor(COLOR_HIGHLIGHTTEXT);
                    closeBackground = Gdiplus::Color(255, GetRValue(cb), GetGValue(cb), GetBValue(cb));
                    closeForeground = Gdiplus::Color(255, GetRValue(cf), GetGValue(cf), GetBValue(cf));
                }
            }
            Gdiplus::GraphicsPath path;
            SurfacePath(path, top, surfaceHeight, separation);
            Gdiplus::SolidBrush fill(settingsBackground);
            graphics.FillPath(&fill, &path);
            // Both HWNDs render the same global surface. Clipping at the split
            // keeps their shared neck coherent during reveal and retreat.
            const auto surfaceClip = graphics.Save();
            graphics.SetClip(Gdiplus::RectF(splitDip, 0, 100.0f, static_cast<float>(height) / rasterScale),
                             Gdiplus::CombineModeIntersect);
            Gdiplus::SolidBrush closeFill(closeBackground);
            graphics.FillPath(&closeFill, &path);
            graphics.Restore(surfaceClip);
            Gdiplus::Pen rim(Gdiplus::Color(highContrast_ ? 255 : 52, foreground.GetR(), foreground.GetG(),
                                           foreground.GetB()), 0.65f);
            graphics.DrawPath(&rim, &path);
            if (glyphAlpha > 0) {
                Gdiplus::Color crossColor(static_cast<BYTE>(255 * glyphAlpha), closeForeground.GetR(),
                                          closeForeground.GetG(), closeForeground.GetB());
                Gdiplus::Pen cross(crossColor, 1.65f);
                cross.SetStartCap(Gdiplus::LineCapRound);
                cross.SetEndCap(Gdiplus::LineCapRound);
                graphics.DrawLine(&cross, 57.0f, centerY - 4, 65.0f, centerY + 4);
                graphics.DrawLine(&cross, 65.0f, centerY - 4, 57.0f, centerY + 4);
                Gdiplus::Color glyph(static_cast<BYTE>(255 * glyphAlpha), foreground.GetR(),
                                     foreground.GetG(), foreground.GetB());
                Gdiplus::Pen line(glyph, 1.5f);
                line.SetStartCap(Gdiplus::LineCapRound);
                line.SetEndCap(Gdiplus::LineCapRound);
                Gdiplus::SolidBrush knob(glyph);
                // Original vector artwork: three adjustment tracks with alternating
                // handles, not a vendor font glyph or an embedded platform asset.
                const float rows[] = {centerY - 5.0f, centerY, centerY + 5.0f};
                const float handles[] = {20.0f, 28.0f, 23.0f};
                for (int row = 0; row < 3; ++row) {
                    graphics.DrawLine(&line, 15.5f, rows[row], 32.5f, rows[row]);
                    Gdiplus::SolidBrush cutout(settingsBackground);
                    graphics.FillEllipse(&cutout, handles[row] - 3, rows[row] - 3, 6.0f, 6.0f);
                    graphics.FillEllipse(&knob, handles[row] - 1.9f, rows[row] - 1.9f, 3.8f, 3.8f);
                }
                if (passThrough_) {
                    Gdiplus::SolidBrush status(highContrast_ ? glyph :
                        Gdiplus::Color(static_cast<BYTE>(255 * glyphAlpha), 111, 190, 255));
                    graphics.FillEllipse(&status, 36.0f, centerY + 5.0f, 2.5f, 2.5f);
                }
            }
            if (surface.dotAlpha > 0 && surface.dotRadius > 0) {
                // Cancel horizontal silhouette scaling: the dot remains round,
                // and CurrentDot reports these exact physical drawing bounds.
                const float radius = surface.dotRadius;
                const float radiusX = radius * 72.0f / surface.width;
                Gdiplus::SolidBrush dot(Gdiplus::Color(static_cast<BYTE>(255 * surface.dotAlpha),
                    foreground.GetR(), foreground.GetG(), foreground.GetB()));
                graphics.FillEllipse(&dot, 38.0f - radiusX, centerY - radius,
                                     radiusX * 2.0f, radius * 2.0f);
            }
            graphics.Flush(Gdiplus::FlushIntentionSync);
        }
        POINT destination{bounds_.left + offset, bounds_.top}, source{};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(target, nullptr, &destination, &size, dc, &source, 0, &blend, ULW_ALPHA);
        SelectObject(dc, old);
        DeleteObject(dib);
        DeleteDC(dc);
    }

    void FinishPointer(bool click) {
        if (!pointerDown_)
            return;
        const bool dragged = dragging_;
        const POINT delta = dragDelta_;
        pointerDown_ = dragging_ = false;
        if (GetCapture() == window_)
            ReleaseCapture();
        Render();
        if (inputSuppressed_ || noteTransition_) return;
        // Callouts are last, and copied first: the app may destroy this HWND or
        // update its state from the callback without leaving a stale callback.
        if (dragged) {
            auto callback = onDragEnd_;
            if (callback) callback(delta.x, delta.y);
        } else if (click) {
            auto callback = onToggle_;
            if (callback) callback();
        }
    }

    void FinishClosePointer(bool click) {
        if (!closePointerDown_) return;
        closePointerDown_ = false;
        if (GetCapture() == closeWindow_) ReleaseCapture();
        Render();
        // Do not turn a drag from close into a note move. It is a button: release
        // outside cancels. Idle/hidden invocation must never perform close.
        if (ExpansionAllowed() && click && visible_ && expanded_ && closeVisible_ && IsWindowVisible(closeWindow_)) {
            auto callback = onClose_;
            if (callback) callback();
        }
    }

    LRESULT HandleClose(UINT message, WPARAM wp, LPARAM lp) {
        if ((inputSuppressed_ || noteTransition_) && (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
                                message == BM_CLICK || message == WM_KEYDOWN)) return 0;
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(closeWindow_, &paint);
            EndPaint(closeWindow_, &paint);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (!visible_ || !expanded_ || !ExpansionAllowed() || !closeVisible_ ||
                !IsWindowVisible(closeWindow_) || !IsWindowEnabled(closeWindow_))
                return 0;
            closePointerDown_ = true;
            SetCapture(closeWindow_);
            PollHover();
            Render();
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, closeWindow_, 0};
            TrackMouseEvent(&tracking);
            PollHover();
            return 0;
        }
        case WM_MOUSELEAVE:
            PollHover();
            return 0;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT client{};
            GetClientRect(closeWindow_, &client);
            FinishClosePointer(PtInRect(&client, point) != FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            FinishClosePointer(false);
            return 0;
        case BM_CLICK:
            if (visible_ && expanded_ && ExpansionAllowed() && closeVisible_ && IsWindowVisible(closeWindow_) && IsWindowEnabled(closeWindow_)) {
                auto callback = onClose_;
                if (callback) callback();
            }
            return 0;
        case WM_KEYDOWN:
            if ((wp == VK_SPACE || wp == VK_RETURN) && !(lp & (1LL << 30))) {
                if (visible_ && expanded_ && ExpansionAllowed() && closeVisible_ && IsWindowVisible(closeWindow_) && IsWindowEnabled(closeWindow_)) {
                    auto callback = onClose_;
                    if (callback) callback();
                }
                return 0;
            }
            break;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            ReadMotionPreference();
            Animate();
            Render();
            break;
        }
        return DefSubclassProc(closeWindow_, message, wp, lp);
    }

    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        if ((inputSuppressed_ || noteTransition_) && (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
                                message == BM_CLICK || message == WM_KEYDOWN)) return 0;
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window_, &paint);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, dragging_ ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        case WM_LBUTTONDOWN:
            if (!IsWindowEnabled(window_)) return 0;
            // The close provider is hidden when retreat begins, before its
            // fading glyph disappears from this raster. Never reinterpret a
            // click on that departing glyph as the settings action.
            if (!expanded_ && !folded_ && Surface().glyphAlpha > 0 &&
                GET_X_LPARAM(lp) >= SplitPixel()) return 0;
            pointerStart_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window_, &pointerStart_);
            dragDelta_ = {};
            pointerDown_ = true;
            dragging_ = false;
            settingsDownFromIdle_ = !closeVisible_;
            expanded_ = ExpansionAllowed();
            SyncCloseVisibility();
            keepExpandedUntil_ = GetTickCount64() + 180;
            SetCapture(window_);
            Animate();
            Render();
            return 0;
        case WM_MOUSEMOVE: {
            if (!hover_) {
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
                TrackMouseEvent(&tracking);
            }
            PollHover();
            if (pointerDown_ && GetCapture() == window_) {
                POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ClientToScreen(window_, &point);
                dragDelta_ = {point.x - pointerStart_.x, point.y - pointerStart_.y};
                const bool wasDragging = dragging_;
                if (std::abs(dragDelta_.x) > GetSystemMetricsForDpi(SM_CXDRAG, dpi_) ||
                    std::abs(dragDelta_.y) > GetSystemMetricsForDpi(SM_CYDRAG, dpi_))
                    dragging_ = true;
                if (dragging_) {
                    if (!wasDragging) Render();
                    auto callback = onDragMove_;
                    if (callback) callback(dragDelta_.x, dragDelta_.y);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            PollHover();
            return 0;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT client{};
            GetClientRect(window_, &client);
            const bool hit = PtInRect(&client, point) && (settingsDownFromIdle_ || point.x < SplitPixel());
            ClientToScreen(window_, &point);
            dragDelta_ = {point.x - pointerStart_.x, point.y - pointerStart_.y};
            FinishPointer(hit);
            return 0;
        }
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            FinishPointer(false);
            return 0;
        case BM_CLICK:
            if (IsWindowEnabled(window_)) {
                auto callback = onToggle_;
                if (callback) callback();
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_SPACE || wp == VK_RETURN || (wp == VK_ESCAPE && menuOpen_)) {
                if ((lp & (1LL << 30)) == 0) {
                    auto callback = onToggle_;
                    if (callback) callback();
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kMotionTimer) {
                MotionFrame();
                return 0;
            }
            if (wp == kHoverTimer) {
                PollHover();
                return 0;
            }
            break;
        case WM_SETTINGCHANGE:
            ReadMotionPreference();
            PollHover();
            Animate();
            Render();
            break;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            ReadMotionPreference();
            Animate();
            Render();
            break;
        }
        return DefSubclassProc(window_, message, wp, lp);
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Controller*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, Procedure, kSubclassId);
            const LRESULT result = DefSubclassProc(window, message, wp, lp);
            if (window == self->closeWindow_) {
                self->closeWindow_ = nullptr;
                self->closeVisible_ = false;
            } else {
                self->window_ = nullptr;
            }
            return result;
        }
        return window == self->closeWindow_ ? self->HandleClose(message, wp, lp) : self->Handle(message, wp, lp);
    }
};

} // namespace GlassControlIsland
