#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <cwctype>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include "platform.h"
#include "backdrop.h"
#ifdef FLOATNOTE_GLASS_LAB
#include "liquid_backdrop.h"
#include "glass_editor_layout.h"
#endif
#include "localization.h"
#include "version.h"
#include "markdown_preview.h"

namespace {

#ifdef FLOATNOTE_GLASS_LAB
#ifdef FLOATNOTE_LOCAL_DESKTOP
constexpr wchar_t kWindowClass[] = L"FloatNote.MainWindow";
constexpr wchar_t kWindowTitle[] = L"FloatNote";
constexpr wchar_t kMutexName[] = L"Local\\FloatNote.Singleton";
#else
constexpr wchar_t kWindowClass[] = L"FloatNote.GlassLab.MainWindow";
constexpr wchar_t kWindowTitle[] = L"FloatNote · Glass Lab";
constexpr wchar_t kMutexName[] = L"Local\\FloatNote.GlassLab.Singleton";
#endif
void OpenGlassLabControls();
void LoadGlassLabPreferences();
void RefreshGlassLabGeometry();
void CreateExperienceUI();
void DestroyExperienceUI();
void SyncExperienceUI();
void ToggleExperienceMenu();
void CloseExperienceMenu();
void RequestExperienceClose();
constexpr UINT kExperienceCloseRequest=WM_APP+75;
void ChooseExperienceColor(bool background);
void SaveExperienceMaterial();
constexpr UINT kExperienceChooseColor=WM_APP+76;
void BeginExperienceResize();
void FinishExperienceResize(const RECT* original=nullptr);
void MaybeAbsorbExperienceResize();
void TickExperienceAbsorb();
bool ExperienceAbsorbing();
bool ExperienceInputBlocked();
float ExperienceAbsorbOpacity();
void ExperienceSavedGeometry(RECT& rectangle);
void SyncExperienceCapture();
bool HandleExperienceKey(const MSG& message);
bool ExperienceCompact();
void ExpandExperienceNote();
void ApplyWindowStacking(bool force=false);
constexpr int kMinimumNoteWidth=76,kMinimumNoteHeight=24;
#else
constexpr wchar_t kWindowClass[] = L"FloatNote.MainWindow";
constexpr wchar_t kWindowTitle[] = L"FloatNote";
constexpr wchar_t kMutexName[] = L"Local\\FloatNote.Singleton";
constexpr int kMinimumNoteWidth=180,kMinimumNoteHeight=90;
#endif

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowEditMessage = WM_APP + 2;
constexpr UINT kRenderMessage = WM_APP + 3;
constexpr UINT kMarkdownPreviewMessage = WM_APP + 4;
constexpr UINT_PTR kCaretTimer = 3;
constexpr UINT_PTR kSaveTimer = 1;
constexpr UINT kHotkeyToggleMode = 1;
constexpr UINT kHotkeyToggleVisibility = 2;
constexpr UINT kHotkeyPassThrough = 3;

constexpr int kControlEdit = 101;
constexpr int kControlPill = 108;
constexpr int kControlGrip = 109;
constexpr int kControlOpacitySlider = 110;

constexpr int kMenuEditMode = 200;
constexpr int kMenuTopmost = 201;
constexpr int kMenuToggleVisible = 202;
constexpr int kMenuGlass = 203;
constexpr int kMenuAutostart = 204;
constexpr int kMenuExit = 205;
constexpr int kMenuPassThrough = 206;
constexpr int kMenuTheme = 207;
constexpr int kMenuLanguage = 208;
constexpr int kMenuLanguageAutomatic = 209;
constexpr int kMenuLanguageChinese = 210;
constexpr int kMenuLanguageEnglish = 211;
constexpr int kMenuTextColor = 212;
constexpr int kMenuAutoTextColor = 213;
constexpr int kMenuShadow = 214;
constexpr int kMenuThemeBase = 500;
constexpr int kMenuOpacityBase = 300;

struct Settings {
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 320;
    int height = 160;
    int fontSize = 13;
    int opacityPercent = 88;
    bool glass = false;
    bool topmost = false;
    bool passThrough = false;
    COLORREF themeColor = RGB(248, 247, 243);
    bool autoTextColor = true;
    COLORREF textColor = RGB(30, 32, 36);
    bool shadow = true;
    UiLanguage language = UiLanguage::Automatic;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_edit = nullptr;
HWND g_canvas = nullptr;
bool g_nativeGlass = false;
#ifdef FLOATNOTE_GLASS_LAB
GlassLabBackdrop g_backdrop;
#else
NativeBackdrop g_backdrop;
#endif
HWND g_pill = nullptr;
HWND g_grip = nullptr;
HWND g_menu = nullptr;
HWND g_slider = nullptr;
HWND g_menuTopmost = nullptr;
HWND g_menuAutostart = nullptr;
HWND g_menuHide = nullptr;
HWND g_menuExit = nullptr;
HWND g_menuGlass = nullptr;
HWND g_menuPassThrough = nullptr;
HWND g_menuTheme = nullptr;
HWND g_menuLanguage = nullptr;
HWND g_menuTextColor = nullptr;
HWND g_menuAutoTextColor = nullptr;
HWND g_menuShadow = nullptr;
HWND g_tooltip = nullptr;
int g_menuScroll = 0;
int g_menuWheel = 0;
int g_menuLayoutMode = -1;
bool g_pillHover = false;
bool g_pointerDown = false;
bool g_pointerDragged = false;
POINT g_pointerStart{};
RECT g_pointerWindow{};
int g_wheelRemainder = 0;
int g_scrollRemainder = 0;
ULONGLONG g_menuDismissedAt = 0;
COLORREF kBackground = RGB(248, 247, 243);
COLORREF kText = RGB(30, 32, 36);
COLORREF kNoteText = RGB(30, 32, 36);
int g_textMask = 0;
constexpr int kCornerRadius = 14;
const COLORREF kThemeColors[] = {RGB(248, 247, 243), RGB(220, 233, 219), RGB(221, 233, 245), RGB(243, 222, 227),
                                 RGB(38, 42, 48)};

struct SmoothGraphicsRuntime {
    ULONG_PTR token = 0;
    SmoothGraphicsRuntime() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }
    ~SmoothGraphicsRuntime() {
        if (token)
            Gdiplus::GdiplusShutdown(token);
    }
};

Gdiplus::Color DrawingColor(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

void SmoothRoundRect(HDC dc, RECT rect, float radius, COLORREF color, bool outline = false) {
    static SmoothGraphicsRuntime runtime;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const float x = static_cast<float>(rect.left) + (outline ? 0.5f : 0.0f);
    const float y = static_cast<float>(rect.top) + (outline ? 0.5f : 0.0f);
    const float width = static_cast<float>(rect.right - rect.left) - (outline ? 1.0f : 0.0f);
    const float height = static_cast<float>(rect.bottom - rect.top) - (outline ? 1.0f : 0.0f);
    const float diameter = std::min(radius * 2.0f, std::min(width, height));
    if (diameter <= 0)
        return;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
    if (outline) {
        Gdiplus::Pen pen(DrawingColor(color), 1.0f);
        graphics.DrawPath(&pen, &path);
    } else {
        Gdiplus::SolidBrush brush(DrawingColor(color));
        graphics.FillPath(&brush, &path);
    }
}
HFONT g_textFont = nullptr;
HFONT g_uiFont = nullptr;
HBRUSH g_editBrush = nullptr;
HANDLE g_singleInstanceMutex = nullptr;
HICON g_trayIcon = nullptr;
HICON g_appIcon = nullptr;
UINT g_taskbarCreatedMessage = 0;
Settings g_settings;
std::filesystem::path g_dataDirectory;
std::filesystem::path g_notePath;
std::filesystem::path g_settingsPath;
bool g_loadingText = false;
bool g_isVisible = true;
bool g_closing = false;
bool g_dirty = false;
bool g_saveFailed = false;
bool g_modeHotkey = false;
bool g_hideHotkey = false;
bool g_passHotkey = false;
bool g_settingsFailed = false;
bool g_loadFailed = false;
bool g_highContrast = false;
bool g_glassActive = false;
bool g_rendering = false;
bool g_renderPosted = false;
bool g_caretVisible = true;
#ifdef FLOATNOTE_DIAGNOSTICS
unsigned long long g_renderCount = 0;
#endif
std::wstring g_glassStatus;
std::wstring g_hitText;
bool g_hitTextDirty = true;
Markdown::Preview g_markdownPreview;
bool g_markdownEditing = false, g_markdownDirty = true;
bool g_markdownComposing = false, g_markdownBlurPending = false;
void RequestRender();
void RenderLayeredWindow();
void RefreshTheme();
void UpdateMenuLabels();
void RecoverWindowPosition();
void SetPassThrough(bool enabled);
void ResetCaret();
void ScrollMenuTo(int position);
void EnsureMenuFocusVisible(HWND control);
void LayoutMenuForMode();
void RefreshLocalizedUi();
const LocalizedStrings& Text() {
    return StringsFor(g_settings.language);
}
int MenuLogicalHeight() {
    return 584;
}
int MenuLogicalWidth() {
    return ResolveUiLanguage(g_settings.language) == UiLanguage::English ? 360 : 280;
}
int EffectiveOpacityPercent() {
    return g_settings.opacityPercent;
}
#ifdef FLOATNOTE_GLASS_LAB
int NoteMaximumCornerRadius() {
    RECT rect{0,0,g_settings.width,g_settings.height};
    if(g_window)GetClientRect(g_window,&rect);
    const float scale=(g_window?GetDpiForWindow(g_window):GetDpiForSystem())/96.0f;
    return GlassGeometry::MaximumRadius(float(rect.right),float(rect.bottom),scale);
}
#endif
int NoteCornerRadius() {
#ifdef FLOATNOTE_GLASS_LAB
    return std::clamp(static_cast<int>(std::lround(g_backdrop.cornerRadius)),std::min(8,NoteMaximumCornerRadius()),NoteMaximumCornerRadius());
#else
    return g_nativeGlass ? 8 : kCornerRadius;
#endif
}

int ScaleForDpi(HWND window, int value) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return MulDiv(value, static_cast<int>(dpi), 96);
}

std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::wstring ReadIniString(const wchar_t* key, const wchar_t* fallback) {
    wchar_t buffer[128]{};
    GetPrivateProfileStringW(L"FloatNote", key, fallback, buffer, ARRAYSIZE(buffer), g_settingsPath.c_str());
    return buffer;
}

int ReadIniInt(const wchar_t* key, int fallback) {
    const std::wstring value = ReadIniString(key, L"");
    if (value.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = wcstol(value.c_str(), &end, 10);
    return errno == ERANGE || end == value.c_str() || *end != L'\0' ? fallback : static_cast<int>(parsed);
}

bool ReadIniBool(const wchar_t* key, bool fallback) {
    return ReadIniInt(key, fallback ? 1 : 0) != 0;
}

void LoadSettings() {
    g_settings.x = ReadIniInt(L"x", CW_USEDEFAULT);
    g_settings.y = ReadIniInt(L"y", CW_USEDEFAULT);
    const bool compactLayout = ReadIniInt(L"layoutVersion", 0) >= 2;
    g_settings.width = compactLayout ? std::clamp(ReadIniInt(L"width", 320), kMinimumNoteWidth, 16384) : ScaleForDpi(nullptr, 320);
    g_settings.height = compactLayout ? std::clamp(ReadIniInt(L"height", 160), kMinimumNoteHeight, 16384) : ScaleForDpi(nullptr, 160);
    g_settings.fontSize = std::clamp(ReadIniInt(L"fontSize", 13), 8, 40);
    g_settings.opacityPercent = std::clamp(ReadIniInt(L"opacity", 88), 0, 100);
    g_settings.glass = ReadIniInt(L"visualVersion", 0) >= 3 ? ReadIniBool(L"glass", true) : true;
    g_settings.topmost = ReadIniBool(L"topmost", false);
    g_settings.passThrough = ReadIniBool(L"passThrough", false);
    g_settings.themeColor =
        static_cast<COLORREF>(std::clamp(ReadIniInt(L"themeColor", RGB(248, 247, 243)), 0, 0xffffff));
    g_settings.autoTextColor = ReadIniBool(L"autoTextColor", true);
    g_settings.textColor = static_cast<COLORREF>(std::clamp(ReadIniInt(L"textColor", RGB(30, 32, 36)), 0, 0xffffff));
    g_settings.shadow = ReadIniBool(L"shadow", true);
    g_settings.language = static_cast<UiLanguage>(std::clamp(ReadIniInt(L"language", 0), 0, 2));
}

void SaveSettings() {
    if (g_window && !IsIconic(g_window) && !g_closing) {
        RECT rectangle{};
        if (GetWindowRect(g_window, &rectangle)) {
#ifdef FLOATNOTE_GLASS_LAB
            ExperienceSavedGeometry(rectangle);
#endif
            g_settings.x = rectangle.left;
            g_settings.y = rectangle.top;
            g_settings.width = rectangle.right - rectangle.left;
            g_settings.height = rectangle.bottom - rectangle.top;
        }
    }
    std::string values = "[FloatNote]\r\n";
    auto add = [&](const char* key, int value) { values += std::string(key) + "=" + std::to_string(value) + "\r\n"; };
    add("x", g_settings.x);
    add("y", g_settings.y);
    add("width", g_settings.width);
    add("height", g_settings.height);
    add("opacity", g_settings.opacityPercent);
    add("glass", g_settings.glass);
    add("topmost", g_settings.topmost);
    add("fontSize", g_settings.fontSize);
    add("passThrough", g_settings.passThrough);
    add("themeColor", g_settings.themeColor);
    add("autoTextColor", g_settings.autoTextColor);
    add("textColor", g_settings.textColor);
    add("shadow", g_settings.shadow);
    add("language", static_cast<int>(g_settings.language));
    add("layoutVersion", 2);
    add("visualVersion", 4);
    g_settingsFailed = !AtomicWrite(g_settingsPath, values);
    RequestRender();
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), bytes, nullptr,
                        nullptr);
    return output;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int characters =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (characters <= 0) {
        characters = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring fallback(static_cast<size_t>(characters), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), fallback.data(), characters);
        return fallback;
    }
    std::wstring output(static_cast<size_t>(characters), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(),
                        characters);
    return output;
}

bool SaveNote();

std::wstring EditorText() {
    const int length = GetWindowTextLengthW(g_edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(g_edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

bool EnsureMarkdownPreview() {
    RECT bounds{};GetClientRect(g_edit,&bounds);
    const float size=static_cast<float>(MulDiv(g_settings.fontSize,GetDpiForWindow(g_edit),72));
    if(!g_markdownDirty && g_markdownPreview.Matches(bounds.right,bounds.bottom,size))return true;
    if(!g_markdownPreview.Build(EditorText(),bounds.right,bounds.bottom,size))return false;
    g_markdownDirty=false;return true;
}

bool PaintMarkdownPreview(HDC dc) {
    if(!EnsureMarkdownPreview())return false;
    RECT bounds{};GetClientRect(g_edit,&bounds);
    return g_markdownPreview.Paint(dc,bounds,g_textMask?RGB(255,255,255):kNoteText,
        g_textMask?(g_textMask==1?RGB(0,0,0):RGB(255,255,255)):kBackground);
}

std::wstring NormalizeNewlines(const std::wstring& input) {
    std::wstring output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == L'\r') {
            output += L"\r\n";
            if (i + 1 < input.size() && input[i + 1] == L'\n')
                ++i;
        } else if (input[i] == L'\n')
            output += L"\r\n";
        else
            output += input[i];
    }
    return output;
}

void LoadNote() {
    g_loadingText = true;
    g_loadFailed = false;
    std::ifstream input(g_notePath, std::ios::binary);
    const bool noteExists = input.good();
    std::error_code error;
    const bool existsOnDisk = std::filesystem::exists(g_notePath, error);
    const auto byteCount = existsOnDisk && !error ? std::filesystem::file_size(g_notePath, error) : 0;
    if (error || (!noteExists && existsOnDisk) || byteCount > 4 * 1024 * 1024) {
        g_loadFailed = true;
        SetWindowTextW(g_edit, byteCount > 4 * 1024 * 1024 && !error ? Text().readTooLarge : Text().readFailed);
        SendMessageW(g_edit, EM_SETREADONLY, TRUE, 0);
        g_loadingText = false;
        return;
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xef\xbb\xbf") == 0)
        bytes.erase(0, 3);
    if (bytes.find('\0') != std::string::npos || bytes.size() > 4 * 1024 * 1024 || input.bad()) {
        g_loadFailed = true;
        SetWindowTextW(g_edit, Text().unsupportedText);
        SendMessageW(g_edit, EM_SETREADONLY, TRUE, 0);
        g_loadingText = false;
        return;
    }
    const std::wstring text = input ? NormalizeNewlines(Utf8ToWide(bytes)) : Text().defaultNote;
    if (text.size() > 1024 * 1024) {
        g_loadFailed = true;
        SetWindowTextW(g_edit, Text().tooManyCharacters);
        SendMessageW(g_edit, EM_SETREADONLY, TRUE, 0);
        g_loadingText = false;
        return;
    }
    SendMessageW(g_edit, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(g_edit, text.c_str());
    SendMessageW(g_edit, EM_SETSEL, 0, 0);
    g_loadingText = false;
    if (!noteExists) {
        g_saveFailed = !SaveNote();
        g_dirty = g_saveFailed;
    }
    g_hitTextDirty = true;
}

bool SaveNote() {
    if (!IsWindow(g_edit) || g_loadFailed) {
        return false;
    }
    const std::wstring text = EditorText();
    const std::string utf8 = WideToUtf8(text);

    return AtomicWrite(g_notePath, utf8);
}

void CloseMenu();
void TogglePillMenu();

void RecreateFonts(HWND window) {
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    HFONT oldText = g_textFont;
    HFONT oldUi = g_uiFont;
    g_textFont = CreateFontW(-MulDiv(g_settings.fontSize, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_uiFont =
        CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    SendMessageW(g_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
    for (HWND control : {g_pill, g_grip, g_menuTopmost, g_menuAutostart, g_menuHide, g_menuExit, g_menuGlass,
                         g_menuPassThrough, g_menuTheme, g_menuLanguage, g_menuTextColor, g_menuAutoTextColor,
                         g_menuShadow})
        if (control)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    if (oldText)
        DeleteObject(oldText);
    if (oldUi)
        DeleteObject(oldUi);
    RequestRender();
}

void RoundWindow(HWND window, int radius) {
#ifdef FLOATNOTE_GLASS_LAB
    if (window == g_window) {
        // Text/tint and shader share the visible silhouette; the custom shader
        // gets a conservative HWND scissor so binary clipping cannot remove AA.
        // DWM's fixed small corner must not override the lab radius.
        const int corners = 1;
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
        radius = NoteCornerRadius();
    }
#else
    if (window == g_window) {
        // DWM's forced rounding also requests its shadow. Opt out and retain
        // our own rounded region when the user disables the shadow.
        const int corners = g_settings.shadow ? 2 : 1;
        DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
    }
    if (window == g_window && g_nativeGlass && g_settings.shadow) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }
    if (window == g_window && g_nativeGlass)
        radius = NoteCornerRadius();
#endif
    RECT rect{};
    GetWindowRect(window, &rect);
#ifdef FLOATNOTE_GLASS_LAB
    if(window==g_window && g_nativeGlass && g_backdrop.mode>0) {
        HRGN sampling=CreateGlassSamplingClip(rect.right-rect.left,rect.bottom-rect.top,ScaleForDpi(window,radius));
        if(sampling){if(!SetWindowRgn(window,sampling,TRUE))DeleteObject(sampling);return;}
    }
#endif
    HRGN region = CreateRoundRectRgn(0, 0, rect.right - rect.left + 1, rect.bottom - rect.top + 1,
                                     ScaleForDpi(window, radius * 2), ScaleForDpi(window, radius * 2));
    if (!SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}

void UpdateLayout(HWND window) {
    if (!g_edit || IsIconic(window))
        return;
    RECT rect{};
    GetClientRect(window, &rect);
    if (g_canvas)
        SetWindowPos(g_canvas, HWND_BOTTOM, 0, 0, rect.right, rect.bottom, SWP_NOACTIVATE);
    g_backdrop.Resize(rect.right, rect.bottom);
    SetWindowPos(g_pill, nullptr, (rect.right - ScaleForDpi(window, 46)) / 2, ScaleForDpi(window, 5),
                 ScaleForDpi(window, 46), ScaleForDpi(window, 18), SWP_NOZORDER | SWP_NOACTIVATE);
#ifdef FLOATNOTE_GLASS_LAB
    const auto layout=GlassEditorLayout::Calculate(rect.right,rect.bottom,float(NoteCornerRadius()),
        GetDpiForWindow(window)/96.0f);
    const bool compact=ExperienceCompact() || ExperienceAbsorbing() || rect.bottom<=ScaleForDpi(window,44);
    ShowWindow(g_pill,SW_HIDE);
    if(compact && GetFocus()==g_edit)SetFocus(g_window);
    ShowWindow(g_edit,compact?SW_HIDE:SW_SHOWNOACTIVATE);
    if(compact)SetWindowPos(g_edit,nullptr,0,0,1,1,SWP_NOZORDER|SWP_NOACTIVATE);
    else SetWindowPos(g_edit,nullptr,layout.editor.left,layout.editor.top,
        layout.editor.Width(),layout.editor.Height(),SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    // EDIT's formatting rectangle is separate from its HWND size. Refresh it
    // explicitly after resizing; this does not replace text or clear undo.
    RECT formatting{};GetClientRect(g_edit,&formatting);
    SendMessageW(g_edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&formatting));
    SetWindowPos(g_grip,HWND_TOP,layout.grip.left,layout.grip.top,
        layout.grip.Width(),layout.grip.Height(),SWP_NOACTIVATE);
    ShowWindow(g_grip,ExperienceCompact() || ExperienceAbsorbing()?SW_HIDE:SW_SHOWNOACTIVATE);
#else
    const int margin=ScaleForDpi(window,12),top=ScaleForDpi(window,29);
    SetWindowPos(g_edit,nullptr,margin,top,std::max(1L,rect.right-2*margin),
        std::max(1L,rect.bottom-top-margin),SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(g_edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
    SetWindowPos(g_grip,HWND_TOP,rect.right-ScaleForDpi(window,22),rect.bottom-ScaleForDpi(window,22),
        ScaleForDpi(window,18),ScaleForDpi(window,18),SWP_NOACTIVATE);
#endif
    RoundWindow(window, kCornerRadius);
#ifdef FLOATNOTE_GLASS_LAB
    RefreshGlassLabGeometry();
    SyncExperienceUI();
#endif
    InvalidateRect(window, nullptr, TRUE);
    RequestRender();
}

void RequestRender() {
    if (!g_window || g_rendering || g_renderPosted || g_closing)
        return;
    g_renderPosted = PostMessageW(g_window, kRenderMessage, 0, 0) != FALSE;
}

#ifdef FLOATNOTE_GLASS_LAB
void SyncAdaptiveInk() {
    static bool timer=false;
    static bool fadeTimer=false;
    static GlassAutoInk::Fade fade;
    static double lastInkPaint=0;
    const bool motion=ExperienceAbsorbing();
    const bool active=g_window && g_settings.autoTextColor && !g_highContrast && g_nativeGlass && g_glassActive &&
        g_backdrop.mode==2 && EffectiveOpacityPercent()<100 && g_isVisible && !IsIconic(g_window) && !ExperienceCompact();
    if(active && !motion && !timer){SetTimer(g_window,82,100,nullptr);timer=true;}
    else if((!active || motion) && timer){KillTimer(g_window,82);timer=false;}
    if(motion) {
        if(fadeTimer){KillTimer(g_window,83);fadeTimer=false;}
        fade.Reset(kNoteText);return; // Freeze ink through the cached-surface animation.
    }
    const int alpha=std::max(g_settings.passThrough?0:1,MulDiv(EffectiveOpacityPercent(),255,100));
    const int choice=g_backdrop.UpdateAutoInk(active,kBackground,alpha,kText==GlassAutoInk::Light);
    const COLORREF next=g_highContrast?kText:!g_settings.autoTextColor?g_settings.textColor:
        choice<0?kText:choice?GlassAutoInk::Light:GlassAutoInk::Dark;
    const double now=GlassClockMs();
    if(active && choice>=0)fade.Aim(next,kNoteText,now);
    else fade.Reset(next); // Explicit colours and accessibility changes apply immediately.
    const bool animating=active && fade.Running(now);
    if(animating && !fadeTimer){SetTimer(g_window,83,16,nullptr);fadeTimer=true;}
    else if(!animating && fadeTimer){KillTimer(g_window,83);fadeTimer=false;}
    // Capture and render-completion events also enter here. Cap fade updates
    // so they cannot turn a short animation into a self-posting repaint loop.
    const COLORREF presented=animating && now-lastInkPaint<16?kNoteText:fade.Sample(now);
    if(presented!=kNoteText) {
        lastInkPaint=now;
        kNoteText=presented;
        if(g_edit)InvalidateRect(g_edit,nullptr,FALSE);
        RequestRender();
    }
}
#endif

void RefreshTheme() {
    g_highContrast = SystemHighContrast();
    kBackground = g_highContrast ? GetSysColor(COLOR_WINDOW) : g_settings.themeColor;
    kText = g_highContrast ? GetSysColor(COLOR_WINDOWTEXT) : ContrastText(kBackground);
    kNoteText = g_highContrast || g_settings.autoTextColor ? kText : g_settings.textColor;
#ifdef FLOATNOTE_GLASS_LAB
    SyncAdaptiveInk();
#endif
    HBRUSH oldBrush = g_editBrush;
    g_editBrush = CreateSolidBrush(kBackground);
    if (oldBrush)
        DeleteObject(oldBrush);
    if (g_window)
        RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    if (g_menu)
        RedrawWindow(g_menu, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RequestRender();
}

void ApplyVisuals(HWND window) {
    const bool wasGlassActive = g_glassActive;
    g_glassActive = false;
    g_glassStatus = Text().glassOff;
    const int none = 1;
    DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
#ifdef FLOATNOTE_GLASS_LAB
    if (g_backdrop.mode>=0 && !ExperienceCompact()) {
#else
    if (g_settings.glass) {
#endif
        if (g_highContrast)
            g_glassStatus = Text().glassHighContrast;
        else if (GetSystemMetrics(SM_REMOTESESSION))
            g_glassStatus = Text().glassRemoteDesktop;
        else if (SystemEnergySaver())
            g_glassStatus = Text().glassEnergySaver;
        else if (!SystemTransparencyEnabled())
            g_glassStatus = Text().glassTransparencyOff;
        else if (!SystemComposition())
            g_glassStatus = Text().glassCompositionOff;
        else {
            g_glassActive = g_backdrop.Enable(window, true);
            g_glassStatus = g_glassActive ? Text().glassOn : Text().glassFallback;
        }
    }
    if (wasGlassActive && !g_glassActive)
        g_backdrop.Enable(window, false);
    const int rounded = g_settings.shadow ? 2 : 1;
    if (g_glassActive &&
        FAILED(DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(33), &rounded, sizeof(rounded)))) {
        g_backdrop.Enable(window, false);
        g_glassActive = false;
        g_glassStatus = Text().glassUnsupported;
    }
    if (g_nativeGlass != g_glassActive) {
        g_nativeGlass = g_glassActive;
        LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        SetWindowLongPtrW(window, GWL_EXSTYLE,
                          g_nativeGlass && !g_settings.passThrough ? ex & ~WS_EX_LAYERED : ex | WS_EX_LAYERED);
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        SetWindowLongPtrW(window, GWL_STYLE,
                          g_nativeGlass ? (style | WS_THICKFRAME) & ~WS_CLIPCHILDREN
                                        : (style & ~WS_THICKFRAME) | WS_CLIPCHILDREN);
        if (g_nativeGlass && g_settings.passThrough)
            SetLayeredWindowAttributes(window, 0, 254, LWA_ALPHA);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ShowWindow(g_canvas, g_nativeGlass ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    const int policy = g_nativeGlass && g_settings.shadow ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
    const COLORREF noBorder = 0xfffffffe;
    DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(34), &noBorder, sizeof(noBorder));
    const BOOL dark = Luminance(kBackground) < 0.18;
    DwmSetWindowAttribute(window, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
    const auto frameStyle = GetWindowLongPtrW(window, GWL_STYLE);
    const auto desiredStyle =
        g_nativeGlass && g_settings.shadow ? frameStyle | WS_THICKFRAME : frameStyle & ~WS_THICKFRAME;
    if (frameStyle != desiredStyle) {
        SetWindowLongPtrW(window, GWL_STYLE, desiredStyle);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    MARGINS margins{g_nativeGlass && g_settings.shadow ? -1 : 0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(window, &margins);
    RoundWindow(window, kCornerRadius);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    RequestRender();
#ifdef FLOATNOTE_GLASS_LAB
    ApplyWindowStacking();
    SyncExperienceUI();
#endif
    if (g_menu && IsWindowVisible(g_menu)) {
        SetLayeredWindowAttributes(g_menu, 0, 255, LWA_ALPHA);
        SetWindowPos(g_menu, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void ChangeFontSize(int delta) {
    const int size = std::clamp(g_settings.fontSize + delta, 8, 40);
    if (size == g_settings.fontSize)
        return;
    g_settings.fontSize = size;
    RecreateFonts(g_window);
    // Font changes never call SetWindowPos or change the outer window bounds.
    SaveSettings();
    if (g_menu)
        InvalidateRect(g_menu, nullptr, FALSE);
}

void ZoomFromWheel(WPARAM value) {
    g_wheelRemainder += GET_WHEEL_DELTA_WPARAM(value);
    const int steps = g_wheelRemainder / WHEEL_DELTA;
    g_wheelRemainder %= WHEEL_DELTA;
    if (steps)
        ChangeFontSize(steps);
}
void UpdateTrayTip();

void SavePendingNote() {
    KillTimer(g_window, kSaveTimer);
    if (g_dirty) {
        g_saveFailed = !SaveNote();
        if (!g_saveFailed)
            g_dirty = false;
    }
    InvalidateRect(g_window, nullptr, FALSE);
    if (g_menu)
        InvalidateRect(g_menu, nullptr, FALSE);
    RequestRender();
}

void BeginMarkdownEditing(size_t source=std::wstring::npos) {
    if(g_loadFailed || g_settings.passThrough)return;
    g_markdownEditing=true;g_markdownBlurPending=false;
    SetFocus(g_edit);
    if(source!=std::wstring::npos) {
        const auto caret=static_cast<WPARAM>(std::min(source,size_t(GetWindowTextLengthW(g_edit))));
        SendMessageW(g_edit,EM_SETSEL,caret,caret);
        SendMessageW(g_edit,EM_SCROLLCARET,0,0);
    }
    InvalidateRect(g_edit,nullptr,FALSE);ResetCaret();
}

void FinishMarkdownEditing() {
    if(!g_markdownEditing)return;
    if(g_markdownComposing){g_markdownBlurPending=true;return;}
    const auto line=SendMessageW(g_edit,EM_GETFIRSTVISIBLELINE,0,0);
    const auto source=SendMessageW(g_edit,EM_LINEINDEX,line,0);
    g_markdownEditing=false;g_markdownBlurPending=false;g_caretVisible=false;
    KillTimer(g_window,kCaretTimer);
    if(EnsureMarkdownPreview() && source>=0)g_markdownPreview.ScrollToSource(static_cast<size_t>(source));
    SavePendingNote();InvalidateRect(g_edit,nullptr,FALSE);RequestRender();
}

void LeaveMarkdownEditor() {
    // Native focus handling commits/cancels IME composition before we display
    // the preview. No source text is replaced, so native undo survives.
    if(GetFocus()==g_edit)SetFocus(g_window);
    FinishMarkdownEditing();
}

void ApplyInteractionMode(bool = false) {
    if (!g_window || g_closing)
        return;
    LONG_PTR style = GetWindowLongPtrW(g_window, GWL_EXSTYLE);
    style |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    if (g_nativeGlass && !g_settings.passThrough)
        style &= ~WS_EX_LAYERED;
    if (g_settings.passThrough)
        style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    else
        style &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtrW(g_window, GWL_EXSTYLE, style);
    if (g_nativeGlass && g_settings.passThrough)
        SetLayeredWindowAttributes(g_window, 0, 254, LWA_ALPHA);
    if (g_isVisible) {
#ifdef FLOATNOTE_GLASS_LAB
        if(!ExperienceCompact())ShowWindow(g_window,SW_SHOWNOACTIVATE);
#else
        SetWindowPos(g_window, (g_settings.topmost || g_settings.passThrough) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0,
                     0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
    }
    UpdateLayout(g_window);
    ApplyVisuals(g_window);
#ifdef FLOATNOTE_GLASS_LAB
    ApplyWindowStacking(true);
    SyncExperienceUI();
#endif
    UpdateTrayTip();
}

void EnterEditor() {
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceInputBlocked())return;
    if(ExperienceCompact()) {
        if(g_settings.passThrough)SetPassThrough(false);
        ExpandExperienceNote();return;
    }
#endif
    if (g_settings.passThrough) {
        g_settings.passThrough = false;
        SaveSettings();
    }
    RecoverWindowPosition();
    g_isVisible = true;
    ShowWindow(g_window, SW_SHOWNORMAL);
    ApplyInteractionMode();
    SetForegroundWindow(g_window);
    BeginMarkdownEditing();
}

void SetPassThrough(bool enabled) {
#ifndef FLOATNOTE_GLASS_LAB
    CloseMenu();
#endif
    SavePendingNote();
    if (enabled && (g_saveFailed || g_loadFailed)) {
        EnterEditor();
        return;
    }
    // Without a hotkey, the tray and second launch still provide a way back.
    g_settings.passThrough = enabled;
    if (enabled) {
        SetFocus(nullptr);
        FinishMarkdownEditing();
        KillTimer(g_window, kCaretTimer);
    }
    ApplyInteractionMode();
    SaveSettings();
    UpdateMenuLabels();
}

void SetTopmost(bool enabled) {
    g_settings.topmost = enabled;
    ApplyInteractionMode();
    SaveSettings();
}

void SetOpacityPercent(int opacity, bool persist = true) {
    g_settings.opacityPercent = std::clamp(opacity, 0, 100);
    ApplyVisuals(g_window);
    if (persist)
        SaveSettings();
    else
        SetTimer(g_window, 2, 300, nullptr);
}
std::filesystem::path StartupShortcutPath() {
    PWSTR startupFolder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &startupFolder))) {
        return {};
    }
    std::filesystem::path result = std::filesystem::path(startupFolder) / L"FloatNote.lnk";
    CoTaskMemFree(startupFolder);
    return result;
}

bool IsAutostartEnabled() {
    const auto shortcut = StartupShortcutPath();
    return !shortcut.empty() && GetFileAttributesW(shortcut.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool SetAutostartEnabled(bool enabled) {
#if defined(FLOATNOTE_GLASS_LAB) && !defined(FLOATNOTE_LOCAL_DESKTOP)
    (void)enabled;
    return false;
#else
    const auto shortcut = StartupShortcutPath();
    if (shortcut.empty()) {
        return false;
    }
    if (!enabled) {
        return DeleteFileW(shortcut.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(result)) {
        return false;
    }
    const std::filesystem::path executable = ExecutableDirectory() / L"FloatNote.exe";
    shellLink->SetPath(executable.c_str());
    shellLink->SetWorkingDirectory(ExecutableDirectory().c_str());
    shellLink->SetArguments(L"--startup");
    shellLink->SetDescription(Text().autostartDescription);

    IPersistFile* persistFile = nullptr;
    result = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
    if (SUCCEEDED(result)) {
        result = persistFile->Save(shortcut.c_str(), TRUE);
        persistFile->Release();
    }
    shellLink->Release();
    return SUCCEEDED(result);
#endif
}

HICON CreateNoteIcon(int size) {
    static SmoothGraphicsRuntime runtime;
    // Draw large, then downsample once so the tray-size outline stays smooth.
    size = std::clamp(size, 16, 128);
    Gdiplus::Bitmap canvas(size * 4, size * 4, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&canvas);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.ScaleTransform(size / 8.0f, size / 8.0f);
        Gdiplus::GraphicsPath note;
        note.AddArc(4.0f, 2.0f, 10.0f, 10.0f, 180.0f, 90.0f);
        note.AddArc(18.0f, 2.0f, 10.0f, 10.0f, 270.0f, 90.0f);
        note.AddArc(18.0f, 20.0f, 10.0f, 10.0f, 0.0f, 90.0f);
        note.AddArc(4.0f, 20.0f, 10.0f, 10.0f, 90.0f, 90.0f);
        note.CloseFigure();
        Gdiplus::SolidBrush paper(Gdiplus::Color(255, 248, 247, 239));
        Gdiplus::Pen outline(Gdiplus::Color(255, 87, 89, 88), 1.4f);
        graphics.FillPath(&paper, &note);
        graphics.DrawPath(&outline, &note);
        Gdiplus::Pen ink(Gdiplus::Color(255, 53, 55, 56), 2.4f);
        ink.SetStartCap(Gdiplus::LineCapRound);
        ink.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLine(&ink, 12.0f, 7.5f, 20.0f, 7.5f);
        graphics.DrawLine(&ink, 9.5f, 15.5f, 22.5f, 15.5f);
        graphics.DrawLine(&ink, 9.5f, 22.0f, 19.0f, 22.0f);
    }
    Gdiplus::Bitmap scaled(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&scaled);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(&canvas, 0, 0, size, size);
    }
    HICON icon = nullptr;
    scaled.GetHICON(&icon);
    return icon;
}

void AddTrayIcon() {
    if (!g_trayIcon)
        g_trayIcon = CreateNoteIcon(GetSystemMetrics(SM_CXSMICON));
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = g_trayIcon;
    wcscpy_s(data.szTip, L"FloatNote");
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void UpdateTrayTip() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_TIP;
    wcscpy_s(data.szTip, g_settings.passThrough ? Text().trayPassThrough : Text().trayNormal);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ToggleVisibility() {
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceInputBlocked())return;
#endif
    CloseMenu();
    if (g_isVisible && (IsWindowVisible(g_window)
#ifdef FLOATNOTE_GLASS_LAB
        || ExperienceCompact()
#endif
    )) {
        SavePendingNote();
        if (g_saveFailed) {
            EnterEditor();
            return;
        }
        g_isVisible = false;
        KillTimer(g_window, kCaretTimer);
        ShowWindow(g_window, SW_HIDE);
    } else {
        EnterEditor();
    }
#ifdef FLOATNOTE_GLASS_LAB
    SyncExperienceUI();
#endif
}

void ShowTrayMenu(POINT position) {
    HMENU menu = CreatePopupMenu();
    const auto shortcut = [](const wchar_t* label, const wchar_t* keys) {
#ifdef FLOATNOTE_GLASS_LAB
        (void)keys;return std::wstring(label);
#else
        return std::wstring(label) + L"\t" + keys;
#endif
    };
#ifdef FLOATNOTE_GLASS_LAB
    AppendMenuW(menu,MF_STRING,kControlPill,L"便签控制…");
#endif
    const auto editLabel = shortcut(Text().editNow, L"Ctrl+Alt+E");
    const auto passLabel = shortcut(Text().passThrough, L"Ctrl+Alt+P");
    const auto visibilityLabel = shortcut(g_isVisible ? Text().hideWindow : Text().showWindow, L"Ctrl+Alt+H");
    AppendMenuW(menu, MF_STRING, kMenuEditMode, editLabel.c_str());
    AppendMenuW(menu, MF_STRING | (g_settings.topmost ? MF_CHECKED : 0), kMenuTopmost, Text().alwaysOnTop);
    AppendMenuW(menu, MF_STRING | (g_settings.passThrough ? MF_CHECKED : 0), kMenuPassThrough, passLabel.c_str());
    AppendMenuW(menu, MF_STRING, kMenuToggleVisible, visibilityLabel.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    {
        HMENU opacityMenu = CreatePopupMenu();
        const int opacityOptions[] = {100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};
        for (int opacity : opacityOptions) {
            std::wstring label = std::to_wstring(opacity) + L"%";
            AppendMenuW(opacityMenu, MF_STRING | (g_settings.opacityPercent == opacity ? MF_CHECKED : 0),
                        kMenuOpacityBase + opacity, label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opacityMenu), Text().backgroundOpacity);
    }
#ifdef FLOATNOTE_GLASS_LAB
    AppendMenuW(menu,MF_STRING,kMenuGlass,L"材质实验…");
#else
    AppendMenuW(menu, MF_STRING | (g_settings.glass ? MF_CHECKED : 0), kMenuGlass, Text().glass);
#endif
    AppendMenuW(menu, MF_STRING, kMenuTheme, Text().customTheme);
    AppendMenuW(menu, MF_STRING, kMenuTextColor, Text().textColor);
    AppendMenuW(menu, MF_STRING | (g_settings.autoTextColor ? MF_CHECKED : 0), kMenuAutoTextColor, Text().autoTextColor);
    AppendMenuW(menu, MF_STRING | (g_settings.shadow ? MF_CHECKED : 0), kMenuShadow, Text().shadow);
    HMENU languageMenu = CreatePopupMenu();
    AppendMenuW(languageMenu, MF_STRING | (g_settings.language == UiLanguage::Automatic ? MF_CHECKED : 0),
                kMenuLanguageAutomatic, Text().languageAutomatic);
    AppendMenuW(languageMenu, MF_STRING | (g_settings.language == UiLanguage::Chinese ? MF_CHECKED : 0),
                kMenuLanguageChinese, Text().languageChinese);
    AppendMenuW(languageMenu, MF_STRING | (g_settings.language == UiLanguage::English ? MF_CHECKED : 0),
                kMenuLanguageEnglish, Text().languageEnglish);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu), Text().language);
#if !defined(FLOATNOTE_GLASS_LAB) || defined(FLOATNOTE_LOCAL_DESKTOP)
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, Text().autostart);
#endif
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, Text().exit);

    SetForegroundWindow(g_window);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN, position.x, position.y, 0,
                                       g_window, nullptr);
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command)
        PostMessageW(g_window, WM_COMMAND, command, 0);
}

void HandleMenuCommand(int command) {
#if defined(FLOATNOTE_GLASS_LAB) && !defined(FLOATNOTE_LOCAL_DESKTOP)
    if(command == kMenuAutostart) return;
#endif
    if (command >= kMenuOpacityBase && command <= kMenuOpacityBase + 100) {
        SetOpacityPercent(command - kMenuOpacityBase);
        return;
    }
    switch (command) {
    case kMenuEditMode:
        EnterEditor();
        break;
    case kMenuTopmost:
        SetTopmost(!g_settings.topmost);
        break;
    case kMenuToggleVisible:
        ToggleVisibility();
        break;
    case kMenuGlass:
#ifdef FLOATNOTE_GLASS_LAB
        OpenGlassLabControls();
#else
        g_settings.glass = !g_settings.glass;
        ApplyVisuals(g_window);
        SaveSettings();
        UpdateMenuLabels();
#endif
        break;
    case kMenuPassThrough:
        SetPassThrough(!g_settings.passThrough);
        break;
    case kMenuAutoTextColor:
        g_settings.autoTextColor = !g_settings.autoTextColor;
        if (!g_settings.autoTextColor)
            g_settings.textColor = kNoteText;
        RefreshTheme();
        SaveSettings();
        UpdateMenuLabels();
        break;
    case kMenuShadow:
        g_settings.shadow = !g_settings.shadow;
        ApplyVisuals(g_window);
        SaveSettings();
        UpdateMenuLabels();
        break;
    case kMenuTextColor:
    case kMenuTheme: {
        CloseMenu();
        static COLORREF custom[16]{};
        CHOOSECOLORW choose{sizeof(choose)};
        choose.hwndOwner = g_window;
        choose.rgbResult = command == kMenuTextColor ? kNoteText : g_settings.themeColor;
        choose.lpCustColors = custom;
        choose.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&choose)) {
            if (command == kMenuTextColor) {
                g_settings.textColor = choose.rgbResult;
                g_settings.autoTextColor = false;
            } else
                g_settings.themeColor = choose.rgbResult;
            RefreshTheme();
            ApplyVisuals(g_window);
            SaveSettings();
            UpdateMenuLabels();
        }
        break;
    }
    case kMenuAutostart:
        if (!SetAutostartEnabled(!IsAutostartEnabled())) {
            MessageBoxW(g_window, Text().autostartError, L"FloatNote", MB_OK | MB_ICONWARNING);
        }
        break;
    case kMenuLanguage:
        g_settings.language = static_cast<UiLanguage>((static_cast<int>(g_settings.language) + 1) % 3);
        RefreshLocalizedUi();
        SaveSettings();
        break;
    case kMenuLanguageAutomatic:
    case kMenuLanguageChinese:
    case kMenuLanguageEnglish:
        g_settings.language = static_cast<UiLanguage>(command - kMenuLanguageAutomatic);
        RefreshLocalizedUi();
        SaveSettings();
        break;
    case kMenuExit:
        SendMessageW(g_window, WM_CLOSE, 0, 0);
        break;
    default:
        if (command >= kMenuThemeBase && command < kMenuThemeBase + static_cast<int>(ARRAYSIZE(kThemeColors))) {
            g_settings.themeColor = kThemeColors[command - kMenuThemeBase];
            RefreshTheme();
            ApplyVisuals(g_window);
            SaveSettings();
            UpdateMenuLabels();
        }
        break;
    }
}

void DrawSurface(HWND window, HDC dc) {
    RECT rect{};
    GetClientRect(window, &rect);
    if (window == g_window && g_nativeGlass) {
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }
    FillRect(dc, &rect, g_editBrush);
    if (!(window == g_window && g_rendering))
        SmoothRoundRect(dc, rect, static_cast<float>(ScaleForDpi(window, kCornerRadius)),
                        BlendColor(kBackground, kText, 18), true);
}

void DrawControl(HWND parent, const DRAWITEMSTRUCT* item) {
    if (!item)
        return;
    const int saved = SaveDC(item->hDC);
    FillRect(item->hDC, &item->rcItem, g_editBrush);
    RECT rect = item->rcItem;
    if (parent == g_window && g_rendering && (item->CtlID == kControlPill || item->CtlID == kControlGrip)) {
        RestoreDC(item->hDC, saved);
        return;
    }
    if (item->CtlID >= kMenuThemeBase && item->CtlID < kMenuThemeBase + ARRAYSIZE(kThemeColors)) {
        const COLORREF swatch = kThemeColors[item->CtlID - kMenuThemeBase];
        InflateRect(&rect, -ScaleForDpi(parent, 3), -ScaleForDpi(parent, 3));
        SmoothRoundRect(item->hDC, rect, static_cast<float>(ScaleForDpi(parent, 8)), swatch);
        SmoothRoundRect(item->hDC, rect, static_cast<float>(ScaleForDpi(parent, 8)),
                        BlendColor(swatch, ContrastText(swatch), 30), true);
        if (g_settings.themeColor == swatch) {
            SelectObject(item->hDC, g_uiFont);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, ContrastText(swatch));
            DrawTextW(item->hDC, L"✓", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (item->itemState & ODS_FOCUS)
            DrawFocusRect(item->hDC, &rect);
        RestoreDC(item->hDC, saved);
        return;
    }
    if (item->CtlID == kControlGrip) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleForDpi(parent, 1)), BlendColor(kBackground, kNoteText, 40));
        SelectObject(item->hDC, pen);
        for (int offset : {5, 9}) {
            MoveToEx(item->hDC, rect.right - ScaleForDpi(parent, offset + 3), rect.bottom - ScaleForDpi(parent, 4),
                     nullptr);
            LineTo(item->hDC, rect.right - ScaleForDpi(parent, 4), rect.bottom - ScaleForDpi(parent, offset + 3));
        }
        RestoreDC(item->hDC, saved);
        DeleteObject(pen);
        return;
    }
    const bool pill = item->CtlID == kControlPill;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF color =
        pill ? BlendColor(kBackground, kText, g_pillHover ? 82 : 92) : BlendColor(kBackground, kText, pressed ? 15 : 6);
    if (pill)
        InflateRect(&rect, 0, -ScaleForDpi(parent, 3));
    SmoothRoundRect(item->hDC, rect,
                    pill ? (rect.bottom - rect.top) / 2.0f : static_cast<float>(ScaleForDpi(parent, 7)), color);
    if (pill) {
        const COLORREF dots = (g_saveFailed || g_settingsFailed || g_loadFailed) ? RGB(255, 162, 131)
                              : g_settings.passThrough                           ? RGB(129, 201, 233)
                                                                                 : kBackground;
        const int mid = (rect.left + rect.right) / 2;
        const int y = (rect.top + rect.bottom) / 2;
        const int dot = std::max(2, ScaleForDpi(parent, 2));
        for (int x : {-6, 0, 6}) {
            const int center = mid + ScaleForDpi(parent, x);
            RECT dotRect{center - dot / 2, y - dot / 2, center + dot / 2 + 1, y + dot / 2 + 1};
            SmoothRoundRect(item->hDC, dotRect, (dotRect.bottom - dotRect.top) / 2.0f, dots);
        }
    } else {
        SelectObject(item->hDC, g_uiFont);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, item->CtlID == kMenuExit
                                    ? (Luminance(kBackground) < 0.18 ? RGB(255, 165, 152) : RGB(157, 53, 44))
                                    : kText);
        wchar_t text[80]{};
        GetWindowTextW(item->hwndItem, text, ARRAYSIZE(text));
        InflateRect(&rect, -ScaleForDpi(parent, 10), 0);
        DrawTextW(item->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&rect, -2, -2);
        DrawFocusRect(item->hDC, &rect);
    }
    RestoreDC(item->hDC, saved);
}

int SliderPixelFromValue(int value, int left, int right) {
    return left + MulDiv(std::clamp(value, 0, 100), right - left, 100);
}

int SliderValueFromPixel(int x, int left, int right) {
    return MulDiv(std::clamp(x, left, right) - left, 100, std::max(1, right - left));
}

void SliderSetFromPointer(HWND window, int x) {
    RECT rect{};
    GetClientRect(window, &rect);
    const int padding = ScaleForDpi(window, 10);
    const int value = SliderValueFromPixel(x, padding, std::max(padding + 1, static_cast<int>(rect.right) - padding));
    SendMessageW(window, TBM_SETPOS, TRUE, value);
}
LRESULT CALLBACK SliderProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL && g_menu && (GetWindowLongPtrW(g_menu, GWL_STYLE) & WS_VSCROLL))
        return SendMessageW(g_menu, message, wp, lp);
    // Pointer handling and drawing use exactly the same coordinate mapping.
    // Native trackbar hit-testing must not use its differently positioned thumb.
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
        SetFocus(window);
        SetCapture(window);
        SliderSetFromPointer(window, GET_X_LPARAM(lp));
        return 0;
    }
    if (message == WM_MOUSEMOVE && GetCapture() == window) {
        SliderSetFromPointer(window, GET_X_LPARAM(lp));
        return 0;
    }
    if (message == WM_LBUTTONUP && GetCapture() == window) {
        SliderSetFromPointer(window, GET_X_LPARAM(lp));
        ReleaseCapture();
        KillTimer(g_window, 2);
        SaveSettings();
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT rect{}, channel{};
        GetClientRect(window, &rect);
        channel.left = ScaleForDpi(window, 10);
        channel.right = std::max(channel.left + 1, rect.right - channel.left);

        FillRect(dc, &rect, g_editBrush);
        const int centerY = rect.bottom / 2;
        const int centerX =
            SliderPixelFromValue(static_cast<int>(SendMessageW(window, TBM_GETPOS, 0, 0)), channel.left, channel.right);
        const int thickness = ScaleForDpi(window, 4);
        HPEN pen = CreatePen(PS_SOLID, thickness, BlendColor(kBackground, kText, 15));
        auto oldPen = SelectObject(dc, pen);
        MoveToEx(dc, channel.left, centerY, nullptr);
        LineTo(dc, channel.right, centerY);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        pen = CreatePen(PS_SOLID, thickness, BlendColor(kBackground, kText, 65));
        SelectObject(dc, pen);
        MoveToEx(dc, channel.left, centerY, nullptr);
        LineTo(dc, centerX, centerY);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        HBRUSH fill = CreateSolidBrush(kBackground);
        pen = CreatePen(PS_SOLID, GetFocus() == window ? 2 : 1, BlendColor(kBackground, kText, 55));
        auto oldBrush = SelectObject(dc, fill);
        SelectObject(dc, pen);
        const int radius = ScaleForDpi(window, 7);
        Ellipse(dc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);
        EndPaint(window, &ps);
        return 0;
    }
    const LRESULT result = DefSubclassProc(window, message, wp, lp);
    if (message == TBM_SETPOS) {
        const int position = static_cast<int>(SendMessageW(window, TBM_GETPOS, 0, 0));
        if (position != g_settings.opacityPercent) {
            SetOpacityPercent(position, false);
            InvalidateRect(GetParent(window), nullptr, FALSE);
        }
    }
    if (message == TBM_SETPOS || message == WM_KEYDOWN || message == WM_MOUSEWHEEL) {
        // The native control invalidates only its own thumb rectangle. Our
        // rounded thumb sits elsewhere, so repaint the entire custom track now.
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
    return result;
}

void UpdateMenuLabels() {
#ifdef FLOATNOTE_GLASS_LAB
    SyncExperienceUI();
#endif
    if (!g_menu)
        return;
    LayoutMenuForMode();
    const auto stateLabel = [](const wchar_t* label, const wchar_t* state) {
        return std::wstring(label) + L"     " + state;
    };
    const auto topmost = stateLabel(Text().alwaysOnTop, g_settings.topmost ? Text().stateOn : Text().stateOff);
    const auto autostart = stateLabel(Text().autostart, IsAutostartEnabled() ? Text().stateOn : Text().stateOff);
#ifdef FLOATNOTE_GLASS_LAB
    const std::wstring glass=L"材质实验…";
#else
    const auto glass = stateLabel(Text().glass, g_glassActive      ? Text().stateOn
                                                : g_settings.glass ? Text().stateSelected
                                                                   : Text().stateOff);
#endif
    const auto passThrough = stateLabel(Text().passThrough, g_settings.passThrough ? Text().stateOn
                                                            : g_passHotkey         ? L"Ctrl+Alt+P"
                                                                                   : Text().menuToggle);
    SetWindowTextW(g_menuTopmost, topmost.c_str());
    SetWindowTextW(g_menuAutostart, autostart.c_str());
    SetWindowTextW(g_menuGlass, glass.c_str());
    SetWindowTextW(g_menuPassThrough, passThrough.c_str());
    wchar_t theme[80]{};
    swprintf_s(theme, L"%s    #%02X%02X%02X", Text().customTheme, GetRValue(g_settings.themeColor),
               GetGValue(g_settings.themeColor), GetBValue(g_settings.themeColor));
    SetWindowTextW(g_menuTheme, theme);
    swprintf_s(theme, L"%s    #%02X%02X%02X", Text().textColor, GetRValue(kNoteText), GetGValue(kNoteText),
               GetBValue(kNoteText));
    SetWindowTextW(g_menuTextColor, theme);
    const auto autoText = stateLabel(Text().autoTextColor, g_settings.autoTextColor ? Text().stateOn : Text().stateOff);
    SetWindowTextW(g_menuAutoTextColor, autoText.c_str());
    const auto shadow = stateLabel(Text().shadow, g_settings.shadow ? Text().stateOn : Text().stateOff);
    SetWindowTextW(g_menuShadow, shadow.c_str());
    const wchar_t* languageValue = g_settings.language == UiLanguage::Automatic ? Text().languageAutomatic
                                   : g_settings.language == UiLanguage::Chinese ? Text().languageChinese
                                                                                : Text().languageEnglish;
    const auto language = stateLabel(Text().language, languageValue);
    SetWindowTextW(g_menuLanguage, language.c_str());
    SendMessageW(g_slider, TBM_SETPOS, TRUE, g_settings.opacityPercent);
    InvalidateRect(g_menu, nullptr, FALSE);
}

void RefreshLocalizedUi() {
    if (g_pill)
        SetWindowTextW(g_pill, Text().pillLabel);
    if (g_grip)
        SetWindowTextW(g_grip, Text().gripLabel);
    if (g_menu) {
        SetWindowTextW(g_menu, Text().settingsWindowTitle);
        for (int i = 0; i < static_cast<int>(ARRAYSIZE(kThemeColors)); ++i)
            SetWindowTextW(GetDlgItem(g_menu, kMenuThemeBase + i), Text().themeNames[i]);
        SetWindowTextW(g_menuHide, Text().hideWindow);
        SetWindowTextW(g_menuExit, Text().exit);
        SetWindowTextW(g_slider, Text().sliderLabel);
        g_menuLayoutMode = -1;
    }
    if (g_tooltip) {
        TOOLINFOW tip{sizeof(tip)};
        tip.uFlags = TTF_IDISHWND;
        tip.hwnd = g_window;
        tip.uId = reinterpret_cast<UINT_PTR>(g_pill);
        tip.lpszText = const_cast<wchar_t*>(Text().tooltip);
        SendMessageW(g_tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tip));
    }
    ApplyVisuals(g_window);
    UpdateTrayTip();
    UpdateMenuLabels();
}

void CloseMenu() {
#ifdef FLOATNOTE_GLASS_LAB
    CloseExperienceMenu();
#endif
    if (g_menu && IsWindowVisible(g_menu)) {
        g_menuDismissedAt = GetTickCount64();
        ShowWindow(g_menu, SW_HIDE);
        InvalidateRect(g_pill, nullptr, FALSE);
    }
}

void UpdateMenuScroll() {
    if (!g_menu)
        return;
    RECT client{};
    GetClientRect(g_menu, &client);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = ScaleForDpi(g_window, MenuLogicalHeight()) - 1;
    info.nPage = client.bottom;
    info.nPos = g_menuScroll;
    SetScrollInfo(g_menu, SB_VERT, &info, TRUE);
}
void ScrollMenuTo(int position) {
    if (!g_menu)
        return;
    RECT client{};
    GetClientRect(g_menu, &client);
    const int maximum = std::max(0, ScaleForDpi(g_window, MenuLogicalHeight()) - static_cast<int>(client.bottom));
    position = std::clamp(position, 0, maximum);
    const int delta = g_menuScroll - position;
    g_menuScroll = position;
    if (delta)
        ScrollWindowEx(g_menu, 0, delta, nullptr, nullptr, nullptr, nullptr,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateMenuScroll();
}
void EnsureMenuFocusVisible(HWND control) {
    if (!g_menu || !IsChild(g_menu, control))
        return;
    RECT child{}, client{};
    GetWindowRect(control, &child);
    GetClientRect(g_menu, &client);
    MapWindowPoints(nullptr, g_menu, reinterpret_cast<POINT*>(&child), 2);
    const int margin = ScaleForDpi(g_window, 6);
    if (child.top < margin)
        ScrollMenuTo(g_menuScroll + child.top - margin);
    else if (child.bottom > client.bottom - margin)
        ScrollMenuTo(g_menuScroll + child.bottom - client.bottom + margin);
}

void LayoutMenuForMode() {
    if (!g_menu || g_menuLayoutMode == static_cast<int>(g_glassActive))
        return;
    g_menuLayoutMode = static_cast<int>(g_glassActive);
    ScrollMenuTo(0);
    const int logicalWidth = MenuLogicalWidth();
    const int halfWidth = (logicalWidth - 40) / 2;
    const int themeGap = (logicalWidth - 32 - 5 * 44) / 4;
    const std::pair<int, int> rows[] = {
        {kMenuGlass, 104},         {kMenuTopmost, 166},       {kMenuPassThrough, 202},
        {kMenuTheme, 238},         {kMenuThemeBase, 274},     {kMenuThemeBase + 1, 274},
        {kMenuThemeBase + 2, 274}, {kMenuThemeBase + 3, 274}, {kMenuThemeBase + 4, 274},
        {kMenuTextColor, 316},     {kMenuAutoTextColor, 352}, {kMenuShadow, 388},
        {kMenuLanguage, 424},      {kMenuAutostart, 460},     {kMenuToggleVisible, 496},
        {kMenuExit, 496}};
    for (const auto& row : rows) {
        HWND child = GetDlgItem(g_menu, row.first);
        int x = 16;
        int width = logicalWidth - 32;
        if (row.first >= kMenuThemeBase && row.first < kMenuThemeBase + 5) {
            x = 16 + (row.first - kMenuThemeBase) * (44 + themeGap);
            width = 44;
        } else if (row.first == kMenuToggleVisible) {
            width = halfWidth;
        } else if (row.first == kMenuExit) {
            x = 24 + halfWidth;
            width = halfWidth;
        }
        SetWindowPos(child, nullptr, ScaleForDpi(g_window, x), ScaleForDpi(g_window, row.second),
                     ScaleForDpi(g_window, width), ScaleForDpi(g_window, 30), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetWindowPos(g_slider, nullptr, ScaleForDpi(g_window, 12), ScaleForDpi(g_window, 70),
                 ScaleForDpi(g_window, logicalWidth - 24), ScaleForDpi(g_window, 26), SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(g_slider, SW_SHOWNOACTIVATE);
    if (IsWindowVisible(g_menu)) {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT bounds{};
        GetWindowRect(g_menu, &bounds);
        const int preferred = ScaleForDpi(g_window, MenuLogicalHeight());
        const int height = std::min(preferred, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) -
                                                   ScaleForDpi(g_window, 12));
        const bool scrolling = height < preferred;
        LONG_PTR style = GetWindowLongPtrW(g_menu, GWL_STYLE);
        SetWindowLongPtrW(g_menu, GWL_STYLE, scrolling ? style | WS_VSCROLL : style & ~WS_VSCROLL);
        const int width = ScaleForDpi(g_window, logicalWidth) +
                          (scrolling ? GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(g_window)) : 0);
        bounds.right = bounds.left + width;
        bounds.bottom = bounds.top + height;
        bounds = ConstrainToWorkArea(bounds, monitor.rcWork);
        SetWindowPos(g_menu, nullptr, bounds.left, bounds.top, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    RedrawWindow(g_menu, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

LRESULT CALLBACK MenuProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCPAINT:
        if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL))
            return 0;
        break;
    case WM_SIZE:
        UpdateMenuScroll();
        RoundWindow(window, kCornerRadius);
        return 0;
    case WM_DPICHANGED:
        RoundWindow(window, kCornerRadius);
        return 0;
    case WM_MOUSEWHEEL:
        g_menuWheel += GET_WHEEL_DELTA_WPARAM(wp);
        ScrollMenuTo(g_menuScroll - (g_menuWheel / WHEEL_DELTA) * ScaleForDpi(g_window, 36));
        g_menuWheel %= WHEEL_DELTA;
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int value = g_menuScroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            value -= ScaleForDpi(g_window, 24);
            break;
        case SB_LINEDOWN:
            value += ScaleForDpi(g_window, 24);
            break;
        case SB_PAGEUP:
            value -= info.nPage;
            break;
        case SB_PAGEDOWN:
            value += info.nPage;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            value = info.nTrackPos;
            break;
        case SB_TOP:
            value = 0;
            break;
        case SB_BOTTOM:
            value = info.nMax;
            break;
        }
        ScrollMenuTo(value);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE)
            CloseMenu();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            CloseMenu();
            return 0;
        }
        break;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == g_slider) {
            SetOpacityPercent(static_cast<int>(SendMessageW(g_slider, TBM_GETPOS, 0, 0)));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_COMMAND: {
        const int command = LOWORD(wp);
        if (command == kMenuTopmost || command == kMenuAutostart || command == kMenuGlass || command == kMenuLanguage ||
            command == kMenuAutoTextColor || command == kMenuShadow ||
            (command >= kMenuThemeBase && command < kMenuThemeBase + static_cast<int>(ARRAYSIZE(kThemeColors)))) {
            HandleMenuCommand(command);
            UpdateMenuLabels();
        } else {
            CloseMenu();
            PostMessageW(g_window, WM_COMMAND, command, 0);
        }
        return 0;
    }
    case WM_DRAWITEM:
        DrawControl(window, reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        DrawSurface(window, dc);
        const int saved = SaveDC(dc);
        SetViewportOrgEx(dc, 0, -g_menuScroll, nullptr);
        SelectObject(dc, g_uiFont);
        SetTextColor(dc, kText);
        SetBkMode(dc, TRANSPARENT);
        RECT title{ScaleForDpi(window, 16), ScaleForDpi(window, 13), ScaleForDpi(window, MenuLogicalWidth() - 16),
                   ScaleForDpi(window, 36)};
        DrawTextW(dc, Text().settingsTitle, -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        {
            RECT label{ScaleForDpi(window, 16), ScaleForDpi(window, 46), ScaleForDpi(window, MenuLogicalWidth() - 16),
                       ScaleForDpi(window, 67)};
            DrawTextW(dc, Text().backgroundOpacity, -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            std::wstring opacity = std::to_wstring(g_settings.opacityPercent) + L"%";
            DrawTextW(dc, opacity.c_str(), -1, &label, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        RECT glassHint{ScaleForDpi(window, 16), ScaleForDpi(window, 139),
                       ScaleForDpi(window, MenuLogicalWidth() - 16), ScaleForDpi(window, 160)};
        SetTextColor(dc, BlendColor(kBackground, kText, 70));
        DrawTextW(dc,
                  g_glassActive       ? Text().glassHint
                  : !g_settings.glass ? Text().normalHint
                                      : g_glassStatus.c_str(),
                  -1, &glassHint, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT hint{ScaleForDpi(window, 12), ScaleForDpi(window, 537),
                  ScaleForDpi(window, MenuLogicalWidth() - 12), ScaleForDpi(window, 576)};
        const bool error = g_saveFailed || g_settingsFailed || g_loadFailed;
        SetTextColor(dc, error ? (Luminance(kBackground) < 0.18 ? RGB(255, 165, 152) : RGB(157, 53, 44))
                               : BlendColor(kBackground, kText, 70));
        std::wstring text = g_loadFailed   ? Text().loadErrorStatus
                            : g_saveFailed ? Text().saveErrorStatus
                            : g_settingsFailed
                                ? Text().settingsErrorStatus
                                : std::wstring(Text().fontHint) + L" · " + std::to_wstring(g_settings.fontSize) +
                                      L" pt\n" + (g_modeHotkey ? Text().restoreEditHint : Text().shortcutConflictHint);
        DrawTextW(dc, text.c_str(), -1, &hint, DT_CENTER | DT_WORDBREAK);
        RestoreDC(dc, saved);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        CloseMenu();
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

void TogglePillMenu() {
#ifdef FLOATNOTE_GLASS_LAB
    ToggleExperienceMenu();return;
#endif
    if (g_menu && IsWindowVisible(g_menu)) {
        CloseMenu();
        return;
    }
    if (GetTickCount64() - g_menuDismissedAt < 200)
        return;
    if (!g_menu) {
        g_menuScroll = 0;
        g_menuWheel = 0;
        g_menuLayoutMode = -1;
        WNDCLASSW klass{};
        klass.lpfnWndProc = MenuProcedure;
        klass.hInstance = g_instance;
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.lpszClassName = L"FloatNote.PillMenu";
        RegisterClassW(&klass);
        g_menu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, klass.lpszClassName,
                                 Text().settingsWindowTitle, WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                                 ScaleForDpi(g_window, MenuLogicalWidth()), ScaleForDpi(g_window, MenuLogicalHeight()),
                                 g_window, nullptr, g_instance, nullptr);
        auto button = [&](int id, const wchar_t* label, int x, int y, int width) {
            HWND control = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                           ScaleForDpi(g_window, x), ScaleForDpi(g_window, y),
                                           ScaleForDpi(g_window, width), ScaleForDpi(g_window, 30), g_menu,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), FALSE);
            return control;
        };
        g_slider = CreateWindowExW(
            0, TRACKBAR_CLASSW, Text().sliderLabel, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS,
            ScaleForDpi(g_window, 12), ScaleForDpi(g_window, 70), ScaleForDpi(g_window, 256), ScaleForDpi(g_window, 26),
            g_menu, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlOpacitySlider)), g_instance, nullptr);
        SendMessageW(g_slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(g_slider, TBM_SETPAGESIZE, 0, 10);
        SetWindowSubclass(g_slider, SliderProcedure, 1, 0);
        g_menuGlass = button(kMenuGlass, Text().glass, 16, 104, 248);
        g_menuTopmost = button(kMenuTopmost, Text().alwaysOnTop, 16, 166, 248);
        g_menuPassThrough = button(kMenuPassThrough, Text().passThrough, 16, 202, 248);
        g_menuTheme = button(kMenuTheme, Text().customTheme, 16, 238, 248);
        for (int i = 0; i < static_cast<int>(ARRAYSIZE(kThemeColors)); ++i)
            button(kMenuThemeBase + i, Text().themeNames[i], 16 + i * 51, 274, 44);
        g_menuTextColor = button(kMenuTextColor, Text().textColor, 16, 316, 248);
        g_menuAutoTextColor = button(kMenuAutoTextColor, Text().autoTextColor, 16, 352, 248);
        g_menuShadow = button(kMenuShadow, Text().shadow, 16, 388, 248);
        g_menuLanguage = button(kMenuLanguage, Text().language, 16, 424, 248);
        g_menuAutostart = button(kMenuAutostart, Text().autostart, 16, 460, 248);
#if defined(FLOATNOTE_GLASS_LAB) && !defined(FLOATNOTE_LOCAL_DESKTOP)
        EnableWindow(g_menuAutostart,FALSE);
#endif
        g_menuHide = button(kMenuToggleVisible, Text().hideWindow, 16, 496, 120);
        g_menuExit = button(kMenuExit, Text().exit, 144, 496, 120);
        RoundWindow(g_menu, kCornerRadius);
    }
    UpdateMenuLabels();
    ScrollMenuTo(0);
    RECT pill{};
    GetWindowRect(g_pill, &pill);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int preferredHeight = ScaleForDpi(g_window, MenuLogicalHeight());
    const int availableHeight = std::max(1L, monitor.rcWork.bottom - monitor.rcWork.top - ScaleForDpi(g_window, 12));
    const bool scrolling = preferredHeight > availableHeight;
    const LONG_PTR style = GetWindowLongPtrW(g_menu, GWL_STYLE);
    SetWindowLongPtrW(g_menu, GWL_STYLE, scrolling ? style | WS_VSCROLL : style & ~WS_VSCROLL);
    const int width = ScaleForDpi(g_window, MenuLogicalWidth()) +
                      (scrolling ? GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(g_window)) : 0);
    const int height = std::min(preferredHeight, availableHeight);
    const int x =
        std::clamp(static_cast<int>((pill.left + pill.right - width) / 2), static_cast<int>(monitor.rcWork.left),
                   std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width));
    int y = pill.bottom + ScaleForDpi(g_window, 6);
    if (y + height > monitor.rcWork.bottom)
        y = std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(pill.top) - height);
    SetLayeredWindowAttributes(g_menu, 0, 255, LWA_ALPHA);
    SetWindowPos(g_menu, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    RoundWindow(g_menu, kCornerRadius);
    UpdateMenuScroll();
    SetForegroundWindow(g_menu);
    SetFocus(g_slider);
}

void ResizeWindowFromPointerDelta(int dx, int dy) {
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceInputBlocked())return;
    BeginExperienceResize();
#endif
    MONITORINFO monitor{sizeof(monitor)};
    const int currentWidth = static_cast<int>(g_pointerWindow.right - g_pointerWindow.left);
    const int currentHeight = static_cast<int>(g_pointerWindow.bottom - g_pointerWindow.top);
    int maxWidth = std::max(ScaleForDpi(g_window, kMinimumNoteWidth), currentWidth + std::max(0, dx));
    int maxHeight = std::max(ScaleForDpi(g_window, kMinimumNoteHeight), currentHeight + std::max(0, dy));
    if (GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        maxWidth = std::max(ScaleForDpi(g_window, kMinimumNoteWidth), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
        maxHeight = std::max(ScaleForDpi(g_window, kMinimumNoteHeight), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
    }
    SetWindowPos(g_window, nullptr, 0, 0, std::clamp(currentWidth + dx, ScaleForDpi(g_window, kMinimumNoteWidth), maxWidth),
                 std::clamp(currentHeight + dy, ScaleForDpi(g_window, kMinimumNoteHeight), maxHeight),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#ifdef FLOATNOTE_GLASS_LAB
    MaybeAbsorbExperienceResize();
#endif
}

void MoveWindowFromPointerDelta(int dx, int dy) {
    SetWindowPos(g_window, nullptr, g_pointerWindow.left + dx, g_pointerWindow.top + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK PointerProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceInputBlocked() && (message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK ||
        message==WM_LBUTTONUP || message==WM_MOUSEMOVE))return 0;
#endif
    if (message == BM_CLICK && id == kControlPill) {
        PostMessageW(g_window, WM_COMMAND, kControlPill, 0);
        return 0;
    }
    // Child controls remain native input/accessibility hosts, but their on-screen
    // alpha is zero. Route hit-tested pointer input from the layered parent.
    if (window == g_window && id == 0 && !g_settings.passThrough && !(g_pointerDown && GetCapture() == g_window) &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP ||
         message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MOUSEMOVE || message == WM_SETCURSOR)) {
        POINT point{};
        if (message == WM_SETCURSOR) {
            GetCursorPos(&point);
            ScreenToClient(window, &point);
        } else
            point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        HWND target = nullptr;
          for (HWND child : {g_grip, g_pill, g_edit}) {
            if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
            RECT bounds{};
            GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            if (PtInRect(&bounds, point)) {
                target = child;
                break;
            }
        }
        if (message == WM_MOUSEMOVE && GetCapture() != g_pill) {
            const bool hover = target == g_pill;
            if (hover != g_pillHover) {
                g_pillHover = hover;
                RequestRender();
            }
        }
        if (target) {
            MapWindowPoints(window, target, &point, 1);
            return SendMessageW(target, message, wp, message == WM_SETCURSOR ? lp : MAKELPARAM(point.x, point.y));
        }
    }
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, id == kControlGrip ? IDC_SIZENWSE : IDC_HAND));
        return TRUE;
    }
    if (message == WM_LBUTTONDOWN) {
        LeaveMarkdownEditor();
#ifdef FLOATNOTE_GLASS_LAB
        if(id==kControlGrip)BeginExperienceResize();
#endif
        if (id == 0)
            SetFocus(g_window);
        g_pointerDown = true;
        g_pointerDragged = false;
        g_pointerStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(window, &g_pointerStart);
        GetWindowRect(g_window, &g_pointerWindow);
        SetCapture(window);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        if (id == kControlPill && !g_pillHover) {
            g_pillHover = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
        if (g_pointerDown && GetCapture() == window) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window, &point);
            const int dx = point.x - g_pointerStart.x, dy = point.y - g_pointerStart.y;
            if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG))
                g_pointerDragged = true;
            if (g_pointerDragged) {
                CloseMenu();
                if (id == kControlGrip)
                    ResizeWindowFromPointerDelta(dx, dy);
                else
                    MoveWindowFromPointerDelta(dx, dy);
            }
        }
        return 0;
    }
    if (message == WM_MOUSELEAVE && id == kControlPill) {
        g_pillHover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_LBUTTONUP && g_pointerDown) {
        const bool dragged = g_pointerDragged;
        g_pointerDown = false;
        ReleaseCapture();
        if (dragged) {
            RecoverWindowPosition();
#ifdef FLOATNOTE_GLASS_LAB
            FinishExperienceResize();
#endif
            SaveSettings();
        } else if (id == kControlPill)
            PostMessageW(g_window, WM_COMMAND, kControlPill, 0);
        return 0;
    }
    if (message == WM_CAPTURECHANGED) {
        if (g_pointerDown && g_pointerDragged) {
#ifdef FLOATNOTE_GLASS_LAB
            FinishExperienceResize();
#endif
            SetTimer(g_window, 2, 300, nullptr);
        }
        g_pointerDown = false;
    }
    const LRESULT result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_PAINT || message == WM_SETFOCUS || message == WM_KILLFOCUS)
        RequestRender();
    return result;
}
bool IsTextPoint(HWND window, POINT point) {
    if (g_hitTextDirty) {
        g_hitText = EditorText();
        g_hitTextDirty = false;
    }
    const std::wstring& text = g_hitText;
    // An empty note must remain writable after deleting all of its text.
    if (text.empty())
        return true;
    const DWORD nearest = static_cast<DWORD>(SendMessageW(window, EM_CHARFROMPOS, 0, MAKELPARAM(point.x, point.y)));
    const int firstLine = static_cast<int>(SendMessageW(window, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int line = firstLine + ((static_cast<int>(HIWORD(nearest)) - (firstLine & 0xffff) + 0x10000) & 0xffff);
    const int start = static_cast<int>(SendMessageW(window, EM_LINEINDEX, line, 0));
    if (start < 0)
        return false;
    int index = start + ((static_cast<int>(LOWORD(nearest)) - (start & 0xffff) + 0x10000) & 0xffff);
    if (index < 0 || index >= static_cast<int>(text.size()) || iswspace(text[index]))
        return false;
    if (index > 0 && text[index] >= 0xdc00 && text[index] <= 0xdfff)
        --index;
    const int length =
        text[index] >= 0xd800 && text[index] <= 0xdbff && index + 1 < static_cast<int>(text.size()) ? 2 : 1;
    const LRESULT position = SendMessageW(window, EM_POSFROMCHAR, index, 0);
    if (position == -1)
        return false;
    const int x = GET_X_LPARAM(position), y = GET_Y_LPARAM(position);
    HDC dc = GetDC(window);
    const auto font = SelectObject(dc, g_textFont);
    SIZE extent{};
    TEXTMETRICW metrics{};
    GetTextExtentPoint32W(dc, text.data() + index, length, &extent);
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, font);
    ReleaseDC(window, dc);
    const LRESULT next = SendMessageW(window, EM_POSFROMCHAR, index + length, 0);
    if (next != -1 && GET_Y_LPARAM(next) == y && GET_X_LPARAM(next) > x)
        extent.cx = GET_X_LPARAM(next) - x;
    return point.x >= x && point.x < x + extent.cx && point.y >= y && point.y < y + metrics.tmHeight;
}

LRESULT CALLBACK EditProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if(message==WM_IME_STARTCOMPOSITION)g_markdownComposing=true;
    if(message==WM_IME_ENDCOMPOSITION) {
        const auto result=DefSubclassProc(window,message,wParam,lParam);
        g_markdownComposing=false;
        if(g_markdownBlurPending)PostMessageW(g_window,kMarkdownPreviewMessage,0,0);
        RequestRender();return result;
    }
    if (message == WM_SETFOCUS)
        ResetCaret();
    if (message == WM_KILLFOCUS) {
        KillTimer(g_window, kCaretTimer);
        g_markdownBlurPending=true;
        PostMessageW(g_window,kMarkdownPreviewMessage,0,0);
        RequestRender();
    }
    if(message==WM_SETTEXT)g_markdownDirty=true;
    if(!g_markdownEditing) {
        if((message==WM_PRINT || message==WM_PRINTCLIENT) && PaintMarkdownPreview(reinterpret_cast<HDC>(wParam)))return 0;
        if(message==WM_PAINT) {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
            const bool painted=PaintMarkdownPreview(dc);
            if(!painted)DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
            EndPaint(window,&paint);RequestRender();return 0;
        }
        if(message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK) {
            size_t source=0;
            if(EnsureMarkdownPreview() && g_markdownPreview.Hit({GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)},source)) {
                BeginMarkdownEditing(source);return 0;
            }
            if(!EnsureMarkdownPreview() && IsTextPoint(window,{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)}))
                BeginMarkdownEditing();
            else return PointerProcedure(window,WM_LBUTTONDOWN,wParam,lParam,0,0);
        }
        if(message==WM_SETCURSOR) {
            POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);size_t source=0;
            const bool hit=EnsureMarkdownPreview()?g_markdownPreview.Hit(point,source):IsTextPoint(window,point);
            SetCursor(LoadCursorW(nullptr,hit?IDC_IBEAM:IDC_HAND));return TRUE;
        }
        if(message==WM_MOUSEWHEEL && !(GET_KEYSTATE_WPARAM(wParam)&MK_CONTROL) && EnsureMarkdownPreview()) {
            g_scrollRemainder+=GET_WHEEL_DELTA_WPARAM(wParam);
            const int steps=g_scrollRemainder/WHEEL_DELTA;g_scrollRemainder%=WHEEL_DELTA;
            UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
            RECT bounds{};GetClientRect(window,&bounds);
            const float amount=lines==WHEEL_PAGESCROLL?static_cast<float>(bounds.bottom):
                lines*MulDiv(g_settings.fontSize,GetDpiForWindow(window),72)*1.4f;
            g_markdownPreview.Scroll(-steps*amount);InvalidateRect(window,nullptr,FALSE);RequestRender();return 0;
        }
        // Keyboard activation is available to keyboard/accessibility users;
        // focusing the window alone must not turn a preview into an editor.
        if(message==WM_KEYDOWN && wParam==VK_F2) {BeginMarkdownEditing();return 0;}
        if(message==WM_CHAR || message==WM_KEYDOWN || message==WM_CUT || message==WM_PASTE || message==WM_CLEAR)return 0;
    }
    if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) &&
        !IsTextPoint(window, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}))
        return PointerProcedure(window, WM_LBUTTONDOWN, wParam, lParam, 0, 0);
    if (message == WM_CAPTURECHANGED)
        g_pointerDown = false;
    if (g_pointerDown && GetCapture() == window &&
        (message == WM_MOUSEMOVE || message == WM_LBUTTONUP || message == WM_CAPTURECHANGED))
        return PointerProcedure(window, message, wParam, lParam, 0, 0);
    if (message == WM_SETCURSOR) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        SetCursor(LoadCursorW(nullptr, IsTextPoint(window, point) ? IDC_IBEAM : IDC_HAND));
        return TRUE;
    }
    if (message == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) {
        ZoomFromWheel(wParam);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        // Scroll explicitly because the compact editor has no WS_VSCROLL bar.
        g_scrollRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
        const int steps = g_scrollRemainder / WHEEL_DELTA;
        g_scrollRemainder %= WHEEL_DELTA;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) {
            for (int i = 0; i < abs(steps); ++i)
                SendMessageW(window, WM_VSCROLL, steps > 0 ? SB_PAGEUP : SB_PAGEDOWN, 0);
        } else
            SendMessageW(window, EM_LINESCROLL, 0, -steps * static_cast<int>(lines));
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == 'A' && GetKeyState(VK_CONTROL) < 0) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    // Multiline EDIT does not send EN_CHANGE for WM_SETTEXT (including UIA).
    if (message == WM_SETTEXT && !g_loadingText)
        SendMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(kControlEdit, EN_CHANGE),
                     reinterpret_cast<LPARAM>(window));
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, EditProcedure, 1);
    if (message == WM_SETTEXT)
        g_hitTextDirty = true;
    if (message == WM_PAINT || message == WM_CHAR || message == WM_KEYDOWN || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP || (message == WM_MOUSEMOVE && GetCapture() == window) ||
        message == WM_IME_COMPOSITION || message == WM_IME_ENDCOMPOSITION || message == EM_SETSEL ||
        message == EM_LINESCROLL || message == WM_VSCROLL || message == WM_SETTEXT || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS) {
        if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
            ResetCaret();
        else
            RequestRender();
    }
    return result;
}

LRESULT CALLBACK CanvasProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        EndPaint(window, &ps);
        return 0;
    }
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(window, message, wp, lp);
}

void CreateControls(HWND window) {
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    RefreshTheme();
    WNDCLASSW canvasClass{};
    canvasClass.hInstance = g_instance;
    canvasClass.lpfnWndProc = CanvasProcedure;
    canvasClass.lpszClassName = L"FloatNote.CompositedCanvas";
    RegisterClassW(&canvasClass);
    g_canvas = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT, canvasClass.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0,
                               window, nullptr, g_instance, nullptr);
    g_edit = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlEdit)), g_instance, nullptr);
    SendMessageW(g_edit, EM_SETLIMITTEXT, 1024 * 1024, 0);
    SetWindowSubclass(g_edit, EditProcedure, 1, 0);
    g_pill =
        CreateWindowExW(0, L"BUTTON", Text().pillLabel, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlPill)), g_instance, nullptr);
    g_grip = CreateWindowExW(0, L"BUTTON", Text().gripLabel, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlGrip)), g_instance, nullptr);
    SetWindowSubclass(g_pill, PointerProcedure, kControlPill, 0);
    SetWindowSubclass(g_grip, PointerProcedure, kControlGrip, 0);
    SetWindowSubclass(window, PointerProcedure, 0, 0);
    for (HWND child : {g_edit, g_pill, g_grip}) {
        SetWindowLongPtrW(child, GWL_EXSTYLE, GetWindowLongPtrW(child, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(child, 0, 0, LWA_ALPHA);
    }
    g_tooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, g_instance, nullptr);
    if (g_tooltip) {
        TOOLINFOW tip{sizeof(tip)};
        tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tip.hwnd = window;
        tip.uId = reinterpret_cast<UINT_PTR>(g_pill);
        tip.lpszText = const_cast<wchar_t*>(Text().tooltip);
        SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
        SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(window, 300));
    }
    RecreateFonts(window);
    UpdateLayout(window);
}
void PlaceInitialWindow() {
    if ((g_settings.x != CW_USEDEFAULT && abs(static_cast<long long>(g_settings.x)) > 1000000) ||
        (g_settings.y != CW_USEDEFAULT && abs(static_cast<long long>(g_settings.y)) > 1000000))
        g_settings.x = g_settings.y = CW_USEDEFAULT;
    if (g_settings.x != CW_USEDEFAULT && g_settings.y != CW_USEDEFAULT) {
        RECT desired{g_settings.x, g_settings.y, g_settings.x + g_settings.width, g_settings.y + g_settings.height};
        HMONITOR monitor = MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) {
            const int workWidth = static_cast<int>(info.rcWork.right - info.rcWork.left);
            const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
            const int workLeft = static_cast<int>(info.rcWork.left);
            const int workTop = static_cast<int>(info.rcWork.top);
            const int workRight = static_cast<int>(info.rcWork.right);
            const int workBottom = static_cast<int>(info.rcWork.bottom);
            g_settings.width = std::min(g_settings.width, workWidth);
            g_settings.height = std::min(g_settings.height, workHeight);
            g_settings.x = std::clamp(g_settings.x, workLeft, workRight - g_settings.width);
            g_settings.y = std::clamp(g_settings.y, workTop, workBottom - g_settings.height);
        }
        return;
    }

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    g_settings.width = ScaleForDpi(nullptr, 320);
    g_settings.height = ScaleForDpi(nullptr, 160);
    g_settings.x = workArea.right - g_settings.width - ScaleForDpi(nullptr, 24);
    g_settings.y = workArea.top + ScaleForDpi(nullptr, 24);
}

void RecoverWindowPosition() {
    if (!IsWindow(g_window) || IsIconic(g_window))
        return;
    RECT original{};
    GetWindowRect(g_window, &original);
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(MonitorFromRect(&original, MONITOR_DEFAULTTONEAREST), &info)) {
        RECT corrected = ConstrainToWorkArea(original, info.rcWork);
        if (!EqualRect(&original, &corrected)) {
            SetWindowPos(g_window, nullptr, corrected.left, corrected.top, corrected.right - corrected.left,
                         corrected.bottom - corrected.top, SWP_NOZORDER | SWP_NOACTIVATE);
            SaveSettings();
        }
    }
}

LRESULT HitTestWindow(HWND, LPARAM) {
    // Only the explicit bottom-right grip can resize; no system non-client frame.
    return HTCLIENT;
}

void PaintWindow(HWND window, HDC dc) {
    DrawSurface(window, dc);
}

struct SurfaceBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    DWORD* pixels = nullptr;
    int width = 0, height = 0;
    ~SurfaceBuffer() { Clear(); }
    void Clear() {
        if (dc && previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        dc = nullptr;
        bitmap = nullptr;
        previous = nullptr;
        pixels = nullptr;
    }
    bool Resize(int w, int h) {
        if (dc && w == width && h == height)
            return true;
        Clear();
        width = w;
        height = h;
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmap || !dc) {
            Clear();
            return false;
        }
        previous = SelectObject(dc, bitmap);
        return true;
    }
};
SurfaceBuffer g_surface;
SurfaceBuffer g_textOnBlack;
SurfaceBuffer g_textOnWhite;

DWORD PremultiplyPixel(COLORREF rgb, int alpha) {
    return (static_cast<DWORD>(alpha) << 24) | ((GetRValue(rgb) * alpha / 255) << 16) |
           ((GetGValue(rgb) * alpha / 255) << 8) | (GetBValue(rgb) * alpha / 255);
}

void CompositePixel(int x, int y, COLORREF color, float coverage, int strength = 255) {
    if (x < 0 || y < 0 || x >= g_surface.width || y >= g_surface.height)
        return;
    const int alpha = static_cast<int>(std::clamp(coverage, 0.0f, 1.0f) * strength + 0.5f);
    if (!alpha)
        return;
    DWORD& destination = g_surface.pixels[y * g_surface.width + x];
    const int remaining = 255 - alpha;
    const int outAlpha = alpha + (((destination >> 24) * remaining + 127) / 255);
    const int red = (GetRValue(color) * alpha + ((destination >> 16) & 255) * remaining + 127) / 255;
    const int green = (GetGValue(color) * alpha + ((destination >> 8) & 255) * remaining + 127) / 255;
    const int blue = (GetBValue(color) * alpha + (destination & 255) * remaining + 127) / 255;
    destination = (static_cast<DWORD>(outAlpha) << 24) | (red << 16) | (green << 8) | blue;
}

float RoundedDistance(float x, float y, RECT rectangle, float radius) {
    const float halfWidth = (rectangle.right - rectangle.left) * 0.5f;
    const float halfHeight = (rectangle.bottom - rectangle.top) * 0.5f;
    radius = std::min({radius, halfWidth, halfHeight});
    const float qx = std::abs(x - (rectangle.left + halfWidth)) - (halfWidth - radius);
    const float qy = std::abs(y - (rectangle.top + halfHeight)) - (halfHeight - radius);
    const float outsideX = std::max(qx, 0.0f), outsideY = std::max(qy, 0.0f);
    return std::sqrt(outsideX * outsideX + outsideY * outsideY) + std::min(std::max(qx, qy), 0.0f) - radius;
}

void CompositeRoundRect(RECT rect, float radius, COLORREF color) {
    for (int y = std::max(0L, rect.top - 1); y < std::min(g_surface.height, static_cast<int>(rect.bottom) + 1); ++y)
        for (int x = std::max(0L, rect.left - 1); x < std::min(g_surface.width, static_cast<int>(rect.right) + 1); ++x)
            CompositePixel(x, y, color, 0.5f - RoundedDistance(x + 0.5f, y + 0.5f, rect, radius));
}

void CompositeLine(float x1, float y1, float x2, float y2, float width, COLORREF color, int strength) {
    const float dx = x2 - x1, dy = y2 - y1, lengthSquared = dx * dx + dy * dy;
    const int left = static_cast<int>(std::floor(std::min(x1, x2) - width));
    const int top = static_cast<int>(std::floor(std::min(y1, y2) - width));
    const int right = static_cast<int>(std::ceil(std::max(x1, x2) + width));
    const int bottom = static_cast<int>(std::ceil(std::max(y1, y2) + width));
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float t =
                lengthSquared > 0 ? std::clamp(((px - x1) * dx + (py - y1) * dy) / lengthSquared, 0.0f, 1.0f) : 0;
            const float ox = px - x1 - t * dx, oy = py - y1 - t * dy;
            CompositePixel(x, y, color, width * 0.5f + 0.5f - std::sqrt(ox * ox + oy * oy), strength);
        }
}

void DrawCompositedDecorations(RECT outer) {
    const RECT card{0, 0, g_surface.width, g_surface.height};
    const int radius = ScaleForDpi(g_window, NoteCornerRadius());
    const float stroke = std::max(1.0f, ScaleForDpi(g_window, 1) * 0.75f);
    auto edge = [&](int x, int y) {
#ifdef FLOATNOTE_GLASS_LAB
        // The liquid shader owns the reflective rim; a dark UI outline on
        // top would cancel its highlight. Retain outlines for other modes.
        if (g_nativeGlass && g_backdrop.mode == 2 && !g_highContrast)
            return;
#endif
        const float distance = RoundedDistance(x + 0.5f, y + 0.5f, card, static_cast<float>(radius));
        CompositePixel(x, y, kText, stroke * 0.5f + 0.5f - std::abs(distance + stroke * 0.5f),
                       g_highContrast ? 255 : 38);
    };
    // Only visit the perimeter, not the complete window, for the fine outline.
    for (int y = 0; y < g_surface.height; ++y) {
        if (y < 2 || y >= g_surface.height - 2) {
            for (int x = 0; x < g_surface.width; ++x)
                edge(x, y);
        } else {
            const int band =
                std::min(g_surface.width / 2, (y < radius + 2 || y >= g_surface.height - radius - 2) ? radius + 2 : 2);
            for (int x = 0; x < band; ++x)
                edge(x, y);
            for (int x = g_surface.width - band; x < g_surface.width; ++x)
                edge(x, y);
        }
    }
    RECT pill{};
    GetWindowRect(g_pill, &pill);
    OffsetRect(&pill, -outer.left, -outer.top);
#ifndef FLOATNOTE_GLASS_LAB
    InflateRect(&pill, 0, -ScaleForDpi(g_window, 3));
    CompositeRoundRect(pill, (pill.bottom - pill.top) * 0.5f,
                       g_highContrast ? kText : BlendColor(kBackground, kText, g_pillHover ? 82 : 92));
    const COLORREF dots = (g_saveFailed || g_settingsFailed || g_loadFailed) ? RGB(255, 162, 131)
                          : g_settings.passThrough                           ? RGB(129, 201, 233)
                                                                             : kBackground;
    const float dotRadius = static_cast<float>(std::max(1, ScaleForDpi(g_window, 1)));
    const float centerY = (pill.top + pill.bottom) * 0.5f + 0.5f;
    for (int offset : {-6, 0, 6}) {
        const float centerX = (pill.left + pill.right) * 0.5f + ScaleForDpi(g_window, offset) + 0.5f;
        for (int y = static_cast<int>(centerY - dotRadius - 1); y <= static_cast<int>(centerY + dotRadius + 1); ++y)
            for (int x = static_cast<int>(centerX - dotRadius - 1); x <= static_cast<int>(centerX + dotRadius + 1);
                 ++x) {
                const float dx = x + 0.5f - centerX, dy = y + 0.5f - centerY;
                CompositePixel(x, y, dots, dotRadius + 0.5f - std::sqrt(dx * dx + dy * dy));
            }
    }
#endif
    // The body becomes an unadorned glass drop during absorption.
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceAbsorbing())return;
#endif
    RECT grip{};
    GetWindowRect(g_grip, &grip);
    OffsetRect(&grip, -outer.left, -outer.top);
    for (int offset : {5, 9}) {
        CompositeLine(static_cast<float>(grip.right - ScaleForDpi(g_window, offset + 3)),
                      static_cast<float>(grip.bottom - ScaleForDpi(g_window, 4)),
                      static_cast<float>(grip.right - ScaleForDpi(g_window, 4)),
                      static_cast<float>(grip.bottom - ScaleForDpi(g_window, offset + 3)),
                      static_cast<float>(ScaleForDpi(g_window, 1)), kNoteText, g_highContrast ? 255 : 110);
    }
    if (IsWindowVisible(g_pill) && GetFocus() == g_pill) {
        RECT focus = pill;
        InflateRect(&focus, ScaleForDpi(g_window, 2), ScaleForDpi(g_window, 2));
        const float focusRadius = (focus.bottom - focus.top) * 0.5f;
        for (int y = focus.top; y < focus.bottom; ++y)
            for (int x = focus.left; x < focus.right; ++x) {
                const float d = RoundedDistance(x + 0.5f, y + 0.5f, focus, focusRadius);
                CompositePixel(x, y, kText, 1.0f - std::abs(d + 0.5f), 100);
            }
    }
}

void ResetCaret() {
    g_caretVisible = true;
    if (g_markdownEditing && g_window && GetFocus() == g_edit && !g_settings.passThrough && !g_loadFailed) {
        const UINT blink = GetCaretBlinkTime();
        KillTimer(g_window, kCaretTimer);
        if (blink != INFINITE && blink != 0)
            SetTimer(g_window, kCaretTimer, std::max(100u, blink), nullptr);
    }
    RequestRender();
}

void RenderLayeredWindow() {
    g_renderPosted = false;
#ifdef FLOATNOTE_GLASS_LAB
    if(ExperienceCompact())return;
#endif
    if (g_rendering || g_closing || !IsWindow(g_window) || !g_isVisible || IsIconic(g_window))
        return;
    RECT outer{};
    if (!GetWindowRect(g_window, &outer))
        return;
    const int width = outer.right - outer.left, height = outer.bottom - outer.top;
    if (width <= 0 || height <= 0 || static_cast<long long>(width) * height > 32 * 1024 * 1024)
        return;
    SurfaceBuffer& surface = g_surface;
    if (!surface.Resize(width, height))
        return;
    // Child visibility controls composition; a hidden parent must not erase
    // text from an off-screen render prepared before the window is shown.
    const bool editorVisible=(GetWindowLongPtrW(g_edit,GWL_STYLE)&WS_VISIBLE)!=0;
    RECT editorClient{};
    GetClientRect(g_edit, &editorClient);
    if (!g_textOnBlack.Resize(editorClient.right, editorClient.bottom) ||
        !g_textOnWhite.Resize(editorClient.right, editorClient.bottom))
        return;
    g_rendering = true;
#ifdef FLOATNOTE_DIAGNOSTICS
    ++g_renderCount;
#endif
    // Native EDIT still owns text, IME, selection, undo and accessibility. Only
    // its pixels are composited, so changing background alpha never fades text.
    RECT sourceBounds{0, 0, width, height};
    FillRect(surface.dc, &sourceBounds, g_editBrush);
    for (HWND child : {g_edit, g_pill, g_grip}) {
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
        RECT childRect{};
        GetWindowRect(child, &childRect);
        OffsetRect(&childRect, -outer.left, -outer.top);
        const int saved = SaveDC(surface.dc);
        SetViewportOrgEx(surface.dc, childRect.left, childRect.top, nullptr);
        IntersectClipRect(surface.dc, 0, 0, childRect.right - childRect.left, childRect.bottom - childRect.top);
        SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(surface.dc), PRF_CLIENT | PRF_ERASEBKGND);
        RestoreDC(surface.dc, saved);
    }
    RECT editor{};
    GetWindowRect(g_edit, &editor);
    OffsetRect(&editor, -outer.left, -outer.top);
    // Obtain coverage directly from white glyphs on black, independent of the
    // theme or chosen ink. A white-background pass identifies opaque native
    // selection/IME pixels (unchanged between passes), which retain their colors.
    for (int pass : {1, 2}) {
        if(!editorVisible)break;
        auto& mask = pass == 1 ? g_textOnBlack : g_textOnWhite;
        g_textMask = pass;
        FillRect(mask.dc, &editorClient, static_cast<HBRUSH>(GetStockObject(pass == 1 ? BLACK_BRUSH : WHITE_BRUSH)));
        SendMessageW(g_edit, WM_PRINT, reinterpret_cast<WPARAM>(mask.dc), PRF_CLIENT | PRF_ERASEBKGND);
    }
    g_textMask = 0;
    // Alpha 1 (less than half a percent) keeps the invisible input surface
    // hittable at 0%. Pass-through mode can use exact zero alpha.
    const int opacity =
        g_highContrast ? 255 : std::max(g_settings.passThrough ? 0 : 1, MulDiv(EffectiveOpacityPercent(), 255, 100));
    const DWORD background = (GetRValue(kBackground) << 16) | (GetGValue(kBackground) << 8) | GetBValue(kBackground);
    const int radius = std::min({ScaleForDpi(g_window, NoteCornerRadius()), width / 2, height / 2});
#ifdef FLOATNOTE_GLASS_LAB
    RECT inkBounds{width,height,0,0};
    const bool collectInk=g_settings.autoTextColor && !g_highContrast && g_backdrop.mode==2 && EffectiveOpacityPercent()<100;
#endif
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            DWORD& pixel = surface.pixels[y * width + x];
            const DWORD rgb = pixel & 0xffffff;
            const COLORREF color = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
            pixel = PremultiplyPixel(color, rgb == background ? opacity : 255);
            if (editorVisible && x >= editor.left && x < editor.right && y >= editor.top && y < editor.bottom) {
                const int index = (y - editor.top) * g_textOnBlack.width + x - editor.left;
                const DWORD black = g_textOnBlack.pixels[index] & 0xffffff;
                const DWORD white = g_textOnWhite.pixels[index] & 0xffffff;
                if (black == white) {
                    pixel = PremultiplyPixel(color, 255);
                } else {
                    const int coverage = static_cast<int>(black & 255);
#ifdef FLOATNOTE_GLASS_LAB
                    if(collectInk && coverage>32) {
                        inkBounds.left=std::min(inkBounds.left,LONG(x));inkBounds.top=std::min(inkBounds.top,LONG(y));
                        inkBounds.right=std::max(inkBounds.right,LONG(x+1));inkBounds.bottom=std::max(inkBounds.bottom,LONG(y+1));
                    }
#endif
                    const int residual = MulDiv(255 - coverage, opacity, 255);
                    const int alpha = coverage + residual;
                    const int red = (GetRValue(kNoteText) * coverage + GetRValue(kBackground) * residual) / 255;
                    const int green = (GetGValue(kNoteText) * coverage + GetGValue(kBackground) * residual) / 255;
                    const int blue = (GetBValue(kNoteText) * coverage + GetBValue(kBackground) * residual) / 255;
                    pixel = (static_cast<DWORD>(alpha) << 24) | (red << 16) | (green << 8) | blue;
                }
            }
            if ((x < radius || x >= width - radius) && (y < radius || y >= height - radius)) {
                const double dx = x < radius ? radius - (x + 0.5) : x + 0.5 - (width - radius);
                const double dy = y < radius ? radius - (y + 0.5) : y + 0.5 - (height - radius);
                const double coverage = std::clamp(radius + 0.5 - std::sqrt(dx * dx + dy * dy), 0.0, 1.0);
                DWORD out = 0;
                for (int shift : {0, 8, 16, 24})
                    out |= static_cast<DWORD>(((pixel >> shift) & 255) * coverage) << shift;
                pixel = out;
            }
        }
    }
    DrawCompositedDecorations(outer);
    if (g_markdownEditing && editorVisible && g_caretVisible && GetFocus() == g_edit && !g_settings.passThrough && !g_loadFailed) {
        DWORD start = 0, end = 0;
        SendMessageW(g_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        GUITHREADINFO info{sizeof(info)};
        if (start == end && GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndCaret == g_edit) {
            RECT caret = info.rcCaret;
            OffsetRect(&caret, editor.left, editor.top);
            caret.right = caret.left + std::max(1, ScaleForDpi(g_window, 1));
            IntersectRect(&caret, &caret, &editor);
            for (int y = std::max(0L, caret.top); y < std::min(height, static_cast<int>(caret.bottom)); ++y)
                for (int x = std::max(0L, caret.left); x < std::min(width, static_cast<int>(caret.right)); ++x)
                    surface.pixels[y * width + x] = PremultiplyPixel(kNoteText, 255);
        }
    }
    POINT position{outer.left, outer.top}, source{};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
#ifdef FLOATNOTE_GLASS_LAB
    blend.SourceConstantAlpha=static_cast<BYTE>(255*ExperienceAbsorbOpacity());
#endif
    HWND target = g_nativeGlass ? g_canvas : g_window;
    if (!UpdateLayeredWindow(target, nullptr, g_nativeGlass ? nullptr : &position, &size, surface.dc, &source, 0,
                             &blend, ULW_ALPHA)) {
        // Window capture/accessibility utilities may switch a layered window to
        // uniform alpha. Reset that API state while preserving interaction flags.
        const LONG_PTR style = GetWindowLongPtrW(target, GWL_EXSTYLE);
        SetWindowLongPtrW(target, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        SetWindowLongPtrW(target, GWL_EXSTYLE, style | WS_EX_LAYERED);
        UpdateLayeredWindow(target, nullptr, g_nativeGlass ? nullptr : &position, &size, surface.dc, &source, 0, &blend,
                            ULW_ALPHA);
    }
    g_rendering = false;
#ifdef FLOATNOTE_GLASS_LAB
    if(collectInk && inkBounds.right>inkBounds.left && inkBounds.bottom>inkBounds.top) {
        const int margin=ScaleForDpi(g_window,3);InflateRect(&inkBounds,margin,margin);
        IntersectRect(&inkBounds,&inkBounds,&editor);
        g_backdrop.SetInkRegion(inkBounds);
    } else if(collectInk && GetWindowTextLengthW(g_edit)==0)g_backdrop.SetInkRegion(editor);
    // A fully selected native EDIT has opaque selection pixels, not a glyph
    // mask. Retain the last text region so selection cannot change the vote.
    SyncAdaptiveInk();
#endif
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef FLOATNOTE_GLASS_LAB
    if (message == WM_TIMER && wParam == 71) { g_backdrop.Tick(); SyncAdaptiveInk(); return 0; }
    if (message == kGlassFrameReady) { g_backdrop.FrameReady(); SyncAdaptiveInk(); ApplyWindowStacking(); return 0; }
    if (message == WM_TIMER && wParam == 82) { SyncAdaptiveInk(); return 0; }
    if (message == WM_TIMER && wParam == 83) { SyncAdaptiveInk(); return 0; }
    if (message == WM_WINDOWPOSCHANGED) {g_backdrop.RequestDraw();SyncExperienceUI();}
    if (message == WM_ACTIVATE && LOWORD(wParam)!=WA_INACTIVE)SyncExperienceUI();
    if (message == WM_TIMER && wParam==73){ApplyWindowStacking();SyncExperienceUI();return 0;}
    if (message == WM_TIMER && wParam==74){KillTimer(window,74);SaveExperienceMaterial();return 0;}
    if (message == WM_TIMER && wParam==75){TickExperienceAbsorb();return 0;}
    if (message == WM_TIMER && wParam==76){SyncExperienceCapture();return 0;}
    if (message == kExperienceCloseRequest){RequestExperienceClose();return 0;}
    if (message == kExperienceChooseColor){ChooseExperienceColor(wParam!=0);return 0;}
    if (message == WM_SIZING)BeginExperienceResize();
    if (message == WM_LBUTTONDBLCLK && ExperienceCompact()){ExpandExperienceNote();return 0;}
    // The laboratory cannot create, remove, or repoint the production startup shortcut.
#ifndef FLOATNOTE_LOCAL_DESKTOP
    if (message == WM_COMMAND && LOWORD(wParam) == kMenuAutostart) return 0;
#endif
#endif
    if (g_taskbarCreatedMessage && message == g_taskbarCreatedMessage) {
        AddTrayIcon();
        UpdateTrayTip();
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        g_window = window;
        CreateControls(window);
        LoadNote();
        AddTrayIcon();
#if !defined(FLOATNOTE_GLASS_LAB) || defined(FLOATNOTE_LOCAL_DESKTOP)
        g_modeHotkey = RegisterHotKey(window, kHotkeyToggleMode, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'E') != FALSE;
        g_hideHotkey =
            RegisterHotKey(window, kHotkeyToggleVisibility, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'H') != FALSE;
        g_passHotkey = RegisterHotKey(window, kHotkeyPassThrough, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P') != FALSE;
#endif
        return 0;
    case kRenderMessage:
        RenderLayeredWindow();
        return 0;
    case kMarkdownPreviewMessage:
        if(g_markdownBlurPending && (GetFocus()!=g_edit || GetForegroundWindow()!=g_window))FinishMarkdownEditing();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            g_markdownBlurPending=true;
            PostMessageW(window,kMarkdownPreviewMessage,0,0);
            KillTimer(window, kCaretTimer);
            g_caretVisible = false;
        } else if (!g_settings.passThrough)
            ResetCaret();
        RequestRender();
        break;
    case WM_DISPLAYCHANGE:
        CloseMenu();
        RecoverWindowPosition();
        ApplyVisuals(window);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
        RefreshTheme();
        if (g_settings.language == UiLanguage::Automatic)
            RefreshLocalizedUi();
        else {
            ApplyVisuals(window);
            UpdateMenuLabels();
        }
        RecoverWindowPosition();
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMPOWERSTATUSCHANGE || wParam == PBT_APMRESUMEAUTOMATIC)
            ApplyVisuals(window);
        else if (wParam == PBT_APMSUSPEND) {
            SavePendingNote();
            SaveSettings();
        }
        return TRUE;
    case WM_NCCALCSIZE:
        if (wParam)
            return 0;
        break;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCPAINT:
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            ZoomFromWheel(wParam);
            return 0;
        }
        return SendMessageW(g_edit, message, wParam, lParam);
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MAXIMIZE || (wParam & 0xfff0) == SC_SIZE)
            return 0;
        break;
    case WM_NCHITTEST:
        return HitTestWindow(window, lParam);
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam);
#ifdef FLOATNOTE_GLASS_LAB
        // DWM glass uses WS_THICKFRAME; DefWindowProc also enforces this minimum
        // on SetWindowPos. Presentation motion must be allowed to reach the dot.
        if(ExperienceAbsorbing()){info->ptMinTrackSize={1,1};return 0;}
#endif
        info->ptMinTrackSize = {ScaleForDpi(window, kMinimumNoteWidth), ScaleForDpi(window, kMinimumNoteHeight)};
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            UpdateLayout(window);
        return 0;
    case WM_DPICHANGED: {
        const auto rect = reinterpret_cast<RECT*>(lParam);
        CloseMenu();
        if (g_menu)
            DestroyWindow(g_menu);
        g_menu = g_slider = g_menuTopmost = g_menuAutostart = g_menuHide = g_menuExit = nullptr;
        g_menuGlass = g_menuPassThrough = g_menuTheme = g_menuLanguage = nullptr;
        g_menuTextColor = g_menuAutoTextColor = g_menuShadow = nullptr;
        RecreateFonts(window);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateLayout(window);
        if (g_pointerDown) {
            GetCursorPos(&g_pointerStart);
            GetWindowRect(window, &g_pointerWindow);
        }
        SetTimer(window, 2, 300, nullptr);
        return 0;
    }
    case WM_EXITSIZEMOVE:
#ifdef FLOATNOTE_GLASS_LAB
        FinishExperienceResize();
#endif
        SaveSettings();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kControlEdit && HIWORD(wParam) == EN_CHANGE && !g_loadingText) {
            g_markdownDirty=true;
            g_hitTextDirty = true;
            g_dirty = true;
            g_saveFailed = false;
            SetTimer(window, kSaveTimer, 700, nullptr);
            InvalidateRect(window, nullptr, FALSE);
            ResetCaret();
            return 0;
        }
        switch (id) {
        case kControlPill:
            TogglePillMenu();
            break;
        default:
            HandleMenuCommand(id);
            break;
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kSaveTimer)
            SavePendingNote();
        else if (wParam == 2) {
            KillTimer(window, 2);
            SaveSettings();
        } else if (wParam == kCaretTimer) {
            if (!g_markdownEditing || GetFocus() != g_edit || GetForegroundWindow() != g_window || !g_isVisible || g_settings.passThrough)
                KillTimer(window, kCaretTimer);
            else {
                g_caretVisible = !g_caretVisible;
                RequestRender();
            }
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == kHotkeyToggleMode) {
            EnterEditor();
        } else if (wParam == kHotkeyToggleVisibility)
            ToggleVisibility();
        else if (wParam == kHotkeyPassThrough) {
            if (g_settings.passThrough)
                EnterEditor();
            else {
                if (!g_isVisible)
                    EnterEditor();
                SetPassThrough(true);
            }
        }
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == NIN_KEYSELECT)
            EnterEditor();
        else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    case kShowEditMessage:
        EnterEditor();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if (g_textMask && reinterpret_cast<HWND>(lParam) == g_edit) {
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
            SetBkColor(reinterpret_cast<HDC>(wParam), g_textMask == 1 ? RGB(0, 0, 0) : RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(GetStockObject(g_textMask == 1 ? BLACK_BRUSH : WHITE_BRUSH));
        }
        SetTextColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam) == g_edit ? kNoteText : kText);
        SetBkColor(reinterpret_cast<HDC>(wParam), kBackground);
        return reinterpret_cast<LRESULT>(g_editBrush);
    case WM_DRAWITEM:
        DrawControl(window, reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        PaintWindow(window, reinterpret_cast<HDC>(wParam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        PaintWindow(window, dc);
        EndPaint(window, &paint);
        RequestRender();
        return 0;
    }
    case WM_QUERYENDSESSION:
        SavePendingNote();
        SaveSettings();
        return g_saveFailed ? FALSE : TRUE;
    case WM_CLOSE:
        CloseMenu();
        SavePendingNote();
        if (g_saveFailed) {
            MessageBoxW(window, Text().saveFailureMessage, Text().saveFailureTitle, MB_OK | MB_ICONWARNING);
            return 0;
        }
        SaveSettings();
        g_closing = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        g_closing = true;
#ifdef FLOATNOTE_GLASS_LAB
        DestroyExperienceUI();
#endif
        g_backdrop.Close();
        if (g_menu)
            DestroyWindow(g_menu);
        if (g_tooltip)
            DestroyWindow(g_tooltip);
        KillTimer(window, kSaveTimer);
        KillTimer(window, 2);
        KillTimer(window, kCaretTimer);
        if (g_modeHotkey)
            UnregisterHotKey(window, kHotkeyToggleMode);
        if (g_hideHotkey)
            UnregisterHotKey(window, kHotkeyToggleVisibility);
        if (g_passHotkey)
            UnregisterHotKey(window, kHotkeyPassThrough);
        RemoveTrayIcon();
        if (g_trayIcon) {
            DestroyIcon(g_trayIcon);
            g_trayIcon = nullptr;
        }
        if (g_textFont)
            DeleteObject(g_textFont);
        if (g_uiFont)
            DeleteObject(g_uiFont);
        if (g_editBrush)
            DeleteObject(g_editBrush);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
bool HandleAppKey(const MSG& message) {
#ifdef FLOATNOTE_GLASS_LAB
    if(HandleExperienceKey(message))return true;
#endif
    if (message.message != WM_KEYDOWN)
        return false;
    const bool menuFocus =
        g_menu && IsWindowVisible(g_menu) && (message.hwnd == g_menu || IsChild(g_menu, message.hwnd));
    if (message.wParam == VK_ESCAPE && g_menu && IsWindowVisible(g_menu)) {
        CloseMenu();
        SetFocus(g_edit);
        ResetCaret();
        return true;
    }
    if (GetKeyState(VK_CONTROL) < 0 && !menuFocus) {
#ifdef FLOATNOTE_GLASS_LAB
        if(message.wParam==VK_OEM_COMMA){ToggleExperienceMenu();return true;}
#endif
        if (message.wParam == 'S') {
            SavePendingNote();
            SaveSettings();
            return true;
        }
        if (message.wParam == VK_OEM_PLUS || message.wParam == VK_ADD) {
            ChangeFontSize(1);
            return true;
        }
        if (message.wParam == VK_OEM_MINUS || message.wParam == VK_SUBTRACT) {
            ChangeFontSize(-1);
            return true;
        }
        if (message.wParam == '0' || message.wParam == VK_NUMPAD0) {
            ChangeFontSize(13 - g_settings.fontSize);
            return true;
        }
    }
    // Escape outside settings never closes or clears a note.
    if (message.wParam == VK_TAB) {
        HWND next = GetNextDlgTabItem(menuFocus ? g_menu : g_window, GetFocus(), GetKeyState(VK_SHIFT) < 0);
        if (next)
            SetFocus(next);
        if (menuFocus && next)
            EnsureMenuFocusVisible(next);
        return true;
    }
    return false;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (g_singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(existing, kShowEditMessage, 0, 0);
        }
        CloseHandle(g_singleInstanceMutex);
        CoUninitialize();
        return 0;
    }

    g_instance = instance;
    g_dataDirectory = ExecutableDirectory() / L"data";
    std::error_code error;
    std::filesystem::create_directories(g_dataDirectory, error);
    g_notePath = g_dataDirectory / L"note.txt";
    g_settingsPath = g_dataDirectory / L"settings.ini";
    LoadSettings();
#ifdef FLOATNOTE_GLASS_LAB
    LoadGlassLabPreferences();
    if(!std::filesystem::exists(g_settingsPath)) {
        g_settings.opacityPercent=8;
        g_settings.width=ScaleForDpi(nullptr,400); g_settings.height=ScaleForDpi(nullptr,260);
        g_settings.fontSize=12; g_settings.glass=true; g_settings.topmost=true;
    }
#endif
    PlaceInitialWindow();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    g_appIcon = CreateNoteIcon(GetSystemMetrics(SM_CXICON));
    windowClass.hIcon = g_appIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) {
        CoUninitialize();
        return 1;
    }

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    DWORD startupExtendedStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;
#ifdef FLOATNOTE_GLASS_LAB
    if(g_settings.topmost)startupExtendedStyle|=WS_EX_TOPMOST;
#endif
    HWND window =
        CreateWindowExW(startupExtendedStyle, kWindowClass, kWindowTitle, WS_POPUP | WS_CLIPCHILDREN, g_settings.x,
                        g_settings.y, g_settings.width, g_settings.height, nullptr, nullptr, instance, nullptr);
    if (!window) {
        CoUninitialize();
        return 2;
    }

    // Saved dimensions are physical pixels. Enforce the minimum after resolving
    // this window's actual monitor DPI, not using the primary monitor at load.
#ifdef FLOATNOTE_GLASS_LAB
    RECT restored{};GetWindowRect(window,&restored);
    const int minimumWidth=ScaleForDpi(window,kMinimumNoteWidth),minimumHeight=ScaleForDpi(window,kMinimumNoteHeight);
    if(restored.right-restored.left<minimumWidth || restored.bottom-restored.top<minimumHeight)
        SetWindowPos(window,nullptr,0,0,std::max(minimumWidth,int(restored.right-restored.left)),
            std::max(minimumHeight,int(restored.bottom-restored.top)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
#endif
    ApplyInteractionMode(true);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
#ifdef FLOATNOTE_GLASS_LAB
    CreateExperienceUI();
    ApplyWindowStacking(true);
#endif
    // Shortcut conflicts never block startup behind a modal dialog. The tray
    // and a second launch remain available, and settings show the exact status.

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (HandleAppKey(message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_singleInstanceMutex) {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
    }
    if (g_appIcon)
        DestroyIcon(g_appIcon);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
