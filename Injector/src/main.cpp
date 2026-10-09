#include <Windows.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cwctype>

#include "ProcessUtil.h"
#include "PEMapper.h"

namespace fs = std::filesystem;

static void Banner() {
    puts("=====================================================================");
    puts(" SDKDumperUE - Manual Map Injector    (made by xSerndev)");
    puts("=====================================================================");
}

static void Usage(const wchar_t* argv0) {
    wprintf(L"usage:\n");
    wprintf(L"  %s <Dumper.dll>                      -- drag & drop, picker opens\n", argv0);
    wprintf(L"  %s --dll <Dumper.dll> --proc <exe>\n", argv0);
    wprintf(L"  %s --dll <Dumper.dll> --pid  <pid>\n", argv0);
    wprintf(L"\n");
    wprintf(L"  * x64 only. DLL and game must both be 64-bit.\n");
    wprintf(L"  * auto-elevates if not admin.\n");
    wprintf(L"  * anti-cheat protected games (EAC/BE/Vanguard) WILL fail with gle=5.\n");
    wprintf(L"\n");
}

static bool OwnsConsole() {
    DWORD pids[2]{};
    DWORD n = GetConsoleProcessList(pids, 2);
    return n <= 1;
}
static void HoldOpen() {
    if (!OwnsConsole()) return;
    puts("");
    puts("[*] press ENTER to exit...");
    (void)getchar();
}

static std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back()))  s.pop_back();
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') { s.erase(s.begin()); s.pop_back(); }
    return s;
}
static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c){ return (wchar_t)towlower(c); });
    return s;
}
static std::wstring Prompt(const wchar_t* label) {
    wprintf(L"%s", label); fflush(stdout);
    wchar_t buf[1024]{};
    if (!_getws_s(buf, (unsigned)std::size(buf))) return {};
    return Trim(buf);
}
static bool HasDllSuffix(const std::wstring& p) {
    std::wstring l = Lower(p);
    return l.size() >= 4 && l.substr(l.size() - 4) == L".dll";
}

// ---------- interactive process picker ----------
static DWORD PickProcess() {
    using Proc::Entry;
    auto ue = Proc::EnumAll64BitUE();
    std::vector<Entry> list = ue;

    bool showAll = false;
    while (true) {
        puts("");
        if (list.empty()) {
            puts(showAll ? "[-] no processes." : "[-] no obvious UE processes found. (type 'a' to list all)");
        } else {
            printf("[*] %s processes:\n", showAll ? "all x64" : "detected UE");
            for (size_t i = 0; i < list.size(); ++i) {
                wprintf(L"  [%2zu] pid=%-6lu %s\n", i, list[i].pid, list[i].name.c_str());
            }
        }
        std::wstring p = Prompt(L"pick index, pid, exe-name, 'a' for all, 'r' to refresh, 'q' quit > ");
        if (p.empty())        continue;
        if (p == L"q" || p == L"Q") return 0;
        if (p == L"r" || p == L"R") {
            list = showAll ? Proc::EnumAllProcesses() : Proc::EnumAll64BitUE();
            continue;
        }
        if (p == L"a" || p == L"A") {
            showAll = true;
            list = Proc::EnumAllProcesses();
            // keep only x64
            list.erase(std::remove_if(list.begin(), list.end(),
                [](const Entry& e){ return !Proc::IsProcess64(e.pid); }), list.end());
            continue;
        }
        // numeric
        if (iswdigit(p[0])) {
            size_t v = (size_t)_wtoi(p.c_str());
            // index into current list?
            if (v < list.size()) return list[v].pid;
            // treat as pid
            return (DWORD)v;
        }
        // name match
        auto found = Proc::EnumAll(p);
        if (!found.empty()) return found.front();
        // partial name match in current list
        std::wstring low = Lower(p);
        for (auto& e : list) if (Lower(e.name).find(low) != std::wstring::npos) return e.pid;
        puts("[-] no match. try again.");
    }
}

// ---------- do inject ----------
static int Run(const std::wstring& dll, DWORD pid) {
    if (!fs::exists(dll)) {
        wprintf(L"[-] dll not found: %s\n", dll.c_str());
        return 2;
    }
    if (!Proc::IsProcess64(pid)) {
        wprintf(L"[-] pid %lu is not x64 (or inaccessible). abort.\n", pid);
        return 4;
    }

    Proc::EnablePrivilege(L"SeDebugPrivilege");

    wprintf(L"[*] target pid=%lu image=%s\n", pid, Proc::ImageOf(pid).c_str());
    wprintf(L"[*] dll=%s\n", dll.c_str());

    auto r = PEMapper::MapIntoProcess(pid, dll);
    if (!r.ok) {
        printf("[-] inject failed: %s (remote exit=0x%08X)\n", r.err.c_str(), r.remoteExitCode);
        if (r.err.find("gle=5") != std::string::npos) {
            puts("");
            puts("    gle=5 (ACCESS_DENIED) on VirtualAllocEx/OpenProcess means an active");
            puts("    user-mode shield (EasyAntiCheat / BattlEye / Vanguard / custom) is");
            puts("    stripping VM-write access from your handle. userland injection into");
            puts("    EAC-protected titles (e.g. live Fortnite) is not possible without a");
            puts("    kernel-mode AC bypass, which is out of scope for this tool.");
            puts("    use a non-protected UE game or offline sample project.");
        }
        return 10;
    }
    printf("[+] mapped. remote base = 0x%016llX. stub exit=0x%08X\n",
           (unsigned long long)r.remoteBase, r.remoteExitCode);
    puts("[+] watch %LocalAppData%\\SDKDumperUE\\<Game>\\ for the dump output.");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    Banner();

    // ---- auto elevate ----
    if (!Proc::IsSelfElevated()) {
        puts("[*] not elevated. relaunching as admin...");
        if (Proc::RelaunchElevated(argc, argv)) return 0;
        puts("[!] elevation declined. continuing anyway (likely will fail).");
    }

    std::wstring dll, procName;
    DWORD pid = 0;

    // ---- flag parse ----
    for (int i = 1; i + 1 < argc; i += 2) {
        std::wstring k = argv[i];
        std::wstring v = argv[i + 1];
        if      (k == L"--dll")  dll = v;
        else if (k == L"--proc") procName = v;
        else if (k == L"--pid")  pid = _wtoi(v.c_str());
    }

    // ---- drag-drop: single arg, looks like a .dll ----
    if (argc == 2 && dll.empty()) {
        std::wstring a = Trim(argv[1]);
        if (HasDllSuffix(a) && fs::exists(a)) {
            dll = a;
            puts("[*] drag-drop detected.");
        }
    }

    // ---- pure double-click (no args at all) ----
    if (argc <= 1 || dll.empty()) {
        Usage(argv[0]);
        if (dll.empty()) {
            std::wstring d = Prompt(L"dll path > ");
            if (d.empty()) { HoldOpen(); return 1; }
            dll = d;
        }
    }

    // ---- resolve process ----
    if (pid == 0 && !procName.empty()) {
        auto all = Proc::EnumAll(procName);
        if (all.size() == 1) pid = all[0];
        else if (all.size() > 1) {
            wprintf(L"[*] %zu instances of %s:\n", all.size(), procName.c_str());
            for (size_t i = 0; i < all.size(); ++i) {
                wprintf(L"  [%zu] pid=%lu %s\n", i, all[i], Proc::ImageOf(all[i]).c_str());
            }
            std::wstring pick = Prompt(L"index > ");
            size_t idx = (size_t)_wtoi(pick.c_str());
            if (idx >= all.size()) idx = 0;
            pid = all[idx];
        }
    }

    // ---- picker if still unresolved ----
    if (pid == 0) {
        pid = PickProcess();
        if (pid == 0) { puts("[*] aborted."); HoldOpen(); return 1; }
    }

    int rc = Run(dll, pid);
    HoldOpen();
    return rc;
}
