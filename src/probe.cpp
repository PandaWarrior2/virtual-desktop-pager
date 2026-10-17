#include "desktop_api.h"
#include <UIAutomation.h>
#include <cstdio>
#include <dwmapi.h>
using Microsoft::WRL::ComPtr;
static DesktopApi* diagnosticApi;
static std::vector<DesktopInfo>* diagnosticDesktops;
static BOOL CALLBACK DiagnoseWindow(HWND hwnd,LPARAM) {
    wchar_t cls[128]{}; GetClassNameW(hwnd,cls,_countof(cls));
    if(wcscmp(cls,L"Shell_TrayWnd")==0 || wcscmp(cls,L"Shell_SecondaryTrayWnd")==0) {
        RECT r{};GetWindowRect(hwnd,&r); DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
        wprintf(L"TASKBAR %p %ls monitor=%p pid=%lu rect=%ld,%ld %ldx%ld\n",hwnd,cls,MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),pid,r.left,r.top,r.right-r.left,r.bottom-r.top);
    }
    if(!IsWindowVisible(hwnd) || !GetWindowTextLengthW(hwnd)) return TRUE;
    LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
    if((ex&WS_EX_TOOLWINDOW) || (GetWindow(hwnd,GW_OWNER) && !(ex&WS_EX_APPWINDOW))) return TRUE;
    GUID id{};HRESULT hr=diagnosticApi->windows->GetWindowDesktopId(hwnd,&id);
    if(FAILED(hr)) return TRUE;
    int desktop=-1;
    for(size_t i=0;i<diagnosticDesktops->size();++i) if((*diagnosticDesktops)[i].id==id) desktop=static_cast<int>(i)+1;
    DWORD cloak=0,pid=0;DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak)); GetWindowThreadProcessId(hwnd,&pid);
    RECT r{};GetWindowRect(hwnd,&r);
    wprintf(L"WINDOW %p class=%ls pid=%lu desktop=%d nullGuid=%d hr=%08X cloak=%lu monitor=%p rect=%ld,%ld %ldx%ld\n",hwnd,cls,pid,desktop,id==GUID{},static_cast<UINT>(hr),cloak,MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),r.left,r.top,r.right-r.left,r.bottom-r.top);
    return TRUE;
}
int wmain(int argc,wchar_t**) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        DesktopApi api;
        HRESULT hr = api.Connect();
        wprintf(L"Connect: %08X\n", static_cast<UINT>(hr));
        std::vector<DesktopInfo> items; GUID current{};
        if (SUCCEEDED(hr)) hr = api.List(items, current);
        wprintf(L"List: %08X count=%zu\n", static_cast<UINT>(hr), items.size());
        for (size_t i = 0; i < items.size(); ++i) wprintf(L"%zu %ls %ls\n", i+1, items[i].id == current ? L"*" : L" ", items[i].name.c_str());
        if(argc>1 && SUCCEEDED(hr)) { diagnosticApi=&api;diagnosticDesktops=&items;EnumWindows(DiagnoseWindow,0);return 0; }
        ComPtr<IUIAutomation> uia;
        hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
        ComPtr<IUIAutomationElement> root;
        HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
        RECT r{}; GetWindowRect(tb, &r);
        wprintf(L"Taskbar: %ld,%ld %ldx%ld DPI=%u\n", r.left,r.top,r.right-r.left,r.bottom-r.top,GetDpiForWindow(tb));
        if (SUCCEEDED(hr)) hr = uia->ElementFromHandle(tb, &root);
        ComPtr<IUIAutomationCondition> condition;
        if (SUCCEEDED(hr)) hr = uia->CreateTrueCondition(&condition);
        ComPtr<IUIAutomationElementArray> elements;
        if (SUCCEEDED(hr)) hr = root->FindAll(TreeScope_Descendants, condition.Get(), &elements);
        int n = 0; if (elements) elements->get_Length(&n);
        for(int i=0;i<n;++i) {
            ComPtr<IUIAutomationElement> e; elements->GetElement(i,&e);
            BSTR name=nullptr, cls=nullptr, aid=nullptr; CONTROLTYPEID type=0;
            e->get_CurrentName(&name); e->get_CurrentClassName(&cls); e->get_CurrentAutomationId(&aid);
            e->get_CurrentControlType(&type); e->get_CurrentBoundingRectangle(&r);
            wprintf(L"%d %ls | %ls | %ls | %ld,%ld %ldx%ld\n",type, name?name:L"",cls?cls:L"",aid?aid:L"",r.left,r.top,r.right-r.left,r.bottom-r.top);
            SysFreeString(name); SysFreeString(cls); SysFreeString(aid);
        }
    }
    CoUninitialize();
}
