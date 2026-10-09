#include "ProcessUtil.h"
#include <TlHelp32.h>
#include <ShellAPI.h>
#include <algorithm>
#include <cwctype>
#pragma comment(lib, "Shell32.lib")

namespace Proc {

    static std::wstring ToLower(std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return (wchar_t)std::towlower(c); });
        return s;
    }

    std::vector<DWORD> EnumAll(const std::wstring& imageName) {
        std::vector<DWORD> out;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        PROCESSENTRY32W pe{ sizeof(pe) };
        std::wstring want = ToLower(imageName);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (ToLower(pe.szExeFile) == want) out.push_back(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return out;
    }

    DWORD FindByName(const std::wstring& imageName) {
        auto v = EnumAll(imageName);
        return v.empty() ? 0 : v.front();
    }

    bool EnablePrivilege(const wchar_t* name) {
        HANDLE tok{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, name, &luid)) { CloseHandle(tok); return false; }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        BOOL ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        CloseHandle(tok);
        return ok && GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    }

    std::wstring ImageOf(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return {};
        wchar_t buf[MAX_PATH]{};
        DWORD sz = MAX_PATH;
        QueryFullProcessImageNameW(h, 0, buf, &sz);
        CloseHandle(h);
        return buf;
    }

    bool IsProcess64(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return false;
        BOOL wow = FALSE;
        IsWow64Process(h, &wow);
        CloseHandle(h);
        // on x64 Windows: !wow => native 64-bit
        return !wow;
    }

    std::vector<Entry> EnumAllProcesses() {
        std::vector<Entry> out;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                Entry e;
                e.pid = pe.th32ProcessID;
                e.name = pe.szExeFile;
                e.path = ImageOf(e.pid);
                out.push_back(std::move(e));
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return out;
    }

    std::vector<Entry> EnumAll64BitUE() {
        auto all = EnumAllProcesses();
        std::vector<Entry> out;
        for (auto& e : all) {
            if (e.pid == 0 || e.pid == 4) continue;
            std::wstring low = ToLower(e.name);
            bool heur = low.find(L"-win64-shipping")  != std::wstring::npos
                     || low.find(L"-win64-development") != std::wstring::npos
                     || low.find(L"ue4editor")        != std::wstring::npos
                     || low.find(L"ue5editor")        != std::wstring::npos
                     || low.find(L"unrealeditor")     != std::wstring::npos
                     || low.find(L"ueeditor")         != std::wstring::npos;
            if (!heur) continue;
            if (!IsProcess64(e.pid)) continue;
            out.push_back(e);
        }
        return out;
    }

    bool IsSelfElevated() {
        HANDLE tok{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
        TOKEN_ELEVATION te{};
        DWORD retlen = 0;
        bool ok = GetTokenInformation(tok, TokenElevation, &te, sizeof(te), &retlen) != 0;
        CloseHandle(tok);
        return ok && te.TokenIsElevated;
    }

    bool RelaunchElevated(int argc, wchar_t** argv) {
        wchar_t self[MAX_PATH]{};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring params;
        for (int i = 1; i < argc; ++i) {
            params += L"\"";
            params += argv[i];
            params += L"\" ";
        }
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = params.c_str();
        sei.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&sei) != 0;
    }
}
