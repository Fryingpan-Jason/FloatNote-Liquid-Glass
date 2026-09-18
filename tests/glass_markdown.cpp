// Hidden production-entry checks. No tray, capture, global hotkeys or user data.
#include "../experiments/local_desktop.cpp"
#include <iostream>
#include <stdexcept>

void Check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
    std::cout<<"PASS "<<message<<'\n';
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>()};
}
LRESULT CALLBACK Fixture(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_COMMAND || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORSTATIC ||
       message==WM_DRAWITEM || message==kMarkdownPreviewMessage || message==WM_ACTIVATE)
        return WindowProcedure(window,message,wp,lp);
    return DefWindowProcW(window,message,wp,lp);
}
void DrainPreview() {
    MSG message{};
    while(PeekMessageW(&message,g_window,kMarkdownPreviewMessage,kMarkdownPreviewMessage,PM_REMOVE))DispatchMessageW(&message);
}
void Snapshot(const wchar_t* name) {
    g_isVisible=true;RenderLayeredWindow();g_isVisible=false;
    std::vector<DWORD> pixels(g_surface.pixels,g_surface.pixels+g_surface.width*g_surface.height);
    for(auto& p:pixels)p|=0xff000000;
    Gdiplus::Bitmap png(g_surface.width,g_surface.height,g_surface.width*4,PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(pixels.data()));
    CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Check(png.Save((ExecutableDirectory()/name).c_str(),&encoder,nullptr)==Gdiplus::Ok,"rendered Markdown evidence saved");
}
int main() {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        SmoothGraphicsRuntime graphics;
        const std::wstring source=L"# 今天的便签\r\n普通文字与 **粗体**、*斜体*、~~删除线~~。\r\n"
            L"## 工作清单\r\n- [x] 完成液态玻璃\r\n- [ ] 支持 Markdown 自动预览\r\n"
            L"1. 点击文字开始编辑\r\n2. 点击别处回到预览\r\n"
            L"> 中文输入和 😀 emoji 都保留\r\n"
            L"行内 `code()` 与 [链接](https://example.com/a_(b))\r\n"
            L"```cpp\r\nconst auto glass = true;\r\n// **这里保持原样**\r\n```\r\n";
        const auto blocks=Markdown::Parse(source);
        Check(blocks.size()==10 && blocks[0].heading==1 && blocks[0].text==L"今天的便签","headings and source markers parse");
        Check(blocks[1].text==L"普通文字与 粗体、斜体、删除线。" && blocks[1].spans.size()==3,"bold italic and strikethrough render without delimiters");
        Check(blocks[3].marker==L"☑" && blocks[4].marker==L"☐" && blocks[5].marker==L"1.","task and ordered list markers parse");
        Check(blocks[7].quote==1 && blocks[8].text==L"行内 code() 与 链接","quotes inline code and balanced link destinations parse");
        Check(blocks[9].code && blocks[9].text==L"const auto glass = true;\n// **这里保持原样**","fenced code remains literal");
        bool mapping=true;
        for(const auto& block:blocks) {
            mapping=mapping && block.source.size()==block.text.size();
            for(size_t i=0;i<block.source.size();++i)
                mapping=mapping && block.source[i]<source.size() && (block.text[i]==L'\n' || source[block.source[i]]==block.text[i]);
        }
        Check(mapping,"every displayed UTF-16 character maps to unchanged Markdown source");
        Check(Markdown::Parse(L"\\*literal\\* snake_case **unfinished")[0].text==L"*literal* snake_case **unfinished",
              "escapes intraword underscores and unfinished markers remain readable");
        Check(Markdown::Parse(L"***both***")[0].spans[0].style==(Markdown::Bold|Markdown::Italic),"combined emphasis parses");
        Check(Markdown::Parse(L"**bold *italic*** / *italic **bold***")[0].text==L"bold italic / italic bold","nested emphasis closes the inner span first");
        Check(Markdown::Parse(L"```\n\nfirst\n\nlast\n```\n")[0].text==L"\nfirst\n\nlast","blank fenced-code lines keep their exact spacing");
        Check(Markdown::Parse(L"[unclosed [bracket")[0].text==L"[unclosed [bracket","unclosed links stay literal");
        Check(Markdown::Parse(L"![**image**](https://example.com/image.png)")[0].text==L"![**image**](https://example.com/image.png)","unsupported images stay literal and require no network access");

        g_instance=GetModuleHandleW(nullptr);g_isVisible=false;g_backdrop.mode=-1;
        g_settings.opacityPercent=100;g_settings.fontSize=14;
        g_dataDirectory=ExecutableDirectory()/L"fixture-data"/std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(g_dataDirectory);
        g_notePath=g_dataDirectory/L"note.txt";g_settingsPath=g_dataDirectory/L"settings.ini";
        WNDCLASSW klass{};klass.hInstance=g_instance;klass.lpfnWndProc=Fixture;
        klass.lpszClassName=L"FloatNote.MarkdownFixture";RegisterClassW(&klass);
        g_window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,klass.lpszClassName,L"Markdown fixture",
            WS_POPUP|WS_CLIPCHILDREN,0,0,MulDiv(620,GetDpiForSystem(),96),MulDiv(650,GetDpiForSystem(),96),nullptr,nullptr,g_instance,nullptr);
        Check(g_window!=nullptr,"isolated hidden window created");CreateControls(g_window);
        SetWindowTextW(g_edit,source.c_str());SavePendingNote();
        Check(!g_markdownEditing && EnsureMarkdownPreview(),"notes start in preview with a shaped Unicode layout");
        Snapshot(L"markdown-preview.png");
        size_t mapped=0;POINT click{};bool found=false;
        for(int y=0;y<70 && !found;++y)for(int x=0;x<200 && !found;++x) {
            if(g_markdownPreview.Hit({x,y},mapped) && mapped>=2 && mapped<8){click={x,y};found=true;}
        }
        Check(found,"preview title has a source-mapped text hit target");
        SendMessageW(g_edit,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(click.x,click.y));
        SendMessageW(g_edit,WM_LBUTTONUP,0,MAKELPARAM(click.x,click.y));
        DWORD start=0,end=0;SendMessageW(g_edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        Check(g_markdownEditing && start==mapped && end==mapped && EditorText()==source,"clicking rendered text enters source editing at its exact position");
        Snapshot(L"markdown-editing.png");
        SendMessageW(g_edit,EM_SETSEL,0,0);
        SendMessageW(g_edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"新增内容\r\n"));
        const auto modified=EditorText();
        Check(modified!=source && SendMessageW(g_edit,EM_CANUNDO,0,0),"native editing preserves Chinese text and undo");
        SetFocus(g_window);DrainPreview();
        Check(!g_markdownEditing && Read(g_notePath)==WideToUtf8(modified),"focus loss returns to preview and saves Markdown source");
        BeginMarkdownEditing();SendMessageW(g_edit,EM_UNDO,0,0);
        Check(EditorText()==source,"undo survives an edit-preview-edit round trip");
        SendMessageW(g_edit,WM_IME_STARTCOMPOSITION,0,0);
        FinishMarkdownEditing();
        Check(g_markdownEditing && g_markdownBlurPending,"preview waits for in-progress IME composition");
        SetFocus(g_window);
        SendMessageW(g_edit,WM_IME_ENDCOMPOSITION,0,0);DrainPreview();
        Check(!g_markdownEditing && Read(g_notePath)==WideToUtf8(source),"IME completion allows preview and saves committed source");
        BeginMarkdownEditing();SetFocus(nullptr);SendMessageW(g_window,WM_ACTIVATE,WA_INACTIVE,0);DrainPreview();
        Check(!g_markdownEditing,"switching to another application returns to preview");
        BeginMarkdownEditing();
        RECT before{},after{};GetWindowRect(g_window,&before);
        SendMessageW(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(3,3));
        SendMessageW(g_window,WM_LBUTTONUP,0,MAKELPARAM(3,3));GetWindowRect(g_window,&after);
        Check(!g_markdownEditing && EqualRect(&before,&after) && EditorText()==source,"blank clicks leave editing without moving the note or changing text");
        BeginMarkdownEditing();
        SendMessageW(g_grip,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(3,3));
        SendMessageW(g_grip,WM_LBUTTONUP,0,MAKELPARAM(3,3));
        Check(!g_markdownEditing,"resize grip clicks return to preview");

        std::wstring longNote;
        for(int i=0;i<120;++i)longNote+=L"## 第 "+std::to_wstring(i)+L" 行\r\n- **中文** and emoji 😀\r\n";
        SetWindowTextW(g_edit,longNote.c_str());EnsureMarkdownPreview();
        const float initialScroll=g_markdownPreview.ScrollPosition();
        SendMessageW(g_edit,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
        Check(g_markdownPreview.ScrollPosition()>initialScroll && !g_markdownEditing,"preview scrolls without activating source editing");
        SetWindowPos(g_window,nullptr,0,0,260,180,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);UpdateLayout(g_window);
        ChangeFontSize(2);
        Check(EnsureMarkdownPreview(),"narrow-window wrapping and font-size changes rebuild preview layout");
        SetWindowTextW(g_edit,L"");EnsureMarkdownPreview();
        SendMessageW(g_edit,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(5,5));
        Check(g_markdownEditing,"empty notes remain clickable for editing");
        SendMessageW(g_edit,WM_CHAR,L'中',0);FinishMarkdownEditing();
        Check(EditorText()==L"中" && Read(g_notePath)==WideToUtf8(L"中"),"empty-note edits save without Markdown conversion");
        g_loadingText=true;SetWindowTextW(g_edit,L"different");g_loadingText=false;LoadNote();
        Check(!g_markdownEditing && EditorText()==L"中","reloading retains source and preview mode");
        g_settings.opacityPercent=0;RefreshTheme();
        SetWindowTextW(g_edit,source.c_str());SetWindowPos(g_window,nullptr,0,0,620,640,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        UpdateLayout(g_window);g_isVisible=true;RenderLayeredWindow();g_isVisible=false;
        RECT editor{};GetWindowRect(g_edit,&editor);MapWindowPoints(nullptr,g_window,reinterpret_cast<POINT*>(&editor),2);
        int opaque=0,transparent=0;
        for(int y=editor.top;y<editor.bottom;++y)for(int x=editor.left;x<editor.right;++x) {
            const auto alpha=g_surface.pixels[y*g_surface.width+x]>>24;
            if(alpha>240)++opaque;if(alpha<=1)++transparent;
        }
        Check(opaque>100 && transparent>1000,"Markdown remains opaque over a transparent glass-compatible canvas");
        g_closing=true;DestroyWindow(g_window);g_window=g_edit=nullptr;CoUninitialize();
        std::cout<<"PASS Markdown parser, automatic preview, native editing, IME lifecycle, source persistence, undo, scrolling and alpha composition\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
