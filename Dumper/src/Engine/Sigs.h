#pragma once
#include <cstdint>

namespace Sigs {
    // resolves and stores base pointers for GObjects / GNames / FName::AppendString / ProcessEvent
    bool Resolve();

    extern uintptr_t GObjectsAddr;      // FUObjectArray*
    extern uintptr_t GNamesAddr;        // FNamePool* (4.25+) or TNameEntryArray** (older)
    extern uintptr_t AppendStringAddr;  // FName::AppendString(FString&) const
    extern uintptr_t ProcessEventAddr;  // UObject::ProcessEvent
}
