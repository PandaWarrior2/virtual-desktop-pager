#include "../src/window_model.h"
#include <memory>
#include <cassert>
#include <cstdio>
int main() {
    auto s=std::make_unique<pager::Snapshot>();
    s->monitor=reinterpret_cast<HMONITOR>(1);s->count=2;
    s->desktops[0].id.Data1=11;s->desktops[1].id.Data1=22;
    const GUID id=s->desktops[1].id;
    assert(pager::DesktopForWindow(*s,GUID{},s->monitor,0)==-1);
    assert(pager::DesktopForWindow(*s,id,reinterpret_cast<HMONITOR>(2),0)==-1);
    assert(pager::DesktopForWindow(*s,id,s->monitor,1)==-1);
    assert(pager::DesktopForWindow(*s,id,s->monitor,4)==-1);
    assert(pager::DesktopForWindow(*s,id,s->monitor,2)==1);
    assert(pager::DesktopForWindow(*s,id,s->monitor,0)==1);
    pager::MiniWindow mini{};
    RECT work{-1920,357,0,1389};
    assert(!pager::NormalizeWindow(RECT{1,400,400,700},work,FALSE,mini));
    assert(!pager::NormalizeWindow(RECT{-32000,-32000,-31840,-31972},work,FALSE,mini));
    assert(pager::NormalizeWindow(RECT{-2000,300,100,1500},work,FALSE,mini));
    assert(mini.left==0 && mini.top==0 && mini.right==1 && mini.bottom==1);
    puts("PASS: unknown desktop is not a wildcard; monitor ownership; cloaking; offscreen/negative-coordinate clipping");
}
