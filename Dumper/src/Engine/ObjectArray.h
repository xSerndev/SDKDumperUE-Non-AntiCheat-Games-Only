#pragma once
#include "UnrealTypes.h"
#include <vector>

namespace Objects {
    bool Init();
    int32_t Num();
    UObject* Get(int32_t idx);
    FUObjectArray* Raw();
}
