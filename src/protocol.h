#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace pager {
constexpr wchar_t kWindowClass[] = L"VDesktopPager.Widget.v2";
constexpr wchar_t kControllerClass[] = L"VDesktopPager.Controller.v2";
constexpr wchar_t kMap[] = L"Local\\VDesktopPager.State.v2";
constexpr wchar_t kLock[] = L"Local\\VDesktopPager.StateLock.v2";
constexpr wchar_t kSingleton[] = L"Local\\VDesktopPager.Controller.v2";
constexpr wchar_t kWidgetSingleton[] = L"Local\\VDesktopPager.Widget.v2";
constexpr wchar_t kStop[] = L"Local\\VDesktopPager.Stop.v2";
constexpr wchar_t kWidgetStop[] = L"Local\\VDesktopPager.WidgetStop.v2";
constexpr UINT kCommandMessage = WM_APP + 41;
constexpr UINT kNavigateMessage = WM_APP + 42;
constexpr UINT kRefreshMessage = WM_APP + 43;
constexpr UINT kVersion = 2;
constexpr int kMaxMonitors = 16;
constexpr int kMaxDesktops = 128;
constexpr int kMaxWindows = 24;
enum class Command : UINT { None, Switch, Create, Stop, Remove, About };
struct MiniWindow { float left, top, right, bottom; BOOL minimized; HWND source; };
struct Desktop {
    GUID id;
    wchar_t name[96];
    int windowCount;
    int miniCount;
    MiniWindow mini[kMaxWindows];
};
struct Snapshot {
    UINT version;
    HWND taskbar;
    HMONITOR monitor;
    int count;
    int totalCount;
    int current;
    HRESULT error;
    int leftDip;
    int maxWidthDip;
    int tileWidthDip;
    BOOL dark;
    DWORD accent; // RGB
    Desktop desktops[kMaxDesktops];
};
struct MonitorState {
    Snapshot snapshot;
    HWND widget;
    DWORD widgetPid;
    HRESULT renderError;
    ULONGLONG layoutTick;
    ULONGLONG renderTick;
    int renderedCurrent;
};
struct SharedData {
    UINT version;
    DWORD ownerPid;
    HWND controller;
    ULONGLONG heartbeat;
    int monitorCount;
    MonitorState monitors[kMaxMonitors];
    Command command;
    GUID target;
};
class SharedMemory {
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
public:
    SharedData* data = nullptr;
    ~SharedMemory() { Close(); }
    bool Open(bool create) {
        Close();
        mutex_ = create ? CreateMutexW(nullptr, FALSE, kLock) : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, kLock);
        mapping_ = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedData), kMap)
                          : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMap);
        if (mapping_) data = static_cast<SharedData*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData)));
        return data && mutex_;
    }
    bool Lock(DWORD timeout = 10) {
        DWORD r = WaitForSingleObject(mutex_, timeout);
        return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
    }
    void Unlock() { ReleaseMutex(mutex_); }
    void Close() {
        if (data) UnmapViewOfFile(data);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        data = nullptr; mapping_ = mutex_ = nullptr;
    }
    bool Send(Command command, const GUID& target = GUID{}) {
        if (!data || !Lock(100)) return false;
        data->target = target;
        data->command = command;
        HWND hwnd = data->controller;
        Unlock();
        return PostMessageW(hwnd, kCommandMessage, 0, 0) != FALSE;
    }
    bool Navigate(int delta) {
        if (!data || !Lock(100)) return false;
        HWND hwnd=data->controller;
        Unlock();
        // Queue each wheel step separately. The controller resolves it against
        // the live desktop, so rapid scrolling never uses a stale rendered frame.
        return PostMessageW(hwnd,kNavigateMessage,0,static_cast<LPARAM>(delta))!=FALSE;
    }
};
inline std::wstring Directory(HMODULE module = nullptr) {
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, _countof(path));
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L"\\/"));
}
inline DWORD ShellPid() {
    DWORD pid = 0;
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &pid);
    return pid;
}
inline std::vector<HWND> Taskbars() {
    std::vector<HWND> result;
    HWND primary=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(primary) result.push_back(primary);
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL {
        wchar_t cls[128]{};GetClassNameW(hwnd,cls,_countof(cls));
        if(wcscmp(cls,L"Shell_SecondaryTrayWnd")==0)
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    if(result.size()>kMaxMonitors) result.resize(kMaxMonitors);
    return result;
}
}
