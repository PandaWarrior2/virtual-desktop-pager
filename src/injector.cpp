// Adapted from C:\wndinject\inject.cpp: LoadLibraryW via CreateRemoteThread.
#include "injector.h"
#include <tlhelp32.h>
#include <cstdint>

static uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &module)) do {
        if (_wcsicmp(module.szModule, name) == 0) { base = reinterpret_cast<uintptr_t>(module.modBaseAddr); break; }
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    return base;
}
bool IsWidgetLoaded(DWORD pid) { return ModuleBase(pid, L"vdesktop-widget.dll") != 0; }
bool InjectWidget(DWORD pid, const std::wstring& path, std::wstring& error) {
    if (!pid) { error = L"Taskbar not found"; return false; }
    if (IsWidgetLoaded(pid)) return true; // Do not increment the DLL reference count.
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { error = L"Widget DLL missing: " + path; return false; }
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                 PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) { error = L"OpenProcess: " + std::to_wstring(GetLastError()); return false; }
    USHORT machine = 0, native = 0;
    if (!IsWow64Process2(process, &machine, &native) || machine != IMAGE_FILE_MACHINE_UNKNOWN || native != IMAGE_FILE_MACHINE_AMD64) {
        error = L"An x64 Explorer process is required"; CloseHandle(process); return false;
    }
    // Resolve the module that actually owns the export (KernelBase on recent Windows),
    // then translate its RVA into the target process instead of assuming identical ASLR.
    FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(load), &owner);
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(owner, modulePath, MAX_PATH);
    const wchar_t* name = wcsrchr(modulePath, L'\\'); name = name ? name+1 : modulePath;
    uintptr_t remoteBase = ModuleBase(pid, name);
    if (!load || !owner || !remoteBase) { error = L"Cannot resolve remote LoadLibraryW"; CloseHandle(process); return false; }
    auto remoteLoad = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner));
    SIZE_T size = (path.size() + 1) * sizeof(wchar_t);
    void* memory = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) { error = L"VirtualAllocEx: " + std::to_wstring(GetLastError()); CloseHandle(process); return false; }
    SIZE_T written = 0;
    HANDLE thread = nullptr;
    if (WriteProcessMemory(process, memory, path.c_str(), size, &written) && written == size)
        thread = CreateRemoteThread(process, nullptr, 0, remoteLoad, memory, 0, nullptr);
    if (!thread) {
        error = L"Injection: " + std::to_wstring(GetLastError());
        VirtualFreeEx(process, memory, 0, MEM_RELEASE); CloseHandle(process); return false;
    }
    DWORD wait = WaitForSingleObject(thread, 5000);
    // On timeout the target may still read the path; never free that buffer early.
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, memory, 0, MEM_RELEASE);
    CloseHandle(thread); CloseHandle(process);
    if (wait != WAIT_OBJECT_0) { error = L"LoadLibraryW timed out; no automatic retry for this Explorer process"; return false; }
    if (!IsWidgetLoaded(pid)) { error = L"Explorer could not load the widget DLL"; return false; }
    return true;
}
