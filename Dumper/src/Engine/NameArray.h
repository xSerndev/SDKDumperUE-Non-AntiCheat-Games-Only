#pragma once
#include <cstdint>
#include <string>

// FNamePool-based resolver with runtime fallback via FName::AppendString if available.
namespace Names {
    bool Init();
    std::string Resolve(int32_t comparisonIndex);
}
