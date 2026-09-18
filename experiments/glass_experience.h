#pragma once
#include "glass_control_island.h"
#include "glass_control_popover.h"
#include "glass_close_behavior.h"
#include "glass_experience_policy.h"
#include "glass_absorb_motion.h"
#include "../src/glass_window_stacking.h"
#include <functional>

namespace {
GlassControlIsland::Controller experienceIsland;
GlassControlPopover::Panel experiencePanel;
void ExperienceAction(GlassControlPopover::Action action,int value);
bool experienceReady=false,experienceSyncing=false,experienceStacking=false,experienceDragging=false;
RECT experienceDragOrigin{};
int experienceNormalWidth=320,experienceNormalHeight=200,experienceNormalCorner=48,experienceGlassOpacity=0;
DWORD experienceStackError=0;
GlassClose::Behavior experienceCloseBehavior=GlassClose::Behavior::Ask;
bool experienceCloseDialogOpen=false;
bool experienceFolded=false,experienceColorDialogOpen=false,experienceColorRequestPending=false,experienceNativeSizing=false;
RECT experienceResizeOrigin{};
std::array<COLORREF,5> experienceBackgroundColors=GlassControlPopover::State{}.backgroundColors;
std::array<COLORREF,5> experienceTextColors=GlassControlPopover::State{}.textColors;
std::array<COLORREF,16> experienceCustomColors{};
bool experienceAbsorbing=false,experienceRestoring=false,experienceAbsorbInputBlocked=false;
bool experienceAbsorbStartedDuringDrag=false;
RECT experienceAbsorbAnchor{};
RECT experienceAbsorbDot{},experienceRestoreRect{};
bool experienceEndpointPresented=false;
unsigned experienceMotionGeometryMismatches=0;
RECT experienceLastRequested{},experienceLastActual{};
ULONGLONG experienceAbsorbStarted=0,experienceAbsorbReleased=0;
float experienceAbsorbProgress=1.0f,experienceAbsorbAlpha=1.0f;
unsigned experienceAbsorbDuration=GlassAbsorbMotion::DurationMs;

bool ExperienceAbsorbing(){return experienceAbsorbing || experienceRestoring;}
bool ExperienceInputBlocked(){return ExperienceAbsorbing() || experienceAbsorbInputBlocked;}
float ExperienceAbsorbOpacity(){return experienceAbsorbAlpha;}
void ExperienceSavedGeometry(RECT& rectangle){
    if(experienceAbsorbing)rectangle=experienceAbsorbAnchor;
    else if(experienceRestoring)rectangle=experienceRestoreRect;
}
void SyncExperienceCapture() {
    if(!experienceReady || g_closing || g_backdrop.mode<=0 || !g_isVisible || experienceFolded)return;
    g_backdrop.SetControlOccluders(experienceIsland.VisibleSurfaceRects());
}

bool ExperienceCompact() {
    return experienceFolded;
}
void ApplyWindowStacking(bool force) {
    if(!g_window || g_closing || experienceStacking)return;
    const bool actual=(GetWindowLongPtrW(g_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    if(!force && actual==g_settings.topmost)return;
    experienceStacking=true;
    // Pinned and click-through are independent. Apply after visual initialization
    // and reconcile the actual HWND bit; labels use that applied state too.
    const BOOL ok=SetWindowPos(g_window,g_settings.topmost?HWND_TOPMOST:HWND_NOTOPMOST,
        0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    experienceStackError=ok?0:GetLastError();
    if(ok && experienceReady && experiencePanel.Visible())
        SetWindowPos(experiencePanel.Window(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    experienceStacking=false;
}
GlassControlPopover::State ExperienceState() {
    GlassControlPopover::State s;
    s.pinned=g_window && (GetWindowLongPtrW(g_window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    s.passThrough=g_settings.passThrough;s.autoText=g_settings.autoTextColor;
    s.material=g_backdrop.mode==2?2:(g_backdrop.mode==0?1:(g_backdrop.mode<0?0:3));
    s.opacity=g_settings.opacityPercent;s.fontSize=g_settings.fontSize;
    s.blur=std::clamp(int(std::lround(g_backdrop.blur*4)),0,80);
    s.background=g_settings.themeColor;s.text=kNoteText;
    s.backgroundColors=experienceBackgroundColors;s.textColors=experienceTextColors;
    s.closeBehavior=static_cast<int>(experienceCloseBehavior);return s;
}
void SyncExperienceUI() {
    if(!experienceReady || experienceSyncing || g_closing || !IsWindow(g_window))return;
    experienceSyncing=true;
    // Pointer capture can end on a different HWND after the rounded grip moves.
    // Complete the same resize transaction once the physical gesture is over,
    // even if the original child did not receive its final button-up message.
    if(experienceNativeSizing && !ExperienceInputBlocked() && !GetCapture() && !(GetAsyncKeyState(VK_LBUTTON)&0x8000))
        FinishExperienceResize();
    RECT note{};GetWindowRect(g_window,&note);const auto state=ExperienceState();
    const RECT anchor=ExperienceAbsorbing()?experienceAbsorbAnchor:note;
    experienceIsland.Update(anchor,GetDpiForWindow(g_window),g_isVisible && (experienceFolded || IsWindowVisible(g_window)) && !IsIconic(g_window),
        state.pinned,state.passThrough,experiencePanel.Visible() || experienceCloseDialogOpen || experienceColorDialogOpen,
        g_window,ScaleForDpi(g_window,NoteCornerRadius()),experienceFolded || ExperienceAbsorbing(),
        experienceAbsorbProgress,ExperienceAbsorbing(),g_settings.themeColor,
        std::max(g_settings.passThrough?0:1,MulDiv(EffectiveOpacityPercent(),255,100)));
    // Activation/restoration can raise the note above its independent hint.
    // Insert the controls immediately above the note, preserving applications
    // already above it rather than raising the controls to the top of the band.
    if(IsWindowVisible(g_window) && IsWindowVisible(experienceIsland.Window())) {
        GlassWindowStacking::PlaceAbove(experienceIsland.Window(),g_window,state.pinned);
        if(IsWindowVisible(experienceIsland.CloseWindow()))
            GlassWindowStacking::PlaceAbove(experienceIsland.CloseWindow(),experienceIsland.Window(),state.pinned);
    }
    SyncExperienceCapture();
    experiencePanel.UpdateState(state);
#ifndef FLOATNOTE_LOCAL_DESKTOP
    static ULONGLONG written=0;
    if(GetTickCount64()-written>500){
        written=GetTickCount64();DWORD affinity=~0u,noteAffinity=~0u;
        GetWindowDisplayAffinity(experienceIsland.Window(),&affinity);GetWindowDisplayAffinity(g_window,&noteAffinity);
        std::ofstream(ExecutableDirectory()/L"ux-status.txt")
            <<"topmostPreference="<<g_settings.topmost<<"\nactualTopmost="<<state.pinned
            <<"\nstackingError="<<experienceStackError<<"\npassThrough="<<state.passThrough
            <<"\nfolded="<<experienceFolded<<"\nnoteVisible="<<IsWindowVisible(g_window)
            <<"\nabsorbing="<<experienceAbsorbing<<"\nabsorbProgress="<<experienceAbsorbProgress
            <<"\nrestoring="<<experienceRestoring
            <<"\nmotionGeometryMismatches="<<experienceMotionGeometryMismatches
            <<"\nlastMotionRequested="<<experienceLastRequested.left<<","<<experienceLastRequested.top<<","<<experienceLastRequested.right<<","<<experienceLastRequested.bottom
            <<"\nlastMotionActual="<<experienceLastActual.left<<","<<experienceLastActual.top<<","<<experienceLastActual.right<<","<<experienceLastActual.bottom
            <<"\nabsorbInputBlocked="<<experienceAbsorbInputBlocked
            <<"\nabsorbStartedDuringDrag="<<experienceAbsorbStartedDuringDrag
            <<"\ncontrolHwnd="<<reinterpret_cast<uintptr_t>(experienceIsland.Window())
            <<"\ncontrolExpanded="<<experienceIsland.Expanded()
            <<"\ncloseControlHwnd="<<reinterpret_cast<uintptr_t>(experienceIsland.CloseWindow())
            <<"\ncloseBehavior="<<static_cast<int>(experienceCloseBehavior)
            <<"\ncloseDialogOpen="<<experienceCloseDialogOpen
            <<"\ncolorDialogOpen="<<experienceColorDialogOpen<<"\nblurQuarter="<<state.blur
            <<"\nmaterialMode="<<g_backdrop.mode<<"\nnoteCaptureAffinity="<<noteAffinity
            <<"\ncontrolCaptureAffinity="<<affinity<<"\ncontrolOwner="<<reinterpret_cast<uintptr_t>(GetWindow(experienceIsland.Window(),GW_OWNER))
            <<"\ncontrolPassThrough="<<((GetWindowLongPtrW(experienceIsland.Window(),GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0)
            <<"\npanelHwnd="<<reinterpret_cast<uintptr_t>(experiencePanel.Window())
            <<"\npanelVisible="<<experiencePanel.Visible()<<"\nnoteWidth="<<note.right-note.left<<"\nnoteHeight="<<note.bottom-note.top
            <<"\nnoteDpi="<<GetDpiForWindow(g_window)<<"\n";
    }
#endif
    experienceSyncing=false;
}
bool SaveExperienceRestoreSize() {
    std::string values="[Experience]\r\nnormalWidth="+std::to_string(experienceNormalWidth)+
        "\r\nnormalHeight="+std::to_string(experienceNormalHeight)+"\r\nnormalCorner="+std::to_string(experienceNormalCorner)+
        "\r\nglassOpacity="+std::to_string(experienceGlassOpacity)+
        "\r\ncloseBehavior="+std::to_string(static_cast<int>(experienceCloseBehavior))+
        "\r\nfolded="+std::to_string(experienceFolded || experienceAbsorbing)+"\r\n";
    for(size_t i=0;i<experienceBackgroundColors.size();++i) {
        values+="backgroundColor"+std::to_string(i)+"="+std::to_string(experienceBackgroundColors[i])+"\r\n";
        values+="textColor"+std::to_string(i)+"="+std::to_string(experienceTextColors[i])+"\r\n";
    }
    return AtomicWrite(g_dataDirectory/L"experience.ini",values);
}
void SaveExperienceMaterial() {SaveGlassLabPreferences();}
UINT_PTR CALLBACK ExperienceColorHook(HWND window,UINT message,WPARAM,LPARAM lp) {
    if(message==WM_INITDIALOG) {
        const auto* choice=reinterpret_cast<const CHOOSECOLORW*>(lp);
        SetWindowTextW(window,choice->lCustData?L"添加背景颜色":L"添加文字颜色");
        SetWindowDisplayAffinity(window,WDA_NONE);
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    return 0;
}
void ChooseExperienceColor(bool background) {
    if(!experienceColorRequestPending)return;
    experienceColorRequestPending=false;
    if(!experienceReady || g_closing || experienceColorDialogOpen || experienceCloseDialogOpen) {
        if(experienceReady)experiencePanel.SetModalOpen(false);
        return;
    }
    experienceColorDialogOpen=true;experiencePanel.SetModalOpen(true);SyncExperienceUI();
    CHOOSECOLORW choice{sizeof(choice)};
    choice.hwndOwner=experiencePanel.Visible()?experiencePanel.Window():experienceIsland.Window();
    choice.rgbResult=background?g_settings.themeColor:kNoteText;
    choice.lpCustColors=experienceCustomColors.data();
    choice.Flags=CC_RGBINIT|CC_FULLOPEN|CC_ANYCOLOR|CC_ENABLEHOOK;
    choice.lpfnHook=ExperienceColorHook;choice.lCustData=background;
    const BOOL accepted=ChooseColorW(&choice);
    experienceColorDialogOpen=false;
    if(g_closing || !IsWindow(g_window))return;
    if(accepted) {
        auto& palette=background?experienceBackgroundColors:experienceTextColors;
        const auto previous=palette;
        palette=GlassExperiencePolicy::PromoteColor(palette,choice.rgbResult);
        if(SaveExperienceRestoreSize()) {
            if(background) {
                g_settings.themeColor=palette.front();
                if(g_settings.opacityPercent==0)g_settings.opacityPercent=14;
            } else {g_settings.textColor=palette.front();g_settings.autoTextColor=false;}
            RefreshTheme();if(background)ApplyVisuals(g_window);SaveSettings();
        } else {
            palette=previous;
            MessageBoxW(choice.hwndOwner,L"未能保存自定义颜色，原来的颜色已保留。",L"FloatNote",MB_OK|MB_ICONWARNING);
        }
    }
    experiencePanel.SetModalOpen(false);SyncExperienceUI();RefreshLabControls();
}
bool SetExperienceCloseBehavior(GlassClose::Behavior value) {
    if(value==experienceCloseBehavior)return true;
    const auto previous=experienceCloseBehavior;experienceCloseBehavior=value;
    if(SaveExperienceRestoreSize())return true;
    experienceCloseBehavior=previous;
    MessageBoxW(experiencePanel.Visible()?experiencePanel.Window():experienceIsland.Window(),
        L"未能保存关闭偏好，仍保留之前的设置。请检查数据目录是否可写。",L"FloatNote",MB_OK|MB_ICONWARNING);
    return false;
}
HRESULT CALLBACK ExperienceCloseDialogProcedure(HWND window,UINT message,WPARAM,LPARAM,LONG_PTR) {
    if(message==TDN_CREATED) {
        SetWindowDisplayAffinity(window,WDA_NONE);
        // The note and its two controls are independent top-level windows. Keep
        // the choice dialog above all three, not merely above its own parent.
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    return S_OK;
}
void RequestExperienceClose() {
    if(!experienceReady || g_closing || experienceCloseDialogOpen || experienceColorDialogOpen || experienceColorRequestPending)return;
    LeaveMarkdownEditor();
    experiencePanel.Close(false);
    auto decision=GlassClose::Resolve(experienceCloseBehavior,GlassClose::Behavior::Ask,false,false);
    if(experienceCloseBehavior==GlassClose::Behavior::Ask) {
        experienceCloseDialogOpen=true;SyncExperienceUI();
        const TASKDIALOG_BUTTON buttons[]={{1001,L"隐藏到托盘"},
                                          {1002,L"退出 FloatNote"},{IDCANCEL,L"取消"}};
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        // The capturable top-level controller owns the dialog, not the note with
        // WDA_EXCLUDEFROMCAPTURE. Post the request so UIA invocation can return
        // before this modal loop begins; repeated requests are guarded above.
        dialog.hwndParent=experienceIsland.Window();dialog.hInstance=g_instance;
        dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW;
        dialog.pszWindowTitle=L"关闭 FloatNote";
        dialog.pszMainInstruction=L"这次如何关闭 FloatNote？";
        dialog.pszContent=L"隐藏到托盘：继续运行，双击托盘图标可找回便签。\n退出 FloatNote：保存笔记并结束程序。";
        dialog.cButtons=3;dialog.pButtons=buttons;dialog.nDefaultButton=1001;
        dialog.pfCallback=ExperienceCloseDialogProcedure;
        dialog.pszVerificationText=L"以后不再询问（可在设置中修改）";
        int clicked=IDCANCEL;BOOL remember=FALSE;
        const HRESULT result=TaskDialogIndirect(&dialog,&clicked,nullptr,&remember);
        experienceCloseDialogOpen=false;
        if(g_closing || !IsWindow(g_window))return;
        const auto choice=clicked==1001?GlassClose::Behavior::Tray:
            clicked==1002?GlassClose::Behavior::Exit:GlassClose::Behavior::Ask;
        decision=GlassClose::Resolve(experienceCloseBehavior,choice,remember!=FALSE,SUCCEEDED(result));
        if(FAILED(result))MessageBoxW(experienceIsland.Window(),L"暂时无法打开关闭选项。便签已保留，请重试。",
            L"FloatNote",MB_OK|MB_ICONINFORMATION);
        if(!SetExperienceCloseBehavior(decision.preference)){SyncExperienceUI();return;}
    }
    if(decision.action==GlassClose::Behavior::Tray) {
        if(g_isVisible && (experienceFolded || IsWindowVisible(g_window)))ToggleVisibility();
    } else if(decision.action==GlassClose::Behavior::Exit) {
        // Keep the existing save-before-exit and save-failure handling in WM_CLOSE.
        PostMessageW(g_window,WM_CLOSE,0,0);
    }
    SyncExperienceUI();
}
void ExpandExperienceNote() {
    if(!ExperienceCompact() || ExperienceInputBlocked())return;
    RECT current{};GetWindowRect(g_window,&current);
    experienceAbsorbDot=experienceIsland.CurrentDot().PixelBounds();
    g_backdrop.cornerRadius=float(experienceNormalCorner);
    const int width=ScaleForDpi(g_window,std::max(kMinimumNoteWidth,experienceNormalWidth));
    const int height=ScaleForDpi(g_window,std::max(60,experienceNormalHeight));
    experienceRestoreRect={(current.left+current.right-width)/2,current.top,
        (current.left+current.right-width)/2+width,current.top+height};
    MONITORINFO monitor{sizeof(monitor)};
    if(GetMonitorInfoW(MonitorFromRect(&current,MONITOR_DEFAULTTONEAREST),&monitor))
        experienceRestoreRect=ConstrainToWorkArea(experienceRestoreRect,monitor.rcWork);
    experienceAbsorbAnchor=experienceRestoreRect;
    experienceFolded=false;experienceRestoring=true;g_isVisible=true;
    experienceAbsorbInputBlocked=true;experienceAbsorbReleased=0;
    experienceEndpointPresented=false;experienceMotionGeometryMismatches=0;experienceAbsorbProgress=1;experienceAbsorbAlpha=1;
    experienceIsland.SetInputSuppressed(true);
    // Persist the final restore state at completion, not on the click path.
    g_backdrop.SetPresentationMotion(true);
    // Establish the real start at the currently drawn (possibly hovered) dot.
    // Geometry is never enlarged after the curve computes its pixel bounds.
    SyncExperienceUI();
    SetWindowPos(g_window,nullptr,experienceAbsorbDot.left,experienceAbsorbDot.top,
        experienceAbsorbDot.right-experienceAbsorbDot.left,experienceAbsorbDot.bottom-experienceAbsorbDot.top,
        SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
    ShowWindow(g_window,SW_SHOWNOACTIVATE);ApplyInteractionMode();
    g_backdrop.SetPresentationOpacity(1.0f);
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    experienceAbsorbDuration=animations && !g_highContrast?GlassAbsorbMotion::RestoreDurationMs:0;
    experienceAbsorbStarted=GetTickCount64();SetTimer(g_window,75,17,nullptr);TickExperienceAbsorb();
}
void BeginExperienceResize() {
    if(!experienceReady || experienceFolded || ExperienceInputBlocked() || experienceNativeSizing)return;
    GetWindowRect(g_window,&experienceResizeOrigin);experienceNativeSizing=true;
    const UINT dpi=GetDpiForWindow(g_window);
    if(experienceResizeOrigin.bottom-experienceResizeOrigin.top>ScaleForDpi(g_window,44)) {
        experienceNormalWidth=MulDiv(experienceResizeOrigin.right-experienceResizeOrigin.left,96,dpi);
        experienceNormalHeight=MulDiv(experienceResizeOrigin.bottom-experienceResizeOrigin.top,96,dpi);
        experienceNormalCorner=int(std::lround(g_backdrop.cornerRadius));
    }
}
void FinishExperienceResize(const RECT* original) {
    if(!experienceReady || experienceFolded || ExperienceInputBlocked() || g_closing)return;
    if(!original && !experienceNativeSizing)return;
    RECT current{};GetWindowRect(g_window,&current);const UINT dpi=GetDpiForWindow(g_window);
    const RECT initial=original?*original:experienceNativeSizing?experienceResizeOrigin:current;
    experienceNativeSizing=false;
    if(!GlassExperiencePolicy::ShouldFold(current.bottom-current.top,dpi)) {
        if(current.bottom-current.top>ScaleForDpi(g_window,44)) {
            experienceNormalWidth=MulDiv(current.right-current.left,96,dpi);
            experienceNormalHeight=MulDiv(current.bottom-current.top,96,dpi);
            experienceNormalCorner=int(std::lround(g_backdrop.cornerRadius));
        }
        SaveExperienceRestoreSize();
        return;
    }
    SavePendingNote();
    if(g_saveFailed) {
        SetWindowPos(g_window,nullptr,initial.left,initial.top,initial.right-initial.left,
            std::max(ScaleForDpi(g_window,60),int(initial.bottom-initial.top)),SWP_NOZORDER|SWP_NOACTIVATE);
        return;
    }
    if(initial.bottom-initial.top>ScaleForDpi(g_window,44)) {
        experienceNormalWidth=MulDiv(initial.right-initial.left,96,dpi);
        experienceNormalHeight=MulDiv(initial.bottom-initial.top,96,dpi);
        experienceNormalCorner=int(std::lround(g_backdrop.cornerRadius));
    }
    // Legacy/capture-lost completion still joins the same animation. The usual
    // path starts earlier, directly from the resizing gesture at its threshold.
    experienceNativeSizing=true;
    MaybeAbsorbExperienceResize();
}
void MaybeAbsorbExperienceResize() {
    if(!experienceReady || !experienceNativeSizing || experienceFolded || ExperienceInputBlocked() || g_closing)return;
    RECT current{};GetWindowRect(g_window,&current);
    if(!GlassExperiencePolicy::ShouldFold(current.bottom-current.top,GetDpiForWindow(g_window)))return;
    SavePendingNote();if(g_saveFailed)return;
    experienceAbsorbStartedDuringDrag=g_pointerDown;
    experienceAbsorbAnchor=current;experienceAbsorbProgress=0;experienceAbsorbAlpha=1;
    experienceAbsorbDot=experienceIsland.FoldedDotBounds(current,GetDpiForWindow(g_window)).PixelBounds();
    experienceEndpointPresented=false;experienceMotionGeometryMismatches=0;
    experienceAbsorbing=true;experienceAbsorbInputBlocked=true;experienceAbsorbReleased=0;
    if(!SaveExperienceRestoreSize()) {
        experienceAbsorbing=experienceAbsorbInputBlocked=false;experienceAbsorbProgress=1;
        SetWindowPos(g_window,nullptr,experienceResizeOrigin.left,experienceResizeOrigin.top,
            experienceResizeOrigin.right-experienceResizeOrigin.left,experienceResizeOrigin.bottom-experienceResizeOrigin.top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        experienceNativeSizing=false;return;
    }
    experienceNativeSizing=false;experiencePanel.Close(false);KillTimer(g_window,kCaretTimer);
    g_backdrop.SetPresentationMotion(true);
    // Take over this gesture before moving/hiding the grip. Its queued up or
    // capture-lost notifications may still arrive, but cannot restart resizing.
    g_pointerDown=false;g_pointerDragged=false;
    experienceIsland.SetInputSuppressed(true);SetCapture(experienceIsland.Window());
    SyncExperienceUI();
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    experienceAbsorbDuration=animations && !g_highContrast?GlassAbsorbMotion::DurationMs:0;
    experienceAbsorbStarted=GetTickCount64();UpdateLayout(g_window);
    SetTimer(g_window,75,17,nullptr);TickExperienceAbsorb();
}
void TickExperienceAbsorb() {
    if(!experienceReady || g_closing)return;
    const ULONGLONG now=GetTickCount64();
    if(ExperienceAbsorbing()) {
        const bool restoring=experienceRestoring;
        const double progress=experienceAbsorbDuration?double(now-experienceAbsorbStarted)/experienceAbsorbDuration:1.0;
        const auto frame=restoring?GlassAbsorbMotion::EvaluateExpand(experienceAbsorbDot,experienceRestoreRect,progress):
            GlassAbsorbMotion::EvaluateCollapse(experienceAbsorbAnchor,experienceAbsorbDot,progress);
        // Share the eased spatial phase with the control shell; time only
        // controls lifecycle so the bar and note cannot take different paths.
        experienceAbsorbProgress=restoring?1.0f-frame.motion:frame.motion;
        experienceAbsorbAlpha=frame.opacity;
        if(frame.progress<1 || !experienceEndpointPresented) {
            SetWindowPos(g_window,nullptr,frame.rect.left,frame.rect.top,
                frame.rect.right-frame.rect.left,frame.rect.bottom-frame.rect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
            RECT actual{};if(GetWindowRect(g_window,&actual)) {
                experienceLastRequested=frame.rect;experienceLastActual=actual;
                if(!EqualRect(&actual,&frame.rect))++experienceMotionGeometryMismatches;
            }
            g_backdrop.SetPresentationOpacity(experienceAbsorbAlpha);
            RequestRender();
            // Present the actual endpoint for a frame before ownership passes to
            // the dot/text. Never skip from the last larger rectangle to hidden.
            if(frame.progress>=1)experienceEndpointPresented=true;
        } else if(!restoring) {
            ShowWindow(g_window,SW_HIDE);g_backdrop.SuspendForFold();
            g_backdrop.SetPresentationOpacity(1.0f);
            experienceAbsorbing=false;experienceFolded=true;experienceAbsorbAlpha=1;
            // Keep saved/restore geometry at the original thin-card anchor, not
            // at any tiny in-flight rectangle near the controller.
            SetWindowPos(g_window,nullptr,experienceAbsorbAnchor.left,experienceAbsorbAnchor.top,
                experienceAbsorbAnchor.right-experienceAbsorbAnchor.left,experienceAbsorbAnchor.bottom-experienceAbsorbAnchor.top,
                SWP_NOZORDER|SWP_NOACTIVATE);
            SaveSettings();SaveExperienceRestoreSize();
        } else {
            experienceRestoring=false;experienceAbsorbAlpha=1;
            g_backdrop.SetPresentationMotion(false);
            g_backdrop.SetPresentationOpacity(1.0f);
            UpdateLayout(g_window);SaveSettings();SaveExperienceRestoreSize();SaveGlassLabPreferences();
        }
        SyncExperienceUI();
    }
    if(experienceAbsorbInputBlocked) {
        if(GetAsyncKeyState(VK_LBUTTON)&0x8000)experienceAbsorbReleased=0;
        else {
            if(GetCapture()==experienceIsland.Window())ReleaseCapture();
            if(!experienceAbsorbReleased)experienceAbsorbReleased=now;
            if(!ExperienceAbsorbing() && now-experienceAbsorbReleased>=150) {
                experienceAbsorbInputBlocked=false;experienceIsland.SetInputSuppressed(false);
            }
        }
    }
    if(!ExperienceAbsorbing() && !experienceAbsorbInputBlocked)KillTimer(g_window,75);
}
void CloseExperienceMenu() {if(experienceReady)experiencePanel.Close();}
void ToggleExperienceMenu() {
    if(ExperienceInputBlocked() || experienceCloseDialogOpen || experienceColorDialogOpen || experienceColorRequestPending)return;
    LeaveMarkdownEditor();
    if(!experienceReady)CreateExperienceUI();
    if(!experienceReady)return;
    if(experienceFolded){ExpandExperienceNote();return;}
    // The settings HWND and its DWM/layered canvas exist only after an explicit
    // request to open settings, never as an unpositioned startup placeholder.
    if(!experiencePanel.Window() && !experiencePanel.Create(g_instance,ExperienceAction,[]{SyncExperienceUI();})) {
        MessageBoxW(experienceIsland.Window(),L"暂时无法打开便签控制，请重试。",L"FloatNote",MB_OK|MB_ICONINFORMATION);
        return;
    }
    if(experiencePanel.Visible()){experiencePanel.Close();return;}
    if(!g_isVisible){g_isVisible=true;ShowWindow(g_window,SW_SHOWNOACTIVATE);ApplyInteractionMode();}
    SyncExperienceUI();RECT note{};GetWindowRect(g_window,&note);
    experiencePanel.Show(experienceIsland.Window(),note,GetDpiForWindow(g_window),ExperienceState(),experienceIsland.CloseWindow());
    SyncExperienceUI();
}
void ExperienceAction(GlassControlPopover::Action action,int value) {
    LeaveMarkdownEditor();
    using A=GlassControlPopover::Action;
    switch(action){
    case A::ToggleTopmost:SetTopmost(!ExperienceState().pinned);break;
    case A::TogglePassThrough:SetPassThrough(!g_settings.passThrough);break;
    case A::MaterialLiquid:case A::MaterialAcrylic:case A::MaterialSolid:{
        const int mode=action==A::MaterialLiquid?2:(action==A::MaterialAcrylic?0:-1);
        if(mode<0 && g_backdrop.mode>=0){experienceGlassOpacity=g_settings.opacityPercent;SaveExperienceRestoreSize();SetOpacityPercent(100);}
        else if(mode>=0 && g_backdrop.mode<0)SetOpacityPercent(experienceGlassOpacity);
        SelectLabMaterial(mode);break;
    }
    case A::BackgroundClear:SetOpacityPercent(0);break;
    case A::BackgroundPreset:
        g_settings.themeColor=experienceBackgroundColors[std::clamp(value,0,4)];
        if(g_settings.opacityPercent==0)g_settings.opacityPercent=14;
        RefreshTheme();ApplyVisuals(g_window);SaveSettings();break;
    case A::TextAutomatic:g_settings.autoTextColor=true;RefreshTheme();SaveSettings();break;
    case A::TextPreset:{
        g_settings.textColor=experienceTextColors[std::clamp(value,0,4)];g_settings.autoTextColor=false;RefreshTheme();SaveSettings();break;
    }
    case A::Opacity:SetOpacityPercent(value,false);break;
    case A::Blur:
        g_backdrop.blur=std::clamp(value,0,80)*.25f;g_backdrop.Changed();SetTimer(g_window,74,220,nullptr);break;
    case A::AddBackgroundColor:case A::AddTextColor:
        if(!experienceColorDialogOpen && !experienceColorRequestPending) {
            experienceColorRequestPending=true;
            experiencePanel.SetModalOpen(true);
            if(!PostMessageW(g_window,kExperienceChooseColor,action==A::AddBackgroundColor,0)) {
                experienceColorRequestPending=false;experiencePanel.SetModalOpen(false);
            }
        }
        break;
    case A::SmallerText:ChangeFontSize(-1);break;
    case A::LargerText:ChangeFontSize(1);break;
    case A::OpenLab:
        experiencePanel.Close(false);OpenGlassLabControls();
        if(labControls)SetForegroundWindow(labControls);break;
    case A::HideNote:experiencePanel.Close(false);ToggleVisibility();break;
    case A::ExitApp:PostMessageW(g_window,WM_CLOSE,0,0);break;
    case A::CloseBehavior:SetExperienceCloseBehavior(GlassClose::FromStored(value));break;
    case A::Close:experiencePanel.Close();break;
    }
    ApplyWindowStacking();SyncExperienceUI();RefreshLabControls();
}
bool HandleExperienceKey(const MSG& message) {
    return experienceReady && experiencePanel.HandleKey(message);
}
void CreateExperienceUI() {
    if(experienceReady || !g_window)return;
    const auto path=g_dataDirectory/L"experience.ini";
    experienceNormalWidth=std::clamp(int(GetPrivateProfileIntW(L"Experience",L"normalWidth",320,path.c_str())),kMinimumNoteWidth,4000);
    experienceNormalHeight=std::clamp(int(GetPrivateProfileIntW(L"Experience",L"normalHeight",200,path.c_str())),60,3000);
    experienceNormalCorner=std::clamp(int(GetPrivateProfileIntW(L"Experience",L"normalCorner",48,path.c_str())),8,4096);
    experienceGlassOpacity=g_backdrop.mode>=0?g_settings.opacityPercent:
        std::clamp(int(GetPrivateProfileIntW(L"Experience",L"glassOpacity",0,path.c_str())),0,100);
    experienceCloseBehavior=GlassClose::FromStored(int(GetPrivateProfileIntW(L"Experience",L"closeBehavior",0,path.c_str())));
    experienceFolded=GetPrivateProfileIntW(L"Experience",L"folded",0,path.c_str())==1;
    for(size_t i=0;i<experienceBackgroundColors.size();++i) {
        const auto bgKey=L"backgroundColor"+std::to_wstring(i),textKey=L"textColor"+std::to_wstring(i);
        const UINT bg=GetPrivateProfileIntW(L"Experience",bgKey.c_str(),experienceBackgroundColors[i],path.c_str());
        const UINT fg=GetPrivateProfileIntW(L"Experience",textKey.c_str(),experienceTextColors[i],path.c_str());
        if(bg<=0xffffff)experienceBackgroundColors[i]=bg;
        if(fg<=0xffffff)experienceTextColors[i]=fg;
    }
    experienceIsland.Create(g_instance,[]{ToggleExperienceMenu();},[](int,int){
        if(experienceDragging){experienceDragging=false;RecoverWindowPosition();SaveSettings();SyncExperienceUI();}
    },[](int dx,int dy){
        if(!experienceDragging){LeaveMarkdownEditor();GetWindowRect(g_window,&experienceDragOrigin);experienceDragging=true;CloseExperienceMenu();}
        SetWindowPos(g_window,nullptr,experienceDragOrigin.left+dx,experienceDragOrigin.top+dy,0,0,
            SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    },[]{PostMessageW(g_window,kExperienceCloseRequest,0,0);});
    experienceReady=true;
    if(experienceFolded){ShowWindow(g_window,SW_HIDE);g_backdrop.Enable(g_window,false);}
    SetTimer(g_window,73,250,nullptr);SetTimer(g_window,76,32,nullptr);
    ApplyWindowStacking(true);SyncExperienceUI();
}
void DestroyExperienceUI() {
    KillTimer(g_window,73);KillTimer(g_window,74);KillTimer(g_window,75);KillTimer(g_window,76);
    // Restore persistence is deferred off the click path; closing mid-motion
    // must still save the intended final state before controls are destroyed.
    if(experienceReady)SaveExperienceRestoreSize();
    SaveExperienceMaterial();experienceReady=false;
    // The popup borrows the note's dispatcher queue; destroy it first.
    experiencePanel.Destroy();experienceIsland.Destroy();
}
}
