#include "../src/layout.h"
#include <cassert>
#include <cstdio>

int main() {
    using pager::Layout;
    auto l=Layout::Make(6,520,52,0,5,true);
    assert(l.visible==6 && !l.overflow && l.width==382);
    assert(l.Hit(10,24,48)==0);
    assert(l.Hit(300,24,48)==5);
    assert(l.Hit(365,24,48)==-2);
    assert(l.Hit(56,24,48)==-1); // Gap must not switch desktops.
    assert(l.Hit(20,1,48)==-1);
    l=Layout::Make(30,300,52,0,29,true);
    assert(l.overflow && l.first+l.visible==30 && l.width<=300);
    assert(l.Hit(10,24,48)==-3);
    assert(l.Hit(l.start+l.visible*58+8.0f,24,48)==-4);
    l=Layout::Make(4,300,52,29,1,true); // Desktop removed while viewing final page.
    assert(l.first==0 && l.visible==4 && !l.overflow);
    l=Layout::Make(6,50,52,0,0,true); // No free space: hide instead of overlapping buttons.
    assert(l.width==0 && l.visible==0);
    for(int count=1;count<=128;++count) for(int width=0;width<=1600;width+=7) {
        for(int active : {0,count/2,count-1}) {
            l=Layout::Make(count,width,52,1000,active,true);
            assert(l.width<=width);
            assert(l.first>=0 && l.first+l.visible<=count);
            if(l.visible) assert(active>=l.first && active<l.first+l.visible);
        }
    }
    puts("PASS: layout, hit testing, overflow, shrinking desktop list, narrow taskbar (87,936 cases)");
}
