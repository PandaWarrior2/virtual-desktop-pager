#include "../src/protocol.h"
#include "../src/layout.h"
#include <cstdio>

using namespace pager;
static bool Read(SharedMemory& memory, Snapshot& state, HWND& widget) {
    if(!memory.Lock(100)) return false;
    if(!memory.data->monitorCount) {memory.Unlock();return false;}
    state=memory.data->monitors[0].snapshot; widget=memory.data->monitors[0].widget;
    memory.Unlock(); return true;
}
static bool WaitCurrent(SharedMemory& memory,int index) {
    for(int i=0;i<500;++i) {
        Sleep(10);
        if(!memory.Lock(100)) continue;
        bool current=memory.data->monitorCount>0;
        for(int m=0;m<memory.data->monitorCount;++m) {
            const auto& monitor=memory.data->monitors[m];
            current=current && monitor.snapshot.current==index && monitor.renderedCurrent==index;
        }
        memory.Unlock();if(current) return true;
    }
    return false;
}
int wmain() {
    SharedMemory memory;
    auto state=new Snapshot{};
    HWND widget=nullptr;
    if(!memory.Open(false) || !Read(memory,*state,widget) || !IsWindow(widget) || state->count<2) {
        puts("FAIL: running widget with at least two desktops required"); delete state; return 1;
    }
    const int original=state->current;
    const GUID originalId=state->desktops[original].id;
    RECT r{}; GetClientRect(widget,&r);
    const float scale=GetDpiForWindow(widget)/96.0f;
    auto layout=Layout::Make(state->count,state->maxWidthDip,state->tileWidthDip,0,original,true);
    int target=original>0?original-1:1;
    if(target<layout.first || target>=layout.first+layout.visible) { puts("SKIP: adjacent desktop not visible"); delete state; return 2; }
    int x=static_cast<int>((layout.start+(target-layout.first)*(layout.tile+layout.gap)+layout.tile/2)*scale);
    POINT screenPoint{x,(r.bottom-r.top)/2};
    ClientToScreen(widget,&screenPoint);
    HWND hitWindow=WindowFromPoint(screenPoint);
    const bool mouseReachable=hitWindow==widget || IsChild(widget,hitWindow);
    LPARAM point=MAKELPARAM(x,(r.bottom-r.top)/2);
    DWORD_PTR ignored=0;
    bool delivered=SendMessageTimeoutW(widget,WM_LBUTTONDOWN,MK_LBUTTON,point,SMTO_ABORTIFHUNG,1000,&ignored)!=0;
    delivered=SendMessageTimeoutW(widget,WM_LBUTTONUP,0,point,SMTO_ABORTIFHUNG,1000,&ignored)!=0 && delivered;
    bool clicked=delivered && WaitCurrent(memory,target);
    memory.Send(Command::Switch,originalId);
    bool restored=WaitCurrent(memory,original);
    // Mouse-wheel bindings use the real widget window procedure as well.
    short delta=original>0?WHEEL_DELTA:-WHEEL_DELTA;
    SendMessageTimeoutW(widget,WM_MOUSEWHEEL,MAKEWPARAM(0,delta),0,SMTO_ABORTIFHUNG,1000,&ignored);
    bool wheeled=WaitCurrent(memory,target);
    memory.Send(Command::Switch,originalId);
    restored=WaitCurrent(memory,original) && restored;
    // Two wheel messages before the renderer has received a fresh snapshot.
    SendMessageTimeoutW(widget,WM_MOUSEWHEEL,MAKEWPARAM(0,delta),0,SMTO_ABORTIFHUNG,1000,&ignored);
    SendMessageTimeoutW(widget,WM_MOUSEWHEEL,MAKEWPARAM(0,delta),0,SMTO_ABORTIFHUNG,1000,&ignored);
    int twice=std::clamp(original+(delta>0?-2:2),0,state->count-1);
    bool rapid=WaitCurrent(memory,twice);
    memory.Send(Command::Switch,originalId);
    restored=WaitCurrent(memory,original) && restored;
    printf("%s: taskbar hit-test=%d click=%d wheel=%d rapid-wheel=%d original desktop restored=%d\n",mouseReachable&&clicked&&wheeled&&rapid&&restored?"PASS":"FAIL",mouseReachable,clicked,wheeled,rapid,restored);
    delete state; return mouseReachable&&clicked&&wheeled&&rapid&&restored?0:1;
}
