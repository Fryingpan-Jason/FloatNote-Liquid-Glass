#pragma once
#include "markdown.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")

namespace Markdown {
// System text shaping gives the preview Unicode wrapping and accurate hit
// testing. The existing native EDIT still owns input, IME, undo and storage.
class Preview {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Row {
        Block block;
        Ptr<IDWriteTextLayout> text, marker;
        float x=0,y=0,height=0,markerX=0;
    };
    Ptr<IDWriteFactory> write;
    Ptr<ID2D1Factory> draw;
    Ptr<ID2D1DCRenderTarget> target;
    Ptr<ID2D1SolidColorBrush> ink;
    std::vector<Row> rows;
    float width=0,height=0,fontSize=0,total=0,scroll=0;
    bool ready=false;

    bool Initialize() {
        if(!write && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write.GetAddressOf()))))return false;
        if(!draw && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,draw.GetAddressOf())))return false;
        return true;
    }
    bool RenderTarget() {
        if(target)return true;
        if(!Initialize())return false;
        const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        if(FAILED(draw->CreateDCRenderTarget(&props,target.GetAddressOf())))return false;
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if(FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0),ink.ReleaseAndGetAddressOf()))) {
            target.Reset();return false;
        }
        return true;
    }
    static D2D1_COLOR_F Color(COLORREF color) {
        return D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f);
    }
    void Clamp() {scroll=std::clamp(scroll,0.f,std::max(0.f,total-height));}
public:
    bool Matches(int w,int h,float font) const {return ready && width==w && height==h && fontSize==font;}
    bool Build(const std::wstring& source,int w,int h,float font) {
        ready=false;rows.clear();total=0;
        width=static_cast<float>(w);height=static_cast<float>(h);fontSize=font;
        if(w<=0 || h<=0 || !Initialize())return false;
        const float scales[]={1.f,1.65f,1.4f,1.2f,1.1f,1.f,1.f};
        for(auto& block:Parse(source)) {
            Row row;row.block=std::move(block);
            const float size=font*scales[row.block.heading];
            Ptr<IDWriteTextFormat> format;
            if(FAILED(write->CreateTextFormat(row.block.code?L"Consolas":L"Microsoft YaHei UI",nullptr,
                row.block.heading?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,size,L"",format.GetAddressOf())))return false;
            row.x=font*(row.block.quote*.8f+std::min(row.block.indent,12)*1.1f+(row.block.code?.5f:0.f));
            row.markerX=row.x;
            if(!row.block.marker.empty()) {
                if(FAILED(write->CreateTextLayout(row.block.marker.c_str(),static_cast<UINT32>(row.block.marker.size()),
                    format.Get(),std::max(1.f,width),1000000.f,row.marker.GetAddressOf())))return false;
                DWRITE_TEXT_METRICS metrics{};row.marker->GetMetrics(&metrics);
                row.x+=std::max(font*1.3f,metrics.width+font*.4f);
            }
            row.x=std::min(row.x,std::max(0.f,width-font));
            if(FAILED(write->CreateTextLayout(row.block.text.c_str(),static_cast<UINT32>(row.block.text.size()),
                format.Get(),std::max(1.f,width-row.x),1000000.f,row.text.GetAddressOf())))return false;
            for(const auto& span:row.block.spans) {
                const DWRITE_TEXT_RANGE range{static_cast<UINT32>(span.start),static_cast<UINT32>(span.length)};
                if(span.style&Bold)row.text->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD,range);
                if(span.style&Italic)row.text->SetFontStyle(DWRITE_FONT_STYLE_ITALIC,range);
                if(span.style&Code)row.text->SetFontFamilyName(L"Consolas",range);
                if(span.style&Link)row.text->SetUnderline(TRUE,range);
                if(span.style&Strike)row.text->SetStrikethrough(TRUE,range);
            }
            DWRITE_TEXT_METRICS metrics{};
            if(FAILED(row.text->GetMetrics(&metrics)))return false;
            const float before=row.block.heading && !rows.empty()?font*.35f:0.f;
            row.y=total+before;row.height=std::max(metrics.height,font*1.25f);
            total=row.y+row.height+(row.block.heading?font*.25f:0.f);
            rows.push_back(std::move(row));
        }
        Clamp();ready=true;return true;
    }
    bool Paint(HDC dc,RECT bounds,COLORREF foreground,COLORREF background) {
        if(!ready || !RenderTarget() || FAILED(target->BindDC(dc,&bounds)))return false;
        target->BeginDraw();target->Clear(Color(background));ink->SetColor(Color(foreground));
        for(const auto& row:rows) {
            const float y=row.y-scroll;
            if(y+row.height<0 || y>height)continue;
            if(row.block.rule)target->DrawLine(D2D1::Point2F(0,y+fontSize*.6f),
                D2D1::Point2F(width,y+fontSize*.6f),ink.Get(),1.f);
            else {
                if(row.block.quote)target->FillRectangle(D2D1::RectF(0,y+2,2,y+row.height-2),ink.Get());
                if(row.block.code)target->DrawLine(D2D1::Point2F(1,y+2),
                    D2D1::Point2F(1,y+row.height-2),ink.Get(),1.f);
                if(row.marker)target->DrawTextLayout(D2D1::Point2F(row.markerX,y),row.marker.Get(),ink.Get());
                target->DrawTextLayout(D2D1::Point2F(row.x,y),row.text.Get(),ink.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        const HRESULT result=target->EndDraw();
        if(FAILED(result)){ink.Reset();target.Reset();return false;}
        return true;
    }
    bool Hit(POINT point,size_t& source) const {
        if(!ready || point.x<0 || point.x>=width || point.y<0 || point.y>=height)return false;
        if(rows.empty()){source=0;return true;}
        const float y=point.y+scroll;
        for(const auto& row:rows) {
            if(y<row.y || y>=row.y+row.height)continue;
            if(row.block.text.empty() || (row.marker && point.x>=row.markerX && point.x<row.x)) {
                source=row.block.start;return true;
            }
            BOOL trailing=FALSE,inside=FALSE;DWRITE_HIT_TEST_METRICS hit{};
            if(FAILED(row.text->HitTestPoint(point.x-row.x,y-row.y,&trailing,&inside,&hit)) || !inside ||
               hit.textPosition>=row.block.source.size() || iswspace(row.block.text[hit.textPosition]))return false;
            const size_t index=std::min(row.block.source.size()-1,size_t(hit.textPosition+(trailing?hit.length-1:0)));
            source=row.block.source[index]+(trailing?1:0);return true;
        }
        return false;
    }
    void Scroll(float delta) {scroll+=delta;Clamp();}
    float ScrollPosition() const {return scroll;}
    void ScrollToSource(size_t source) {
        for(const auto& row:rows) {
            if(source<row.block.start || source>=row.block.end)continue;
            const auto i=std::lower_bound(row.block.source.begin(),row.block.source.end(),source);
            FLOAT x=0,y=0;DWRITE_HIT_TEST_METRICS hit{};
            row.text->HitTestTextPosition(static_cast<UINT32>(i-row.block.source.begin()),FALSE,&x,&y,&hit);
            scroll=row.y+y;Clamp();return;
        }
    }
};
} // namespace Markdown
