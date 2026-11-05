#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace GlassAutoInk {
constexpr COLORREF Dark=RGB(30,32,36),Light=RGB(248,249,250);
inline double Luminance(COLORREF colour) {
    static const auto linear=[] {
        std::array<double,256> table{};
        for(int i=0;i<256;i++){double c=i/255.;table[i]=c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}
        return table;
    }();
    return .2126*linear[GetRValue(colour)]+.7152*linear[GetGValue(colour)]+.0722*linear[GetBValue(colour)];
}
inline bool PrefersWhite(COLORREF colour) {
    static const double boundary=std::sqrt((Luminance(Dark)+.05)*(Luminance(Light)+.05))-.05;
    return Luminance(colour)<boundary;
}
// Vote per area sample AFTER the actual overlay's byte-space alpha blend.
// Averaging colours first would incorrectly turn a black/white split into grey.
inline double WhiteShare(const void* data,unsigned pitch,int width,int height,COLORREF tint,int alpha) {
    unsigned white=0;
    alpha=std::clamp(alpha,0,255);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
        const auto p=static_cast<const unsigned char*>(data)+size_t(y)*pitch+x*4;
        auto blend=[alpha](int b,int t){return (b*(255-alpha)+t*alpha+127)/255;};
        white+=PrefersWhite(RGB(blend(p[2],GetRValue(tint)),blend(p[1],GetGValue(tint)),blend(p[0],GetBValue(tint))));
    }
    return width>0 && height>0 ? double(white)/(double(width)*height) : .5;
}
// Sample the composed note without changing its capture affinity. This includes
// the system backdrop and tint as they are actually presented on the desktop.
inline bool ScreenWhiteShare(RECT rect,double& share) {
    if(rect.right<=rect.left || rect.bottom<=rect.top)return false;
    HDC screen=GetDC(nullptr);if(!screen)return false;
    HDC memory=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HBITMAP bitmap=memory?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
    bool ok=false;
    if(bitmap && pixels) {
        HGDIOBJ old=SelectObject(memory,bitmap);
        SetStretchBltMode(memory,COLORONCOLOR);
        ok=StretchBlt(memory,0,0,32,16,screen,rect.left,rect.top,
                      rect.right-rect.left,rect.bottom-rect.top,SRCCOPY|CAPTUREBLT)!=FALSE;
        if(ok){GdiFlush();share=WhiteShare(pixels,32*4,32,16,0,0);}
        SelectObject(memory,old);
    }
    if(bitmap)DeleteObject(bitmap);
    if(memory)DeleteDC(memory);
    ReleaseDC(nullptr,screen);
    return ok;
}
struct Decision {
    bool white=false,valid=false,pending=false,candidate=false;
    double candidateAt=0;
    void Reset(bool fallbackWhite=false){*this={};white=fallbackWhite;}
    void Update(double share,double now) {
        if(!valid){if(share>.55)white=true;else if(share<.45)white=false;valid=true;return;}
        bool next=share>.55?true:share<.45?false:white;
        if(next==white){pending=false;return;}
        if(share>=.85 || share<=.15){white=next;pending=false;return;}
        if(!pending || candidate!=next){pending=true;candidate=next;candidateAt=now;return;}
        if(now-candidateAt>=90){white=next;pending=false;}
    }
};
struct Fade {
    static constexpr double duration=180;
    COLORREF from=Dark,target=Dark;
    double started=0;
    bool initialized=false;
    void Reset(COLORREF colour){from=target=colour;initialized=true;started=0;}
    bool Running(double now) const {return from!=target && now-started<duration;}
    COLORREF Sample(double now) const {
        double t=std::clamp((now-started)/duration,0.,1.);
        t=t*t*(3-2*t); // smooth departure and arrival, no long grey plateau
        auto channel=[t](int a,int b){return int(std::lround(a+(b-a)*t));};
        return RGB(channel(GetRValue(from),GetRValue(target)),channel(GetGValue(from),GetGValue(target)),channel(GetBValue(from),GetBValue(target)));
    }
    void Aim(COLORREF colour,COLORREF current,double now) {
        if(!initialized)Reset(current);
        if(colour==target)return;
        from=Sample(now);target=colour;started=now;
    }
};
}
