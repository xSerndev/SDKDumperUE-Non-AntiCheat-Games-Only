#pragma once
#include <Windows.h>
#include <cstdint>

// Context block pushed into the target. Addresses are resolved locally (same base in Win kernel32/ntdll).
struct ManualMapCtx {
    uint8_t*       imageBase;
    uint8_t*       baseRelocDir;   // RVA'd into imageBase; 0 if none
    uint32_t       baseRelocSize;
    uint8_t*       importDir;      // image import descriptor; 0 if none
    uint64_t       deltaFromPref;  // imageBase - preferredBase
    uint64_t       entryRVA;       // AddressOfEntryPoint (DllMain)
    uint64_t       tlsDir;         // IMAGE_TLS_DIRECTORY64 pointer or 0

    // function pointers (resolved by injector; same addresses in target due to ASLR per-boot)
    decltype(&LoadLibraryA)       pLoadLibraryA;
    decltype(&GetProcAddress)     pGetProcAddress;
    decltype(&GetLastError)       pGetLastError;
};

// executed in the remote process. Must be position-independent and must NOT touch any imports
// beyond those passed in ctx.
#pragma runtime_checks("", off)
#pragma optimize("", on)
inline DWORD __stdcall ManualMapStub(ManualMapCtx* c) {
    // ---------- base relocations ----------
    if (c->baseRelocDir && c->deltaFromPref) {
        IMAGE_BASE_RELOCATION* reloc = (IMAGE_BASE_RELOCATION*)c->baseRelocDir;
        uint32_t consumed = 0;
        while (consumed < c->baseRelocSize && reloc->SizeOfBlock) {
            uint32_t count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            uint16_t* list = (uint16_t*)((uint8_t*)reloc + sizeof(IMAGE_BASE_RELOCATION));
            uint8_t* page = c->imageBase + reloc->VirtualAddress;
            for (uint32_t i = 0; i < count; ++i) {
                uint16_t e = list[i];
                int type = e >> 12;
                int off  = e & 0xFFF;
                if (type == IMAGE_REL_BASED_DIR64) {
                    *(uint64_t*)(page + off) += c->deltaFromPref;
                }
            }
            consumed += reloc->SizeOfBlock;
            reloc = (IMAGE_BASE_RELOCATION*)((uint8_t*)reloc + reloc->SizeOfBlock);
        }
    }

    // ---------- imports ----------
    if (c->importDir) {
        IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)c->importDir;
        while (imp->Name) {
            const char* dllName = (const char*)(c->imageBase + imp->Name);
            HMODULE mod = c->pLoadLibraryA(dllName);
            if (!mod) return 0x1001;

            uint64_t* thunk = (uint64_t*)(c->imageBase + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            uint64_t* iat   = (uint64_t*)(c->imageBase + imp->FirstThunk);

            while (*thunk) {
                FARPROC fn = nullptr;
                if (*thunk & 0x8000000000000000ULL) {
                    fn = c->pGetProcAddress(mod, (LPCSTR)(*thunk & 0xFFFF));
                } else {
                    IMAGE_IMPORT_BY_NAME* byName = (IMAGE_IMPORT_BY_NAME*)(c->imageBase + *thunk);
                    fn = c->pGetProcAddress(mod, byName->Name);
                }
                if (!fn) return 0x1002;
                *iat = (uint64_t)fn;
                ++thunk; ++iat;
            }
            ++imp;
        }
    }

    // ---------- TLS callbacks ----------
    if (c->tlsDir) {
        IMAGE_TLS_DIRECTORY64* tls = (IMAGE_TLS_DIRECTORY64*)c->tlsDir;
        if (tls->AddressOfCallBacks) {
            PIMAGE_TLS_CALLBACK* cb = (PIMAGE_TLS_CALLBACK*)tls->AddressOfCallBacks;
            while (*cb) {
                (*cb)(c->imageBase, DLL_PROCESS_ATTACH, nullptr);
                ++cb;
            }
        }
    }

    // ---------- DllMain ----------
    using DllEntry = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    DllEntry entry = (DllEntry)(c->imageBase + c->entryRVA);
    entry((HINSTANCE)c->imageBase, DLL_PROCESS_ATTACH, nullptr);
    return 0;
}
#pragma optimize("", off)
#pragma runtime_checks("", restore)
