#include "about.h"
#include "resource.h"
#include <dwmapi.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dcomp.h>
#include <dwrite.h>
#include <wincodec.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <memory>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t kClass[]=L"VDesktopPager.About";
constexpr wchar_t kRepository[]=L"https://github.com/PandaWarrior2/virtual-desktop-pager";
constexpr int kRepositoryButton=1001;
constexpr int kWidth=480,kHeight=412;
HWND g_about=nullptr;

// Premultiplied composition leaves the system Mica backdrop visible. DWM owns
// the native caption, rounded corners and shadow.
class AboutGraphics {
    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID2D1Device> d2d_;
    ComPtr<IDCompositionDevice> composition_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDCompositionSurface> surface_;
    ComPtr<IDWriteFactory> write_;
    UINT width_=0,height_=0;
public:
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1Bitmap> icon;
    ComPtr<IDWriteTextFormat> title,body,caption,semibold;
    HRESULT Init(HWND hwnd) {
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&d3d_,nullptr,nullptr);
        if(FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,&d3d_,nullptr,nullptr);
        if(FAILED(hr)) return hr;
        ComPtr<IDXGIDevice> dxgi;
        if(FAILED(hr=d3d_.As(&dxgi))) return hr;
        if(FAILED(hr=D2D1CreateDevice(dxgi.Get(),nullptr,&d2d_))) return hr;
        if(FAILED(hr=d2d_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&dc))) return hr;
        dc->SetDpi(96,96);
        if(FAILED(hr=dc->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush))) return hr;
        if(FAILED(hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_.GetAddressOf())))) return hr;
        auto format=[&](float size,DWRITE_FONT_WEIGHT weight,ComPtr<IDWriteTextFormat>& result) {
            HRESULT resultHr=write_->CreateTextFormat(L"Segoe UI Variable Text",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,size,L"en-US",&result);
            if(SUCCEEDED(resultHr)) {
                result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                result->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
            return resultHr;
        };
        if(FAILED(hr=format(28,DWRITE_FONT_WEIGHT_SEMI_BOLD,title))) return hr;
        if(FAILED(hr=format(14,DWRITE_FONT_WEIGHT_NORMAL,body))) return hr;
        if(FAILED(hr=format(12,DWRITE_FONT_WEIGHT_NORMAL,caption))) return hr;
        if(FAILED(hr=format(14,DWRITE_FONT_WEIGHT_SEMI_BOLD,semibold))) return hr;
        ComPtr<IWICImagingFactory> wic;
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))) {
            HICON source=reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_VDESKTOP),IMAGE_ICON,256,256,0));
            if(source) {
                ComPtr<IWICBitmap> bitmap;
                ComPtr<IWICFormatConverter> converter;
                if(SUCCEEDED(wic->CreateBitmapFromHICON(source,&bitmap)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))
                    dc->CreateBitmapFromWicBitmap(converter.Get(),&icon);
                DestroyIcon(source);
            }
        }
        if(FAILED(hr=DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_)))) return hr;
        if(FAILED(hr=composition_->CreateTargetForHwnd(hwnd,TRUE,&target_))) return hr;
        if(FAILED(hr=composition_->CreateVisual(&visual_))) return hr;
        return target_->SetRoot(visual_.Get());
    }
    HRESULT Begin(UINT width,UINT height,float scale,bool dark,bool mica) {
        if(!width || !height) return E_UNEXPECTED;
        HRESULT hr;
        if(!surface_ || width_!=width || height_!=height) {
            surface_.Reset();
            if(FAILED(hr=composition_->CreateSurface(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_ALPHA_MODE_PREMULTIPLIED,&surface_))) return hr;
            if(FAILED(hr=visual_->SetContent(surface_.Get()))) return hr;
            width_=width;height_=height;
        }
        ComPtr<IDXGISurface> surface;POINT offset{};
        if(FAILED(hr=surface_->BeginDraw(nullptr,IID_PPV_ARGS(&surface),&offset))) return hr;
        auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        ComPtr<ID2D1Bitmap1> bitmap;
        if(FAILED(hr=dc->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap))) {surface_->EndDraw();return hr;}
        dc->SetTarget(bitmap.Get());dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x),static_cast<float>(offset.y)));
        dc->PushAxisAlignedClip(D2D1::RectF(0,0,width/scale,height/scale),D2D1_ANTIALIAS_MODE_ALIASED);
        dc->Clear(mica ? D2D1::ColorF(0,0,0,0) : D2D1::ColorF(dark?0x202020:0xf3f3f3));
        dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return S_OK;
    }
    HRESULT End() {
        dc->PopAxisAlignedClip();
        HRESULT hr=dc->EndDraw();dc->SetTarget(nullptr);
        HRESULT finish=surface_->EndDraw();
        if(FAILED(hr)) return hr;
        if(FAILED(finish)) return finish;
        return composition_->Commit();
    }
    void Round(D2D1_RECT_F rect,float radius,UINT color,float opacity=1,bool outline=false) {
        brush->SetColor(D2D1::ColorF(color,opacity));
        auto rounded=D2D1::RoundedRect(rect,radius,radius);
        if(outline) dc->DrawRoundedRectangle(rounded,brush.Get());
        else dc->FillRoundedRectangle(rounded,brush.Get());
    }
    void Text(const wchar_t* value,D2D1_RECT_F rect,IDWriteTextFormat* format,UINT color,bool center=false) {
        format->SetTextAlignment(center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
        brush->SetColor(D2D1::ColorF(color));
        dc->DrawTextW(value,static_cast<UINT32>(wcslen(value)),format,rect,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
};

struct AboutWindow {
    HWND hwnd=nullptr,repository=nullptr,close=nullptr,hover=nullptr;
    bool dark=true,mica=false;
    std::unique_ptr<AboutGraphics> graphics;
    int Pixels(int value) const {return MulDiv(value,GetDpiForWindow(hwnd),96);}
    void Place(HWND child,int x,int y,int width,int height) {
        SetWindowPos(child,nullptr,Pixels(x),Pixels(y),Pixels(width),Pixels(height),SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void Layout() {
        Place(repository,24,252,432,70);Place(close,348,354,108,34);
        Place(GetDlgItem(hwnd,1101),120,26,336,40);
        Place(GetDlgItem(hwnd,1102),120,70,100,24);
        Place(GetDlgItem(hwnd,1103),24,125,432,44);
        Place(GetDlgItem(hwnd,1104),44,191,380,40);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void Theme() {
        DWORD light=1,size=sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
        dark=light==0;
        BOOL useDark=dark;
        DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&useDark,sizeof(useDark));
        DWM_WINDOW_CORNER_PREFERENCE corner=DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));
        DWM_SYSTEMBACKDROP_TYPE backdrop=DWMSBT_MAINWINDOW;
        mica=SUCCEEDED(DwmSetWindowAttribute(hwnd,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop)));
        MARGINS margins{-1,-1,-1,-1};DwmExtendFrameIntoClientArea(hwnd,&margins);
        InvalidateRect(hwnd,nullptr,FALSE);
    }
    void Draw() {
        if(!graphics) {
            graphics=std::make_unique<AboutGraphics>();
            if(FAILED(graphics->Init(hwnd))) {graphics.reset();return;}
        }
        RECT client{};GetClientRect(hwnd,&client);
        auto& g=*graphics;
        if(FAILED(g.Begin(client.right,client.bottom,GetDpiForWindow(hwnd)/96.0f,dark,mica))) {graphics.reset();return;}
        UINT primary=dark?0xf5f5f5:0x1a1a1a,secondary=dark?0xc4c4c4:0x606060,accent=dark?0x99dfff:0x0067c0;
        if(g.icon) g.dc->DrawBitmap(g.icon.Get(),D2D1::RectF(24,26,96,98),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        g.Text(L"VDesktop Pager",D2D1::RectF(120,26,456,66),g.title.Get(),primary);
        g.Round(D2D1::RectF(120,73,204,97),4,dark?0xffffff:0x000000,dark?0.06f:0.045f);
        g.Text(L"Version 1.0",D2D1::RectF(120,73,204,97),g.caption.Get(),secondary,true);
        g.Text(L"Your virtual desktops, at a glance.",D2D1::RectF(24,125,456,147),g.body.Get(),primary);
        g.Text(L"Right in the Windows 11 taskbar, on every monitor.",D2D1::RectF(24,147,456,169),g.body.Get(),secondary);
        g.Round(D2D1::RectF(24,185,456,241),8,0xffffff,dark?0.045f:0.65f);
        g.Round(D2D1::RectF(24.5f,185.5f,455.5f,240.5f),8,dark?0xffffff:0x000000,0.055f,true);
        g.Text(L"Author",D2D1::RectF(44,185,150,241),g.body.Get(),secondary);
        g.Text(L"Moonl1ght",D2D1::RectF(300,185,436,241),g.semibold.Get(),primary,true);
        bool repoPressed=(SendMessageW(repository,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
        g.Round(D2D1::RectF(24,252,456,322),8,0xffffff,repoPressed?0.025f:hover==repository?(dark?0.09f:0.9f):(dark?0.045f:0.65f));
        g.Round(D2D1::RectF(24.5f,252.5f,455.5f,321.5f),8,dark?0xffffff:0x000000,0.055f,true);
        g.Text(L"View on GitHub",D2D1::RectF(44,263,400,287),g.semibold.Get(),accent);
        g.Text(L"PandaWarrior2 / virtual-desktop-pager",D2D1::RectF(44,288,418,308),g.caption.Get(),secondary);
        g.brush->SetColor(D2D1::ColorF(accent));
        g.dc->DrawLine({426,279},{435,279},g.brush.Get(),1.5f);
        g.dc->DrawLine({435,279},{435,288},g.brush.Get(),1.5f);
        g.dc->DrawLine({435,279},{424,290},g.brush.Get(),1.5f);
        bool closePressed=(SendMessageW(close,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
        g.Round(D2D1::RectF(348,354,456,388),4,accent,closePressed?0.75f:hover==close?0.9f:1.0f);
        g.Text(L"Close",D2D1::RectF(348,354,456,388),g.body.Get(),dark?0x00364d:0xffffff,true);
        HWND focus=GetFocus();
        if((SendMessageW(hwnd,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)==0) {
            if(focus==repository) g.Round(D2D1::RectF(27,255,453,319),6,accent,1,true);
            if(focus==close) g.Round(D2D1::RectF(351,357,453,385),2,dark?0x00364d:0xffffff,1,true);
        }
        if(FAILED(g.End())) graphics.reset();
    }
};

LRESULT CALLBACK ButtonProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR reference) {
    auto* window=reinterpret_cast<AboutWindow*>(reference);
    if(message==WM_MOUSEMOVE && window->hover!=hwnd) {
        window->hover=hwnd;TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);
        InvalidateRect(window->hwnd,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE) {window->hover=nullptr;InvalidateRect(window->hwnd,nullptr,FALSE);}
    if(message==WM_SETCURSOR && hwnd==window->repository) {SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
    HWND parent=window->hwnd;
    LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
    // Native controls provide keyboard, accessibility and click semantics. All
    // visuals are rendered on the composition surface above the child HWNDs.
    if(IsWindow(parent) && (message==WM_SETFOCUS || message==WM_KILLFOCUS || message==BM_SETSTATE ||
        message==WM_LBUTTONDOWN || message==WM_LBUTTONUP || message==WM_KEYDOWN || message==WM_KEYUP))
        InvalidateRect(parent,nullptr,FALSE);
    return result;
}

LRESULT CALLBACK AboutProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* window=reinterpret_cast<AboutWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    switch(message) {
    case WM_CREATE: {
        window=new AboutWindow;window->hwnd=hwnd;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(window));
        HINSTANCE module=GetModuleHandleW(nullptr);
        auto button=[&](const wchar_t* text,int id) {
            HWND child=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
            SetWindowSubclass(child,ButtonProc,1,reinterpret_cast<DWORD_PTR>(window));return child;
        };
        window->repository=button(L"View on GitHub: PandaWarrior2 / virtual-desktop-pager",kRepositoryButton);
        window->close=button(L"Close",IDCANCEL);
        const wchar_t* labels[]={L"VDesktop Pager",L"Version 1.0",L"Your virtual desktops, at a glance. Right in the Windows 11 taskbar, on every monitor.",L"Author: Moonl1ght"};
        for(int i=0;i<4;++i) CreateWindowExW(0,L"STATIC",labels[i],WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,
            0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(1101+i)),module,nullptr);
        window->Theme();window->Layout();return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);
        if(window) window->Draw();
        EndPaint(hwnd,&paint);return 0;
    }
    case WM_SIZE: if(window) window->Layout();return 0;
    case WM_DPICHANGED: {
        auto* rect=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
        if(window) window->Layout();return 0;
    }
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_DWMCOMPOSITIONCHANGED:
        if(window) window->Theme();break;
    case WM_UPDATEUISTATE:
        InvalidateRect(hwnd,nullptr,FALSE);break;
    case WM_COMMAND:
        if(LOWORD(wp)==IDCANCEL || LOWORD(wp)==IDOK) {DestroyWindow(hwnd);return 0;}
        if(LOWORD(wp)==kRepositoryButton) {
            auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",kRepository,nullptr,nullptr,SW_SHOWNORMAL));
            if(result<=32) MessageBoxW(hwnd,L"Unable to open the browser.\n\nhttps://github.com/PandaWarrior2/virtual-desktop-pager",L"VDesktop Pager",MB_OK|MB_ICONERROR);
            return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd);return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete window;g_about=nullptr;break;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
} // namespace

void ShowAbout() {
    if(!IsWindow(g_about)) {
        HINSTANCE module=GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=AboutProc;wc.hInstance=module;wc.lpszClassName=kClass;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=reinterpret_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(IDI_VDESKTOP),IMAGE_ICON,32,32,LR_SHARED));
        wc.hIconSm=reinterpret_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(IDI_VDESKTOP),IMAGE_ICON,16,16,LR_SHARED));
        RegisterClassExW(&wc);
        POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
        constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
        constexpr DWORD exStyle=WS_EX_APPWINDOW|WS_EX_NOREDIRECTIONBITMAP|WS_EX_CONTROLPARENT;
        g_about=CreateWindowExW(exStyle,kClass,L"About VDesktop Pager",style,
            monitor.rcWork.left,monitor.rcWork.top,kWidth,kHeight,nullptr,nullptr,module,nullptr);
        if(!g_about) return;
        UINT dpi=GetDpiForWindow(g_about);
        RECT rect{0,0,MulDiv(kWidth,dpi,96),MulDiv(kHeight,dpi,96)};
        AdjustWindowRectExForDpi(&rect,style,FALSE,exStyle,dpi);
        int width=rect.right-rect.left,height=rect.bottom-rect.top;
        SetWindowPos(g_about,nullptr,monitor.rcWork.left+std::max(0L,(monitor.rcWork.right-monitor.rcWork.left-width)/2),
            monitor.rcWork.top+std::max(0L,(monitor.rcWork.bottom-monitor.rcWork.top-height)/2),width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    ShowWindow(g_about,SW_SHOWNORMAL);
    // A hidden controller's initial SW_HIDE must not hide this user-requested UI.
    SetWindowPos(g_about,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(g_about);SetFocus(GetDlgItem(g_about,IDCANCEL));
}
void CloseAbout() {if(IsWindow(g_about)) DestroyWindow(g_about);}
bool AboutVisible() {return IsWindow(g_about)!=FALSE;}
bool ProcessAboutMessage(MSG* message) {
    if(!IsWindow(g_about) || (message->hwnd!=g_about && !IsChild(g_about,message->hwnd))) return false;
    if(message->message==WM_KEYDOWN) {
        if(message->wParam==VK_ESCAPE) {CloseAbout();return true;}
        if(message->wParam==VK_RETURN) {
            HWND focus=GetFocus();
            if(focus==GetDlgItem(g_about,kRepositoryButton)) SendMessageW(g_about,WM_COMMAND,kRepositoryButton,0);
            else CloseAbout();
            return true;
        }
    }
    return IsDialogMessageW(g_about,message)!=FALSE;
}
