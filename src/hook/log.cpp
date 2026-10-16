#include "log.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;

FILE* OpenLog() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) {
        strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "hotd2_hook.log");
    }
    // Shared so the log can be read while the game is running.
    return _fsopen(path, "w", _SH_DENYNO);
}
}  // namespace

void Log(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (!g_file && !(g_file = OpenLog())) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
}
