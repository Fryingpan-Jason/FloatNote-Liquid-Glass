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
}
