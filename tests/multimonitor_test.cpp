#include "../src/protocol.h"
#include "../src/desktop_api.h"
#include <memory>
#include <functional>
#include <cstdio>

using namespace pager;
static SharedMemory memory;
static std::unique_ptr<SharedData> state=std::make_unique<SharedData>();
static bool Read() {
    if(!memory.Lock(100)) return false;
    memcpy(state.get(),memory.data,sizeof(SharedData));memory.Unlock();return true;
}
static bool WaitFor(const std::function<bool()>& predicate,DWORD timeout=5000) {
    ULONGLONG end=GetTickCount64()+timeout;
    do {
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(Read() && predicate()) return true;
        Sleep(10);
    } while(GetTickCount64()<end);
    return false;
}
static bool Current(const GUID& id) {
    if(state->monitorCount<2) return false;
    for(int i=0;i<state->monitorCount;++i) {
        const auto& m=state->monitors[i];int current=m.snapshot.current;
        if(current<0 || m.snapshot.desktops[current].id!=id || m.renderedCurrent!=current) return false;
    }
    return true;
}
static bool WindowOnlyAt(HWND hwnd,int monitor,const GUID& desktop) {
    int hits=0;bool correct=true;
    for(int m=0;m<state->monitorCount;++m) {
        const auto& s=state->monitors[m].snapshot;
        for(int d=0;d<s.count;++d) for(int w=0;w<s.desktops[d].miniCount;++w) {
            if(s.desktops[d].mini[w].source==hwnd) {++hits;correct=correct && m==monitor && s.desktops[d].id==desktop;}
        }
    }
    return monitor<0 ? hits==0 : hits==1 && correct;
}
static HWND Fixture(int monitor,DWORD extended=0) {
    MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(state->monitors[monitor].snapshot.monitor,&mi);
    HWND w=CreateWindowExW(extended,L"PagerRegressionFixture",L"VDesktop regression fixture",WS_OVERLAPPEDWINDOW,
        mi.rcWork.left+83,mi.rcWork.top+97,337,219,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(w,SW_SHOWNOACTIVATE);return w;
}
static bool Check(bool condition,const char* name) {printf("%s: %s\n",condition?"PASS":"FAIL",name);return condition;}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    DesktopApi api;
    if(FAILED(api.Connect()) || !memory.Open(false) || !Read() || state->monitorCount<2) {puts("FAIL: two live taskbars required");return 1;}
    const auto& first=state->monitors[0].snapshot;
    const GUID original=first.desktops[first.current].id;
    const GUID other=first.desktops[first.current?0:1].id;
    const int originalCount=first.count;
    GUID created{};
    HWND fixture=nullptr,helper=nullptr,moved=nullptr;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PagerRegressionFixture";
    RegisterClassW(&wc);
    bool ok=[&]() {
        auto bars=Taskbars();
        if(!Check(state->monitorCount==static_cast<int>(bars.size()),"one widget for every taskbar")) return false;
        for(int i=0;i<state->monitorCount;++i) {
            const auto& m=state->monitors[i];
            if(!Check(IsWindow(m.widget) && GetParent(m.widget)==m.snapshot.taskbar,"widget parent belongs to its monitor")) return false;
        }
        fixture=Fixture(0);
        if(!Check(WaitFor([&]{return WindowOnlyAt(fixture,0,original);}),"window appears only on primary monitor and its desktop")) return false;
        MONITORINFO second{sizeof(second)};GetMonitorInfoW(state->monitors[1].snapshot.monitor,&second);
        SetWindowPos(fixture,nullptr,second.rcWork.left+83,second.rcWork.top+97,337,219,SWP_NOACTIVATE|SWP_NOZORDER);
        if(!Check(WaitFor([&]{return WindowOnlyAt(fixture,1,original);}),"moving a window transfers its thumbnail to second monitor")) return false;
        helper=Fixture(1,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE);
        if(!Check(WaitFor([&]{return WindowOnlyAt(helper,-1,GUID{});}),"tool window excluded")) return false;
        // Allow taskbar to expose the newly created test app button before measuring relocation.
        Sleep(250);Read();
        int left[kMaxMonitors]{};for(int i=0;i<state->monitorCount;++i) left[i]=state->monitors[i].snapshot.leftDip;
        ULONGLONG start=GetTickCount64();memory.Send(Command::Switch,other);
        if(!Check(WaitFor([&]{return Current(other);}),"all monitors repaint after desktop switch")) return false;
        ULONGLONG activeLatency=GetTickCount64()-start;
        bool changed=WaitFor([&]{for(int i=0;i<state->monitorCount;++i) if(state->monitors[i].snapshot.leftDip!=left[i]) return true;return false;},1000);
        ULONGLONG layoutLatency=GetTickCount64()-start;
        printf("Switch repaint latency: %llu ms; taskbar relocation: %llu ms; position changed=%d\n",activeLatency,layoutLatency,changed);
        if(!Check(activeLatency<1000 && changed && layoutLatency<1000,"desktop switch and relocation complete below one second")) return false;
        if(!Check(WindowOnlyAt(fixture,1,original),"window on inactive desktop is retained only in its own tile")) return false;
        memory.Send(Command::Switch,original);
        if(!WaitFor([&]{return Current(original);})) return false;
        memory.Send(Command::Create);
        if(!Check(WaitFor([&]{const auto& s=state->monitors[0].snapshot;return s.count==originalCount+1 && s.current>=0 && s.desktops[s.current].id!=original;}),"create disposable desktop via widget command")) return false;
        const auto& s=state->monitors[0].snapshot;created=s.desktops[s.current].id;
        if(!WaitFor([&]{return Current(created);})) return false;
        moved=Fixture(0);
        if(!WaitFor([&]{return WindowOnlyAt(moved,0,created);})) return false;
        memory.Send(Command::Remove,created);
        if(!Check(WaitFor([&]{const auto& t=state->monitors[0].snapshot;return t.count==originalCount && std::none_of(t.desktops,t.desktops+t.count,[&](const Desktop& d){return d.id==created;});}),"quick removal deletes only the disposable desktop")) return false;
        GUID windowDesktop{};
        if(!Check(IsWindow(moved) && SUCCEEDED(api.windows->GetWindowDesktopId(moved,&windowDesktop)) && windowDesktop!=created && windowDesktop!=GUID{},"removed desktop's window survives on fallback desktop")) return false;
        created={};
        memory.Send(Command::Switch,original);
        if(!WaitFor([&]{return Current(original);})) return false;
        HWND secondaryWidget=state->monitors[1].widget;
        HWND primaryWidget=state->monitors[0].widget;
        PostMessageW(secondaryWidget,WM_CLOSE,0,0);
        if(!Check(WaitFor([&]{return IsWindow(state->monitors[1].widget) && state->monitors[1].widget!=secondaryWidget && state->monitors[0].widget==primaryWidget;}),"secondary widget recreated without disturbing primary widget")) return false;
        return true;
    }();
    if(moved) DestroyWindow(moved);if(helper) DestroyWindow(helper);if(fixture) DestroyWindow(fixture);
    if(created!=GUID{}) {api.Switch(original);api.Remove(created,original);}
    memory.Send(Command::Switch,original);
    bool restored=WaitFor([&]{return Current(original) && state->monitors[0].snapshot.count==originalCount;});
    Check(restored,"original desktop and desktop count restored");
    api=DesktopApi{};CoUninitialize();
    return ok && restored?0:1;
}
