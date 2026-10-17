#pragma once
#include <windows.h>
#include <string>
bool InjectWidget(DWORD pid, const std::wstring& path, std::wstring& error);
bool IsWidgetLoaded(DWORD pid);
