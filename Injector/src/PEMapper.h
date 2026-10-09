#pragma once
#include <Windows.h>
#include <string>
#include <vector>

namespace PEMapper {
    struct Result {
        bool     ok = false;
        uint64_t remoteBase = 0;
        DWORD    remoteExitCode = 0;
        std::string err;
    };

    // reads <dllPath>, carves it in local memory, allocates in <pid>, writes sections,
    // relocates, resolves imports via remote stub, runs TLS callbacks and DllMain.
    Result MapIntoProcess(DWORD pid, const std::wstring& dllPath);
}
