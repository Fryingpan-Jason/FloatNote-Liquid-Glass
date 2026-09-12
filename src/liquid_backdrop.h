#pragma once
#include "backdrop.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include "monitor_backdrop_capture.h"
#include "glass_geometry.h"
#include "glass_sampling_clip.h"
#include "glass_lens.h"
#include "glass_bevel.h"
#include "glass_surface.h"
#include "glass_auto_ink.h"
#include <array>
#include <cmath>
#include <sstream>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Experimental, app-local renderer. No injected DLLs or DWM/driver modifications.
// The original Windows backdrop owns the visual and remains the failure fallback.
class GlassLabBackdrop {
    friend struct GlassAdaptationTest;
    friend struct GlassAutoInkTest;
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    NativeBackdrop system;
    Ptr<ABI::Windows::UI::Composition::ICompositionBrush> originalBrush;
    Ptr<ABI::Windows::UI::Composition::ICompositionSurface> surface;
    Ptr<ABI::Windows::UI::Composition::ICompositionSurfaceBrush> surfaceBrush;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> context;
    MonitorBackdropCapture windowCapture;
    Ptr<IDXGISwapChain1> swap;
    Ptr<ID3D11Texture2D> desktop;
    Ptr<ID3D11Texture2D> patch;
    Ptr<ID3D11ShaderResourceView> patchView;
    GlassSurface oldSurface;
    Ptr<ID3D11Texture2D> oldHeightTexture;
    Ptr<ID3D11ShaderResourceView> oldHeightView;
    std::array<Ptr<ID3D11Texture2D>, 2> blurred;
    std::array<Ptr<ID3D11ShaderResourceView>, 2> blurredViews;
    std::array<Ptr<ID3D11RenderTargetView>, 2> blurredTargets;
    std::array<Ptr<ID3D11Texture2D>,2> adaptation;
    std::array<Ptr<ID3D11ShaderResourceView>,2> adaptationViews;
    std::array<Ptr<ID3D11RenderTargetView>,2> adaptationTargets;
    int adaptationWidth=0,adaptationHeight=0,adaptationIndex=0;
    double adaptationAt=0,adaptationUntil=0;
    Ptr<ID3D11VertexShader> vertex;
    Ptr<ID3D11VertexShader> inkVertex;
    Ptr<ID3D11Texture2D> inkTarget,inkReadback;
    Ptr<ID3D11RenderTargetView> inkTargetView;
    GlassAutoInk::Decision inkDecision;
    RECT inkRegion{};
    bool inkEnabled=false,inkPending=false,inkDirty=false,inkFrameValid=false,inkUnavailable=false;
    unsigned inkGeneration=0,inkPendingGeneration=0;
    COLORREF inkTint=0;
    int inkAlpha=0;
    double inkSubmittedAt=0;
    double inkLastShare=.5;
    unsigned inkSubmissions=0,inkPollMisses=0;
    static constexpr int inkWidth=32,inkHeight=16;
    Ptr<ID3D11PixelShader> blurShader, glassShader, adaptationShader;
    Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11SamplerState> sampler;
    HWND hwnd = nullptr;
    HMONITOR monitor = nullptr;
    RECT outputRect{}, lastRect{}, valid{};
    std::array<RECT,2> controlOccluders{}, previousControlOccluders{};
    ULONGLONG previousControlOccludersUntil=0;
    int width = 0, height = 0;
    // Real pixels outside the card are now available from the complete monitor.
    // Keep a halo for wide-angle lens/environment samples plus the blur kernel.
    static constexpr int padding = 512;
    bool enabled = false, haveDesktop = false, needRender = true;
    bool frozen = false, failed = false, presentationMotion = false;
    unsigned gpuInitializations=0, bufferResizes=0;
    ULONGLONG retryAt = 0;
    unsigned long long frames = 0, renders = 0, copies = 0;
    std::wstring adapterName;
    std::wstring damageDescription;
    HRESULT lastError = S_OK;
    std::shared_ptr<CaptureSignal> signal;
    CaptureWindowEvents windowEvents;
    using AccessStatus=winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
    winrt::Windows::Foundation::IAsyncOperation<AccessStatus> accessOperation{nullptr};
    bool accessRequested=false;
    std::array<double,512> sourceAges{},eventDelays{},submitTimes{};
    size_t ageCount=0,eventCount=0,submitCount=0;
    static double P95(const std::array<double,512>& values,size_t count) {
        count=std::min(count,values.size()); if(!count) return 0;
        std::vector<double> sorted(values.begin(),values.begin()+count); std::sort(sorted.begin(),sorted.end());
        return sorted[std::min(count-1,size_t(std::ceil(count*.95)-1))];
    }
    void RequestBorderless() {
        if(accessRequested) return;
        accessRequested=true;
        try {
            accessOperation=winrt::Windows::Graphics::Capture::GraphicsCaptureAccess::RequestAccessAsync(
                winrt::Windows::Graphics::Capture::GraphicsCaptureAccessKind::Borderless);
            auto state=signal;
            accessOperation.Completed([state](auto const& op,winrt::Windows::Foundation::AsyncStatus status) {
                try { state->borderAccess.store(status==winrt::Windows::Foundation::AsyncStatus::Completed ? int(op.GetResults()) : -2); }
                catch(...) {state->borderAccess.store(-2);}
                state->Notify();
            });
        } catch(...) {signal->borderAccess.store(-2);}
    }
    struct Constants {
        float dimensions[4]; // patch width/height, window width/height
        float material[4]; // sigma, edge bend, corner radius, glass mode
        float direction[4]; // blur x/y, padding, sharp mixture
        float bounds[4]; // valid patch pixel bounds
        float optics[4]; // edge width, fine highlight, DPI scale, appearance enabled
        float response[4]; // dispersion, adaptive contrast, optical model, profile
        float surface[4]; // base optical depth, relief height, reflection strength, rim width
        float field[4]; // old Poisson field grid and cell spacing
        float finish[4]; // roughness, environment colour, adaptation blend, history valid
        float controlMasks[4][4]; // current/previous visible control bounds in patch pixels
        float inkProbe[4]; // normalized text region, used only by the tiny ink probe
    };
    static_assert(sizeof(Constants)%16==0);
    Constants inkFrame{};
    inline static const std::string shaderSource = std::string(GlassLens::Shader) + GlassBevel::Shader + R"HLSL(
cbuffer Params : register(b0) {
    float4 dims; float4 material; float4 direction; float4 bounds; float4 optics;
    float4 response; float4 surface; float4 field; float4 finish;
    float4 controlMasks[4];
    float4 inkProbe;
};
Texture2D source0 : register(t0);
Texture2D source1 : register(t1);
Texture2D<float4> oldHeightField : register(t2);
Texture2D<float4> adaptationField : register(t3);
SamplerState linearClamp : register(s0);
struct V { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
V VS(uint id:SV_VertexID) {
    V o; o.uv=float2((id<<1)&2,id&2);
    o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float2 bounded(float2 pixel) { return clamp(pixel,bounds.xy,bounds.zw)/dims.xy; }
float4 rawControlFree(float2 pixel,bool sharp) {
    float2 p=clamp(pixel,bounds.xy,bounds.zw);
    float4 original=sharp ? source1.SampleLevel(linearClamp,p/dims.xy,0)
                          : source0.SampleLevel(linearClamp,p/dims.xy,0);
    float low=0,high=0; bool covered=false;
    [unroll] for(int n=0;n<4;n++) {
        float4 r=controlMasks[n];
        if(r.z>r.x && r.w>r.y && p.x>=r.x && p.x<r.z && p.y>=r.y && p.y<r.w) {
            if(!covered) {low=r.y;high=r.w;covered=true;}
            else {low=min(low,r.y);high=max(high,r.w);}
        }
    }
    float4 result=original;
    [branch] if(covered) {
    // Merge only the vertical intervals crossing this sample's x. This keeps
    // adjacent lobes separate and never erases a large enclosing HWND rectangle.
    // Four bounded rounds include a previous position overlapping a current one.
    [loop] for(int mergeStep=0;mergeStep<4;mergeStep++) {
        [unroll] for(int n=0;n<4;n++) {
            float4 r=controlMasks[n];
            if(r.z>r.x && r.w>r.y && p.x>=r.x && p.x<r.z && r.y<=high+2 && r.w>=low-2) {
                low=min(low,r.y);high=max(high,r.w);
            }
        }
    }
    float above=low-1.5,below=high+.5;
    bool haveAbove=above>=bounds.y,haveBelow=below<=bounds.w;
    // At a monitor edge use the available side, never clamp a neighbour back
    // inside the control. If no background exists on either side, retain input.
    if(haveAbove || haveBelow) {
        if(!haveAbove)above=below;
        if(!haveBelow)below=above;
        float4 a=sharp ? source1.SampleLevel(linearClamp,float2(p.x,above)/dims.xy,0)
                      : source0.SampleLevel(linearClamp,float2(p.x,above)/dims.xy,0);
        float4 b=sharp ? source1.SampleLevel(linearClamp,float2(p.x,below)/dims.xy,0)
                      : source0.SampleLevel(linearClamp,float2(p.x,below)/dims.xy,0);
        result=lerp(a,b,below>above?saturate((p.y-above)/(below-above)):0);
    }
    }
    return result;
}
float4 blurInput(float2 pixel) {
    // Only the first (horizontal) blur pass reads the captured raw patch.
    float4 result=source0.SampleLevel(linearClamp,bounded(pixel),0);
    [branch] if(direction.x>.5)result=rawControlFree(pixel,false);
    return result;
}
float4 Blur(V i):SV_TARGET {
    float2 p=i.uv*dims.xy;
    if(material.x<0.05) return blurInput(p);
    float4 c=blurInput(p); float total=1;
    int radius=min(60,int(ceil(material.x*3)));
    // Paired bilinear taps avoid the comb artifacts of sparse wide kernels.
    [loop] for(int k=1;k<=radius;k+=2) {
        float t1=k/material.x, t2=(k+1)/material.x;
        float w1=exp(-0.5*t1*t1), w2=k+1<=radius ? exp(-0.5*t2*t2) : 0;
        float w=w1+w2, offset=(k*w1+(k+1)*w2)/w;
        c+=(blurInput(p+direction.xy*offset)
           +blurInput(p-direction.xy*offset))*w;
        total+=2*w;
    }
    return c/total;
}
V VSInk(uint id:SV_VertexID) {
    V o=VS(id);o.uv=inkProbe.xy+o.uv*inkProbe.zw;return o;
}
float4 Adapt(V i):SV_TARGET {
    // Store local colour and spatial luminance variance. Detail is measured
    // BEFORE blur/refraction, so equal-mean text and flat grey remain distinct.
    // Overlapping footprints and bilinear reconstruction smooth this tiny field.
    float2 p=i.uv*dims.zw+direction.zz;
    float4 moments=0;
    // Keep this tiny pass compact: duplicating the capture-repair code 25
    // times adds avoidable startup compilation cost.
    [loop] for(int y=-2;y<=2;y++) {
        [loop] for(int x=-2;x<=2;x++) {
            float2 offset=float2(x*4.7+y*.63,y*4.3+x*.37)*optics.z;
            float3 colour=rawControlFree(p+offset,true).rgb;
            float luminance=dot(colour,float3(.2126,.7152,.0722));
            moments+=float4(colour,luminance*luminance);
        }
    }
    moments/=25;
    float mean=dot(moments.rgb,float3(.2126,.7152,.0722));
    moments.a=max(0,moments.a-mean*mean);
    // Compute variance before temporal filtering: a flat white-to-black scene
    // transition must not be misclassified as spatial detail.
    if(finish.w>.5)
        moments=lerp(adaptationField.SampleLevel(linearClamp,i.uv,0),moments,finish.z);
    return moments;
}
float3 transmission(float2 pixel) {
    return lerp(source0.SampleLevel(linearClamp,bounded(pixel),0).rgb,
                rawControlFree(pixel,true).rgb,direction.w);
}
float3 filteredTransmission(float2 pixel) {
    // Integrate a small source footprint where the nonlinear mapping minifies
    // background detail. This filters sampling, not the final displacement.
    float2 x=ddx(pixel),y=ddy(pixel);
    float3 color=transmission(pixel);
    if(max(length(x),length(y))>1.25) {
        float2 a=x*.25,b=y*.25;
        color=(transmission(pixel-a-b)+transmission(pixel+a-b)+
               transmission(pixel-a+b)+transmission(pixel+a+b))*.25;
    }
    return color;
}
void oldHermite(float t,out float4 b,out float4 db) {
    float t2=t*t,t3=t2*t;
    b=float4(2*t3-3*t2+1,-2*t3+3*t2,t3-2*t2+t,t3-t2);
    db=float4(6*t2-6*t,-6*t2+6*t,3*t2-4*t+1,3*t2-2*t);
}
float3 oldGlassHeight(float2 pixel) {
    float2 grid=clamp(pixel/field.zw,float2(0,0),field.xy);
    int2 cell=min(int2(floor(grid)),int2(field.xy)-1);
    float4 bx,by,dx,dy;oldHermite(grid.x-cell.x,bx,dx);oldHermite(grid.y-cell.y,by,dy);
    float4 a=oldHeightField.Load(int3(cell,0)),b=oldHeightField.Load(int3(cell+int2(1,0),0));
    float4 c=oldHeightField.Load(int3(cell+int2(0,1),0)),d=oldHeightField.Load(int3(cell+int2(1,1),0));
    float4 r0=float4(a.x,b.x,a.y,b.y),r1=float4(c.x,d.x,c.y,d.y);
    float4 r2=float4(a.z,b.z,a.w,b.w),r3=float4(c.z,d.z,c.w,d.w);
    float4 h=float4(dot(r0,bx),dot(r1,bx),dot(r2,bx),dot(r3,bx));
    float4 gx=float4(dot(r0,dx),dot(r1,dx),dot(r2,dx),dot(r3,dx));
    return float3(dot(by,h),dot(by,gx)/field.z,dot(dy,h)/field.w);
}
float2 lensDirection(float2 v,float2 q) {
    // The inward normal of one rounded contour, not four overlapping strips.
    // Only used in the regular collar depth < band <= radius. At its medial
    // axis the transport is already identically zero with two flat derivatives.
    float2 corner=max(q,0);
    float2 outward=length(corner)>1e-5 ? corner/length(corner) :
        (q.x>q.y ? float2(1,0) : float2(0,1));
    return -sign(v)*outward;
}
float3 edgeSample(float2 p,float roughness) {
    float2 center=dims.zw*.5,v=p-center,q=abs(v)-(center-material.z);
    float d=length(max(q,0))+min(max(q.x,q.y),0)-material.z;
    float a=saturate(.5-d),depth=max(-d,0),scale=optics.z;
    float2 outward=-lensDirection(v,q);
    float upper=pow(saturate(dot(outward,normalize(float2(-.65,-.76)))),7);
    float lower=pow(saturate(outward.y),.75);
    float key=max(upper,.96*lower);
    float rim=exp2(-2*pow(depth/((.9+.25*roughness)*scale),2));
    float inner=exp2(-2*pow((depth-1.5*scale)/(.8*scale),2));
    return float3(a,a*rim*(.035+.965*key),
        a*inner*(.18+.82*saturate(-outward.y))*(.12+.22*(1-key)));
}
float3 edgeFootprint(float2 p,float distance,float roughness) {
    // Integrate radiance WITH coverage. Averaging the line and alpha separately
    // makes diagonals fluctuate when a bright, narrow rim crosses a pixel.
    if(abs(distance)>max(6*optics.z,3))return float3(saturate(.5-distance),0,0);
    float3 integral=0;
    [unroll] for(int y=0;y<4;y++) {
        [unroll] for(int x=0;x<4;x++)
            integral+=edgeSample(p+(float2(x,y)+.5)/4-.5,roughness);
    }
    return integral/16;
}
float4 Glass(V i):SV_TARGET {
    float2 p=i.uv*dims.zw, center=dims.zw*0.5;
    float2 v=p-center, q=abs(v)-(center-material.z);
    float d=length(max(q,0))+min(max(q.x,q.y),0)-material.z;
    float3 edge=edgeFootprint(p,d,saturate(finish.x));
    float2 samplePixel=p+direction.zz;
    float3 c=transmission(samplePixel);
    if(material.w>1.5) {
        float band=min(optics.x,material.z),depth=max(-d,0);
        float2 outward=-lensDirection(v,q);
        float2 offsetsR=0,offsetsG=0,offsetsB=0;
        float3 normal=float3(0,0,1);
        if(response.z<.5) {
            // Given a local cross-section, derive slope and trace one incident
            // interface to the background plane. The center is a flat platform.
            float slope=bevelSlope(depth,band,surface.y,response.w);
            float height=bevelHeight(depth,band,surface.x,surface.y,response.w);
            normal=normalize(float3(outward*slope,1));
            offsetsR=outward*bevelTravel(slope,height,1.5+response.x*.02);
            offsetsG=outward*bevelTravel(slope,height,1.5);
            offsetsB=outward*bevelTravel(slope,height,1.5-response.x*.02);
        } else if(response.z<1.5) {
            // The early continuous height-field candidate, without its later
            // displacement mask. Same IOR and optical depth units for comparison.
            if(band>0) {
            float3 sheet=oldGlassHeight(p);
            float2 gradient=sheet.yz*surface.y;
            normal=normalize(float3(-gradient,1));
            float height=surface.x+surface.y*saturate(sheet.x);
            float3 r=refract(float3(0,0,-1),normal,1/(1.5+response.x*.02));
            float3 g=refract(float3(0,0,-1),normal,1/1.5);
            float3 b=refract(float3(0,0,-1),normal,1/(1.5-response.x*.02));
            offsetsR=r.xy*height/max(-r.z,.0001);
            offsetsG=g.xy*height/max(-g.z,.0001);
            offsetsB=b.xy*height/max(-b.z,.0001);
            }
        } else {
            // Exact previous monotone edge mapping at 100% central size / bend.
            float2 inward=depth<band ? -outward : float2(0,0);
            offsetsG=float2(lensDisplacement(v.x,inward.x,depth,band,1,1,0,0),
                            lensDisplacement(v.y,inward.y,depth,band,1,1,0,0));
            normal=normalize(float3(-offsetsG/lensOpticalDistance(optics.z),1));
            offsetsR=offsetsG*lensChannel(response.x,-1);offsetsB=offsetsG*lensChannel(response.x,1);
        }
        c.r=filteredTransmission(samplePixel+offsetsR).r;
        c.g=filteredTransmission(samplePixel+offsetsG).g;
        c.b=filteredTransmission(samplePixel+offsetsB).b;
        float2 transmittedPixel=samplePixel+offsetsG;
        if(optics.w>.5) {
        float grazing=1-normal.z;
        float fresnel=.034+.966*pow(grazing,5);
        float scale=optics.z;
        float roughness=saturate(finish.x);
        // Appearance has its own narrow support; changing optical travel cannot
        // move a reflective stripe into the body. Shade from the actual scene,
        // not from a folded transmission coordinate.
        float shoulderWidth=max(2*scale,min(band*.30,8*scale));
        float shoulder=1-smoothstep(0,shoulderWidth,depth);
        if(roughness>.001 && shoulder>.001) {
            float radius=(.5+2.5*roughness)*scale;
            float3 scattered=transmission(transmittedPixel)*.4;
            scattered+=(transmission(transmittedPixel+float2(radius,0))
                       +transmission(transmittedPixel-float2(radius,0))
                       +transmission(transmittedPixel+float2(0,radius))
                       +transmission(transmittedPixel-float2(0,radius)))*.15;
            c=lerp(c,scattered,roughness*shoulder);
        }
        // A continuous, symmetric environment footprint. No light/dark gate,
        // distant two-point echoes or full-collar reflection mask.
        float2 tangent=float2(-outward.y,outward.x);
        float2 reflected=samplePixel+outward*(3+5*grazing)*scale;
        float span=(3+5*roughness)*scale;
        float3 environment=source0.SampleLevel(linearClamp,bounded(reflected),0).rgb*.4;
        environment+=(source0.SampleLevel(linearClamp,bounded(reflected+tangent*span),0).rgb
                     +source0.SampleLevel(linearClamp,bounded(reflected-tangent*span),0).rgb)*.2;
        environment+=(source0.SampleLevel(linearClamp,bounded(reflected+outward*span),0).rgb
                     +source0.SampleLevel(linearClamp,bounded(reflected-outward*span),0).rgb)*.1;
        float envLum=dot(environment,float3(.2126,.7152,.0722));
        float3 reflectedColour=lerp(float3(envLum,envLum,envLum),environment,saturate(finish.y));
        c=lerp(c,reflectedColour,saturate(surface.z*shoulder*(.025+.25*fresnel)));
        // Art-directed lighting, not a claim about Apple's private BRDF:
        // narrow upper-left key plus a broad lower reflected light. A dim
        // contour remains between lobes instead of an invariant white outline.
        float detail=0,sceneLum=envLum;
        float3 sceneColour=environment;
        if(response.y>.5) {
            float4 stats=adaptationField.SampleLevel(linearClamp,i.uv,0);
            sceneColour=stats.rgb;
            sceneLum=dot(sceneColour,float3(.2126,.7152,.0722));
            detail=smoothstep(.025,.20,sqrt(max(0,stats.a)));
            // Tonal headroom is confined to the curved shoulder. The centre
            // stays unchanged. A broad normal-dependent roll replaces the
            // fixed inner grey stroke; busy backgrounds need more separation.
            float rollWidth=max(2*scale,min(band*.48,12*scale));
            float roll=1-smoothstep(0,rollWidth,depth);
            float curvature=saturate(grazing*3);
            float upper=saturate(dot(outward,normalize(float2(-.65,-.76))));
            float bright=smoothstep(.40,.95,sceneLum);
            float shade=roll*(.28+.72*curvature)*(.045+.055*upper+.025*detail)*bright;
            c*=1-shade;
            // Very small contrast floor between the reflection lobes, with
            // reduced weight on featureless white. Keep integrated edge AA.
            c*=1-(.12+.32*detail)*bright*edge.z/max(edge.x,.0001);
            float lower=saturate(dot(normal.xy,normalize(float2(.55,.84))));
            float sheen=roll*lower*(.018+.035*detail)*(1-.65*bright)*surface.z;
            float3 sheenColour=lerp(float3(1,1,1),sceneColour/max(max(sceneColour.r,sceneColour.g),max(sceneColour.b,.001)),saturate(finish.y)*.35);
            c=lerp(c,sheenColour,sheen);
        }
        float peak=max(environment.r,max(environment.g,environment.b));
        float3 lightColour=lerp(float3(1,1,1),environment/max(peak,.001),saturate(finish.y)*.6*smoothstep(.04,.3,peak));
        float specular=optics.y*edge.y/max(edge.x,.0001)*(.70+.30*(1-sceneLum)+.12*detail);
        c=lerp(c,lightColour,saturate(specular));
        }
    }
    float a=edge.x;
    // Composite the AA footprint against the actual sharp desktop here. A
    // partially transparent edge otherwise blends with the host's blurred
    // backdrop, and the final integer HWND scissor can cut its highlight off.
    // The scissor now lies beyond this footprint; its pixels match the scene.
    c=saturate(c);
    if(a<1)c=lerp(rawControlFree(samplePixel,true).rgb,c,a);
    return float4(c,1);
}
)HLSL";

    void BindSystemBrush() {
        if (system.visual && originalBrush) {
            Ptr<ABI::Windows::UI::Composition::ISpriteVisual> sprite;
            if (SUCCEEDED(system.visual.As(&sprite))) sprite->put_Brush(originalBrush.Get());
        }
    }
    void ReleaseGpu() {
        BindSystemBrush();
        if (context) { context->ClearState(); context->Flush(); }
        surfaceBrush.Reset(); surface.Reset(); swap.Reset(); windowCapture.Close();
        oldHeightView.Reset();oldHeightTexture.Reset();
        desktop.Reset(); patchView.Reset(); patch.Reset();
        for (int i=0;i<2;i++) { blurredTargets[i].Reset(); blurredViews[i].Reset(); blurred[i].Reset(); }
        for(int i=0;i<2;i++) {adaptationTargets[i].Reset();adaptationViews[i].Reset();adaptation[i].Reset();}
        adaptationWidth=adaptationHeight=0;adaptationAt=adaptationUntil=0;
        inkTargetView.Reset();inkTarget.Reset();inkReadback.Reset();inkVertex.Reset();
        inkPending=inkFrameValid=inkUnavailable=false;inkDecision.Reset();
        vertex.Reset(); blurShader.Reset(); glassShader.Reset(); adaptationShader.Reset(); constants.Reset(); sampler.Reset();
        context.Reset(); device.Reset(); width=height=0; monitor=nullptr; haveDesktop=false;
    }
    bool Check(HRESULT hr) { lastError=hr; return SUCCEEDED(hr); }
    struct ShaderBytecode {
        Ptr<ID3DBlob> vertex, blur, glass, adapt, ink;
        unsigned compilations=0;
    };
    static ShaderBytecode& CachedShaders() {
        // All renderer operations run on the UI thread. Bytecode is independent
        // of the D3D device and survives suspension, fallback and monitor changes.
        static ShaderBytecode shaders;
        return shaders;
    }
    bool CaptureExclusion(bool value) {
        const DWORD desired=value ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
        DWORD current=~0u;
        if(GetWindowDisplayAffinity(hwnd,&current) && current==desired)return true;
        if(SetWindowDisplayAffinity(hwnd,desired)){if(value)DwmFlush();return true;}
        return Check(HRESULT_FROM_WIN32(GetLastError()));
    }
    bool Initialize() {
        ReleaseGpu();
        ++gpuInitializations;
        monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
        Ptr<IDXGIFactory1> factory;
        if (!Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        Ptr<IDXGIAdapter1> selected;

        for(UINT a=0;!selected;a++) {
            Ptr<IDXGIAdapter1> adapter;
            if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND) break;
            for(UINT o=0;;o++) {
                Ptr<IDXGIOutput> output;
                if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC desc{}; output->GetDesc(&desc);
                if(desc.Monitor==monitor) {
                    if(desc.Rotation!=DXGI_MODE_ROTATION_IDENTITY && desc.Rotation!=DXGI_MODE_ROTATION_UNSPECIFIED) {
                        lastError=DXGI_ERROR_UNSUPPORTED; return false;
                    }
                    selected=adapter; outputRect=desc.DesktopCoordinates; break;
                }
            }
        }
        if(!selected) { lastError=DXGI_ERROR_NOT_FOUND; return false; }
        DXGI_ADAPTER_DESC1 adapterDesc{}; selected->GetDesc1(&adapterDesc); adapterName=adapterDesc.Description;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        if(!Check(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    levels,3,D3D11_SDK_VERSION,&device,nullptr,&context))) return false;
        Ptr<IDXGIDevice1> pacing;
        if(SUCCEEDED(device.As(&pacing))) pacing->SetMaximumFrameLatency(1);
        windowCapture.SetSignal(signal);

        if(!Check(WarmShaderBytecode()))return false;
        const auto& shaders=CachedShaders();
        if(!Check(device->CreateVertexShader(shaders.vertex->GetBufferPointer(),shaders.vertex->GetBufferSize(),nullptr,&vertex)) ||
           !Check(device->CreateVertexShader(shaders.ink->GetBufferPointer(),shaders.ink->GetBufferSize(),nullptr,&inkVertex)) ||
           !Check(device->CreatePixelShader(shaders.blur->GetBufferPointer(),shaders.blur->GetBufferSize(),nullptr,&blurShader)) ||
           !Check(device->CreatePixelShader(shaders.adapt->GetBufferPointer(),shaders.adapt->GetBufferSize(),nullptr,&adaptationShader)) ||
           !Check(device->CreatePixelShader(shaders.glass->GetBufferPointer(),shaders.glass->GetBufferSize(),nullptr,&glassShader))) return false;
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(!Check(device->CreateBuffer(&cb,nullptr,&constants))) return false;
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        return Check(device->CreateSamplerState(&sd,&sampler));
    }
    bool ResizeBuffers(int w,int h) {
        if(w==width && h==height && swap) return true;
        if(w<=0 || h<=0 || w>3000 || h>2200) { lastError=E_INVALIDARG; return false; }
        ++bufferResizes;
        inkFrameValid=false;++inkGeneration;
        context->ClearState();
        if(swap) {
            if(!Check(swap->ResizeBuffers(2,w,h,DXGI_FORMAT_UNKNOWN,0))) return false;
        } else {
            Ptr<IDXGIDevice> dxgi; device.As(&dxgi);
            Ptr<IDXGIAdapter> a; dxgi->GetAdapter(&a);
            Ptr<IDXGIFactory2> f; a->GetParent(IID_PPV_ARGS(&f));
            DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=w; desc.Height=h;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
            desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
            desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
            desc.Scaling=DXGI_SCALING_STRETCH;
            if(!Check(f->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&swap))) return false;
            Ptr<ABI::Windows::UI::Composition::ICompositorInterop> interop;
            if(!Check(system.compositor.As(&interop)) || !Check(interop->CreateCompositionSurfaceForSwapChain(swap.Get(),&surface))) return false;
            if(!Check(system.compositor->CreateSurfaceBrushWithSurface(surface.Get(),&surfaceBrush))) return false;
            surfaceBrush->put_Stretch(ABI::Windows::UI::Composition::CompositionStretch_Fill);
        }
        patchView.Reset(); patch.Reset();
        D3D11_TEXTURE2D_DESC t{}; t.Width=w+padding*2; t.Height=h+padding*2;
        t.MipLevels=t.ArraySize=1; t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        t.SampleDesc.Count=1; t.Usage=D3D11_USAGE_DEFAULT; t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(!Check(device->CreateTexture2D(&t,nullptr,&patch)) || !Check(device->CreateShaderResourceView(patch.Get(),nullptr,&patchView))) return false;
        t.BindFlags|=D3D11_BIND_RENDER_TARGET;
        for(int i=0;i<2;i++) {
            blurredTargets[i].Reset(); blurredViews[i].Reset(); blurred[i].Reset();
            if(!Check(device->CreateTexture2D(&t,nullptr,&blurred[i])) ||
               !Check(device->CreateShaderResourceView(blurred[i].Get(),nullptr,&blurredViews[i])) ||
               !Check(device->CreateRenderTargetView(blurred[i].Get(),nullptr,&blurredTargets[i]))) return false;
        }
        width=w; height=h; needRender=true; return true;
    }
    bool PrepareAdaptation(float scale) {
        const int w=std::clamp(int(std::ceil(width/(8*scale))),8,128);
        const int h=std::clamp(int(std::ceil(height/(8*scale))),8,96);
        if(w==adaptationWidth && h==adaptationHeight && adaptation[0] && adaptation[1])return true;
        adaptationAt=0;
        D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=t.ArraySize=1;
        t.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;t.SampleDesc.Count=1;
        t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        for(int i=0;i<2;i++) {
            adaptationTargets[i].Reset();adaptationViews[i].Reset();adaptation[i].Reset();
            if(!Check(device->CreateTexture2D(&t,nullptr,&adaptation[i])) ||
               !Check(device->CreateShaderResourceView(adaptation[i].Get(),nullptr,&adaptationViews[i])) ||
               !Check(device->CreateRenderTargetView(adaptation[i].Get(),nullptr,&adaptationTargets[i])))return false;
        }
        adaptationWidth=w;adaptationHeight=h;adaptationIndex=0;return true;
    }
    bool Capture() {
        if(frozen) return true;
        bool updated=false;
        const HRESULT result=windowCapture.Poll(hwnd,device.Get(),context.Get(),desktop,outputRect,updated);
        if(FAILED(result)) return Check(result);
        if(windowCapture.sourceChanged) needRender=true;
        if(result==S_FALSE) { haveDesktop=false; BindSystemBrush(); return true; }
        if(updated) { ++frames; ++copies; haveDesktop=windowCapture.ready; needRender=true; adaptationUntil=GlassClockMs()+300; }
        if(updated && windowCapture.latestFrameAgeMs>0 && windowCapture.latestFrameAgeMs<2000)
            sourceAges[ageCount++%sourceAges.size()]=windowCapture.latestFrameAgeMs;
        return true;
    }
    bool PrepareOldSurface(float scale) {
        if(mode!=2 || opticalModel!=1)return true;
        const float radius=GlassGeometry::EffectiveRadius(cornerRadius,float(width),float(height),scale)*scale;
        const bool rebuilt=oldSurface.Build(float(width),float(height),radius,std::min(edgeWidth*scale,radius),std::max(2.0f,2*scale));
        if(!rebuilt && oldHeightView)return true;
        oldHeightView.Reset();oldHeightTexture.Reset();
        D3D11_TEXTURE2D_DESC desc{};desc.Width=oldSurface.columns;desc.Height=oldSurface.rows;
        desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=oldSurface.texels.data();data.SysMemPitch=UINT(oldSurface.columns*sizeof(GlassSurface::Texel));
        return Check(device->CreateTexture2D(&desc,&data,&oldHeightTexture)) &&
               Check(device->CreateShaderResourceView(oldHeightTexture.Get(),nullptr,&oldHeightView));
    }
    bool Render(const RECT& rect) {
        if(!haveDesktop) return true;
        const int ox=rect.left-outputRect.left-padding, oy=rect.top-outputRect.top-padding;
        D3D11_TEXTURE2D_DESC td{}; desktop->GetDesc(&td);
        const int left=std::max(0,ox), top=std::max(0,oy);
        const int right=std::min(static_cast<int>(td.Width),ox+width+2*padding);
        const int bottom=std::min(static_cast<int>(td.Height),oy+height+2*padding);
        if(right<=left || bottom<=top) return true;
        ID3D11ShaderResourceView* empty[4]={}; context->PSSetShaderResources(0,4,empty);
        D3D11_BOX box{static_cast<UINT>(left),static_cast<UINT>(top),0,static_cast<UINT>(right),static_cast<UINT>(bottom),1};
        context->CopySubresourceRegion(patch.Get(),0,left-ox,top-oy,0,desktop.Get(),0,&box);
        valid={left-ox,top-oy,right-ox,bottom-oy};
        const float scale=GetDpiForWindow(hwnd)/96.0f;
        if(!PrepareOldSurface(scale))return false;
        Constants c{{float(width+padding*2),float(height+padding*2),float(width),float(height)},
                     {blur,1,GlassGeometry::EffectiveRadius(cornerRadius,float(width),float(height),scale)*scale,float(mode)},
                     {1,0,float(padding),sharpMix},
                     {float(valid.left)+.5f,float(valid.top)+.5f,float(valid.right)-.5f,float(valid.bottom)-.5f},
                     {edgeWidth*scale,highlight,scale,opticalOnly?0.0f:1.0f},
                     {dispersion,adaptiveContrast?1.0f:0.0f,float(opticalModel),float(edgeProfile)},
                     {baseThickness*scale,reliefHeight*scale,reflection,1.2f*scale},
                     {float(std::max(oldSurface.columns-1,1)),float(std::max(oldSurface.rows-1,1)),oldSurface.stepX,oldSurface.stepY},
                     {edgeRoughness,environmentTint,0,0},{}};
        const bool includePrevious=GetTickCount64()<previousControlOccludersUntil;
        for(size_t i=0;i<4;++i) {
            const RECT r=i<2?controlOccluders[i]:includePrevious?previousControlOccluders[i-2]:RECT{};
            if(r.right<=r.left || r.bottom<=r.top)continue;
            c.controlMasks[i][0]=float(double(r.left)-rect.left+padding);
            c.controlMasks[i][1]=float(double(r.top)-rect.top+padding);
            c.controlMasks[i][2]=float(double(r.right)-rect.left+padding);
            c.controlMasks[i][3]=float(double(r.bottom)-rect.top+padding);
        }
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetSamplers(0,1,sampler.GetAddressOf());
        context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        D3D11_VIEWPORT viewport{0,0,c.dimensions[0],c.dimensions[1],0,1}; context->RSSetViewports(1,&viewport);
        context->PSSetShader(blurShader.Get(),nullptr,0);
        for(int i=0;i<2;i++) {
            context->PSSetShaderResources(0,4,empty);
            context->OMSetRenderTargets(1,blurredTargets[i].GetAddressOf(),nullptr);
            c.direction[0]=i==0 ? 1.0f : 0.0f; c.direction[1]=i==1 ? 1.0f : 0.0f;
            context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
            ID3D11ShaderResourceView* src=i==0 ? patchView.Get() : blurredViews[0].Get();
            context->PSSetShaderResources(0,1,&src); context->Draw(3,0);
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->PSSetShaderResources(0,4,empty);
        const bool adapt=mode==2 && adaptiveContrast && !opticalOnly;
        const double now=GlassClockMs();
        if(adapt) {
            if(!PrepareAdaptation(scale))return false;
            // Reset on geometry changes / long suspension: never drag old scene
            // statistics across the desktop. Smooth material response only,
            // not the captured image, and never read GPU pixels on the CPU.
            const bool history=adaptationAt>0 && now-adaptationAt<250 && EqualRect(&rect,&lastRect);
            c.finish[2]=history ? float(1-std::exp(-(now-adaptationAt)/75.0)) : 1.0f;
            c.finish[3]=history?1.0f:0.0f;
            context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
            viewport.Width=float(adaptationWidth);viewport.Height=float(adaptationHeight);context->RSSetViewports(1,&viewport);
            const int next=1-adaptationIndex;
            context->OMSetRenderTargets(1,adaptationTargets[next].GetAddressOf(),nullptr);
            ID3D11ShaderResourceView* inputs[]={nullptr,patchView.Get(),nullptr,history?adaptationViews[adaptationIndex].Get():nullptr};
            context->PSSetShaderResources(0,4,inputs);context->PSSetShader(adaptationShader.Get(),nullptr,0);context->Draw(3,0);
            context->OMSetRenderTargets(0,nullptr,nullptr);context->PSSetShaderResources(0,4,empty);
            adaptationIndex=next;adaptationAt=now;
        } else adaptationAt=0;
        Ptr<ID3D11Texture2D> buffer; Ptr<ID3D11RenderTargetView> target;
        if(!Check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer))) || !Check(device->CreateRenderTargetView(buffer.Get(),nullptr,&target))) return false;
        viewport.Width=float(width); viewport.Height=float(height); context->RSSetViewports(1,&viewport);
        context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        context->PSSetShader(glassShader.Get(),nullptr,0);
        ID3D11ShaderResourceView* sources[]={blurredViews[1].Get(),patchView.Get(),oldHeightView.Get(),adapt?adaptationViews[adaptationIndex].Get():nullptr}; context->PSSetShaderResources(0,4,sources);
        context->Draw(3,0);
        context->OMSetRenderTargets(0,nullptr,nullptr); context->PSSetShaderResources(0,4,empty);
        if(!Check(swap->Present(0,0))) return false;
        Ptr<ABI::Windows::UI::Composition::ISpriteVisual> sprite; system.visual.As(&sprite);
        Ptr<ABI::Windows::UI::Composition::ICompositionBrush> brush; surfaceBrush.As(&brush);
        sprite->put_Brush(brush.Get());
        inkFrame=c;inkFrameValid=true;inkDirty=true;
        ++renders; needRender=adapt && now<adaptationUntil; lastRect=rect; return true;
    }
    void Fail() {
        frozen=false; failed=true; retryAt=GetTickCount64()+2000; ReleaseGpu(); BindSystemBrush();
        // Release capture before exposing the native fallback to remote viewing.
        const HRESULT failure=lastError;CaptureExclusion(false);lastError=failure;
    }
public:
    void SetInkRegion(const RECT& region) {
        if(EqualRect(&region,&inkRegion))return;
        inkRegion=region;inkDirty=true;++inkGeneration;
    }
    // Returns -1 for the existing theme-based Auto fallback, 0 dark, 1 light.
    // Only the 2 KB probe can cross to the CPU, using nonblocking Map. Busy
    // readbacks are skipped, never waited on. Native text/IME rendering stays intact.
    int UpdateAutoInk(bool requested,COLORREF tint,int alpha,bool fallbackWhite) {
        if(requested!=inkEnabled || tint!=inkTint || alpha!=inkAlpha) {
            inkEnabled=requested;inkTint=tint;inkAlpha=alpha;++inkGeneration;
            inkDirty=true;inkDecision.Reset(fallbackWhite);
        }
        if(!inkEnabled || mode!=2 || !enabled || failed || inkUnavailable || !haveDesktop || !inkFrameValid)return -1;
        if(presentationMotion)return inkDecision.valid?int(inkDecision.white):-1;
        const double now=GlassClockMs();
        if(inkPending) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr=context->Map(inkReadback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(SUCCEEDED(hr)) {
                if(inkPendingGeneration==inkGeneration && now-inkSubmittedAt<500) {
                    inkLastShare=GlassAutoInk::WhiteShare(mapped.pData,mapped.RowPitch,inkWidth,inkHeight,inkTint,inkAlpha);
                    inkDecision.Update(inkLastShare,now);
                }
                context->Unmap(inkReadback.Get(),0);inkPending=false;
            } else if(hr==DXGI_ERROR_WAS_STILL_DRAWING)++inkPollMisses;
            else {inkUnavailable=true;return -1;}
        }
        if(inkDecision.valid)inkDecision.Update(inkLastShare,now);
        if(!inkPending && inkDirty && now-inkSubmittedAt>=100) {
            if(!inkTarget) {
                D3D11_TEXTURE2D_DESC t{};t.Width=inkWidth;t.Height=inkHeight;t.ArraySize=t.MipLevels=1;
                t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET;
                if(FAILED(device->CreateTexture2D(&t,nullptr,&inkTarget)) || FAILED(device->CreateRenderTargetView(inkTarget.Get(),nullptr,&inkTargetView))) {
                    inkUnavailable=true;return -1;
                }
                t.Usage=D3D11_USAGE_STAGING;t.BindFlags=0;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                if(FAILED(device->CreateTexture2D(&t,nullptr,&inkReadback))){inkUnavailable=true;return -1;}
            }
            RECT region{std::clamp(inkRegion.left,0L,LONG(width-1)),std::clamp(inkRegion.top,0L,LONG(height-1)),
                        std::clamp(inkRegion.right,1L,LONG(width)),std::clamp(inkRegion.bottom,1L,LONG(height))};
            if(region.right<=region.left || region.bottom<=region.top)return -1;
            Constants c=inkFrame;
            c.inkProbe[0]=float(region.left)/width;c.inkProbe[1]=float(region.top)/height;
            c.inkProbe[2]=float(region.right-region.left)/width;c.inkProbe[3]=float(region.bottom-region.top)/height;
            context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
            context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(inkVertex.Get(),nullptr,0);context->VSSetConstantBuffers(0,1,constants.GetAddressOf());
            context->PSSetShader(glassShader.Get(),nullptr,0);context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
            context->PSSetSamplers(0,1,sampler.GetAddressOf());
            D3D11_VIEWPORT v{0,0,float(inkWidth),float(inkHeight),0,1};context->RSSetViewports(1,&v);
            context->OMSetRenderTargets(1,inkTargetView.GetAddressOf(),nullptr);
            ID3D11ShaderResourceView* inputs[]={blurredViews[1].Get(),patchView.Get(),oldHeightView.Get(),adaptiveContrast&&!opticalOnly?adaptationViews[adaptationIndex].Get():nullptr};
            context->PSSetShaderResources(0,4,inputs);context->Draw(3,0);
            context->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* empty[4]={};context->PSSetShaderResources(0,4,empty);
            context->CopyResource(inkReadback.Get(),inkTarget.Get());
            // Submit without waiting, including when the captured desktop is
            // static and no further swap-chain Present would submit this copy.
            context->Flush();
            inkPending=true;inkDirty=false;inkSubmittedAt=now;inkPendingGeneration=inkGeneration;++inkSubmissions;
        }
        return inkDecision.valid?int(inkDecision.white):-1;
    }
    static HRESULT WarmShaderBytecode() {
        auto& cached=CachedShaders();
        if(cached.vertex && cached.blur && cached.glass && cached.adapt && cached.ink)return S_OK;
        Ptr<ID3DBlob> vs,ps,gs,ads,ivs,errors;
        HRESULT result=D3DCompile(shaderSource.data(),shaderSource.size(),nullptr,nullptr,nullptr,"VS","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors);
        if(FAILED(result))return result;
        result=D3DCompile(shaderSource.data(),shaderSource.size(),nullptr,nullptr,nullptr,"Blur","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors);
        if(FAILED(result))return result;
        result=D3DCompile(shaderSource.data(),shaderSource.size(),nullptr,nullptr,nullptr,"Glass","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&gs,&errors);
        if(FAILED(result))return result;
        result=D3DCompile(shaderSource.data(),shaderSource.size(),nullptr,nullptr,nullptr,"Adapt","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ads,&errors);
        if(FAILED(result))return result;
        result=D3DCompile(shaderSource.data(),shaderSource.size(),nullptr,nullptr,nullptr,"VSInk","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ivs,&errors);
        if(FAILED(result))return result;
        cached.vertex=vs;cached.blur=ps;cached.glass=gs;cached.adapt=ads;cached.ink=ivs;++cached.compilations;
        return S_OK;
    }
    static unsigned ShaderCompilations() {return CachedShaders().compilations;}
    void SetPresentationMotion(bool active) {
        if(presentationMotion==active)return;
        presentationMotion=active;
        if(!active){needRender=true;RequestDraw();}
    }
    void SuspendForFold() {
        // Retain the device, shaders and last rendered surface for immediate
        // restore. Stop screen capture and wakeups while the note is hidden.
        enabled=false;presentationMotion=false;
        inkFrameValid=false;inkDecision.Reset();++inkGeneration;
        adaptationAt=0;
        if(hwnd)KillTimer(hwnd,71);
        windowEvents.Stop();windowCapture.Close();
        if(system.visual)system.visual->put_IsVisible(FALSE);
        CaptureExclusion(false);
    }
    // Approximate the tiny background hidden by independently capturable
    // controls, before both blur and sharp/refraction sampling. This preserves
    // WDA_NONE on the controls; it cannot recover exact covered desktop detail.
    // One previous position is kept for 150 ms for asynchronous WGC frames.
    // Capture delayed beyond that history can still show a transient residue.
    void SetControlOccluders(const std::array<RECT,2>& screenVisibleRects) {
        std::array<RECT,2> next{};
        for(size_t i=0;i<next.size();++i) {
            const RECT r=screenVisibleRects[i];
            if(r.right<=r.left || r.bottom<=r.top)continue;
            // One physical pixel for the visible path's antialias fringe.
            next[i]={r.left>LONG_MIN?r.left-1:r.left,r.top>LONG_MIN?r.top-1:r.top,
                     r.right<LONG_MAX?r.right+1:r.right,r.bottom<LONG_MAX?r.bottom+1:r.bottom};
        }
        if(EqualRect(&next[0],&controlOccluders[0]) && EqualRect(&next[1],&controlOccluders[1]))return;
        previousControlOccluders=controlOccluders;
        previousControlOccludersUntil=GetTickCount64()+150;
        controlOccluders=next;needRender=true;RequestDraw();
    }
    int mode=2; // -1 = transparent, 0 = system backdrop, 1 = soft blur, 2 = liquid
    float blur=1.0f, sharpMix=0.0f;
    int opticalModel=0,edgeProfile=0;
    float baseThickness=32,reliefHeight=10,reflection=.5f;
    float cornerRadius=48.0f, edgeWidth=30.0f, highlight=.8f;
    float edgeRoughness=.30f,environmentTint=.35f;
    float dispersion=0;
    bool adaptiveContrast=true,opticalOnly=false;
    static const wchar_t* ModelName(int model) {
        static const wchar_t* names[]={L"局部截面（研究路线）",L"旧高度场（对照）",L"普通透镜（对照）"};
        return names[std::clamp(model,0,2)];
    }
    bool Enable(HWND window,bool value) {
        hwnd=window; enabled=value;
        if(!signal) {signal=std::make_shared<CaptureSignal>(); signal->window=window;}
        const bool ok=system.Enable(window,value);
        if(ok && value && !originalBrush) {
            Ptr<ABI::Windows::UI::Composition::ISpriteVisual> sprite; system.visual.As(&sprite);
            sprite->get_Brush(&originalBrush);
        }
        if(!value || mode<=0) { frozen=false; KillTimer(window,71); windowEvents.Stop(); ReleaseGpu(); CaptureExclusion(false); }
        else if(ok) { needRender=true; RequestBorderless(); if(!frozen) windowEvents.Start(signal); SetTimer(window,71,250,nullptr); RequestDraw(); }
        return ok;
    }
    // Transient window presentation only; optical parameters and pixels remain unchanged.
    void SetPresentationOpacity(float value) { if(system.visual)system.visual->put_Opacity(std::clamp(value,0.0f,1.0f)); }
    void Resize(int w,int h) { system.Resize(w,h); needRender=true; RequestDraw(); }
    void RequestDraw() {if(signal && enabled && mode>0 && !presentationMotion) signal->Notify();}
    void FrameReady() {
        if(!signal) return;
        const double notified=signal->notifiedAt.load(); signal->posted.store(false);
        if(notified>0) eventDelays[eventCount++%eventDelays.size()]=std::max(0.0,GlassClockMs()-notified);
        Tick();
    }
    void Changed() {
        inkFrameValid=false;inkDecision.Reset();++inkGeneration;
        adaptationAt=0;
        needRender=true; failed=false; retryAt=0;
        if(mode<=0) { ReleaseGpu(); KillTimer(hwnd,71); windowEvents.Stop(); CaptureExclusion(false); }
        else if(enabled) { if(!frozen) windowEvents.Start(signal); SetTimer(hwnd,71,250,nullptr); RequestDraw(); }
    }
    void Freeze(bool value) {
        if(value==frozen || mode<=0) return;
        if(value && !haveDesktop) return;
        frozen=value;
        if(value) { windowCapture.Close(); windowEvents.Stop(); CaptureExclusion(false); }
        else { windowCapture.Close(); haveDesktop=false; needRender=true; windowEvents.Start(signal); RequestDraw(); }
    }
    bool Frozen() const { return frozen; }
    bool PrepareScreenshot(HWND window) {
        hwnd=window;
        if(enabled && mode>0){Freeze(true);if(!frozen)return false;}
        if(!CaptureExclusion(false))return false;
        DwmFlush();return true;
    }
    std::wstring Diagnostics() const {
        DWORD affinity=~0u; GetWindowDisplayAffinity(hwnd,&affinity);
        return Status()+L"\r\ndisplayAffinity="+std::to_wstring(affinity)+L"\r\nborderAccess="+std::to_wstring(signal ? signal->borderAccess.load() : -1)
            +L"\r\ncornerRadiusDip="+std::to_wstring(cornerRadius)+L"\r\nedgeWidthDip="+std::to_wstring(edgeWidth)+L"\r\nhighlight="+std::to_wstring(highlight)
            +L"\r\nopticalModel="+std::to_wstring(opticalModel)+L"\r\nedgeProfile="+std::to_wstring(edgeProfile)
            +L"\r\nbaseThicknessDip="+std::to_wstring(baseThickness)+L"\r\nreliefHeightDip="+std::to_wstring(reliefHeight)
            +L"\r\nreflection="+std::to_wstring(reflection)+L"\r\nopticalOnly="+std::to_wstring(opticalOnly)
            +L"\r\nfinishVersion=1\r\nedgeRoughness="+std::to_wstring(edgeRoughness)+L"\r\nenvironmentTint="+std::to_wstring(environmentTint)
            +L"\r\nshaderCompilations="+std::to_wstring(ShaderCompilations())+L"\r\ngpuInitializations="+std::to_wstring(gpuInitializations)+L"\r\nbufferResizes="+std::to_wstring(bufferResizes)
            +L"\r\nedgeAA=coverage-radiance-4x4\r\nclipGuardPx=2"
            +L"\r\nlensMapping=profile-comparison-v1\r\ncentralMagnification=1.0"
            +L"\r\ndispersion="+std::to_wstring(dispersion)
            +L"\r\nadaptiveContrast="+std::to_wstring(adaptiveContrast)
            +L"\r\nadaptation=local-colour-variance-v1\r\nadaptationGrid="+std::to_wstring(adaptationWidth)+L"x"+std::to_wstring(adaptationHeight)
            +L"\r\nautoInkProbe=32x16-10Hz\r\nautoInkSubmissions="+std::to_wstring(inkSubmissions)+L"\r\nautoInkBusySkips="+std::to_wstring(inkPollMisses)
            +L"\r\nsourceAgeP95Ms="+(ageCount ? std::to_wstring(P95(sourceAges,ageCount)) : L"unavailable")+L"\r\neventDelayP95Ms="+std::to_wstring(P95(eventDelays,eventCount))
            +L"\r\nsubmitP95Ms="+std::to_wstring(P95(submitTimes,submitCount))+L"\r\ndrainedFrames="+std::to_wstring(windowCapture.drained)+L"\r\n"+windowCapture.Detail();
    }
    void Tick() {
        if(!enabled || presentationMotion || mode<=0 || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return;
        // The existing idle tick also clears stale masks when the desktop has
        // stopped producing capture frames; no additional timer is introduced.
        if(previousControlOccludersUntil && GetTickCount64()>=previousControlOccludersUntil) {
            previousControlOccludersUntil=0;previousControlOccluders={};needRender=true;
        }
        if(signal && signal->borderAccess.load()==-1) return;
        if(failed && GetTickCount64()<retryAt) return;
        const double started=GlassClockMs();
        if(!frozen && !CaptureExclusion(true)){Fail();return;}
        if(!device || (!frozen && monitor!=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST))) {
            DwmFlush();
            if(!Initialize()) { Fail(); return; }
            failed=false;
        }
        RECT rect{}; GetClientRect(hwnd,&rect);
        POINT p{}; ClientToScreen(hwnd,&p); OffsetRect(&rect,p.x,p.y);
        if(!ResizeBuffers(rect.right-rect.left,rect.bottom-rect.top)) { Fail(); return; }
        if(!EqualRect(&rect,&lastRect)) needRender=true;
        if(!Capture()) { Fail(); return; }
        if(needRender) {
            if(!Render(rect)) Fail();
            else submitTimes[submitCount++%submitTimes.size()]=GlassClockMs()-started;
        }
    }
    std::wstring Status() const {
        if(mode<0) return L"普通透明 · 无背景捕获";
        if(mode==0) return L"系统毛玻璃 · 远程兼容";
        std::wostringstream s;
        if(failed) { s<<L"已回退系统毛玻璃 · 错误 0x"<<std::hex<<static_cast<unsigned long>(lastError); return s.str(); }
        s<<(frozen ? L"已冻结 · 远程与截图可见" : (haveDesktop ? L"整屏实时 · 仅本机可见" : L"正在准备整屏背景"));
        if(mode==2)s<<L"\r\n"<<ModelName(opticalModel)<<(opticalOnly?L" · 只看折射":L" · 完整材质");
        else s<<L"\r\n模糊 "<<blur<<L" px";
        s<<L"\r\n渲染 "<<renders<<L"  |  背景复制 "<<copies<<L"  |  捕获帧 "<<frames
         <<L"\r\n"<<(signal && signal->BorderlessAllowed() ? L"无边框捕获已获系统授权" : L"无边框捕获尚未获系统授权");
        return s.str();
    }
    void Close() { if(signal) signal->alive.store(false); windowEvents.Stop(); if(hwnd) KillTimer(hwnd,71); ReleaseGpu(); if(IsWindow(hwnd))CaptureExclusion(false); originalBrush.Reset(); system.Close(); }
};
