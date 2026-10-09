#include "NameArray.h"
#include "Sigs.h"
#include "Offsets.h"
#include "UnrealTypes.h"
#include "../Utils/Memory.h"
#include "../Utils/Logger.h"
#include <string>
#include <unordered_map>
#include <mutex>

namespace Names {

    static std::unordered_map<int32_t, std::string> cache;
    static std::mutex cacheMtx;

    using AppendStringFn = void(__fastcall*)(const FName*, FString*);
    static AppendStringFn pAppend = nullptr;

    bool Init() {
        if (Sigs::AppendStringAddr) {
            pAppend = (AppendStringFn)Sigs::AppendStringAddr;
            LOG("Names: using AppendString at %p", (void*)pAppend);
            return true;
        }
        LOG("Names: no AppendString; falling back to FNamePool walk");
        return Sigs::GNamesAddr != 0;
    }

    static bool SafeCallAppend(AppendStringFn fn, const FName* n, FString* s) {
        __try {
            fn(n, s);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static std::string ResolveViaAppend(int32_t idx) {
        if (!pAppend) return {};
        FName n{ idx, 0 };
        wchar_t buf[1024]{};
        FString s{};
        s.Data = buf; s.Num = 0; s.Max = 1024;
        if (!SafeCallAppend(pAppend, &n, &s)) return {};
        if (!s.Data || s.Num <= 0) return {};
        std::string out;
        out.reserve(s.Num);
        for (int i = 0; i < s.Num && buf[i]; ++i) out.push_back((char)buf[i]);
        return out;
    }

    // FNamePool pool-walk fallback. Blocks are at pool+0x10, each 64KB with 2-byte stride.
    // Entry: uint16 Header; char data[Len].
    static std::string ResolveViaPool(int32_t idx) {
        if (!Sigs::GNamesAddr) return {};
        uintptr_t pool = Sigs::GNamesAddr;
        int32_t block = idx >> 16;
        int32_t offs  = (idx & 0xFFFF) * 2;

        uint8_t** blocks = *(uint8_t***)(pool + Off::FNamePool::Blocks);
        // some games keep the array inline (array itself at Blocks offset), not a pointer-to-pointer.
        // handle both:
        uint8_t* b = nullptr;
        if (Mem::IsReadable((uintptr_t)blocks)) {
            b = blocks[block];
        } else {
            // inline array of uint8_t* at pool+Blocks
            uint8_t** inlineArr = (uint8_t**)(pool + Off::FNamePool::Blocks);
            if (Mem::IsReadable((uintptr_t)&inlineArr[block])) b = inlineArr[block];
        }
        if (!b || !Mem::IsReadable((uintptr_t)b + offs)) return {};

        uint16_t hdr = *(uint16_t*)(b + offs);
        int len = hdr >> 6;
        bool wide = (hdr & 1) != 0;
        const char* data = (const char*)(b + offs + 2);
        if (len <= 0 || len > 1024) return {};
        std::string out;
        out.reserve(len);
        if (wide) {
            const wchar_t* w = (const wchar_t*)data;
            for (int i = 0; i < len; ++i) out.push_back((char)w[i]);
        } else {
            out.assign(data, data + len);
        }
        return out;
    }

    std::string Resolve(int32_t idx) {
        {
            std::lock_guard<std::mutex> lk(cacheMtx);
            auto it = cache.find(idx);
            if (it != cache.end()) return it->second;
        }
        std::string s = pAppend ? ResolveViaAppend(idx) : ResolveViaPool(idx);
        if (s.empty()) s = ResolveViaPool(idx);
        if (s.empty()) s = "<bad_name>";
        std::lock_guard<std::mutex> lk(cacheMtx);
        cache[idx] = s;
        return s;
    }
}
