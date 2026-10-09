#include "Sigs.h"
#include "../Utils/Memory.h"
#include "../Utils/Logger.h"

namespace Sigs {
    uintptr_t GObjectsAddr = 0;
    uintptr_t GNamesAddr = 0;
    uintptr_t AppendStringAddr = 0;
    uintptr_t ProcessEventAddr = 0;

    // these sigs cover the UE 4.25+ / UE5 "shipping" layout most commonly seen.
    // if a target game has stripped/encrypted names, swap patterns via config.
    //
    // GObjects:  common load -> lea rcx, [GObjects]; call AddObject...
    //   pattern (lea rcx, [rip+disp]): 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??
    //
    // FName::AppendString:  48 8D ?? ?? ?? 48 8B ?? E8 ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8B C8 ...
    //   simplified: begin of body often "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B DA ..."
    //
    // GNames (FNamePool): 48 8D 05 ?? ?? ?? ?? EB ?? 48 8D 05 ?? ?? ?? ?? (reference from GetNamePool / FName::ToString)
    //
    // ProcessEvent: standard prologue + accesses to Func->Offset etc.

    static uintptr_t FirstOf(const Mem::Module& m, std::initializer_list<const char*> pats) {
        for (auto p : pats) {
            uintptr_t a = Mem::FindPattern(m, p);
            if (a) return a;
        }
        return 0;
    }

    bool Resolve() {
        Mem::Module m = Mem::GetMainModule();
        LOG("main module: %s base=%p size=%zx", m.name.c_str(), (void*)m.base, m.size);

        // GObjects: lea rcx/rdx, [disp32]; followed by call near UObjectBase::* / AddObject
        {
            const char* pats[] = {
                "48 8B 05 ? ? ? ? 48 8B 0C C8 48 8D 04 D1",      // mov rax, [GObjects]; mov rcx, [rax+r*8]; lea rax, [rcx+rdx*8]
                "48 8D 0D ? ? ? ? E8 ? ? ? ? C6 05",             // lea rcx, GObjects; call AddObject
                "48 8D 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 8B D8", // lea rdx, GObjects
            };
            for (auto p : pats) {
                uintptr_t a = Mem::FindPattern(m, p);
                if (!a) continue;
                // first instruction is a lea/mov with rel32 at offset 3, length 7
                GObjectsAddr = Mem::ResolveRel32(a, 3, 7);
                LOG("GObjects pattern hit @ %p -> %p", (void*)a, (void*)GObjectsAddr);
                break;
            }
        }

        // AppendString: find by its stable prologue; many games keep this uninlined
        {
            const char* pats[] = {
                "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B DA 48 8B F9 48 85 D2",
                "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F2 48 8B F9 B9",
                "40 53 48 83 EC 30 48 8B DA 48 8B C9 E8",
            };
            AppendStringAddr = FirstOf(m, { pats[0], pats[1], pats[2] });
            if (AppendStringAddr) LOG("AppendString @ %p", (void*)AppendStringAddr);
        }

        // GNames (FNamePool). Multiple reference sites; grab from FName::ToString neighborhood.
        {
            const char* pats[] = {
                "48 8D 05 ? ? ? ? EB ? 48 8D 05 ? ? ? ?",                 // lea rax, GNames; jmp .. lea rax, GNames
                "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 83 7B ? 00",              // lea rcx, GNames; call StaticFindName
                "4C 8D 05 ? ? ? ? EB ? 4C 8D 05 ? ? ? ?",                 // r8 variant
            };
            for (auto p : pats) {
                uintptr_t a = Mem::FindPattern(m, p);
                if (!a) continue;
                GNamesAddr = Mem::ResolveRel32(a, 3, 7);
                LOG("GNames pattern hit @ %p -> %p", (void*)a, (void*)GNamesAddr);
                break;
            }
        }

        // ProcessEvent (robust across many builds)
        {
            const char* pats[] = {
                "40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC",
                "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24",
            };
            ProcessEventAddr = FirstOf(m, { pats[0], pats[1] });
            if (ProcessEventAddr) LOG("ProcessEvent @ %p", (void*)ProcessEventAddr);
        }

        return GObjectsAddr && GNamesAddr;
    }
}
