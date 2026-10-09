#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Mem {

struct Module {
    uintptr_t base = 0;
    size_t    size = 0;
    std::string name;
};

inline Module GetMainModule() {
    HMODULE h = GetModuleHandleA(nullptr);
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi));
    char path[MAX_PATH]{};
    GetModuleFileNameA(h, path, MAX_PATH);
    std::string name = path;
    size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return { (uintptr_t)mi.lpBaseOfDll, mi.SizeOfImage, name };
}

inline bool IsReadable(uintptr_t p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

inline bool PatternMatch(const uint8_t* data, const char* pattern) {
    for (; *pattern; ++data) {
        if (*pattern == ' ') { ++pattern; continue; }
        if (*pattern == '?') {
            ++pattern;
            if (*pattern == '?') ++pattern;
            continue;
        }
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int h = hex(pattern[0]);
        int l = hex(pattern[1]);
        if (h < 0 || l < 0) return false;
        uint8_t b = (uint8_t)((h << 4) | l);
        if (*data != b) return false;
        pattern += 2;
    }
    return true;
}

inline uintptr_t FindPattern(const Module& m, const char* pattern) {
    const uint8_t* begin = (const uint8_t*)m.base;
    const uint8_t* end   = begin + m.size;

    // walk sections to limit scan to code
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)m.base;
    IMAGE_NT_HEADERS* nt  = (IMAGE_NT_HEADERS*)(m.base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* s = begin + sec[i].VirtualAddress;
        const uint8_t* e = s + sec[i].Misc.VirtualSize;
        if (e > end) e = end;
        for (const uint8_t* p = s; p + 16 < e; ++p) {
            if (PatternMatch(p, pattern)) return (uintptr_t)p;
        }
    }
    return 0;
}

// resolve relative call/lea displacement: at <addr>, instruction is opcode(s) + rel32 at offset <relOff>, length <instrLen>
inline uintptr_t ResolveRel32(uintptr_t addr, int relOff, int instrLen) {
    int32_t rel = *(int32_t*)(addr + relOff);
    return addr + instrLen + rel;
}

} // namespace Mem
