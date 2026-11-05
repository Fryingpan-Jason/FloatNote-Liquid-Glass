// Interactive compositor fixture; contains no user data.
#include "../experiments/local_desktop.cpp"
#include <cstdio>
#include <stdexcept>

static COLORREF fixtureColor=RGB(0,0,0);
static LRESULT CALLBACK InkFixture(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
        HBRUSH brush=CreateSolidBrush(fixtureColor);FillRect(dc,&paint.rcPaint,brush);DeleteObject(brush);
        EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Paint(COLORREF colour) {
    fixtureColor=colour;InvalidateRect(g_window,nullptr,TRUE);UpdateWindow(g_window);DwmFlush();Sleep(80);
}
static void Settle() {SyncAdaptiveInk();Sleep(230);SyncAdaptiveInk();Sleep(230);SyncAdaptiveInk();}
int main() {
    try {
        g_instance=GetModuleHandleW(nullptr);
        WNDCLASSW wc{};wc.lpfnWndProc=InkFixture;wc.hInstance=g_instance;wc.lpszClassName=L"FloatNote.InkFixture";
        Check(RegisterClassW(&wc)!=0,"fixture class registration failed");
        g_window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,
                                 30,30,240,120,nullptr,nullptr,g_instance,nullptr);
        Check(g_window!=nullptr,"fixture window creation failed");
        ShowWindow(g_window,SW_SHOWNOACTIVATE);
        g_autoInkRegion={10,10,200,100};g_settings.autoTextColor=true;g_settings.opacityPercent=50;
        g_highContrast=false;g_nativeGlass=false;g_isVisible=true;kText=kNoteText=GlassAutoInk::Dark;
        for(int mode : {-1,0}) {
            g_backdrop.mode=mode;Paint(RGB(0,0,0));Settle();
            Check(kNoteText==GlassAutoInk::Light,"dark composed background did not select light ink");
            Paint(RGB(255,255,255));Sleep(210);Settle();
            Check(kNoteText==GlassAutoInk::Dark,"light composed background did not select dark ink");
            DWORD affinity=99;Check(GetWindowDisplayAffinity(g_window,&affinity) && affinity==WDA_NONE,
                                    "screen sampler changed window capture affinity");
        }
        double share=0;Check(!GlassAutoInk::ScreenWhiteShare({0,0,0,0},share),"empty sample should fail");
        g_settings.autoTextColor=false;g_settings.textColor=RGB(23,45,67);SyncAdaptiveInk();
        Check(kNoteText==g_settings.textColor,"manual ink was overridden");
        KillTimer(g_window,82);KillTimer(g_window,83);DestroyWindow(g_window);g_window=nullptr;
        std::puts("PASS screen sampling for solid and system backdrop, capture visibility, manual ink, invalid region.");
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
