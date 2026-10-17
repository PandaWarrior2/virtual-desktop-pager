#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <servprov.h>
#include <winstring.h>
#include <wrl/client.h>
#include <vector>
#include <string>

// Windows 11 24H2 (26100.2605+) ABI. Reference and license: THIRD_PARTY.md.
// These private interfaces are only used by the controller, never by Explorer.
struct __declspec(uuid("3F07F4BE-B107-441A-AF0F-39D82529072C")) IDesktop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown*, BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetID(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsRemote(BOOL*) = 0;
};
struct __declspec(uuid("53F5CA0B-158F-4124-900C-057158060B27")) IDesktopManager : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop(IUnknown*, IDesktop*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops(IUnknown*, BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IDesktop**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop(IDesktop*, int, IDesktop**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop(IDesktop*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView(IDesktop*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop(IDesktop**) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop(IDesktop*, int) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop(IDesktop*, IDesktop*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID*, IDesktop**) = 0;
};

struct DesktopInfo { GUID id{}; std::wstring name; };
inline bool SupportedDesktopAbi() {
    wchar_t text[32]{}; DWORD size=sizeof(text),ubr=0;
    const wchar_t* key=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    if (RegGetValueW(HKEY_LOCAL_MACHINE,key,L"CurrentBuildNumber",RRF_RT_REG_SZ,nullptr,text,&size)!=ERROR_SUCCESS) return false;
    DWORD build=wcstoul(text,nullptr,10);
    size=sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE,key,L"UBR",RRF_RT_REG_DWORD,nullptr,&ubr,&size);
    return (build==26100 && ubr>=2605) || build==26200 || build==26300;
}
class DesktopApi {
    Microsoft::WRL::ComPtr<IDesktopManager> manager_;
public:
    Microsoft::WRL::ComPtr<IVirtualDesktopManager> windows;
    HRESULT Connect() {
        manager_.Reset(); windows.Reset();
        if (!SupportedDesktopAbi()) return E_NOTIMPL;
        Microsoft::WRL::ComPtr<IServiceProvider> shell;
        const CLSID shellId = {0xc2f03a33,0x21f5,0x47fa,{0xb4,0xbb,0x15,0x63,0x62,0xa2,0xf2,0x39}};
        const GUID serviceId = {0xc5e0cdca,0x7b6e,0x41b2,{0x9f,0xc4,0xd9,0x39,0x75,0xcc,0x46,0x7b}};
        HRESULT hr = CoCreateInstance(shellId, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&shell));
        if (FAILED(hr)) return hr;
        hr = shell->QueryService(serviceId, IID_PPV_ARGS(&manager_));
        if (FAILED(hr)) return hr;
        return CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&windows));
    }
    HRESULT List(std::vector<DesktopInfo>& result, GUID& current) {
        result.clear();
        if (!manager_) return CO_E_NOTINITIALIZED;
        Microsoft::WRL::ComPtr<IDesktop> active;
        HRESULT hr = manager_->GetCurrentDesktop(&active);
        if (FAILED(hr) || !active) return FAILED(hr) ? hr : E_UNEXPECTED;
        if (FAILED(hr = active->GetID(&current))) return hr;
        Microsoft::WRL::ComPtr<IObjectArray> array;
        if (FAILED(hr = manager_->GetDesktops(&array)) || !array) return FAILED(hr) ? hr : E_UNEXPECTED;
        UINT count = 0;
        if (FAILED(hr = array->GetCount(&count))) return hr;
        if (count == 0 || count > 4096) return E_UNEXPECTED;
        for (UINT i = 0; i < count; ++i) {
            Microsoft::WRL::ComPtr<IDesktop> d;
            if (FAILED(hr = array->GetAt(i, IID_PPV_ARGS(&d)))) return hr;
            DesktopInfo info;
            if (FAILED(hr = d->GetID(&info.id))) return hr;
            HSTRING name = nullptr;
            if (SUCCEEDED(d->GetName(&name)) && name) {
                UINT32 size = 0;
                const wchar_t* value = WindowsGetStringRawBuffer(name, &size);
                info.name.assign(value, size);
            }
            if (name) WindowsDeleteString(name);
            if (info.name.empty()) info.name = L"Desktop " + std::to_wstring(i + 1);
            result.push_back(std::move(info));
        }
        return S_OK;
    }
    HRESULT Switch(const GUID& id) {
        if (!manager_) return CO_E_NOTINITIALIZED;
        Microsoft::WRL::ComPtr<IDesktop> desktop;
        HRESULT hr = manager_->FindDesktop(&id, &desktop);
        return SUCCEEDED(hr) ? manager_->SwitchDesktop(desktop.Get()) : hr;
    }
    HRESULT Create(GUID* id) {
        if (!manager_) return CO_E_NOTINITIALIZED;
        Microsoft::WRL::ComPtr<IDesktop> desktop;
        HRESULT hr = manager_->CreateDesktop(&desktop);
        return SUCCEEDED(hr) ? desktop->GetID(id) : hr;
    }
    // Windows moves remaining windows to the fallback desktop when removing one.
    HRESULT Remove(const GUID& id, const GUID& fallback) {
        Microsoft::WRL::ComPtr<IDesktop> desktop, other;
        HRESULT hr = manager_->FindDesktop(&id, &desktop);
        if (SUCCEEDED(hr)) hr = manager_->FindDesktop(&fallback, &other);
        return SUCCEEDED(hr) ? manager_->RemoveDesktop(desktop.Get(), other.Get()) : hr;
    }
};
