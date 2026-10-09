#pragma once
#include <cstdio>
#include <string>
#include <mutex>
#include <Windows.h>

class Logger {
public:
    static Logger& Get() { static Logger l; return l; }

    void Open(const std::wstring& path) {
        std::lock_guard<std::mutex> lk(m);
        if (fp) fclose(fp);
        _wfopen_s(&fp, path.c_str(), L"w");
        AllocConsole();
        freopen_s(&console, "CONOUT$", "w", stdout);
        SetConsoleTitleA("SDKDumperUE - xSerndev");
    }

    void Close() {
        std::lock_guard<std::mutex> lk(m);
        if (fp) { fclose(fp); fp = nullptr; }
        if (console) { fclose(console); console = nullptr; }
        FreeConsole();
    }

    void Log(const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(m);
        char buf[2048];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (fp) { fputs(buf, fp); fputc('\n', fp); fflush(fp); }
        fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    }

private:
    FILE* fp = nullptr;
    FILE* console = nullptr;
    std::mutex m;
};

#define LOG(fmt, ...) Logger::Get().Log("[SDKDumperUE] " fmt, ##__VA_ARGS__)
