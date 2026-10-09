#pragma once
#include <cstdint>

// Default offsets tuned for UE 4.27 / UE5 shipping builds (WITH_CASE_PRESERVING_NAME=0).
// These are starting points; adjust per target game by RE'ing one struct and the rest cascades.
namespace Off {

    // UObjectBase layout
    namespace UObject {
        inline int Vft           = 0x00;
        inline int Flags         = 0x08; // EObjectFlags
        inline int Index         = 0x0C; // internal index
        inline int Class         = 0x10; // UClass*
        inline int Name          = 0x18; // FName (comparison index @+0, number @+4) size 8
        inline int Outer         = 0x20;
        inline int Size          = 0x28;
    }

    // UField
    namespace UField {
        inline int Next          = Off::UObject::Size + 0x00; // 0x28
    }

    // UStruct (post 4.25 - FField split)
    namespace UStruct {
        inline int SuperStruct   = 0x40;
        inline int Children      = 0x48; // UField* (functions, inner structs)
        inline int ChildProperties = 0x50; // FField* (properties, 4.25+)
        inline int PropertiesSize = 0x58; // int32
        inline int MinAlignment   = 0x5C; // int32
    }

    // UClass
    namespace UClass {
        inline int CastFlags       = 0xC8;
        inline int DefaultObject   = 0x118;
    }

    // UFunction
    namespace UFunction {
        inline int FunctionFlags   = 0xB0;
        inline int Func            = 0xB0 + 0x28; // native func ptr (varies)
    }

    // UEnum
    namespace UEnum {
        inline int Names           = 0x40; // TArray<TPair<FName,int64>>
    }

    // FField
    namespace FField {
        inline int Vft             = 0x00;
        inline int ClassPrivate    = 0x08; // FFieldClass*
        inline int Owner           = 0x10;
        inline int Next            = 0x20; // FField*
        inline int Name            = 0x28; // FName
        inline int Flags           = 0x30;
    }

    // FProperty : FField
    namespace FProperty {
        inline int ArrayDim        = 0x38;
        inline int ElementSize     = 0x3C;
        inline int PropertyFlags   = 0x40;
        inline int Offset_Internal = 0x4C; // offset in owning struct
    }

    // FNamePool (UE 4.25+)
    namespace FNamePool {
        inline int Lock             = 0x00;
        inline int CurrentBlock     = 0x08;
        inline int CurrentByteCursor= 0x0C;
        inline int Blocks           = 0x10; // uint8_t* Blocks[8192]
        constexpr int BlockSizeBits = 16;
        constexpr int BlockSize     = 1 << BlockSizeBits; // 65536
        constexpr int Stride        = 2;                  // alignment
    }

    // FNameEntry header
    namespace FNameEntry {
        inline int HeaderSize   = 2;  // uint16_t header: bIsWide:1, LenHi? etc. layout: Len << 6 | bIsWide<<0  (fortnite-ish)
        // Many games encode as: uint16 Header = (Len << 6) | (bIsWide ? 1 : 0);
        inline int StringOffset = 2;
    }

    // FUObjectArray
    namespace FUObjectArray {
        inline int ObjObjects      = 0x10; // TUObjectArray
    }

    // TUObjectArray (chunked)
    namespace TUObjectArray {
        inline int Objects         = 0x00; // FUObjectItem** chunks
        inline int PreAllocatedObjects = 0x10;
        inline int MaxElements     = 0x18;
        inline int NumElements     = 0x1C;
        inline int MaxChunks       = 0x20;
        inline int NumChunks       = 0x24;
        inline int ElementsPerChunk= 64 * 1024;
    }

    // FUObjectItem
    namespace FUObjectItem {
        inline int Object          = 0x00; // UObject*
        inline int Size            = 0x18;
    }

    void InitDefaults();
}
