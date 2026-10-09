#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <string>

#include "Utils/Logger.h"
#include "Engine/Sigs.h"
#include "Engine/Offsets.h"
#include "Engine/NameArray.h"
#include "Engine/ObjectArray.h"
#include "Generator/Generator.h"
#include "Watermark.h"

namespace fs = std::filesystem;

static std::wstring BuildOutDir() {
    wchar_t appdata[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata);
    fs::path p = fs::path(appdata) / L"SDKDumperUE";

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring game = fs::path(exe).stem().wstring();
    p /= game;
    return p.wstring();
}

static DWORD WINAPI MainThread(LPVOID module) {
    std::wstring outDir = BuildOutDir();
    fs::create_directories(outDir);

    Logger::Get().Open((fs::path(outDir) / L"dumper.log").wstring());

    LOG("%s", kWatermarkLine);
    LOG("SDKDumperUE starting. outDir=%ls", outDir.c_str());

    Off::InitDefaults();

    if (!Sigs::Resolve()) {
        LOG("sig resolve failed - GObjects or GNames not found. aborting.");
        goto done;
    }
    if (!Names::Init()) {
        LOG("name init failed. aborting.");
        goto done;
    }
    if (!Objects::Init()) {
        LOG("object array init failed. aborting.");
        goto done;
    }

    Generator::Run(outDir);
    LOG("done. output: %ls", outDir.c_str());

done:
    // keep console open briefly then unload
    for (int i = 10; i > 0; --i) {
        LOG("unloading in %d...", i);
        Sleep(1000);
    }
    Logger::Get().Close();
    FreeLibraryAndExitThread((HMODULE)module, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HANDLE t = CreateThread(nullptr, 0, MainThread, h, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
