#include "ObjectArray.h"
#include "Sigs.h"
#include "Offsets.h"
#include "../Utils/Logger.h"

namespace Objects {
    static FUObjectArray* g = nullptr;

    bool Init() {
        if (!Sigs::GObjectsAddr) return false;
        g = (FUObjectArray*)Sigs::GObjectsAddr;
        LOG("Objects: FUObjectArray @ %p; Num=%d MaxChunks=%d NumChunks=%d",
            (void*)g, g->ObjObjects.NumElements, g->ObjObjects.MaxChunks, g->ObjObjects.NumChunks);
        return g->ObjObjects.NumElements > 0 && g->ObjObjects.NumElements < 50'000'000;
    }

    int32_t Num() { return g ? g->ObjObjects.NumElements : 0; }

    UObject* Get(int32_t idx) {
        if (!g) return nullptr;
        auto* it = g->ObjObjects.GetItem(idx);
        return it ? it->Object : nullptr;
    }

    FUObjectArray* Raw() { return g; }
}
