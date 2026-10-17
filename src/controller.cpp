#include "desktop_api.h"
#include "protocol.h"
#include "window_model.h"
#include "injector.h"
#include "about.h"
#include <UIAutomation.h>
#include <dwmapi.h>
#include <cstdio>
#include <memory>
#include <utility>
#include <map>

using namespace pager;
using Microsoft::WRL::ComPtr;
static SharedMemory g_shared;
static DesktopApi g_api;
static std::vector<DesktopInfo> g_desktops;
static GUID g_current{};
static HWND g_hwnd = nullptr;
static HANDLE g_stop = nullptr, g_widgetStop = nullptr;
static std::wstring g_dir, g_config;
static ULONGLONG g_lastInject = 0;
static DWORD g_failedPid = 0;
struct Position { int left=0, width=0; };
static std::map<HWND,Position> g_positions;
static ComPtr<IUIAutomation> g_uia;
static bool g_auto = true;

static void Log(const wchar_t* message, HRESULT hr = S_OK) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"\\vdesktop.log").c_str(), L"a, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME t{}; GetLocalTime(&t);
        fwprintf(f, L"%02u:%02u:%02u %ls (0x%08X)\n",t.wHour,t.wMinute,t.wSecond,message,static_cast<UINT>(hr));
        fclose(f);
    }
}
static int Setting(const wchar_t* key, int fallback, int low, int high) {
    int value = static_cast<int>(GetPrivateProfileIntW(L"Pager", key, fallback, g_config.c_str()));
    return std::clamp(value, low, high);
}

static Position FindPlacement(HWND taskbar) {
    const int configuredWidth = Setting(L"MaxWidth", 520, 100, 1600);
    auto& position=g_positions[taskbar];
    RECT tb{}; GetWindowRect(taskbar, &tb);
    const float dpi = GetDpiForWindow(taskbar) / 96.0f;
    if (!g_auto) {
        position.left = Setting(L"Left", 12, 0, 16000); position.width = configuredWidth; return position;
    }
    if(!g_uia) {
        if(FAILED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&g_uia)))) return position;
        ComPtr<IUIAutomation2> uia2;
        if (SUCCEEDED(g_uia.As(&uia2))) { uia2->put_ConnectionTimeout(300); uia2->put_TransactionTimeout(300); }
    }
    ComPtr<IUIAutomationElement> root;
    if (FAILED(g_uia->ElementFromHandle(taskbar, &root))) return position;
    VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_ButtonControlTypeId;
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(g_uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition))) return position;
    ComPtr<IUIAutomationCacheRequest> cache;
    if(FAILED(g_uia->CreateCacheRequest(&cache))) return position;
    cache->AddProperty(UIA_BoundingRectanglePropertyId);
    cache->AddProperty(UIA_IsOffscreenPropertyId);
    cache->put_AutomationElementMode(AutomationElementMode_None);
    cache->put_TreeScope(TreeScope_Element);
    ComPtr<IUIAutomationElementArray> elements;
    // One cross-process request per taskbar instead of one request per property/button.
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, condition.Get(),cache.Get(), &elements))) return position;
    std::vector<std::pair<int,int>> used;
    int length = 0; elements->get_Length(&length);
    for (int i = 0; i < length; ++i) {
        ComPtr<IUIAutomationElement> e; elements->GetElement(i, &e);
        RECT r{}; BOOL offscreen = FALSE;
        if (!e || FAILED(e->get_CachedBoundingRectangle(&r))) continue;
        e->get_CachedIsOffscreen(&offscreen);
        if (!offscreen && r.right > r.left && r.bottom > tb.top && r.top < tb.bottom)
            used.emplace_back(std::max(0L,r.left-tb.left), std::min(tb.right-tb.left,r.right-tb.left));
    }
    // Account for other native taskbar widgets, including the wndinject sample.
    for (HWND child = GetWindow(taskbar, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t cls[128]{}; GetClassNameW(child, cls, _countof(cls));
        if (wcsncmp(cls, L"VDesktopPager.Widget.",20) == 0 || !IsWindowVisible(child)) continue;
        RECT r{}; GetWindowRect(child, &r);
        if (r.right > r.left && r.right-r.left < (tb.right-tb.left)/2)
            used.emplace_back(std::max(0L,r.left-tb.left), std::min(tb.right-tb.left,r.right-tb.left));
    }
    if (used.empty()) return position; // Keep last verified placement if UIA is unavailable.
    std::sort(used.begin(), used.end());
    int cursor = 0, bestLeft = 0, bestWidth = 0;
    used.emplace_back(tb.right-tb.left, tb.right-tb.left);
    for (const auto& interval : used) {
        if (interval.first - cursor > bestWidth) { bestLeft = cursor; bestWidth = interval.first-cursor; }
        cursor = std::max(cursor, interval.second);
    }
    const int padding = static_cast<int>(12 * dpi);
    position.left = static_cast<int>((bestLeft + padding) / dpi);
    position.width = std::max(0, std::min(configuredWidth, static_cast<int>((bestWidth - 2*padding) / dpi)));
    return position;
}

struct WindowEnumeration { std::vector<std::unique_ptr<Snapshot>>* snapshots; };
static BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM param) {
    auto& snapshots = *reinterpret_cast<WindowEnumeration*>(param)->snapshots;
    if (!IsWindowVisible(hwnd) || !GetWindowTextLengthW(hwnd)) return TRUE;
    wchar_t cls[128]{}; GetClassNameW(hwnd,cls,_countof(cls));
    if (wcscmp(cls,L"Progman")==0 || wcscmp(cls,L"WorkerW")==0 || wcscmp(cls,L"Shell_TrayWnd")==0 || wcscmp(cls,L"Shell_SecondaryTrayWnd")==0) return TRUE;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_EX_TOOLWINDOW) || (GetWindow(hwnd, GW_OWNER) && !(style & WS_EX_APPWINDOW))) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;
    GUID id{};
    if (!g_api.windows || g_api.windows->GetWindowDesktopId(hwnd, &id)!=S_OK || id==GUID{}) return TRUE;
    DWORD cloak=0;
    DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak));
    // Keep DWM_CLOAKED_SHELL windows on other desktops; ignore application/owner-cloaked helpers.
    if(cloak & (DWM_CLOAKED_APP|DWM_CLOAKED_INHERITED)) return TRUE;
    RECT rect{}; GetWindowRect(hwnd, &rect);
    const bool minimized = IsIconic(hwnd) != FALSE;
    const HMONITOR ownerMonitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(ownerMonitor, &monitor)) return TRUE;
    if (minimized) {
        WINDOWPLACEMENT placement{sizeof(placement)};
        if(!GetWindowPlacement(hwnd,&placement)) return TRUE;
        rect=placement.rcNormalPosition;
        OffsetRect(&rect,monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top);
    } else {
        RECT frame{};
        if(SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame)))) rect=frame;
    }
    const RECT work = monitor.rcWork;
    MiniWindow mini{};
    if(!NormalizeWindow(rect,work,minimized,mini)) return TRUE;
    mini.source=hwnd;
    for (auto& state : snapshots) {
        const int desktop=DesktopForWindow(*state,id,ownerMonitor,cloak);
        if(desktop<0) continue;
        Desktop& d = state->desktops[desktop];
        ++d.windowCount;
        if (d.miniCount >= kMaxWindows) continue;
        d.mini[d.miniCount++]=mini;
    }
    return TRUE;
}

static HRESULT Publish() {
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->version = kVersion;
    HRESULT hr = g_api.List(g_desktops, g_current);
    if (FAILED(hr)) { g_api.Connect(); hr = g_api.List(g_desktops, g_current); }
    snapshot->error = hr;
    snapshot->current = -1;
    snapshot->totalCount = static_cast<int>(g_desktops.size());
    snapshot->count = std::min(snapshot->totalCount, kMaxDesktops);
    for (int i=0; i<snapshot->count; ++i) {
        Desktop& d = snapshot->desktops[i];
        d.id = g_desktops[i].id;
        wcsncpy_s(d.name, g_desktops[i].name.c_str(), _TRUNCATE);
        if (d.id == g_current) snapshot->current = i;
    }
    snapshot->tileWidthDip = Setting(L"TileWidth", 52, 38, 100);
    DWORD light = 0, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    snapshot->dark = !light;
    snapshot->accent = RGB(103, 210, 255);
    std::vector<std::unique_ptr<Snapshot>> snapshots;
    auto taskbars=Taskbars();
    for(HWND taskbar:taskbars) {
        auto state=std::make_unique<Snapshot>(*snapshot);
        state->taskbar=taskbar;
        state->monitor=MonitorFromWindow(taskbar,MONITOR_DEFAULTTONEAREST);
        const Position pos=FindPlacement(taskbar);
        state->leftDip=pos.left;state->maxWidthDip=pos.width;
        snapshots.push_back(std::move(state));
    }
    for(auto it=g_positions.begin();it!=g_positions.end();) {
        if(std::find(taskbars.begin(),taskbars.end(),it->first)==taskbars.end()) it=g_positions.erase(it); else ++it;
    }
    if (SUCCEEDED(hr)) { WindowEnumeration data{&snapshots}; EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&data)); }
    const ULONGLONG now = GetTickCount64();
    std::vector<HWND> notify;
    if (g_shared.Lock(100)) {
        struct Report { HWND taskbar,widget;DWORD pid;HRESULT error;ULONGLONG tick;int current; };
        std::vector<Report> reports;
        for(int i=0;i<g_shared.data->monitorCount;++i) {
            auto& m=g_shared.data->monitors[i];
            reports.push_back({m.snapshot.taskbar,m.widget,m.widgetPid,m.renderError,m.renderTick,m.renderedCurrent});
        }
        g_shared.data->heartbeat = now;
        g_shared.data->monitorCount=static_cast<int>(snapshots.size());
        for(size_t i=0;i<snapshots.size();++i) {
            auto& m=g_shared.data->monitors[i];
            m.snapshot=*snapshots[i]; m.layoutTick=now;
            m.widget=nullptr;m.widgetPid=0;m.renderError=S_OK;m.renderTick=0;m.renderedCurrent=-1;
            for(const auto& report:reports) if(report.taskbar==m.snapshot.taskbar) {
                m.widget=report.widget;m.widgetPid=report.pid;m.renderError=report.error;m.renderTick=report.tick;m.renderedCurrent=report.current;
                if(IsWindow(m.widget)) notify.push_back(m.widget);
                break;
            }
        }
        g_shared.Unlock();
    }
    for(HWND widget:notify) PostMessageW(widget,kRefreshMessage,0,0);
    return hr;
}

static void EnsureInjected() {
    DWORD pid = ShellPid();
    if (!pid) return;
    if (g_failedPid == pid || IsWidgetLoaded(pid)) return;
    ResetEvent(g_widgetStop);
    std::wstring error;
    if (!InjectWidget(pid, g_dir + L"\\vdesktop-widget.dll", error)) { g_failedPid=pid; Log(error.c_str(), E_FAIL); }
    else Log(L"Widget injected");
}
static HRESULT RemoveDesktop(const GUID& target) {
    std::vector<DesktopInfo> desktops;GUID current{};
    HRESULT hr=g_api.List(desktops,current);
    if(FAILED(hr)) return hr;
    if(desktops.size()<=1) return E_ACCESSDENIED;
    auto it=std::find_if(desktops.begin(),desktops.end(),[&](const DesktopInfo& d){return d.id==target;});
    if(it==desktops.end()) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    size_t index=static_cast<size_t>(it-desktops.begin());
    GUID fallback=desktops[index>0?index-1:1].id;
    return g_api.Remove(target,fallback);
}
static LRESULT CALLBACK ControllerProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == kNavigateMessage) {
        std::vector<DesktopInfo> desktops; GUID current{};
        HRESULT hr=g_api.List(desktops,current);
        if (SUCCEEDED(hr) && !desktops.empty()) {
            auto it=std::find_if(desktops.begin(),desktops.end(),[&](const DesktopInfo& d) { return d.id==current; });
            if (it!=desktops.end()) {
                int index=static_cast<int>(it-desktops.begin());
                int target=std::clamp(index+std::clamp(static_cast<int>(lp),-128,128),0,static_cast<int>(desktops.size())-1);
                if(target!=index) hr=g_api.Switch(desktops[target].id);
            }
        }
        if(FAILED(hr)) Log(L"Desktop navigation failed",hr);
        Publish(); return 0;
    }
    if (message == kCommandMessage) {
        Command command = Command::None; GUID target{};
        if (g_shared.Lock(100)) {
            command = g_shared.data->command; target = g_shared.data->target;
            g_shared.data->command = Command::None; g_shared.Unlock();
        }
        HRESULT hr = S_OK;
        if (command == Command::Switch) hr = g_api.Switch(target);
        else if (command == Command::Create) { hr = g_api.Create(&target); if (SUCCEEDED(hr)) hr = g_api.Switch(target); }
        else if (command == Command::Remove) hr=RemoveDesktop(target);
        else if (command == Command::About) ShowAbout();
        else if (command == Command::Stop) SetEvent(g_stop);
        if (FAILED(hr)) Log(L"Desktop command failed", hr);
        Publish();
        return 0;
    }
    if (message == WM_TIMER) {
        if (WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0) { PostQuitMessage(0); return 0; }
        Publish();
        if (GetTickCount64()-g_lastInject >= 2000) { EnsureInjected(); g_lastInject = GetTickCount64(); }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

static int Status() {
    SharedMemory memory;
    if (!memory.Open(false) || !memory.Lock(100)) { wprintf(L"STOPPED\n"); return 1; }
    const auto& d = *memory.data;
    wprintf(L"pid=%lu monitors=%d heartbeatAge=%llu\n",d.ownerPid,d.monitorCount,GetTickCount64()-d.heartbeat);
    bool ok=d.version==kVersion && d.monitorCount>0 && GetTickCount64()-d.heartbeat<5000;
    for(int i=0;i<d.monitorCount;++i) {
        const auto& m=d.monitors[i];const auto& s=m.snapshot;
        wprintf(L"monitor[%d]=%p taskbar=%p widget=%p widgetPid=%lu error=%08X render=%08X layoutAge=%llu renderedDesktop=%d\n",
            i,s.monitor,s.taskbar,m.widget,m.widgetPid,static_cast<UINT>(s.error),static_cast<UINT>(m.renderError),GetTickCount64()-m.layoutTick,m.renderedCurrent+1);
        wprintf(L"desktops=%d current=%d left=%d maxWidth=%d\n",s.totalCount,s.current+1,s.leftDip,s.maxWidthDip);
        for(int j=0;j<s.count;++j) {
            int visible=0;const auto& desktop=s.desktops[j];
            for(int n=0;n<desktop.miniCount;++n) if(!desktop.mini[n].minimized) ++visible;
            wprintf(L"  %d windows=%d visible=%d\n",j+1,desktop.windowCount,visible);
        }
        ok=ok && IsWindow(m.widget) && GetParent(m.widget)==s.taskbar && SUCCEEDED(s.error) && SUCCEEDED(m.renderError);
    }
    memory.Unlock(); return ok ? 0 : 2;
}
static int Stop() {
    HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStop);
    HANDLE widget = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWidgetStop);
    if (e) { SetEvent(e); CloseHandle(e); }
    if (widget) { SetEvent(widget); CloseHandle(widget); }
    // Wait for both the controller and the injected module to release resources.
    DWORD pid = ShellPid();
    for (int i=0;i<100;++i) {
        HANDLE singleton = OpenMutexW(SYNCHRONIZE,FALSE,kSingleton);
        bool running = singleton != nullptr;
        if (singleton) CloseHandle(singleton);
        if (!running && (!pid || !IsWidgetLoaded(pid))) { wprintf(L"STOPPED\n"); return 0; }
        Sleep(50);
    }
    wprintf(L"Stop timed out\n"); return 2;
}

static int IntegrationTest() {
    DesktopApi api;
    HRESULT hr = api.Connect();
    std::vector<DesktopInfo> before, after; GUID original{}, temporary{}, current{};
    if (SUCCEEDED(hr)) hr = api.List(before, original);
    if (FAILED(hr)) { wprintf(L"FAIL connect/list: %08X\n",static_cast<UINT>(hr)); return 1; }
    hr = api.Create(&temporary);
    if (FAILED(hr)) { wprintf(L"FAIL create: %08X\n",static_cast<UINT>(hr)); return 1; }
    bool ok = SUCCEEDED(api.List(after,current)) && after.size() == before.size()+1;
    ok = SUCCEEDED(api.Switch(temporary)) && ok;
    Sleep(500);
    ok = SUCCEEDED(api.List(after,current)) && current == temporary && ok;
    HRESULT restore = api.Switch(original);
    Sleep(300);
    HRESULT remove = SUCCEEDED(restore) ? api.Remove(temporary,original) : restore;
    ok = SUCCEEDED(restore) && SUCCEEDED(remove) && ok;
    ok = SUCCEEDED(api.List(after,current)) && current == original && after.size() == before.size() && ok;
    wprintf(L"%ls: enumerate, create, switch, restore, remove; desktops=%zu current restored\n",ok?L"PASS":L"FAIL",after.size());
    if (FAILED(remove)) {
        wchar_t id[40]{}; StringFromGUID2(temporary,id,_countof(id));
        wprintf(L"Temporary desktop remains: %ls (cleanup HRESULT %08X)\n",id,static_cast<UINT>(remove));
    }
    return ok ? 0 : 1;
}

static int Run() {
    g_dir = Directory(); g_config = g_dir + L"\\..\\vdesktop.ini";
    HANDLE singleton = CreateMutexW(nullptr, FALSE, kSingleton);
    if (!singleton) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(singleton); return 0; }
    g_stop = CreateEventW(nullptr, TRUE, FALSE, kStop);
    g_widgetStop = CreateEventW(nullptr, TRUE, FALSE, kWidgetStop);
    if (!g_stop || !g_widgetStop || !g_shared.Open(true)) { CloseHandle(singleton); return 1; }
    ResetEvent(g_stop); ResetEvent(g_widgetStop);
    g_auto = GetPrivateProfileIntW(L"Pager",L"AutoPosition",1,g_config.c_str()) != 0;
    HRESULT hr = g_api.Connect();
    WNDCLASSW wc{}; wc.lpfnWndProc=ControllerProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=kControllerClass;
    RegisterClassW(&wc);
    g_hwnd = CreateWindowW(kControllerClass,L"VDesktop Pager controller",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    if (!g_hwnd || !g_shared.Lock(100)) { CloseHandle(singleton); return 1; }
    ZeroMemory(g_shared.data,sizeof(SharedData));
    g_shared.data->version=kVersion;
    g_shared.data->ownerPid = GetCurrentProcessId(); g_shared.data->controller = g_hwnd;
    g_shared.Unlock();
    if (SUCCEEDED(hr)) hr = Publish();
    if (FAILED(hr)) {
        Log(L"Virtual desktop API unavailable",hr);
        MessageBoxW(nullptr,L"Unable to connect to virtual desktops on this Windows build. See bin\\vdesktop.log for details.",L"VDesktop Pager",MB_OK|MB_ICONERROR);
    } else {
        EnsureInjected(); SetTimer(g_hwnd,1,100,nullptr);
        Log(L"Controller started");
        MSG msg{};
        while (GetMessageW(&msg,nullptr,0,0)>0) {
            if(!ProcessAboutMessage(&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
    }
    SetEvent(g_widgetStop);
    CloseAbout();
    DestroyWindow(g_hwnd);
    g_shared.Close();
    CloseHandle(g_widgetStop); CloseHandle(g_stop); CloseHandle(singleton);
    Log(L"Controller stopped");
    return FAILED(hr) ? 1 : 0;
}
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1 && _wcsicmp(argv[1],L"/stop")==0) return Stop();
    if (argc > 1 && _wcsicmp(argv[1],L"/status")==0) return Status();
    if (argc > 1 && _wcsicmp(argv[1],L"/about")==0) {
        SharedMemory memory;
        if(memory.Open(false)) return memory.Send(Command::About)?0:1;
        HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(init)) return 1;
        ShowAbout();
        if(!AboutVisible()) {CoUninitialize();return 1;}
        MSG message{};
        while(AboutVisible() && GetMessageW(&message,nullptr,0,0)>0) {
            if(!ProcessAboutMessage(&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
        }
        CoUninitialize();return 0;
    }
    if (argc > 2 && _wcsicmp(argv[1],L"/switch")==0) {
        SharedMemory memory;
        int n = _wtoi(argv[2])-1;
        if (!memory.Open(false) || !memory.Lock(100)) return 1;
        bool valid = memory.data->monitorCount>0 && n>=0 && n<memory.data->monitors[0].snapshot.count;
        GUID id = valid ? memory.data->monitors[0].snapshot.desktops[n].id : GUID{};
        memory.Unlock(); return valid && memory.Send(Command::Switch,id) ? 0 : 1;
    }
    HRESULT init = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return 1;
    int result = argc>1 && _wcsicmp(argv[1],L"/self-test")==0 ? IntegrationTest() : Run();
    g_api.windows.Reset(); // Release COM objects before CoUninitialize.
    g_api = DesktopApi{};
    g_uia.Reset();
    CoUninitialize(); return result;
}
