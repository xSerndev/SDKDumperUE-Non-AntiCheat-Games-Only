#pragma once
#include <cstdint>
#include <string>
#include "Offsets.h"

// opaque UObject pointer with helpers that read via offsets
struct UObject;

struct FName {
    int32_t ComparisonIndex;
    int32_t Number;
};
static_assert(sizeof(FName) == 8, "FName size");

template <typename T>
struct TArray {
    T* Data;
    int32_t Num;
    int32_t Max;
};

struct FString : TArray<wchar_t> {};

struct FUObjectItem {
    UObject* Object;
    int32_t  Flags;
    int32_t  ClusterIndex;
    int32_t  SerialNumber;
    int32_t  _pad;
};

// chunked array (TUObjectArray) — fields at known offsets
struct TUObjectArray {
    FUObjectItem** Objects;         // chunk pointers
    FUObjectItem*  PreAllocatedObjects;
    int32_t MaxElements;
    int32_t NumElements;
    int32_t MaxChunks;
    int32_t NumChunks;

    FUObjectItem* GetItem(int32_t idx) const {
        if (idx < 0 || idx >= NumElements) return nullptr;
        const int32_t PerChunk = Off::TUObjectArray::ElementsPerChunk;
        int32_t chunk = idx / PerChunk;
        int32_t within = idx % PerChunk;
        if (chunk >= NumChunks) return nullptr;
        FUObjectItem* c = Objects[chunk];
        if (!c) return nullptr;
        return &c[within];
    }
};

struct FUObjectArray {
    uint8_t  _pad[0x10];
    TUObjectArray ObjObjects;
};

// helpers
inline uint8_t* ByteAt(void* base, int off) { return (uint8_t*)base + off; }
template <typename T> inline T& Field(void* base, int off) { return *(T*)((uint8_t*)base + off); }

inline FName GetObjectFName(UObject* o) {
    return Field<FName>(o, Off::UObject::Name);
}
inline UObject* GetOuter(UObject* o) { return Field<UObject*>(o, Off::UObject::Outer); }
inline UObject* GetClass(UObject* o) { return Field<UObject*>(o, Off::UObject::Class); }
inline int32_t  GetIndex(UObject* o) { return Field<int32_t>(o, Off::UObject::Index); }
