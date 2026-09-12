#pragma once
#include "glass_auto_ink.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>

namespace GlassControlAppearance {
// Owned by one thread. Retain DIB/DC allocations across animation frames and
// background samples; no screen image is written to disk.
struct Raster {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;
    void* pixels=nullptr;int width=0,height=0;unsigned allocations=0;
    Raster()=default;Raster(const Raster&)=delete;Raster& operator=(const Raster&)=delete;
    ~Raster(){Clear();}
    void Clear(){if(dc && old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);dc=nullptr;bitmap=nullptr;old=nullptr;pixels=nullptr;width=height=0;}
    bool Ensure(int w,int h) {
        if(w==width && h==height && pixels)return true;
        if(w<=0 || h<=0 || w>2048 || h>1024)return false;
        Clear();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);
        if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!dc || !bitmap || !pixels){Clear();return false;}
        old=SelectObject(dc,bitmap);width=w;height=h;++allocations;return true;
    }
};
struct Request {
    RECT surface{},note{};int padding=6,guard=2,radius=0,alpha=0;COLORREF tint=0;
    bool enabled=false;
};
inline bool Same(const Request& a,const Request& b) {
    return EqualRect(&a.surface,&b.surface) && EqualRect(&a.note,&b.note) && a.padding==b.padding &&
        a.guard==b.guard && a.radius==b.radius && a.alpha==b.alpha && a.tint==b.tint && a.enabled==b.enabled;
}
inline bool Inside(RECT r,int x,int y){return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
inline bool InsideNote(const Request& r,int x,int y) {
    if(!Inside(r.note,x,y))return false;
    float cx=(r.note.left+r.note.right)*.5f,cy=(r.note.top+r.note.bottom)*.5f;
    float radius=float(std::min({r.radius,int(r.note.right-r.note.left)/2,int(r.note.bottom-r.note.top)/2}));
    float qx=std::max(std::abs(x+.5f-cx)-(r.note.right-r.note.left)*.5f+radius,0.f);
    float qy=std::max(std::abs(y+.5f-cy)-(r.note.bottom-r.note.top)*.5f+radius,0.f);
    return qx*qx+qy*qy<=radius*radius;
}
inline bool Vote(const Raster& raster,RECT crop,const Request& request,double& darkShare) {
    RECT excluded=request.surface;InflateRect(&excluded,request.guard,request.guard);
    int dark=0,total=0;
    const int step=std::max(1,int(std::sqrt(double(raster.width)*raster.height/512)));
    for(int y=step/2;y<raster.height;y+=step)for(int x=step/2;x<raster.width;x+=step) {
        const int sx=crop.left+x,sy=crop.top+y;
        if(Inside(excluded,sx,sy))continue;
        const auto p=static_cast<const BYTE*>(raster.pixels)+(size_t(y)*raster.width+x)*4;
        int red=p[2],green=p[1],blue=p[0];
        // Capture-excluded notes may be absent from BitBlt. Restore only their
        // known overlay tint for perimeter samples inside the rounded note.
        if(request.alpha>0 && InsideNote(request,sx,sy)) {
            auto mix=[&](int a,int b){return (a*(255-request.alpha)+b*request.alpha+127)/255;};
            red=mix(red,GetRValue(request.tint));green=mix(green,GetGValue(request.tint));blue=mix(blue,GetBValue(request.tint));
        }
        dark+=GlassAutoInk::PrefersWhite(RGB(red,green,blue));++total;
    }
    if(total<8)return false;
    darkShare=double(dark)/total;return true;
}
inline bool Capture(Raster& raster,const Request& request,double& darkShare) {
    RECT crop=request.surface;InflateRect(&crop,request.padding,request.padding);
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromRect(&request.surface,MONITOR_DEFAULTTONEAREST),&monitor) ||
       !IntersectRect(&crop,&crop,&monitor.rcMonitor) || !raster.Ensure(crop.right-crop.left,crop.bottom-crop.top))return false;
    HDC screen=GetDC(nullptr);if(!screen)return false;
    const bool copied=BitBlt(raster.dc,0,0,raster.width,raster.height,screen,crop.left,crop.top,SRCCOPY|CAPTUREBLT)!=FALSE;
    if(copied)GdiFlush();ReleaseDC(nullptr,screen);
    return copied && Vote(raster,crop,request,darkShare);
}
class Sampler {
    std::function<bool(Raster&,const Request&,double&)> capture_;
    std::mutex mutex_;std::condition_variable wake_;std::thread worker_;
    Request request_{};bool stop_=false;unsigned generation_=0,sequence_=0;
    double share_=.5,elapsedMs_=0;unsigned resultGeneration_=0;
    void Run() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Raster raster;auto next=std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);
        while(!stop_) {
            wake_.wait(lock,[&]{return stop_ || request_.enabled;});if(stop_)break;
            if(wake_.wait_until(lock,next,[&]{return stop_ || !request_.enabled;}))continue;
            const Request request=request_;const unsigned generation=generation_;
            lock.unlock();double share=0;const auto began=std::chrono::steady_clock::now();
            bool ok=capture_(raster,request,share);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            lock.lock();
            if(ok && generation==generation_ && request_.enabled){share_=share;elapsedMs_=ms;resultGeneration_=generation;++sequence_;}
            // Expensive/unsupported desktop capture backs off instead of
            // spending the normal 4 Hz budget or blocking the UI thread.
            next=std::chrono::steady_clock::now()+std::chrono::milliseconds(ok && ms<8?250:1000);
        }
    }
public:
    explicit Sampler(std::function<bool(Raster&,const Request&,double&)> capture=Capture):capture_(std::move(capture)){}
    ~Sampler(){Stop();}
    void Submit(const Request& request) {
        std::lock_guard lock(mutex_);
        if(Same(request,request_))return;
        request_=request;++generation_;
        if(request.enabled && !worker_.joinable()){stop_=false;worker_=std::thread([this]{Run();});}
        wake_.notify_one();
    }
    bool Read(unsigned& seen,double& share,double* elapsed=nullptr) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || sequence_==seen || resultGeneration_!=generation_)return false;
        seen=sequence_;share=share_;if(elapsed)*elapsed=elapsedMs_;return true;
    }
    void Stop() {
        {std::lock_guard lock(mutex_);stop_=true;wake_.notify_one();}
        if(worker_.joinable())worker_.join();
        std::lock_guard lock(mutex_);request_={};++generation_;
    }
};
}
