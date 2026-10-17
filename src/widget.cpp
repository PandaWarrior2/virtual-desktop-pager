// Native taskbar child + DirectComposition pipeline adapted from
// C:\wndinject\hellodll.cpp. No private desktop COM calls run in Explorer.
#include "protocol.h"
#include "layout.h"
#include "resource.h"
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <dcomp.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <memory>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace pager;
static HMODULE g_module = nullptr;

class Graphics {
    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID2D1Device> d2d_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<IDCompositionDevice> composition_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDCompositionSurface> surface_;
    UINT width_=0,height_=0;
public:
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> label;
    bool Ready() const { return composition_ != nullptr; }
    void Reset() {
        if(dc) dc->SetTarget(nullptr);
        surface_.Reset(); visual_.Reset(); target_.Reset(); composition_.Reset();
        label.Reset(); brush.Reset(); write_.Reset(); dc.Reset(); d2d_.Reset(); d3d_.Reset();
        width_=height_=0;
    }
    HRESULT Init(HWND window) {
        Reset();
        HRESULT hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&d3d_,nullptr,nullptr);
        if (FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&d3d_,nullptr,nullptr);
        if (FAILED(hr)) return hr;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(hr=d3d_.As(&dxgi))) return hr;
        if (FAILED(hr=D2D1CreateDevice(dxgi.Get(),nullptr,&d2d_))) return hr;
        if (FAILED(hr=d2d_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&dc))) return hr;
        dc->SetDpi(96,96);
        if (FAILED(hr=dc->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush))) return hr;
        if (FAILED(hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.GetAddressOf())))) return hr;
        if (FAILED(hr=write_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,11,L"en-US",&label))) return hr;
        label->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        label->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (FAILED(hr=DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_)))) return hr;
        if (FAILED(hr=composition_->CreateTargetForHwnd(window,TRUE,&target_))) return hr;
        if (FAILED(hr=composition_->CreateVisual(&visual_))) return hr;
        return target_->SetRoot(visual_.Get());
    }
    HRESULT Begin(UINT width, UINT height, float scale, bool preview, bool dark) {
        if (!composition_ || !width || !height) return E_UNEXPECTED;
        HRESULT hr;
        if (!surface_ || width_!=width || height_!=height) {
            surface_.Reset();
            if (FAILED(hr=composition_->CreateSurface(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&surface_))) return hr;
            if (FAILED(hr=visual_->SetContent(surface_.Get()))) return hr;
            width_=width; height_=height;
        }
        ComPtr<IDXGISurface> dxgiSurface; POINT offset{};
        if (FAILED(hr=surface_->BeginDraw(nullptr,IID_PPV_ARGS(&dxgiSurface),&offset))) return hr;
        const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        ComPtr<ID2D1Bitmap1> bitmap;
        hr=dc->CreateBitmapFromDxgiSurface(dxgiSurface.Get(),&props,&bitmap);
        if (FAILED(hr)) { surface_->EndDraw(); return hr; }
        dc->SetTarget(bitmap.Get()); dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
        dc->PushAxisAlignedClip(D2D1::RectF(0,0,width/scale,height/scale),D2D1_ANTIALIAS_MODE_ALIASED);
        dc->Clear(preview ? D2D1::ColorF(dark?0x202020:0xf3f3f3) : D2D1::ColorF(0,0,0,0));
        dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return S_OK;
    }
    HRESULT End() {
        dc->PopAxisAlignedClip();
        HRESULT hr=dc->EndDraw(); dc->SetTarget(nullptr);
        HRESULT finish=surface_->EndDraw();
        if (FAILED(hr)) return hr;
        if (FAILED(finish)) return finish;
        return composition_->Commit();
    }
    void Color(UINT rgb, float alpha=1) { brush->SetColor(D2D1::ColorF(rgb,alpha)); }
    void Round(D2D1_RECT_F r,float radius,UINT rgb,float alpha=1,bool outline=false,float stroke=1) {
        Color(rgb,alpha); const auto rr=D2D1::RoundedRect(r,radius,radius);
        if(outline) dc->DrawRoundedRectangle(rr,brush.Get(),stroke); else dc->FillRoundedRectangle(rr,brush.Get());
    }
    void Text(const wchar_t* s,D2D1_RECT_F r,UINT color,float alpha=1) {
        Color(color,alpha); dc->DrawTextW(s,static_cast<UINT32>(wcslen(s)),label.Get(),r,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Line(float x1,float y1,float x2,float y2,UINT color,float alpha=1,float width=1.3f) {
        Color(color,alpha); dc->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),brush.Get(),width);
    }
};

class Widget {
    SharedMemory memory_;
    Snapshot state_{};
    std::unique_ptr<Snapshot> next_=std::make_unique<Snapshot>();
    Graphics graphics_;
    Layout layout_;
    HANDLE stop_=nullptr, owner_=nullptr;
    HWND window_=nullptr,taskbar_=nullptr,tooltip_=nullptr;
    std::wstring tip_;
    float scale_=1;
    int hover_=-1, pressed_=-1,wheel_=0, lastCurrent_=-1;
    UINT width_=0,height_=0;
    bool preview_=false,dirty_=true,tracking_=false;
    ULONGLONG retry_=0;
    HRESULT renderError_=S_OK;
    int renderedCurrent_=-1;

    void Report() {
        if (!preview_ && memory_.Lock()) {
            for(int i=0;i<memory_.data->monitorCount;++i) {
                auto& m=memory_.data->monitors[i];
                if(m.snapshot.taskbar!=taskbar_) continue;
                m.widget=window_;m.widgetPid=GetCurrentProcessId();m.renderError=renderError_;
                m.renderTick=GetTickCount64();m.renderedCurrent=renderedCurrent_;break;
            }
            memory_.Unlock();
        }
    }
    bool Refresh() {
        if (!memory_.Lock()) return true;
        bool found=false;
        if(memory_.data->version==kVersion) for(int i=0;i<memory_.data->monitorCount;++i) {
            if(memory_.data->monitors[i].snapshot.taskbar==taskbar_) {
                *next_=memory_.data->monitors[i].snapshot;found=true;break;
            }
        }
        ULONGLONG age=GetTickCount64()-memory_.data->heartbeat;
        memory_.Unlock();
        if (!found || age>15000 || next_->version!=kVersion) return false;
        next_->count=std::clamp(next_->count,0,kMaxDesktops);
        if (memcmp(&state_,next_.get(),sizeof(state_))!=0) { state_=*next_; dirty_=true; }
        return true;
    }
    void Position() {
        RECT tb{};
        if(preview_) { tb.bottom=48; tb.right=std::max(160,state_.maxWidthDip); scale_=GetDpiForWindow(window_)/96.0f; }
        else { GetClientRect(taskbar_,&tb); scale_=GetDpiForWindow(taskbar_)/96.0f; }
        const int maxWidth=preview_ ? state_.maxWidthDip : std::min(state_.maxWidthDip,static_cast<int>(tb.right/scale_)-state_.leftDip);
        Layout updated=Layout::Make(state_.count,std::max(0,maxWidth),state_.tileWidthDip,layout_.first,state_.current,state_.current!=lastCurrent_);
        lastCurrent_=state_.current;
        if (updated.first!=layout_.first || updated.visible!=layout_.visible || updated.tile!=layout_.tile || updated.width!=layout_.width) dirty_=true;
        layout_=updated;
        UINT newWidth=static_cast<UINT>(std::max(1,static_cast<int>(layout_.width*scale_)));
        UINT newHeight=static_cast<UINT>(preview_ ? 48*scale_ : tb.bottom);
        if (newWidth!=width_ || newHeight!=height_) {
            width_=newWidth; height_=newHeight; dirty_=true;
            if (tooltip_) {
                TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1;
                tool.rect={0,0,static_cast<LONG>(width_),static_cast<LONG>(height_)};
                SendMessageW(tooltip_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&tool));
            }
        }
        if(preview_) {
            RECT r{0,0,static_cast<LONG>(width_),static_cast<LONG>(height_)};
            AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_NOREDIRECTIONBITMAP,GetDpiForWindow(window_));
            SetWindowPos(window_,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        } else {
            SetWindowPos(window_,HWND_TOP,static_cast<int>(state_.leftDip*scale_),0,static_cast<int>(width_),static_cast<int>(height_),SWP_NOACTIVATE|(layout_.width ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
    }
    void Draw() {
        if (!dirty_ || !layout_.width || !width_ || !height_) return;
        if (!graphics_.Ready()) {
            if (GetTickCount64()<retry_) return;
            renderError_=graphics_.Init(window_);
            if (FAILED(renderError_)) { graphics_.Reset(); retry_=GetTickCount64()+3000; Report(); return; }
        }
        HRESULT hr=graphics_.Begin(width_,height_,scale_,preview_,state_.dark!=FALSE);
        if (SUCCEEDED(hr)) {
            Paint(); hr=graphics_.End();
        }
        renderError_=hr;
        if(SUCCEEDED(hr)) renderedCurrent_=state_.current;
        Report();
        if(FAILED(hr)) { graphics_.Reset(); retry_=GetTickCount64()+3000; }
        else dirty_=false;
    }
    void Paint() {
        auto& g=graphics_;
        const float h=height_/scale_, top=(h-30)/2;
        const UINT fg=state_.dark?0xffffff:0x20262c;
        const UINT accent=state_.dark?0x77d6ff:0x006eaa;
        const float base=state_.dark?0.075f:0.045f;
        for(int slot=0;slot<layout_.visible;++slot) {
            const int index=layout_.first+slot;
            const Desktop& desktop=state_.desktops[index];
            const bool active=index==state_.current,hover=index==hover_;
            const float x=static_cast<float>(layout_.start+slot*(layout_.tile+layout_.gap));
            const float w=static_cast<float>(layout_.tile);
            auto rect=D2D1::RectF(x+0.5f,top+0.5f,x+w-0.5f,top+29.5f);
            g.Round(rect,5,active?accent:fg,active?0.12f:(hover?0.14f:base));
            // Window rectangles preserve their positions relative to their monitor's work area.
            const auto inner=D2D1::RectF(x+4,top+4,x+w-4,top+26);
            for (int j=std::min(desktop.miniCount,kMaxWindows)-1;j>=0;--j) {
                const MiniWindow& m=desktop.mini[j];
                if(m.minimized) continue;
                const float iw=inner.right-inner.left,ih=inner.bottom-inner.top;
                auto r=D2D1::RectF(inner.left+m.left*iw,inner.top+m.top*ih,inner.left+m.right*iw,inner.top+m.bottom*ih);
                g.Round(r,1.2f,active?accent:fg,active?0.20f:0.14f);
                g.Round(r,1.2f,active?accent:fg,active?0.40f:0.25f,true,0.65f);
            }
            g.Round(rect,5,active?accent:fg,active?0.95f:(hover?0.42f:0.23f),true,active?1.5f:1);
            // Solid number badge remains readable even on a maximized-window thumbnail.
            g.Round(D2D1::RectF(x+3,top+13,x+20,top+28),3,state_.dark?0x25292e:0xf2f4f6,0.94f);
            wchar_t label[12]{}; swprintf_s(label,L"%d",index+1);
            g.Text(label,D2D1::RectF(x+3,top+12,x+20,top+28),active?accent:fg,active?1:0.82f);
            if(active) g.Round(D2D1::RectF(x+w/2-7,top+33,x+w/2+7,top+35),1,accent);
        }
        float plus=static_cast<float>(layout_.start+layout_.visible*(layout_.tile+layout_.gap));
        if(layout_.overflow) {
            const bool canBack=layout_.first>0,canNext=layout_.first+layout_.visible<state_.count;
            if(hover_==-3) g.Round(D2D1::RectF(1,top,21,top+30),4,fg,0.08f);
            g.Line(13,h/2-4,9,h/2,fg,canBack?0.7f:0.2f);
            g.Line(9,h/2,13,h/2+4,fg,canBack?0.7f:0.2f);
            if(hover_==-4) g.Round(D2D1::RectF(plus,top,plus+20,top+30),4,fg,0.08f);
            g.Line(plus+7,h/2-4,plus+11,h/2,fg,canNext?0.7f:0.2f);
            g.Line(plus+11,h/2,plus+7,h/2+4,fg,canNext?0.7f:0.2f);
            plus+=20;
        }
        if(hover_==-2) g.Round(D2D1::RectF(plus,top,plus+26,top+30),5,fg,0.1f);
        g.Line(plus+8,h/2,plus+18,h/2,fg,hover_==-2?1:0.5f);
        g.Line(plus+13,h/2-5,plus+13,h/2+5,fg,hover_==-2?1:0.5f);
        if(FAILED(state_.error)) g.Text(L"!",D2D1::RectF(0,0,20,h),0xffa366);
    }
    int Hit(LPARAM point) const { return layout_.Hit(GET_X_LPARAM(point)/scale_,GET_Y_LPARAM(point)/scale_,height_/scale_); }
    void Action(int hit) {
        if(hit>=0 && hit<state_.count) memory_.Send(Command::Switch,state_.desktops[hit].id);
        else if(hit==-2) memory_.Send(Command::Create);
        else if(hit==-3) { layout_.first=std::max(0,layout_.first-layout_.visible); dirty_=true; }
        else if(hit==-4) { layout_.first=std::min(std::max(0,state_.count-layout_.visible),layout_.first+layout_.visible); dirty_=true; }
        Draw();
    }
    void Menu(POINT point) {
        HMENU menu=CreatePopupMenu();
        std::vector<GUID> targets;
        POINT client=point;ScreenToClient(window_,&client);
        int selected=layout_.Hit(client.x/scale_,client.y/scale_,height_/scale_);
        if(selected<0 || selected>=state_.count) selected=state_.current;
        GUID removeId=selected>=0?state_.desktops[selected].id:GUID{};
        for(int i=0;i<state_.count;++i) {
            targets.push_back(state_.desktops[i].id);
            std::wstring title=std::to_wstring(i+1)+L"  "+state_.desktops[i].name;
            for(size_t p=0;(p=title.find(L'&',p))!=std::wstring::npos;p+=2) title.insert(p,1,L'&');
            AppendMenuW(menu,MF_STRING|(i==state_.current?MF_CHECKED:0),100+i,title.c_str());
        }
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,1,L"New desktop");
        std::wstring removeTitle=selected>=0 ? L"Remove desktop \u201c"+std::wstring(state_.desktops[selected].name)+L"\u201d" : L"Remove current desktop";
        for(size_t p=0;(p=removeTitle.find(L'&',p))!=std::wstring::npos;p+=2) removeTitle.insert(p,1,L'&');
        AppendMenuW(menu,MF_STRING|((state_.count<=1 || selected<0)?MF_GRAYED:0),3,removeTitle.c_str());
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,4,L"About…");
        AppendMenuW(menu,MF_STRING,2,preview_?L"Close preview":L"Exit widget");
        SetForegroundWindow(window_);
        UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window_,nullptr);
        DestroyMenu(menu);
        PostMessageW(window_,WM_NULL,0,0);
        if(command>=100 && command<100+targets.size()) memory_.Send(Command::Switch,targets[command-100]);
        else if(command==1) Action(-2);
        else if(command==3 && removeId!=GUID{}) memory_.Send(Command::Remove,removeId);
        else if(command==4) memory_.Send(Command::About);
        else if(command==2) { if(preview_) DestroyWindow(window_); else memory_.Send(Command::Stop); }
    }
    void Tooltip() {
        if(hover_>=0 && hover_<state_.count) {
            const auto& d=state_.desktops[hover_];
            tip_=std::to_wstring(hover_+1)+L" · "+d.name+L"\nWindows: "+std::to_wstring(d.windowCount)+L"\nClick to switch · Scroll for adjacent desktops";
        } else if(hover_==-2) tip_=L"Create desktop";
        else if(hover_==-3) tip_=L"Previous desktops";
        else if(hover_==-4) tip_=L"Next desktops";
        else tip_=L"Virtual desktops";
    }
    LRESULT Message(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        switch(message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); Draw(); return 0; }
        case WM_MOUSEACTIVATE: return preview_?MA_ACTIVATE:MA_NOACTIVATE;
        case kRefreshMessage:
        case WM_TIMER:
            if(WaitForSingleObject(stop_,0)==WAIT_OBJECT_0 || (owner_ && WaitForSingleObject(owner_,0)==WAIT_OBJECT_0) ||
                (!preview_ && !IsWindow(taskbar_)) || !Refresh()) { DestroyWindow(hwnd); return 0; }
            Position(); Draw(); return 0;
        case WM_MOUSEMOVE: {
            if(!tracking_) { TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0}; TrackMouseEvent(&track); tracking_=true; }
            int hover=Hit(lp);
            if(hover!=hover_) { hover_=hover; dirty_=true; SendMessageW(tooltip_,TTM_POP,0,0); Draw(); }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_=false; hover_=-1; dirty_=true; Draw(); return 0;
        case WM_LBUTTONDOWN: pressed_=Hit(lp); SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: { int hit=Hit(lp); int down=pressed_; pressed_=-1; ReleaseCapture(); if(hit==down) Action(hit); return 0; }
        case WM_CAPTURECHANGED: pressed_=-1; return 0;
        case WM_MOUSEWHEEL: {
            wheel_+=GET_WHEEL_DELTA_WPARAM(wp);
            if(std::abs(wheel_)>=WHEEL_DELTA) {
                int step=-wheel_/WHEEL_DELTA; wheel_%=WHEEL_DELTA;
                memory_.Navigate(step);
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(point.x==-1 && point.y==-1) { point={0,0}; ClientToScreen(hwnd,&point); }
            Menu(point); return 0;
        }
        case WM_NOTIFY: {
            auto* header=reinterpret_cast<NMHDR*>(lp);
            if(header->code==TTN_GETDISPINFOW) { Tooltip(); reinterpret_cast<NMTTDISPINFOW*>(lp)->lpszText=tip_.data(); }
            return 0;
        }
        case WM_KEYDOWN:
            if(wp==VK_ESCAPE && preview_) DestroyWindow(hwnd);
            else if(wp==VK_LEFT) memory_.Navigate(-1);
            else if(wp==VK_RIGHT) memory_.Navigate(1);
            return 0;
        case WM_DISPLAYCHANGE: case WM_DPICHANGED: case WM_THEMECHANGED: dirty_=true; return 0;
        case WM_DESTROY: KillTimer(hwnd,1); if(preview_) PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        Widget* widget=reinterpret_cast<Widget*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            widget=static_cast<Widget*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            widget->window_=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(widget));
        }
        try { return widget?widget->Message(hwnd,msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp); }
        catch (...) { PostMessageW(hwnd,WM_CLOSE,0,0); return 0; }
    }
public:
    ~Widget() {
        graphics_.Reset();
        if(tooltip_ && IsWindow(tooltip_)) DestroyWindow(tooltip_);
        if(window_ && IsWindow(window_)) DestroyWindow(window_);
        if(!preview_ && memory_.data && memory_.Lock(100)) {
            for(int i=0;i<memory_.data->monitorCount;++i) {
                auto& m=memory_.data->monitors[i];
                if(m.widget==window_) {m.widget=nullptr;m.widgetPid=0;}
            }
            memory_.Unlock();
        }
        if(owner_) CloseHandle(owner_);
        if(stop_) CloseHandle(stop_);
    }
    HWND Taskbar() const {return taskbar_;}
    bool Alive() const {return IsWindow(window_) && (preview_ || IsWindow(taskbar_));}
    int Initialize(bool preview,HWND taskbar) {
        preview_=preview;
        taskbar_=taskbar;
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if(!memory_.Open(false) || !Refresh()) return 2;
        if(memory_.Lock(100)) {
            owner_=OpenProcess(SYNCHRONIZE,FALSE,memory_.data->ownerPid); memory_.Unlock();
        }
        if(!owner_) return 3;
        stop_=OpenEventW(SYNCHRONIZE,FALSE,kWidgetStop);
        if(!stop_ || !taskbar_) return 4;
        DWORD pid=0;GetWindowThreadProcessId(taskbar_,&pid);
        if(!preview && pid!=GetCurrentProcessId()) return 5;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
        WNDCLASSW wc{}; wc.lpfnWndProc=Proc; wc.hInstance=g_module; wc.lpszClassName=kWindowClass; wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
        wc.hIcon=LoadIconW(g_module,MAKEINTRESOURCEW(IDI_VDESKTOP));
        if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 6;
        DWORD style=preview?(WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU):(WS_CHILD);
        DWORD ex=WS_EX_NOREDIRECTIONBITMAP|(preview?0:WS_EX_NOACTIVATE);
        window_=CreateWindowExW(ex,kWindowClass,preview?L"VDesktop Pager — Preview":L"VDesktop Pager",style,400,300,520,48,preview?nullptr:taskbar_,nullptr,g_module,this);
        if(!window_) return 7;
        tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window_,nullptr,g_module,nullptr);
        TOOLINFOW tool{sizeof(tool)}; tool.uFlags=TTF_SUBCLASS; tool.hwnd=window_; tool.uId=1; tool.lpszText=LPSTR_TEXTCALLBACKW; tool.rect={0,0,520,48};
        SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,360);
        SendMessageW(tooltip_,TTM_SETDELAYTIME,TTDT_INITIAL,400);
        SetTimer(window_,1,250,nullptr);
        Position(); Draw(); ShowWindow(window_,SW_SHOWNOACTIVATE); Report();
        return 0;
    }
};

struct SessionHandles {
    HANDLE owner=nullptr,stop=nullptr,singleton=nullptr;
    ~SessionHandles() {if(owner) CloseHandle(owner);if(stop) CloseHandle(stop);if(singleton) CloseHandle(singleton);}
};
static int RunWidgets(bool preview) {
    SessionHandles handles;
    if(!preview) {
        handles.singleton=CreateMutexW(nullptr,FALSE,kWidgetSingleton);
        if(!handles.singleton || GetLastError()==ERROR_ALREADY_EXISTS) return 1;
    }
    SharedMemory memory;
    if(!memory.Open(false) || !memory.Lock(100)) return 2;
    handles.owner=OpenProcess(SYNCHRONIZE,FALSE,memory.data->ownerPid);memory.Unlock();
    handles.stop=OpenEventW(SYNCHRONIZE,FALSE,kWidgetStop);
    if(!handles.owner || !handles.stop) return 3;
    struct ClassScope {~ClassScope(){UnregisterClassW(kWindowClass,g_module);}} classScope;
    std::vector<std::unique_ptr<Widget>> widgets;
    auto reconcile=[&]() {
        widgets.erase(std::remove_if(widgets.begin(),widgets.end(),[](const auto& w){return !w->Alive();}),widgets.end());
        auto taskbars=Taskbars();
        if(preview && taskbars.size()>1) taskbars.resize(1);
        for(HWND taskbar:taskbars) {
            DWORD pid=0;GetWindowThreadProcessId(taskbar,&pid);
            if(!preview && pid!=GetCurrentProcessId()) continue;
            bool exists=std::any_of(widgets.begin(),widgets.end(),[&](const auto& w){return w->Taskbar()==taskbar;});
            if(exists) continue;
            auto widget=std::make_unique<Widget>();
            if(widget->Initialize(preview,taskbar)==0) widgets.push_back(std::move(widget));
        }
    };
    reconcile();
    UINT_PTR timer=SetTimer(nullptr,0,250,nullptr);
    if(!timer) return 4;
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        if(!msg.hwnd && msg.message==WM_TIMER && msg.wParam==timer) {
            if(WaitForSingleObject(handles.owner,0)==WAIT_OBJECT_0 || WaitForSingleObject(handles.stop,0)==WAIT_OBJECT_0) break;
            reconcile();
        } else {TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    KillTimer(nullptr,timer);
    widgets.clear();
    return 0;
}

#ifdef PAGER_PREVIEW
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    g_module=instance;
    return RunWidgets(true);
}
#else
static DWORD WINAPI Worker(void*) {
    DWORD result=ERROR_UNHANDLED_EXCEPTION;
    try { result=RunWidgets(false); }
    catch (...) { /* A C++ allocation failure must not terminate Explorer. */ }
    FreeLibraryAndExitThread(g_module,result);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        g_module=module;
        // /MT needs CRT thread notifications; do not disable them.
        HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
        if(thread) CloseHandle(thread); else return FALSE;
    }
    return TRUE;
}
#endif
