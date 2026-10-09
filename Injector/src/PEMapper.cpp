#include "PEMapper.h"
#include "Shellcode.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdio>

namespace PEMapper {

    static std::vector<uint8_t> ReadFile(const std::wstring& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        auto sz = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> buf((size_t)sz);
        f.read((char*)buf.data(), sz);
        return buf;
    }

    static void SetErr(Result& r, const char* w) {
        r.err = w;
        char buf[64];
        snprintf(buf, sizeof(buf), " (gle=%lu)", GetLastError());
        r.err += buf;
    }

    Result MapIntoProcess(DWORD pid, const std::wstring& dllPath) {
        Result r;
        auto file = ReadFile(dllPath);
        if (file.empty()) { r.err = "cannot read dll"; return r; }

        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)file.data();
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) { r.err = "bad DOS header"; return r; }
        IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) { r.err = "bad NT sig"; return r; }
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) { r.err = "DLL is not x64"; return r; }
        if (!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL)) { r.err = "not a DLL"; return r; }

        const uint64_t preferredBase = nt->OptionalHeader.ImageBase;
        const DWORD    imageSize     = nt->OptionalHeader.SizeOfImage;
        const DWORD    headersSize   = nt->OptionalHeader.SizeOfHeaders;

        // ---------- local carve ----------
        std::vector<uint8_t> local(imageSize, 0);
        memcpy(local.data(), file.data(), headersSize);
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (sec[i].SizeOfRawData == 0) continue;
            memcpy(local.data() + sec[i].VirtualAddress,
                   file.data() + sec[i].PointerToRawData,
                   sec[i].SizeOfRawData);
        }

        // ---------- open target ----------
        HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ |
                                   PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProc) { SetErr(r, "OpenProcess failed"); return r; }

        // ---------- allocate image ----------
        uint8_t* remoteImage = (uint8_t*)VirtualAllocEx(hProc, nullptr, imageSize,
                                                        MEM_COMMIT | MEM_RESERVE,
                                                        PAGE_EXECUTE_READWRITE);
        if (!remoteImage) { SetErr(r, "VirtualAllocEx image"); CloseHandle(hProc); return r; }

        // write full image (headers get wiped after mapping)
        if (!WriteProcessMemory(hProc, remoteImage, local.data(), imageSize, nullptr)) {
            SetErr(r, "WriteProcessMemory image"); CloseHandle(hProc); return r;
        }

        // ---------- build context ----------
        ManualMapCtx ctx{};
        ctx.imageBase       = remoteImage;
        ctx.deltaFromPref   = (uint64_t)remoteImage - preferredBase;
        ctx.entryRVA        = nt->OptionalHeader.AddressOfEntryPoint;

        auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        ctx.baseRelocDir  = relocDir.Size ? (remoteImage + relocDir.VirtualAddress) : nullptr;
        ctx.baseRelocSize = relocDir.Size;

        auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        ctx.importDir = impDir.Size ? (remoteImage + impDir.VirtualAddress) : nullptr;

        auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        ctx.tlsDir = tlsDir.Size ? (uint64_t)(remoteImage + tlsDir.VirtualAddress) : 0;

        // kernel32/ntdll are at the same address in all 64-bit user processes on the same boot,
        // so grabbing them locally works for the remote.
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        ctx.pLoadLibraryA   = (decltype(ctx.pLoadLibraryA))GetProcAddress(k32, "LoadLibraryA");
        ctx.pGetProcAddress = (decltype(ctx.pGetProcAddress))GetProcAddress(k32, "GetProcAddress");
        ctx.pGetLastError   = (decltype(ctx.pGetLastError))GetProcAddress(k32, "GetLastError");

        // ---------- write ctx ----------
        void* remoteCtx = VirtualAllocEx(hProc, nullptr, sizeof(ctx), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remoteCtx) { SetErr(r, "VirtualAllocEx ctx"); CloseHandle(hProc); return r; }
        WriteProcessMemory(hProc, remoteCtx, &ctx, sizeof(ctx), nullptr);

        // ---------- write stub ----------
        constexpr SIZE_T stubBudget = 0x1000;
        void* remoteStub = VirtualAllocEx(hProc, nullptr, stubBudget, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!remoteStub) { SetErr(r, "VirtualAllocEx stub"); CloseHandle(hProc); return r; }

        // find the function size by scanning for the ret-sled; simplest: copy a conservative chunk.
        // ManualMapStub is small; copying 0x600 bytes from its start covers it comfortably in both configs.
        if (!WriteProcessMemory(hProc, remoteStub, (void*)&ManualMapStub, 0x600, nullptr)) {
            SetErr(r, "WriteProcessMemory stub"); CloseHandle(hProc); return r;
        }

        // ---------- run stub ----------
        HANDLE th = CreateRemoteThread(hProc, nullptr, 0,
                                       (LPTHREAD_START_ROUTINE)remoteStub, remoteCtx, 0, nullptr);
        if (!th) { SetErr(r, "CreateRemoteThread"); CloseHandle(hProc); return r; }
        WaitForSingleObject(th, INFINITE);
        DWORD ec = 0;
        GetExitCodeThread(th, &ec);
        CloseHandle(th);

        r.remoteExitCode = ec;

        // ---------- wipe PE headers for stealth ----------
        {
            std::vector<uint8_t> zero(headersSize, 0);
            WriteProcessMemory(hProc, remoteImage, zero.data(), headersSize, nullptr);
        }

        // ---------- cleanup ----------
        VirtualFreeEx(hProc, remoteCtx,  0, MEM_RELEASE);
        VirtualFreeEx(hProc, remoteStub, 0, MEM_RELEASE);
        CloseHandle(hProc);

        if (ec != 0) { r.err = "remote stub returned nonzero"; return r; }

        r.ok = true;
        r.remoteBase = (uint64_t)remoteImage;
        return r;
    }

}
