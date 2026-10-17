#pragma once
#include <algorithm>

namespace pager {
struct Layout {
    int first = 0, visible = 0, width = 0, tile = 52, gap = 6;
    bool overflow = false;
    int start = 4;
    static Layout Make(int count, int available, int tileWidth, int requestedFirst, int active, bool followActive) {
        Layout l;
        l.tile = std::clamp(tileWidth,38,100);
        const int pitch = l.tile+l.gap;
        l.overflow = count*pitch+34 > available;
        const int navigation = l.overflow ? 40 : 0;
        l.visible = std::min(count,std::max(0,(available-34-navigation)/pitch));
        if (count && l.visible==0) { l.width=0; return l; }
        l.first = std::clamp(requestedFirst,0,std::max(0,count-l.visible));
        if (followActive && active>=0 && active<count) {
            if (active < l.first) l.first=active;
            if (active >= l.first+l.visible) l.first=active-l.visible+1;
        }
        l.start = l.overflow ? 24 : 4;
        l.width = l.visible*pitch+34+navigation;
        return l;
    }
    int Hit(float x, float y, float height) const {
        if (x<0 || x>=width || y<4 || y>height-4) return -1;
        if (overflow && x<22) return -3;
        int relative=static_cast<int>(x)-start;
        int slot=relative/(tile+gap);
        if (relative>=0 && slot<visible) return relative%(tile+gap)<tile ? first+slot : -1;
        int end=start+visible*(tile+gap);
        if (overflow && x>=end && x<end+20) return -4;
        if (x>=end+(overflow?20:0)) return -2;
        return -1;
    }
};
}
