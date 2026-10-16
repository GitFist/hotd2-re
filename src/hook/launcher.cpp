// hotd2_launch.exe <game exe> [args...]
// Starts the game suspended, injects hotd2_hook.dll (from this exe's folder), then resumes it.

#include <windows.h>

#include <detours.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: hotd2_launch <game exe> [args...]\n");
        return 1;
    }

    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dll(self);
    dll = dll.substr(0, dll.find_last_of('\\') + 1) + "hotd2_hook.dll";

    char exe[MAX_PATH];
    if (!GetFullPathNameA(argv[1], MAX_PATH, exe, nullptr)) {
        fprintf(stderr, "bad path: %s\n", argv[1]);
        return 1;
    }
    std::string workDir(exe);
    workDir = workDir.substr(0, workDir.find_last_of('\\'));

    std::string cmd = "\"" + std::string(exe) + "\"";
    for (int i = 2; i < argc; ++i) {
        cmd += " ";
        cmd += argv[i];
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    const char* dlls[] = { dll.c_str() };
    if (!DetourCreateProcessWithDllsA(exe, cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                                      workDir.c_str(), &si, &pi, 1, dlls, nullptr)) {
        fprintf(stderr, "failed to start %s (error %lu)\n", exe, GetLastError());
        return 1;
    }
    ResumeThread(pi.hThread);
    printf("started %s with %s (pid %lu)\n", exe, dll.c_str(), pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
