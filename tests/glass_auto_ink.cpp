#define FLOATNOTE_ADAPTATION_FIXTURE
#include "glass_adaptation.cpp"

struct GlassAutoInkTest {
    using Fixture=GlassAdaptationTest;
    static void Require(bool ok,const char* message){Fixture::Require(ok,message);}
    static void Math() {
        std::array<unsigned char,400> bg{};
        auto fill=[&](int black){for(int i=0;i<100;i++)for(int c=0;c<4;c++)bg[i*4+c]=BYTE(i<black?0:255);};
        fill(60);Require(std::abs(GlassAutoInk::WhiteShare(bg.data(),40,10,10,RGB(255,255,255),0)-.6)<.001,"60% dark must win by area");
        Require(GlassAutoInk::WhiteShare(bg.data(),40,10,10,RGB(255,255,255),230)==0,"strong white tint ignored");
        Require(GlassAutoInk::WhiteShare(bg.data(),40,10,10,RGB(0,0,0),230)==1,"strong dark tint ignored");
        fill(0);Require(GlassAutoInk::WhiteShare(bg.data(),40,10,10,RGB(0,0,0),25)==0,"weak dark tint wrongly makes white ink");
        fill(100);Require(GlassAutoInk::WhiteShare(bg.data(),40,10,10,RGB(255,255,255),25)==1,"weak white tint wrongly makes dark ink");
        GlassAutoInk::Decision d;d.Reset();d.Update(.5,0);Require(!d.white,"tie must keep fallback");
        for(int i=1;i<100;i++)d.Update(i%2?.53:.47,i*100);
        Require(!d.white,"near tie flickered");d.Update(.65,10000);Require(!d.white,"moderate majority skipped stability delay");
        d.Update(.65,10100);Require(d.white,"sustained majority did not win");
        d.Update(0,10120);Require(!d.white,"unambiguous white background response delayed");
        d.Reset(true);d.Update(.5,10200);Require(d.white,"tie lost white fallback");
    }
    static int Wait(Fixture& f,COLORREF tint,int alpha) {
        int value=-1;
        const double start=GlassClockMs();
        while(GlassClockMs()-start<3000) {
            value=f.g.UpdateAutoInk(true,tint,alpha,false);
            if(!f.g.inkPending && f.g.inkDecision.valid)return value;
            Sleep(1);
        }
        throw std::runtime_error("asynchronous probe did not complete");
    }
    static void Setup(Fixture& f) {
        auto& g=f.g;const auto& s=g.CachedShaders();
        Fixture::Check(g.device->CreateVertexShader(s.ink->GetBufferPointer(),s.ink->GetBufferSize(),nullptr,&g.inkVertex));
        g.enabled=g.haveDesktop=true;g.mode=2;g.inkFrameValid=true;g.inkFrame=f.c;
        g.blurredViews[1]=g.patchView;g.SetInkRegion({40,40,280,120});
    }
    static void Fresh(Fixture& f,int background) {
        f.Background(background);f.Analyze();f.g.inkDirty=true;f.g.inkSubmittedAt=0;
    }
    static void Run() {
        Math();Fixture f;Setup(f);
        Fresh(f,1);Require(Wait(f,RGB(255,255,255),0)==1,"dark GPU background did not select white");
        Fresh(f,0);Require(Wait(f,RGB(0,0,0),0)==0,"white GPU background did not select dark");
        const auto count=f.g.inkSubmissions;
        for(int i=0;i<100;i++)f.g.UpdateAutoInk(true,RGB(0,0,0),0,false);
        Require(count==f.g.inkSubmissions,"static scene resubmitted work");
        Require(f.g.UpdateAutoInk(false,0,0,false)==-1,"manual colour not bypassed");
        Require(count==f.g.inkSubmissions,"disabled feature submitted GPU work");
        Fresh(f,1);f.g.adaptiveContrast=false;f.g.inkFrame.response[1]=0;
        Require(Wait(f,RGB(255,255,255),0)==1,"ink depends on contour switch");
        Fresh(f,0);Require(Wait(f,RGB(0,0,0),240)==1,"GPU probe tint composition broken");
        f.g.mode=1;Require(f.g.UpdateAutoInk(true,0,0,false)==-1,"frosted mode did not fall back");f.g.mode=2;
        f.g.failed=true;Require(f.g.UpdateAutoInk(true,0,0,false)==-1,"capture failure did not fall back");f.g.failed=false;
        // Region priority: most of the note is white, but the text rectangle is
        // over black. Use a large raw patch so the test shares real optics.
        f.Background(0);
        for(int y=Fixture::P+30;y<Fixture::P+130;y++)for(int x=Fixture::P+30;x<Fixture::P+290;x++)
            f.pixels[size_t(y)*(Fixture::W+2*Fixture::P)+x]={0,0,0,1};
        f.g.context->UpdateSubresource(f.g.patch.Get(),0,nullptr,f.pixels.data(),(Fixture::W+2*Fixture::P)*sizeof(Fixture::Pixel),0);
        f.g.inkDirty=true;f.g.inkSubmittedAt=0;
        Require(Wait(f,RGB(255,255,255),0)==1,"text region was not prioritized");
        // A configuration change must discard a queued sample's old generation.
        Fresh(f,0);f.g.UpdateAutoInk(true,0,0,false);Require(f.g.inkPending,"probe not queued");
        f.g.UpdateAutoInk(false,0,0,false);f.g.SetInkRegion({60,50,250,110});
        Require(Wait(f,RGB(255,255,255),0)==0,"stale sample survived configuration change");
        std::puts("PASS area voting, tint 0/10/90%, ties/hysteresis, real GPU text-region probe, disable/fallback, stale readback, static-scene gating.");
    }
    static void Benchmark() {
        Fixture f(true);Setup(f);Fresh(f,0);Wait(f,0,0);
        std::vector<double> cpu,gpu;
        for(int i=0;i<40;i++) {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint,start,end;
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};Fixture::Check(f.g.device->CreateQuery(&desc,&disjoint));
            desc.Query=D3D11_QUERY_TIMESTAMP;Fixture::Check(f.g.device->CreateQuery(&desc,&start));Fixture::Check(f.g.device->CreateQuery(&desc,&end));
            f.g.inkDirty=true;f.g.inkSubmittedAt=0;
            f.g.context->Begin(disjoint.Get());f.g.context->End(start.Get());
            const double at=GlassClockMs();f.g.UpdateAutoInk(true,0,0,false);cpu.push_back(GlassClockMs()-at);
            f.g.context->End(end.Get());f.g.context->End(disjoint.Get());f.g.context->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};UINT64 a=0,b=0;
            const double deadline=GlassClockMs()+3000;
            while(f.g.context->GetData(disjoint.Get(),&timing,sizeof(timing),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE && GlassClockMs()<deadline)Sleep(1);
            Require(GlassClockMs()<deadline,"GPU timing timeout");
            Fixture::Check(f.g.context->GetData(start.Get(),&a,sizeof(a),0));Fixture::Check(f.g.context->GetData(end.Get(),&b,sizeof(b),0));
            if(!timing.Disjoint && timing.Frequency && b>=a)gpu.push_back(double(b-a)*1000/timing.Frequency);
            Wait(f,0,0);
        }
        std::sort(cpu.begin(),cpu.end());std::sort(gpu.begin(),gpu.end());Require(!gpu.empty(),"no valid GPU timings");
        std::printf("Hardware probe incl submit/copy: CPU p95 %.4f ms; GPU p95 %.4f ms; %zu samples, max 10 probes/s, 2048 bytes/probe.\n",cpu[size_t(cpu.size()*.95)],gpu[size_t(gpu.size()*.95)],gpu.size());
        Require(cpu[size_t(cpu.size()*.95)]<1.0 && gpu[size_t(gpu.size()*.95)]<.5,"probe overhead exceeds local acceptance budget");
    }
};
int main(int argc,char** argv) {
    try {if(argc>1 && std::string(argv[1])=="--benchmark")GlassAutoInkTest::Benchmark();else GlassAutoInkTest::Run();return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
