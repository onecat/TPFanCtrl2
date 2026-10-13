// Integrated, low-overhead Win32 dashboard hosted in original TPFanControl.exe.
// It does NOT access the ThinkPad embedded controller directly.
// SPDX-License-Identifier: Unlicense
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <string>
#include <cstring>
#include "../fancontrol/sharedstate.h"
#include "modern_ui.h"
extern bool FanControlAutostartEnabled();

namespace {
constexpr wchar_t kWindowClass[] = L"TPFanCtrl2ModernDashboard";
constexpr wchar_t kSharedMapping[] = L"Global\\TPFanControl_State";
constexpr UINT_PTR kPollTimer = 1;
constexpr ULONGLONG kStaleMs = 8000;
constexpr ULONGLONG kPendingMs = 10000;

constexpr const wchar_t* kNames[12] = {
    L"CPU", L"APS", L"Card", L"GPU", L"Battery", L"EC 7D",
    L"Battery 2", L"EC 7F", L"Bus", L"PCI", L"Power", L"EC C3"
};

struct Theme {
    COLORREF background, surface, raised, outline, text, secondary, accent, good, warn, track;
};
Theme Palette(bool dark) {
    if (dark) return {
        RGB(15,19,27), RGB(25,31,41), RGB(35,43,55), RGB(53,64,78),
        RGB(242,247,252), RGB(159,174,190), RGB(81,169,254),
        RGB(83,210,158), RGB(253,184,92), RGB(49,60,75)
    };
    return {
        RGB(245,248,252), RGB(255,255,255), RGB(244,248,252),
        RGB(217,226,236), RGB(25,40,55), RGB(100,115,131),
        RGB(22,111,217), RGB(11,138,94), RGB(175,104,7), RGB(226,234,243)
    };
}
bool SystemDark() {
    DWORD light = 1, size = sizeof(light);
    const LONG result = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return result == ERROR_SUCCESS && light == 0;
}

struct Dashboard {
    HWND hwnd = nullptr;
    HWND legacy = nullptr;
    bool autostart = false;
    bool checkAutostart = true;
    ULONGLONG lastAutostartCheck = 0;
    HANDLE mapping = nullptr;
    FCSHARED* shared = nullptr;
    bool dark = false;
    bool accessDenied = false;
    bool pending = false;
    LONG pendingSeq = 0;
    LONG stateSeq = 0;
    ULONGLONG stateAt = 0;
    ULONGLONG commandAt = 0;
    int mode = 1;
    int profile = 0;
    int manualLevel = 4; // Level 0 stops fans, so it is intentionally excluded.
    int fan1 = -1;
    int fan2 = -1;
    int rawFanLevel = 0;
    std::array<int,12> sensors{};
    UINT dpi = 96;
    HFONT normal = nullptr;
    HFONT smallFont = nullptr;
    HFONT section = nullptr;
    HFONT number = nullptr;
    HFONT heading = nullptr;
    RECT modes[3]{};
    RECT down{}, up{}, profileOne{}, profileTwo{}, startup{};
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr;
    HGDIOBJ oldBackBitmap = nullptr;
    int backWidth = 0;
    int backHeight = 0;
} app;

int S(int n) { return MulDiv(n, static_cast<int>(app.dpi), 96); }
RECT R(int x, int y, int w, int h) { return {S(x), S(y), S(x+w), S(y+h)}; }
bool Hit(RECT region, POINT p) { return PtInRect(&region, p) != 0; }

HFONT MakeFont(int size, int weight) {
    return CreateFontW(-S(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}
void FontsDestroy() {
    for (HFONT* font : { &app.normal, &app.smallFont, &app.section, &app.number, &app.heading }) {
        if (*font) { DeleteObject(*font); *font = nullptr; }
    }
}
void FontsCreate() {
    FontsDestroy();
    app.normal = MakeFont(15, FW_NORMAL);
    app.smallFont = MakeFont(13, FW_NORMAL);
    app.section = MakeFont(17, FW_SEMIBOLD);
    app.number = MakeFont(43, FW_BOLD);
    app.heading = MakeFont(23, FW_BOLD);
}
// Reuse the framebuffer while the dashboard is visible. A typical 900 x 860
// bitmap costs about 3 MiB, so release it immediately on hide.
void ReleaseBackBuffer() {
    if (app.backDc) {
        if (app.oldBackBitmap) SelectObject(app.backDc, app.oldBackBitmap);
        if (app.backBitmap) DeleteObject(app.backBitmap);
        DeleteDC(app.backDc);
    }
    app.backDc = nullptr;
    app.backBitmap = nullptr;
    app.oldBackBitmap = nullptr;
    app.backWidth = 0;
    app.backHeight = 0;
}
bool EnsureBackBuffer(HDC target, int width, int height) {
    if (app.backDc && app.backWidth == width && app.backHeight == height)
        return true;
    ReleaseBackBuffer();
    HDC mem = CreateCompatibleDC(target);
    if (!mem) return false;
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (!bitmap) { DeleteDC(mem); return false; }
    HGDIOBJ original = SelectObject(mem, bitmap);
    if (!original || original == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(mem);
        return false;
    }
    app.backDc = mem;
    app.backBitmap = bitmap;
    app.oldBackBitmap = original;
    app.backWidth = width;
    app.backHeight = height;
    return true;
}
void InvalidateDashboard() {
    if (app.hwnd && IsWindowVisible(app.hwnd))
        InvalidateRect(app.hwnd, nullptr, FALSE);
}
void Fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}
void Round(HDC dc, RECT r, COLORREF fill, COLORREF line, int radius=16) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), line);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(radius), S(radius));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}
void Label(HDC dc, const std::wstring& s, int x, int y, int w, int h, HFONT font,
           COLORREF color, UINT flags=DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r=R(x,y,w,h);
    DrawTextW(dc, s.c_str(), -1, &r, flags | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}
std::wstring Reading(int v, const wchar_t* unit) {
    return v >= 0 ? std::to_wstring(v) + unit : L"—";
}
bool Connected() {
    return app.shared && app.stateAt && GetTickCount64()-app.stateAt <= kStaleMs;
}
int Rpm(int low, int high) {
    const int rpm = ((high & 0xff) << 8) | (low & 0xff);
    return rpm <= 0x1fff ? rpm : -1;
}
void Unmap() {
    if (app.shared) { UnmapViewOfFile(app.shared); app.shared=nullptr; }
    if (app.mapping) { CloseHandle(app.mapping); app.mapping=nullptr; }
    app.pending=false;
    app.stateAt=0;
}
void UpdateTelemetry() {
    // EC polling is owned by the existing fan engine, not the dashboard.
    const bool wasConnected = Connected();
    const bool wasPending = app.pending;
    const bool wasDenied = app.accessDenied;
    const bool wasAutostart = app.autostart;
    const LONG wasStateSeq = app.stateSeq;
    const ULONGLONG now = GetTickCount64();

    if (!app.shared) {
        app.mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, kSharedMapping);
        if (app.mapping)
            app.shared = static_cast<FCSHARED*>(
                MapViewOfFile(app.mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(FCSHARED)));
        if (!app.shared) {
            const DWORD error = GetLastError();
            app.accessDenied = (error == ERROR_ACCESS_DENIED);
            if (app.mapping) { CloseHandle(app.mapping); app.mapping=nullptr; }
        } else {
            app.accessDenied = false;
            app.stateSeq = 0;
            app.stateAt = 0;
        }
    }

    if (app.shared) {
        for (int tries=0; tries<3; ++tries) {
            const LONG before = InterlockedCompareExchange(&app.shared->stateSeq,0,0);
            if (!before) {
                app.stateSeq=0; app.stateAt=0; app.pending=false;
                break;
            }
            if (before == app.stateSeq) break;
            const int mode=app.shared->mode;
            const int profile=app.shared->smartLevel;
            const int raw=app.shared->fanCtrl;
            const int fan1=Rpm(app.shared->fan1lo,app.shared->fan1hi);
            const int fan2=Rpm(app.shared->fan2lo,app.shared->fan2hi);
            std::array<int,12> values{};
            for (size_t i=0;i<values.size();++i) {
                const int v=app.shared->sensors[i];
                values[i]=(v>0 && v<125)?v:-1;
            }
            MemoryBarrier();
            if(before != InterlockedCompareExchange(&app.shared->stateSeq,0,0))
                continue;
            app.mode=mode;
            app.profile=profile;
            app.rawFanLevel=raw;
            app.fan1=fan1;
            app.fan2=fan2;
            app.sensors=values;
            app.stateSeq=before;
            app.stateAt=now;
            break;
        }
        if (!Connected()) app.pending=false;
        if (app.pending) {
            LONG ack=InterlockedCompareExchange(&app.shared->ackSeq,0,0);
            if(static_cast<LONG>(ack-app.pendingSeq)>=0 ||
               now-app.commandAt > kPendingMs)
                app.pending=false;
        }
    } else {
        app.pending=false;
    }

    // Avoid querying the Service Control Manager once every second.
    if (app.checkAutostart || !app.lastAutostartCheck ||
        now-app.lastAutostartCheck >= 60000) {
        app.autostart=FanControlAutostartEnabled();
        app.checkAutostart=false;
        app.lastAutostartCheck=now;
    }

    // No new snapshot or connection transition: don't force a repaint.
    if (wasConnected!=Connected() || wasPending!=app.pending ||
        wasDenied!=app.accessDenied || wasAutostart!=app.autostart ||
        wasStateSeq!=app.stateSeq)
        InvalidateDashboard();
}
void RequestMode(int mode) {
    if(!Connected() || app.pending || mode<1 || mode>3) return;
    FCSHARED* state=app.shared;
    state->cmdSmart=-1;
    state->cmdMode=mode;
    const std::string level=std::to_string(app.manualLevel);
    ZeroMemory(state->cmdLevelText,sizeof(state->cmdLevelText));
    memcpy(state->cmdLevelText,level.c_str(),
        std::min(level.size(),sizeof(state->cmdLevelText)-1));
    MemoryBarrier();
    app.pendingSeq=InterlockedIncrement(&state->cmdSeq);
    app.commandAt=GetTickCount64();
    app.pending=true;
    InvalidateRect(app.hwnd,nullptr,FALSE);
}
void RequestProfile(int profile) {
    if(!Connected() || app.pending || (profile != 0 && profile != 1)) return;
    FCSHARED* state=app.shared;
    state->cmdMode=2;
    state->cmdSmart=profile;
    state->cmdLevelText[0]=0;
    MemoryBarrier();
    app.pendingSeq=InterlockedIncrement(&state->cmdSeq);
    app.commandAt=GetTickCount64();
    app.pending=true;
    InvalidateRect(app.hwnd,nullptr,FALSE);
}
void SetFrameTheme() {
    BOOL dark=app.dark?TRUE:FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 11)
    DwmSetWindowAttribute(app.hwnd,20,&dark,sizeof(dark));
}
void ModeCard(HDC dc,int index,int x,int y,int w,const Theme& theme) {
    const int mode=index+1;
    const bool active=Connected() && app.mode==mode;
    const bool enabled=Connected() && !app.pending;
    app.modes[index]=R(x,y,w,111);
    Round(dc,app.modes[index],active?theme.raised:theme.surface,
          active?theme.accent:theme.outline);
    const wchar_t* titles[]={L"BIOS Auto",L"Smart",L"Manual"};
    const wchar_t* details[]={L"Let firmware decide",L"Temperature profile",L"Fixed fan level"};
    Label(dc,titles[index],x+18,y+19,w-36,30,app.section,
          enabled?theme.text:theme.secondary);
    Label(dc,details[index],x+18,y+54,w-36,25,app.smallFont,theme.secondary);
    if(active) {
        Round(dc,R(x+w-34,y+21,16,16),theme.accent,theme.accent,9);
        Label(dc,L"ACTIVE",x+18,y+81,85,20,app.smallFont,theme.good);
    }
}
void SensorCard(HDC dc,int index,int x,int y,int width,const Theme& theme) {
    const int temp=Connected()?app.sensors[index]:-1;
    Round(dc,R(x,y,width,52),theme.surface,theme.outline,12);
    Label(dc,kNames[index],x+12,y+8,width/2+15,18,app.smallFont,theme.secondary);
    Label(dc,Reading(temp,L"°C"),x+width-69,y+6,58,24,app.section,
          temp>=85?theme.warn:theme.text,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    const int barWidth=width-24;
    Fill(dc,R(x+12,y+38,barWidth,4),theme.track);
    if(temp>=0) Fill(dc,R(x+12,y+38,
        barWidth*std::min(100,temp)/100,4),temp>=85?theme.warn:theme.accent);
}
void PaintDashboard(HDC dc,RECT rect) {
    const Theme t=Palette(app.dark);
    const int W=MulDiv(rect.right-rect.left,96,static_cast<int>(app.dpi));
    const int left=38;
    const int content=W-2*left;
    Fill(dc,rect,t.background);
    Label(dc,L"ThinkPad",left,19,200,34,app.heading,t.text);
    Label(dc,L"FAN CONTROL",left,53,250,21,app.smallFont,t.secondary);
    const bool live=Connected();
    const std::wstring status=live
        ? (app.pending?L"Applying change…":L"Engine connected")
        : (app.accessDenied?L"Access denied — check permissions":L"Engine not connected");
    Label(dc,status,W-338,24,296,32,app.normal,live?t.good:t.warn,
          DT_RIGHT|DT_SINGLELINE|DT_VCENTER);

    const int half=(content-16)/2, top=100;
    Round(dc,R(left,top,half,176),t.surface,t.outline);
    Round(dc,R(left+half+16,top,half,176),t.surface,t.outline);
    Label(dc,L"CPU TEMPERATURE",left+22,top+17,half-44,28,app.smallFont,t.secondary);
    Label(dc,Reading(live?app.sensors[0]:-1,L"°C"),left+22,top+47,half-44,62,
          app.number,live && app.sensors[0]>=85?t.warn:t.text);
    Label(dc,live?L"ThinkPad EC sensor":L"Awaiting telemetry",
          left+22,top+128,half-44,25,app.smallFont,t.secondary);
    int rpmX=left+half+16;
    Label(dc,L"FAN SPEED",rpmX+22,top+17,half-44,28,app.smallFont,t.secondary);
    Label(dc,Reading(live?app.fan1:-1,L" RPM"),rpmX+22,top+47,half-44,62,app.number,t.text);
    const std::wstring f2=live && app.fan2>=0 && app.fan2!=app.fan1
        ? L"Second fan: "+Reading(app.fan2,L" RPM")
        : L"EC fan speed feedback";
    Label(dc,f2,rpmX+22,top+128,half-44,25,app.smallFont,t.secondary);

    Label(dc,L"CONTROL MODE",left,295,280,28,app.section,t.text);
    const int mw=(content-26)/3;
    for(int i=0;i<3;i++) ModeCard(dc,i,left+i*(mw+13),329,mw,t);

    Label(dc,L"Manual fan level",left,452,215,35,app.normal,t.secondary);
    app.down=R(left+220,454,34,31);
    app.up=R(left+339,454,34,31);
    const bool canAdjust=live && !app.pending && app.mode==3;
    Round(dc,app.down,canAdjust?t.raised:t.surface,t.outline,9);
    Round(dc,app.up,canAdjust?t.raised:t.surface,t.outline,9);
    Label(dc,L"−",left+220,454,34,31,app.section,canAdjust?t.text:t.secondary,
          DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Label(dc,std::to_wstring(app.manualLevel),left+268,454,56,31,app.section,t.text,
          DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Label(dc,L"+",left+339,454,34,31,app.section,canAdjust?t.text:t.secondary,
          DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Label(dc,L"Levels 1–7 (fan stop disabled)",left+386,454,content-386,31,
          app.smallFont,t.secondary);

    Label(dc,L"SMART PROFILE",left,493,160,29,app.smallFont,t.secondary);
    app.profileOne=R(left+160,493,116,30);
    app.profileTwo=R(left+286,493,116,30);
    for(int i=0;i<2;i++){
        const RECT r=i==0?app.profileOne:app.profileTwo;
        bool selected=live && app.mode==2 && app.profile==i;
        Round(dc,r,selected?t.raised:t.surface,selected?t.accent:t.outline,9);
        Label(dc,i==0?L"Profile 1":L"Profile 2",
              left+(i==0?160:286),493,116,30,app.smallFont,
              live?t.text:t.secondary,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    app.startup=R(W-282,493,244,30);
    Round(dc,app.startup,t.surface,t.outline,9);
    Label(dc,app.autostart?L"Start with Windows: ON":L"Start with Windows: OFF",
          W-278,493,236,30,app.smallFont,t.text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    Label(dc,L"TEMPERATURE SENSORS",left,543,360,31,app.section,t.text);
    const int gap=12,columns=4;
    const int cell=(content-gap*(columns-1))/columns;
    for(int i=0;i<12;++i)
        SensorCard(dc,i,left+(i%columns)*(cell+gap),579+(i/columns)*62,cell,t);
    Label(dc,L"TPFanCtrl2 Modern  •  Closing UI does not stop the fan service",
          left,775,content,26,app.smallFont,t.secondary);
}
void AdjustManual(int amount) {
    if(!Connected()||app.pending||app.mode!=3)return;
    const int level=std::max(1,std::min(7,app.manualLevel+amount));
    if(level!=app.manualLevel){app.manualLevel=level;RequestMode(3);}
}
void ClickAt(int x,int y) {
    if(!Connected()||app.pending) return;
    POINT p{x,y};
    for(int i=0;i<3;i++) if(Hit(app.modes[i],p)) {
        if(app.mode!=i+1)RequestMode(i+1);
        return;
    }
    if(Hit(app.profileOne,p)) {RequestProfile(0);return;}
    if(Hit(app.profileTwo,p)) {RequestProfile(1);return;}
    if(Hit(app.startup,p) && app.legacy) {
        app.checkAutostart = true;
        PostMessageW(app.legacy,WM_COMMAND,MAKEWPARAM(5050,BN_CLICKED),0);
        return;
    }
    if(Hit(app.down,p)) AdjustManual(-1);
    if(Hit(app.up,p)) AdjustManual(1);
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE:
        app.hwnd=hwnd;
        app.dark=SystemDark();
        app.dpi=GetDpiForWindow(hwnd);
        if(!app.dpi)app.dpi=96;
        FontsCreate();
        SetFrameTheme();
        // Defer display-only timer until this window becomes visible.
        return 0;
    case WM_SHOWWINDOW:
        if (wp) {
            app.checkAutostart = true;
            SetTimer(hwnd,kPollTimer,1000,nullptr);
            UpdateTelemetry();
            InvalidateDashboard();
        } else {
            KillTimer(hwnd,kPollTimer);
            ReleaseBackBuffer();
        }
        return 0;
    case WM_TIMER:
        if(wp==kPollTimer){UpdateTelemetry();return 0;}
        break;
    case WM_DPICHANGED: {
        app.dpi=HIWORD(wp);
        FontsCreate();
        ReleaseBackBuffer();
        RECT* suggested=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
            suggested->right-suggested->left,suggested->bottom-suggested->top,
            SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(hwnd,nullptr,TRUE);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        {
            const bool newDark = SystemDark();
            if (newDark != app.dark) {
                app.dark = newDark;
                SetFrameTheme();
                InvalidateDashboard();
            }
        }
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={S(820),S(810)};
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) ShowWindow(hwnd,SW_HIDE);
        return 0;
    case WM_LBUTTONUP:
        ClickAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
        return 0;
    case WM_KEYDOWN:
        if(wp==VK_F5){UpdateTelemetry();return 0;}
        if(wp=='B'){RequestMode(1);return 0;}
        if(wp=='S'){RequestMode(2);return 0;}
        if(wp=='M'){RequestMode(3);return 0;}
        if(wp==VK_UP||wp==VK_RIGHT){AdjustManual(+1);return 0;}
        if(wp==VK_DOWN||wp==VK_LEFT){AdjustManual(-1);return 0;}
        break;
    case WM_CLOSE:
        ShowWindow(hwnd,SW_HIDE);
        return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(hwnd,&ps);
        RECT rect{};
        GetClientRect(hwnd,&rect);
        if(rect.right>0 && rect.bottom>0) {
            if (EnsureBackBuffer(dc,rect.right,rect.bottom)) {
                PaintDashboard(app.backDc,rect);
                BitBlt(dc,0,0,rect.right,rect.bottom,app.backDc,0,0,SRCCOPY);
            } else {
                // Draw directly if allocation fails; don't hide the controls.
                PaintDashboard(dc,rect);
            }
        }
        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd,kPollTimer);
        ReleaseBackBuffer();
        Unmap();FontsDestroy();
        app.hwnd=nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
} // namespace

// The engine, service and dashboard are all modes of one TPFanControl.exe.
bool ModernUI_Start(HWND legacyWindow,bool startMinimized) {
    if(!legacyWindow)return false;
    app.legacy=legacyWindow;
    app.dpi=GetDpiForSystem();
    if(!app.dpi)app.dpi=96;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize=sizeof(wc);
    wc.hInstance=instance;
    wc.lpszClassName=kWindowClass;
    wc.lpfnWndProc=WindowProc;
    wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    wc.hIcon=LoadIconW(nullptr,MAKEINTRESOURCEW(32512));
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    RECT rect{0,0,S(900),S(860)};
    AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,0,app.dpi);
    HWND window=CreateWindowExW(0,kWindowClass,L"TPFanControl — Fan Control",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,
        rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(!window)return false;
    ShowWindow(legacyWindow,SW_HIDE);
    if(!startMinimized)ShowWindow(window,SW_SHOWNORMAL);
    return true;
}
bool ModernUI_IsActive(){return app.hwnd&&IsWindow(app.hwnd);}
void ModernUI_Show(){if(ModernUI_IsActive()){ShowWindow(app.hwnd,SW_RESTORE);SetForegroundWindow(app.hwnd);}}
void ModernUI_Hide(){if(ModernUI_IsActive())ShowWindow(app.hwnd,SW_HIDE);}
void ModernUI_Stop(){if(ModernUI_IsActive())DestroyWindow(app.hwnd);}
