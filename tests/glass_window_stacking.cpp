#include <windows.h>
#include "../src/glass_window_stacking.h"
#include <vector>
#include <stdexcept>
#include <cstdio>

static void Check(bool valid,const char* message) {if(!valid)throw std::runtime_error(message);}
struct HiddenWindows {
    std::vector<HWND> windows;
    HWND Add(bool pinned) {
        HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,
            0,0,8,8,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(w!=nullptr,"Fixture creation failed");windows.push_back(w);
        Check(SetWindowPos(w,pinned?HWND_TOPMOST:HWND_TOP,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,"Fixture ordering failed");
        return w;
    }
    ~HiddenWindows(){for(auto w:windows)DestroyWindow(w);}
};
int main() {try {
    const HWND foreground=GetForegroundWindow();
    for(bool pinned:{false,true}) {
        HiddenWindows fixture;
        HWND control=fixture.Add(pinned);
        for(int i=0;i<160;++i)fixture.Add(pinned);
        HWND note=fixture.Add(pinned);
        HWND covering=fixture.Add(pinned);
        unsigned distance=0;
        for(HWND w=GetWindow(control,GW_HWNDPREV);w && w!=note;w=GetWindow(w,GW_HWNDPREV))++distance;
        Check(distance>=160,"Fixture does not reproduce the old traversal limit");
        Check(GlassWindowStacking::PlaceAbove(control,note,pinned),"Control placement failed");
        Check(GetWindow(note,GW_HWNDPREV)==control,"Control is still behind the note");
        Check(GetWindow(control,GW_HWNDPREV)==covering,"Existing covering window was overtaken");
        HWND close=fixture.Add(pinned);
        Check(GlassWindowStacking::PlaceAbove(close,control,pinned),"Close placement failed");
        for(int i=0;i<20;++i) {
            GlassWindowStacking::PlaceAbove(control,note,pinned);
            GlassWindowStacking::PlaceAbove(close,control,pinned);
            Check(GetWindow(note,GW_HWNDPREV)==control && GetWindow(control,GW_HWNDPREV)==close,
                "Repeated synchronization changes button order");
            Check(GetWindow(close,GW_HWNDPREV)==covering,"Synchronization overtakes another window");
        }
        Check(((GetWindowLongPtrW(control,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)==pinned,
            "Control changed topmost band");
        for(HWND w:fixture.windows)Check(!IsWindowVisible(w),"Hidden fixture became visible");
    }
    Check(GetForegroundWindow()==foreground,"Test changed foreground focus");
    std::puts("Stacking: 160 intervening windows, pinned/unpinned, covering-window preservation and repeated synchronization passed.");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
