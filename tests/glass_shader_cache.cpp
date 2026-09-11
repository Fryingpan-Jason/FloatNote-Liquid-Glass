#include <windows.h>
#include <dwmapi.h>
#include "../src/liquid_backdrop.h"
#include <cstdio>

int main() {
    const double coldStart=GlassClockMs();
    if(FAILED(GlassLabBackdrop::WarmShaderBytecode()))return 1;
    const double coldMs=GlassClockMs()-coldStart;
    const unsigned compiled=GlassLabBackdrop::ShaderCompilations();
    if(compiled!=1)return 2;
    const double warmStart=GlassClockMs();
    for(int i=0;i<100;++i)
        if(FAILED(GlassLabBackdrop::WarmShaderBytecode()))return 3;
    const double warmMs=GlassClockMs()-warmStart;
    if(GlassLabBackdrop::ShaderCompilations()!=compiled)return 4;
    // Device/capture lifecycle must not own or invalidate compiled HLSL.
    { GlassLabBackdrop backdrop; backdrop.SetPresentationMotion(true); backdrop.Tick(); backdrop.Close(); }
    if(FAILED(GlassLabBackdrop::WarmShaderBytecode()) || GlassLabBackdrop::ShaderCompilations()!=compiled)return 5;
    std::printf("Actual material shaders: cold %.2f ms; 100 cache hits %.4f ms; one compilation across renderer lifetimes.\n",coldMs,warmMs);
    return 0;
}
