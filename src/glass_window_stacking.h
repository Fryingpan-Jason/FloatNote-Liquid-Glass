#pragma once
#include <windows.h>

namespace GlassWindowStacking {
// Place an independent control immediately above its anchor without walking
// the desktop's window list. Windows above the anchor stay above the control.
inline bool PlaceAbove(HWND control, HWND anchor, bool topmost) {
    if(!IsWindow(control) || !IsWindow(anchor) || control==anchor)return false;
    HWND previous=GetWindow(anchor,GW_HWNDPREV);
    if(previous==control)return true;
    if(!previous || (!topmost && (GetWindowLongPtrW(previous,GWL_EXSTYLE)&WS_EX_TOPMOST)))
        previous=HWND_TOP;
    return SetWindowPos(control,previous,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER)!=FALSE;
}
}
