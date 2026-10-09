#include "Generator.h"
#include "../Engine/UnrealTypes.h"
#include "../Engine/Offsets.h"
#include "../Engine/NameArray.h"
#include "../Engine/ObjectArray.h"
#include "../Utils/Memory.h"
#include "../Utils/Logger.h"
#include "../Watermark.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

namespace Generator {

    // ---------- name helpers ----------
    static std::string NameOf(UObject* o) {
        if (!o) return "null";
        FName n = GetObjectFName(o);
        std::string s = Names::Resolve(n.ComparisonIndex);
        if (n.Number > 0) s += "_" + std::to_string(n.Number - 1);
        return s;
    }

    static std::string FullPath(UObject* o) {
        if (!o) return "";
        std::vector<std::string> parts;
        for (UObject* cur = o; cur; cur = GetOuter(cur)) {
            parts.push_back(NameOf(cur));
            if (parts.size() > 32) break;
        }
        std::string out;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!out.empty()) out += ".";
            out += *it;
        }
        return out;
    }

    static std::string SafeIdent(std::string s) {
        for (auto& c : s) {
            if (!(isalnum((unsigned char)c) || c == '_')) c = '_';
        }
        if (!s.empty() && isdigit((unsigned char)s[0])) s = "_" + s;
        return s;
    }

    // ---------- classification by UClass name walk ----------
    static bool ClassIsA(UObject* obj, const char* targetName) {
        UObject* cls = GetClass(obj);
        while (cls) {
            if (NameOf(cls) == targetName) return true;
            cls = Field<UObject*>(cls, Off::UStruct::SuperStruct);
        }
        return false;
    }

    static bool IsClass(UObject* o)    { return o && ClassIsA(o, "Class"); }
    static bool IsStruct(UObject* o)   { return o && (ClassIsA(o, "ScriptStruct") || ClassIsA(o, "Struct")); }
    static bool IsEnum(UObject* o)     { return o && ClassIsA(o, "Enum"); }
    static bool IsFunction(UObject* o) { return o && ClassIsA(o, "Function"); }
    static bool IsPackage(UObject* o)  { return o && ClassIsA(o, "Package"); }

    // ---------- package grouping ----------
    static UObject* OutermostPackage(UObject* o) {
        UObject* cur = o;
        while (cur) {
            UObject* outer = GetOuter(cur);
            if (!outer) return cur;
            cur = outer;
        }
        return nullptr;
    }

    // ---------- FField property walker ----------
    struct PropInfo {
        std::string name;
        std::string typeStr;
        int offset = 0;
        int size = 0;
        int arrayDim = 1;
    };

    static std::string FFieldClassName(void* fieldClass) {
        if (!fieldClass) return "Unknown";
        // FFieldClass: {FName Name; ...}
        FName* nm = (FName*)fieldClass;
        return Names::Resolve(nm->ComparisonIndex);
    }

    static std::string PropertyTypeString(void* prop) {
        if (!prop) return "void";
        void* fc = Field<void*>(prop, Off::FField::ClassPrivate);
        std::string t = FFieldClassName(fc);
        // normalize common suffixes
        if (t.ends_with("Property")) t = t.substr(0, t.size() - 8);
        if (t == "Bool")   return "bool";
        if (t == "Int")    return "int32_t";
        if (t == "Int64")  return "int64_t";
        if (t == "UInt32") return "uint32_t";
        if (t == "UInt64") return "uint64_t";
        if (t == "Byte")   return "uint8_t";
        if (t == "Float")  return "float";
        if (t == "Double") return "double";
        if (t == "Str")    return "FString";
        if (t == "Name")   return "FName";
        if (t == "Text")   return "FText";
        if (t == "Object" || t == "Class") return "class UObject*";
        if (t == "Struct")                 return "struct FStruct";
        if (t == "Array")                  return "TArray<uint8_t>";
        if (t == "Map")                    return "TMap<uint8_t,uint8_t>";
        if (t == "Set")                    return "TSet<uint8_t>";
        if (t == "Enum")                   return "uint8_t";
        return t;
    }

    static std::vector<PropInfo> WalkProperties(UObject* strct) {
        std::vector<PropInfo> out;
        void* f = Field<void*>(strct, Off::UStruct::ChildProperties);
        int guard = 0;
        while (f && guard++ < 4096 && Mem::IsReadable((uintptr_t)f + 0x40)) {
            FName nm = Field<FName>(f, Off::FField::Name);
            PropInfo p;
            p.name = SafeIdent(Names::Resolve(nm.ComparisonIndex));
            p.typeStr = PropertyTypeString(f);
            p.arrayDim = Field<int32_t>(f, Off::FProperty::ArrayDim);
            p.size     = Field<int32_t>(f, Off::FProperty::ElementSize);
            p.offset   = Field<int32_t>(f, Off::FProperty::Offset_Internal);
            out.push_back(p);
            f = Field<void*>(f, Off::FField::Next);
        }
        return out;
    }

    // ---------- writers ----------
    static void WriteHeaderBanner(std::ofstream& o, const std::string& pkg) {
        o << kWatermarkBanner;
        o << "// Package: " << pkg << "\n";
        o << "// Generated by SDKDumperUE - xSerndev\n";
        o << "// =====================================================================\n\n";
        o << "#pragma once\n";
        o << "#include <cstdint>\n#include <string>\n\n";
        o << "// NOTE: types are reconstructed; verify alignment and sizes against the target.\n\n";
    }

    static void EmitEnum(std::ofstream& o, UObject* e) {
        o << "// Enum " << FullPath(e) << "\n";
        o << "enum class E" << SafeIdent(NameOf(e)) << " : uint8_t {\n";
        // UEnum::Names is TArray<TPair<FName,int64>>
        TArray<uint8_t>* arr = (TArray<uint8_t>*)((uint8_t*)e + Off::UEnum::Names);
        if (arr && Mem::IsReadable((uintptr_t)arr->Data) && arr->Num > 0 && arr->Num < 4096) {
            uint8_t* data = arr->Data;
            for (int i = 0; i < arr->Num; ++i) {
                // pair stride = 16 (FName=8, pad=0, int64=8)
                FName* n = (FName*)(data + i * 16);
                int64_t v = *(int64_t*)(data + i * 16 + 8);
                std::string nm = SafeIdent(Names::Resolve(n->ComparisonIndex));
                o << "    " << nm << " = " << v << ",\n";
            }
        }
        o << "};\n\n";
    }

    static void EmitStruct(std::ofstream& o, UObject* s, bool asClass) {
        auto props = WalkProperties(s);
        UObject* super = Field<UObject*>(s, Off::UStruct::SuperStruct);
        int32_t total = Field<int32_t>(s, Off::UStruct::PropertiesSize);

        o << "// " << (asClass ? "Class " : "Struct ") << FullPath(s)
          << "   size=0x" << std::hex << total << std::dec << "\n";
        o << (asClass ? "class " : "struct ")
          << (asClass ? "U" : "F") << SafeIdent(NameOf(s));
        if (super) o << " : public " << (asClass ? "U" : "F") << SafeIdent(NameOf(super));
        o << " {\npublic:\n";

        int cursor = super ? Field<int32_t>(super, Off::UStruct::PropertiesSize) : 0;
        int pad = 0;
        for (auto& p : props) {
            if (p.offset > cursor) {
                o << "    uint8_t _pad_" << std::hex << cursor << std::dec
                  << "[0x" << std::hex << (p.offset - cursor) << std::dec << "];\n";
            }
            o << "    " << p.typeStr << " " << p.name;
            if (p.arrayDim > 1) o << "[" << p.arrayDim << "]";
            o << "; // 0x" << std::hex << p.offset << " size=0x" << p.size << std::dec << "\n";
            cursor = p.offset + p.size * (p.arrayDim ? p.arrayDim : 1);
            ++pad;
        }
        if (total > cursor) {
            o << "    uint8_t _tail_pad[0x" << std::hex << (total - cursor) << std::dec << "];\n";
        }
        o << "};\n\n";
    }

    static void EmitFunctions(std::ofstream& o, UObject* cls) {
        UObject* child = Field<UObject*>(cls, Off::UStruct::Children);
        int guard = 0;
        while (child && guard++ < 4096) {
            if (IsFunction(child)) {
                o << "    // Function " << FullPath(child) << "\n";
                o << "    //   " << NameOf(child) << "();  // flags=0x"
                  << std::hex << Field<uint32_t>(child, Off::UFunction::FunctionFlags) << std::dec << "\n";
            }
            child = Field<UObject*>(child, Off::UField::Next);
        }
    }

    // ---------- main ----------
    bool Run(const std::wstring& outDir) {
        fs::create_directories(outDir);
        LOG("Generator: output dir = %ls", outDir.c_str());

        // group objects by outermost package
        std::unordered_map<UObject*, std::vector<UObject*>> byPkg;
        int32_t num = Objects::Num();
        LOG("Generator: %d objects", num);

        for (int32_t i = 0; i < num; ++i) {
            UObject* o = Objects::Get(i);
            if (!o || !Mem::IsReadable((uintptr_t)o + 0x30)) continue;
            UObject* pkg = OutermostPackage(o);
            if (!pkg) continue;
            byPkg[pkg].push_back(o);
        }

        LOG("Generator: %zu packages", byPkg.size());

        // write per-package header
        for (auto& [pkg, objs] : byPkg) {
            std::string pkgName = SafeIdent(NameOf(pkg));
            if (pkgName.empty() || pkgName == "None") continue;
            fs::path file = fs::path(outDir) / (pkgName + "_classes.hpp");
            std::ofstream of(file);
            if (!of) continue;

            WriteHeaderBanner(of, pkgName);

            // enums first
            for (auto* o : objs) if (IsEnum(o))   EmitEnum(of, o);
            // structs
            for (auto* o : objs) if (IsStruct(o) && !IsClass(o)) EmitStruct(of, o, false);
            // classes
            for (auto* o : objs) if (IsClass(o)) {
                EmitStruct(of, o, true);
                EmitFunctions(of, o);
            }

            of << "\n" << kWatermarkLine << "\n";
        }

        // top-level SDK.hpp
        {
            fs::path idx = fs::path(outDir) / "SDK.hpp";
            std::ofstream of(idx);
            of << kWatermarkBanner;
            of << "#pragma once\n// SDK index\n\n";
            for (auto& [pkg, objs] : byPkg) {
                std::string pkgName = SafeIdent(NameOf(pkg));
                if (pkgName.empty() || pkgName == "None") continue;
                of << "#include \"" << pkgName << "_classes.hpp\"\n";
            }
            of << "\n" << kWatermarkLine << "\n";
        }

        LOG("Generator: done.");
        return true;
    }
}
