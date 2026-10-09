#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace Proc {
    struct Entry {
        DWORD        pid;
        std::wstring name;
        std::wstring path;
    };

    DWORD FindByName(const std::wstring& imageName);
    std::vector<DWORD> EnumAll(const std::wstring& imageName);
    std::vector<Entry> EnumAll64BitUE();      // best-effort UE / x64 process list
    std::vector<Entry> EnumAllProcesses();    // every pid we can touch
    bool EnablePrivilege(const wchar_t* name);
    std::wstring ImageOf(DWORD pid);
    bool IsProcess64(DWORD pid);
    bool IsSelfElevated();
    bool RelaunchElevated(int argc, wchar_t** argv);
}
