#include <windows.h>
#include <dwmapi.h>
#include "../src/liquid_backdrop.h"
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <cstring>

// Offscreen actual HLSL acceptance fixtures. CPU readback exists only in this
// test, never in the application. No desktop capture or user data is touched.
struct GlassAdaptationTest {
    using Pixel=std::array<float,4>;
    GlassLabBackdrop g;
    GlassLabBackdrop::Constants c{};
    static constexpr int W=320,H=160,P=512;
    std::vector<Pixel> pixels;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    static void Check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("D3D operation failed");}
    ~GlassAdaptationTest(){g.Close();}
    GlassAdaptationTest() {
        Check(GlassLabBackdrop::WarmShaderBytecode());
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&g.device,nullptr,&g.context));
        auto& s=g.CachedShaders();
        Check(g.device->CreateVertexShader(s.vertex->GetBufferPointer(),s.vertex->GetBufferSize(),nullptr,&g.vertex));
        Check(g.device->CreatePixelShader(s.glass->GetBufferPointer(),s.glass->GetBufferSize(),nullptr,&g.glassShader));
        Check(g.device->CreatePixelShader(s.adapt->GetBufferPointer(),s.adapt->GetBufferSize(),nullptr,&g.adaptationShader));
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(c);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Check(g.device->CreateBuffer(&cb,nullptr,&g.constants));
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        Check(g.device->CreateSamplerState(&sd,&g.sampler));
        g.width=W;g.height=H;if(!g.PrepareAdaptation(1))throw std::runtime_error("analysis allocation failed");
        D3D11_TEXTURE2D_DESC t{};t.Width=W+2*P;t.Height=H+2*P;t.ArraySize=t.MipLevels=1;
        t.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Check(g.device->CreateTexture2D(&t,nullptr,&g.patch));Check(g.device->CreateShaderResourceView(g.patch.Get(),nullptr,&g.patchView));
        pixels.resize(size_t(t.Width)*t.Height);
        t.Width=W;t.Height=H;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        Check(g.device->CreateTexture2D(&t,nullptr,&output));Check(g.device->CreateRenderTargetView(output.Get(),nullptr,&target));
        c={{float(W+2*P),float(H+2*P),float(W),float(H)},
           {0,1,48,2},{0,1,P,0},{.5f,.5f,W+2*P-.5f,H+2*P-.5f},
           {30,.8f,1,1},{0,1,0,0},{32,10,.5f,1.2f},{1,1,1,1},{.3f,.35f,1,0},{}};
        g.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g.context->VSSetShader(g.vertex.Get(),nullptr,0);
        g.context->PSSetConstantBuffers(0,1,g.constants.GetAddressOf());g.context->PSSetSamplers(0,1,g.sampler.GetAddressOf());
    }
    void Unbind() {
        ID3D11ShaderResourceView* empty[4]={};g.context->PSSetShaderResources(0,4,empty);g.context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void Background(int kind) {
        Unbind();
        for(int y=0;y<H+2*P;y++)for(int x=0;x<W+2*P;x++) {
            float v=kind==0?1.f:kind==1?0.f:kind==2?.5f:float(((x/3)+(y/3))%2);
            Pixel p{v,v,v,1};
            if(kind==4)p=y<P+H/2?Pixel{0,0,0,1}:Pixel{.68f,.04f,.14f,1};
            pixels[size_t(y)*(W+2*P)+x]=p;
        }
        g.context->UpdateSubresource(g.patch.Get(),0,nullptr,pixels.data(),(W+2*P)*sizeof(Pixel),0);
    }
    void Analyze(bool history=false,float blend=1) {
        Unbind();c.finish[2]=blend;c.finish[3]=history?1.f:0.f;
        g.context->UpdateSubresource(g.constants.Get(),0,nullptr,&c,0,0);
        D3D11_VIEWPORT v{0,0,float(g.adaptationWidth),float(g.adaptationHeight),0,1};g.context->RSSetViewports(1,&v);
        const int next=1-g.adaptationIndex;g.context->OMSetRenderTargets(1,g.adaptationTargets[next].GetAddressOf(),nullptr);
        ID3D11ShaderResourceView* src[]={nullptr,g.patchView.Get(),nullptr,history?g.adaptationViews[g.adaptationIndex].Get():nullptr};
        g.context->PSSetShaderResources(0,4,src);g.context->PSSetShader(g.adaptationShader.Get(),nullptr,0);g.context->Draw(3,0);Unbind();g.adaptationIndex=next;
    }
    std::vector<Pixel> Read(ID3D11Texture2D* texture) {
        D3D11_TEXTURE2D_DESC t{};texture->GetDesc(&t);
        // Decode half-float analysis values for exact field checks.
        t.Usage=D3D11_USAGE_STAGING;t.BindFlags=0;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;Check(g.device->CreateTexture2D(&t,nullptr,&staging));
        g.context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE m{};Check(g.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
        std::vector<Pixel> result(size_t(t.Width)*t.Height);
        for(UINT y=0;y<t.Height;y++)for(UINT x=0;x<t.Width;x++) {
            auto row=static_cast<const unsigned char*>(m.pData)+y*m.RowPitch;
            Pixel p{};
            if(t.Format==DXGI_FORMAT_R32G32B32A32_FLOAT)std::memcpy(p.data(),row+x*sizeof(Pixel),sizeof(Pixel));
            else for(int k=0;k<4;k++) {
                unsigned h=reinterpret_cast<const unsigned short*>(row)[x*4+k];
                int e=(h>>10)&31;float f=e?std::ldexp(1.f+float(h&1023)/1024,e-15):std::ldexp(float(h&1023),-24);
                p[k]=(h&32768)?-f:f;
            }
            result[size_t(y)*t.Width+x]=p;
        }
        g.context->Unmap(staging.Get(),0);return result;
    }
    std::vector<Pixel> Draw(bool adaptive=true) {
        Unbind();c.response[1]=adaptive?1.f:0.f;g.context->UpdateSubresource(g.constants.Get(),0,nullptr,&c,0,0);
        D3D11_VIEWPORT v{0,0,W,H,0,1};g.context->RSSetViewports(1,&v);g.context->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        ID3D11ShaderResourceView* src[]={g.patchView.Get(),g.patchView.Get(),nullptr,g.adaptationViews[g.adaptationIndex].Get()};
        g.context->PSSetShaderResources(0,4,src);g.context->PSSetShader(g.glassShader.Get(),nullptr,0);g.context->Draw(3,0);Unbind();return Read(output.Get());
    }
    static void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
    static void Save(const char* name,const std::vector<Pixel>& image) {
        BITMAPFILEHEADER f{};BITMAPINFOHEADER h{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(h);f.bfSize=f.bfOffBits+W*H*4;
        h.biSize=sizeof(h);h.biWidth=W;h.biHeight=-H;h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;
        std::ofstream out(name,std::ios::binary);out.write(reinterpret_cast<char*>(&f),sizeof(f));out.write(reinterpret_cast<char*>(&h),sizeof(h));
        for(auto p:image){unsigned char b[4]={BYTE(std::clamp(p[2],0.f,1.f)*255),BYTE(std::clamp(p[1],0.f,1.f)*255),BYTE(std::clamp(p[0],0.f,1.f)*255),255};out.write(reinterpret_cast<char*>(b),4);}
    }
    void Run() {
        Background(0);Analyze();auto white=Draw();auto disabled=Draw(false);
        Require(white[(H/2)*W+W/2][0]>.999f,"white centre changed");
        Require(white[4*W+W/2][0]<.99f && white[4*W+W/2][0]>.86f,"white shoulder has no controlled headroom");
        Require(disabled[4*W+W/2][0]>.999f,"disabled adaptation changed white");
        Save("build/adaptive-checks/white.bmp",white);
        Background(1);Analyze(true,.2f);auto history=Read(g.adaptation[g.adaptationIndex].Get());
        Require(history[0][0]>.79f && history[0][0]<.81f,"temporal response broken");
        Require(history[0][3]<.001f,"flat scene transition mistaken for texture");
        Analyze();auto black=Draw();Require(black[(H/2)*W+W/2][0]<.001f,"black centre changed");
        for(auto p:black)for(float v:p)Require(std::isfinite(v)&&v>=0&&v<=1,"invalid output");
        Background(2);Analyze();auto flat=Read(g.adaptation[g.adaptationIndex].Get());
        Background(3);Analyze();auto busy=Read(g.adaptation[g.adaptationIndex].Get());
        double flatVariance=0,busyVariance=0;
        for(size_t i=0;i<flat.size();i++){flatVariance+=flat[i][3];busyVariance+=busy[i][3];}
        Require(std::abs(flatVariance/flat.size())<.001 && busyVariance/busy.size()>.08,"equal-mean texture not distinguished");
        Background(4);Analyze();Save("build/adaptive-checks/red-boundary.bmp",Draw());
        // A visible independent controller must not become a dark patch in the
        // adaptation field. The analysis uses the same repaired capture input.
        Background(0);
        for(int y=P+20;y<P+40;y++)for(int x=P+120;x<P+200;x++)pixels[size_t(y)*(W+2*P)+x]={0,0,0,1};
        g.context->UpdateSubresource(g.patch.Get(),0,nullptr,pixels.data(),(W+2*P)*sizeof(Pixel),0);
        // Match SetControlOccluders' one-pixel antialias guard.
        c.controlMasks[0][0]=P+119;c.controlMasks[0][1]=P+19;c.controlMasks[0][2]=P+201;c.controlMasks[0][3]=P+41;
        Analyze();auto masked=Read(g.adaptation[g.adaptationIndex].Get());
        for(auto p:masked)Require(p[0]>.999f && p[3]<.001f,"controller polluted background analysis");
        std::memset(c.controlMasks,0,sizeof(c.controlMasks));
        // Centre remains the raw background even while old brightness statistics
        // are deliberately retained: smoothing never blends captured frames.
        Background(0);Analyze(true,.01f);auto fresh=Draw();Require(fresh[(H/2)*W+W/2][0]>.999f,"background content ghosted");
        std::printf("Actual HLSL: white/black centre preserved; white shoulder %.3f; flat variance %.5f vs detail %.5f; temporal response and disable pass. Field %dx%d.\n",white[4*W+W/2][0],flatVariance/flat.size(),busyVariance/busy.size(),g.adaptationWidth,g.adaptationHeight);
    }
};
int main(){try{GlassAdaptationTest test;test.Run();return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
