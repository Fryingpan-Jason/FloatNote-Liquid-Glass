// Hidden native editor checks; no capture, tray, hooks, or user files.
#include "../experiments/local_desktop.cpp"
#include <cstdio>
#include <stdexcept>

struct GlassAutoInkTest {
    static void Seed() {
        g_backdrop.mode=2;g_backdrop.enabled=g_backdrop.haveDesktop=g_backdrop.inkFrameValid=true;
        g_backdrop.inkEnabled=true;g_backdrop.inkTint=kBackground;
        g_backdrop.inkAlpha=MulDiv(EffectiveOpacityPercent(),255,100);
        g_backdrop.inkDirty=g_backdrop.inkPending=false;g_backdrop.inkLastShare=1;
        g_backdrop.inkDecision.Reset(true);g_backdrop.inkDecision.valid=true;
    }
    static void Check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
    static void Run() {
        g_instance=GetModuleHandleW(nullptr);
        g_window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Hidden ink test",WS_POPUP,0,0,320,160,nullptr,nullptr,g_instance,nullptr);
        g_edit=CreateWindowExW(0,L"EDIT",L"Editable text",WS_CHILD|ES_MULTILINE,0,0,200,100,g_window,nullptr,g_instance,nullptr);
        Check(g_window&&g_edit,"hidden editor creation failed");
        g_settings.autoTextColor=true;g_settings.opacityPercent=50;
        g_highContrast=false;g_nativeGlass=g_glassActive=g_isVisible=true;
        kBackground=RGB(255,255,255);kText=kNoteText=GlassAutoInk::Dark;
        SendMessageW(g_edit,EM_SETSEL,0,1);SendMessageW(g_edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"X"));
        DWORD beforeStart=0,beforeEnd=0;SendMessageW(g_edit,EM_GETSEL,reinterpret_cast<WPARAM>(&beforeStart),reinterpret_cast<LPARAM>(&beforeEnd));
        Seed();SyncAdaptiveInk();Check(kNoteText==GlassAutoInk::Light,"Auto did not apply sampled white ink");
        Check(kText==GlassAutoInk::Dark,"Auto recoloured settings UI");
        DWORD afterStart=0,afterEnd=0;SendMessageW(g_edit,EM_GETSEL,reinterpret_cast<WPARAM>(&afterStart),reinterpret_cast<LPARAM>(&afterEnd));
        Check(afterStart==beforeStart&&afterEnd==beforeEnd&&SendMessageW(g_edit,EM_CANUNDO,0,0),"ink update disturbed native selection/undo");
        g_settings.autoTextColor=false;g_settings.textColor=RGB(230,20,80);SyncAdaptiveInk();Check(kNoteText==g_settings.textColor,"manual ink overridden");
        g_settings.autoTextColor=true;g_settings.opacityPercent=100;Seed();SyncAdaptiveInk();Check(kNoteText==kText,"opaque tint did not use existing Auto");
        g_settings.opacityPercent=50;Seed();g_backdrop.mode=1;SyncAdaptiveInk();Check(kNoteText==kText,"frosted Auto changed");
        Seed();g_highContrast=true;kText=RGB(255,255,0);SyncAdaptiveInk();Check(kNoteText==kText,"high contrast overridden");
        g_highContrast=false;Seed();g_backdrop.failed=true;SyncAdaptiveInk();Check(kNoteText==kText,"renderer failure did not retain theme fallback");
        g_backdrop.enabled=false;KillTimer(g_window,82);DestroyWindow(g_window);g_window=g_edit=nullptr;
        std::puts("PASS native Auto, manual colour, opaque tint, frosted mode, high contrast, renderer fallback, separate settings colours, selection and undo.");
    }
};
int main(){try{GlassAutoInkTest::Run();return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
