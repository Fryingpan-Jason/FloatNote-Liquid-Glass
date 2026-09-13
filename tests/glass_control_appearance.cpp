#include "../src/glass_control_appearance.h"
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <utility>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#define private public
#include "../experiments/glass_control_island.h"
#undef private

using namespace GlassControlAppearance;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static double Clock(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void Save(const char* name,const Raster& image) {
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+image.width*image.height*4;
    info.biSize=sizeof(info);info.biWidth=image.width;info.biHeight=-image.height;info.biPlanes=1;info.biBitCount=32;
    std::ofstream out(name,std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));
    const auto pixels=static_cast<const DWORD*>(image.pixels);
    for(int i=0;i<image.width*image.height;i++) {
        const DWORD p=pixels[i];const unsigned a=p>>24;
        BYTE b[4]={BYTE((p&255)+(255-a)*128/255),BYTE(((p>>8)&255)+(255-a)*128/255),BYTE(((p>>16)&255)+(255-a)*128/255),255};
        out.write(reinterpret_cast<char*>(b),4);
    }
}
static void Run() {
    Raster raster;Check(raster.Ensure(100,40),"raster allocation");const auto allocations=raster.allocations;
    for(int i=0;i<100;i++)Check(raster.Ensure(100,40),"reuse failed");Check(raster.allocations==allocations,"raster reallocated per frame");
    Request request;request.surface={10,10,90,30};request.guard=2;
    auto fill=[&](bool bright,bool body){auto p=static_cast<DWORD*>(raster.pixels);for(int y=0;y<40;y++)for(int x=0;x<100;x++)p[y*100+x]=Inside(request.surface,x,y)?(body?0xffffff:0):(bright?0xffffff:0);};
    double share=0;fill(true,false);Check(Vote(raster,{0,0,100,40},request,share)&&share==0,"dark control polluted bright surroundings");
    fill(false,true);Check(Vote(raster,{0,0,100,40},request,share)&&share==1,"white control polluted dark surroundings");
    request.note={0,0,100,40};request.alpha=255;request.tint=RGB(255,255,255);
    Check(Vote(raster,{0,0,100,40},request,share)&&share==0,"known note tint not restored");
    request.alpha=0;request.enabled=true;
    std::atomic<int> calls=0;std::atomic<bool> busy=false;
    Sampler sampler([&](Raster&,const Request&,double& result){busy=true;++calls;Sleep(30);result=.9;busy=false;return true;});
    sampler.Submit(request);
    const double deadline=Clock()+1000;while(!busy && Clock()<deadline)Sleep(1);Check(busy,"worker did not start");
    const double began=Clock();for(int i=0;i<100;i++)sampler.Submit(request);Check(Clock()-began<10,"UI waits for screen capture");
    request.enabled=false;sampler.Submit(request);Sleep(50);unsigned seen=0;
    Check(!sampler.Read(seen,share),"disabled request published stale result");
    sampler.Stop();request.enabled=true;sampler.Submit(request);Sleep(80);
    Check(sampler.Read(seen,share)&&share==.9,"sampler did not restart");
    sampler.Stop();Check(calls<=2,"slow capture did not back off");
    std::atomic<int> fastCalls=0;
    Sampler cadence([&](Raster&,const Request&,double& result){++fastCalls;result=0;return true;});
    cadence.Submit(request);Sleep(560);cadence.Stop();Check(fastCalls>=2&&fastCalls<=3,"4 Hz cadence exceeded");

    GlassControlIsland::Controller control;
    Check(control.Create(GetModuleHandleW(nullptr),[]{},[](int,int){},[](int,int){},[]{}),"hidden controls creation");
    control.bounds_={0,0,180,90};control.noteRect_={-260,70,440,370};control.dpi_=192;control.visible_=true;
    control.insideCenterY_=70;control.outsideCenterY_=30;control.foldedCenterY_=70;
    control.expanded_=control.closeVisible_=true;control.expansion_.value=1;
    control.highContrast_=false;control.closeTint_=1;
    for(int light=0;light<2;light++) {
        control.lightAppearance_=float(light);control.Render();
        const auto& close=control.rasters_[1];auto p=static_cast<const DWORD*>(close.pixels);
        unsigned cross=0,red=0;
        const DWORD ink=light?0:0xffffff;
        for(int i=0;i<close.width*close.height;i++)if((p[i]>>24)==255){cross+=(p[i]&0xffffff)==ink;red+=(p[i]&0xffffff)==0xdc3441;}
        Check(cross>8&&red>100,"red hover changed X colour or lost red background");
        Save(light?"build/control-appearance-tests/close-light.bmp":"build/control-appearance-tests/close-dark.bmp",close);
        Save(light?"build/control-appearance-tests/settings-light.bmp":"build/control-appearance-tests/settings-dark.bmp",control.rasters_[0]);
    }
    control.closeTint_=0;control.Render();const unsigned before=control.rasters_[0].allocations+control.rasters_[1].allocations;
    for(int i=0;i<60;i++){control.settingsTint_=i/60.f;control.Render();}
    Check(before==control.rasters_[0].allocations+control.rasters_[1].allocations,"control hover reallocates buffers");
    control.expansion_.value=0;control.expanded_=control.closeVisible_=false;control.Render();
    Save("build/control-appearance-tests/hint-light.bmp",control.rasters_[0]);
    control.lightAppearance_=0;control.Render();Save("build/control-appearance-tests/hint-dark.bmp",control.rasters_[0]);
    control.Destroy();
    std::puts("PASS perimeter excludes self, tint correction, async nonblocking submission, stop/restart, stale result rejection, 4 Hz/backoff, both X colours on red, cached control rasters.");
}
static void Benchmark() {
    MONITORINFO monitor{sizeof(monitor)};POINT origin{};Check(GetMonitorInfoW(MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY),&monitor),"monitor");
    const int x=(monitor.rcMonitor.left+monitor.rcMonitor.right)/2,y=(monitor.rcMonitor.top+monitor.rcMonitor.bottom)/2;
    Request request;request.surface={x-72,y-5,x+72,y+5};request.padding=12;request.enabled=true;
    Raster raster;std::array<double,8> samples{};
    for(auto& ms:samples){double share=0;double began=Clock();Check(Capture(raster,request,share),"regional capture failed");ms=Clock()-began;Sleep(250);}
    std::sort(samples.begin(),samples.end());
    std::printf("Regional capture %dx%d, 8 reads: median %.3f ms, max %.3f ms (worker wall time); one cached DIB allocation; no images saved.\n",raster.width,raster.height,samples[4],samples.back());
}
int main(int argc,char** argv){try{if(argc>1&&std::string(argv[1])=="--benchmark")Benchmark();else Run();return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
