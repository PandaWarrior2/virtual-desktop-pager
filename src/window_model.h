#pragma once
#include "protocol.h"

namespace pager {
// Unknown GUID is NOT a wildcard. Only a real desktop/monitor pair can own a window.
inline int DesktopForWindow(const Snapshot& state,const GUID& id,HMONITOR monitor,DWORD cloak) {
    if(id==GUID{} || monitor!=state.monitor || (cloak & (1u|4u))) return -1;
    for(int i=0;i<state.count;++i) if(state.desktops[i].id==id) return i;
    return -1;
}
inline bool NormalizeWindow(const RECT& window,const RECT& work,BOOL minimized,MiniWindow& mini) {
    RECT visible{};
    if(!IntersectRect(&visible,&window,&work) || work.right<=work.left || work.bottom<=work.top) return false;
    const float width=static_cast<float>(work.right-work.left),height=static_cast<float>(work.bottom-work.top);
    mini.left=(visible.left-work.left)/width; mini.top=(visible.top-work.top)/height;
    mini.right=(visible.right-work.left)/width; mini.bottom=(visible.bottom-work.top)/height;
    mini.minimized=minimized;
    return true;
}
}
