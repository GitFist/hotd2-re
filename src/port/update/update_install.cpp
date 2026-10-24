#include "update/update_install.h"
#include "update/update_check.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include "update_scripts.h"
#endif

namespace port::update {
#ifdef _WIN32
namespace {
namespace fs = std::filesystem;

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("A file path contains invalid characters.");
    std::string text(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        text.data(), size, nullptr, nullptr);
    return text;
}

std::string JsonString(const std::wstring& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : Utf8(value)) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}

fs::path Executable() {
    std::wstring path(32768, '\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("The game's installation folder could not be found.");
    path.resize(count);
    return fs::path(path).lexically_normal();
}

void CheckPath(const fs::path& path) {
    // Refuse junctions/symlinks anywhere in the installation ancestry, including
    // the updater workspace. Neither staging nor recovery may escape that tree.
    for (fs::path part = path; !part.empty();) {
        DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
                throw std::runtime_error("The update folder cannot be accessed.");
        } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            throw std::runtime_error("Automatic updates require a game folder without linked folders or files.");
        }
        const auto parent = part.parent_path();
        if (parent == part) break;
        part = parent;
    }
}

void WriteNew(const fs::path& path, std::string_view text) {
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("The updater could not write to the game folder. Check its permissions and free space.");
    DWORD written = 0;
    if (!WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(file.value))
        throw std::runtime_error("The updater could not save its startup files. Check the available disk space.");
}

void RemoveSession(const fs::path& session) {
    // Only files created for this handoff; never recursively delete a supplied path.
    for (const wchar_t* name : {L"request.json", L"update_runner.ps1", L"update_transaction.ps1", L"ready", L"error.txt"})
        DeleteFileW((session / name).c_str());
    RemoveDirectoryW(session.c_str());
}

bool Launch(bool recover, const std::string& tag, std::string* error) {
    fs::path session;
    try {
        const auto exe = Executable();
        if (_wcsicmp(exe.filename().c_str(), L"hotd2.exe") != 0)
            throw std::runtime_error("Automatic installation is available when you launch hotd2.exe.");
        const auto root = exe.parent_path();
        const auto state = root / L".hotd2-update";
        CheckPath(exe);
        CheckPath(state);
        fs::create_directory(state);
        GUID guid{};
        wchar_t identifier[40]{};
        if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, identifier, 40))
            throw std::runtime_error("The updater could not start. Please try again.");
        // StringFromGUID2 includes braces. Session names use the standard bare GUID.
        session = state / (L"session-" + std::wstring(identifier + 1, 36));
        if (!fs::create_directory(session)) throw std::runtime_error("The updater could not create its working folder.");
        WriteNew(session / L"update_transaction.ps1", kTransactionScript);
        WriteNew(session / L"update_runner.ps1", kRunnerScript);

        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            throw std::runtime_error("The updater could not identify the running game.");
        const auto creationTime = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        std::string request = "{\"root\":" + JsonString(root.wstring()) + ",\"tag\":\"" + tag +
            "\",\"architecture\":\"" + (sizeof(void*) == 8 ? "x64" : "x86") +
            "\",\"parentPid\":" + std::to_string(GetCurrentProcessId()) +
            ",\"parentStartTime\":\"" + std::to_string(creationTime) +
            "\",\"executable\":" + JsonString(exe.wstring()) +
            ",\"workingDirectory\":" + JsonString(fs::current_path().wstring()) +
            ",\"recover\":" + (recover ? "true" : "false") + ",\"arguments\":[";
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) throw std::runtime_error("The updater could not preserve the game's launch options.");
        for (int i = 1; i < count; ++i) {
            if (i != 1) request += ',';
            request += JsonString(arguments[i]);
        }
        LocalFree(arguments);
        request += "]}";
        WriteNew(session / L"request.json", request);

        wchar_t systemPath[MAX_PATH]{};
        if (!GetSystemDirectoryW(systemPath, MAX_PATH)) throw std::runtime_error("Windows PowerShell could not be located.");
        const auto powershell = fs::path(systemPath) / L"WindowsPowerShell/v1.0/powershell.exe";
        std::wstring command = QuoteWindowsArgument(powershell.wstring()) +
            L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -STA -File " +
            QuoteWindowsArgument((session / L"update_runner.ps1").wstring()) +
            L" -RequestPath " + QuoteWindowsArgument((session / L"request.json").wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        // Hide only the console. SW_HIDE also overrides the helper's first
        // ShowWindow call and makes its WinForms progress dialog invisible.
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, root.c_str(), &startup, &process))
            throw std::runtime_error("Windows could not start the updater. You can keep playing and try again later.");
        Handle child{process.hProcess}, thread{process.hThread};
        const auto deadline = GetTickCount64() + 10000;
        for (;;) {
            // The helper has shown its progress window before signaling ready,
            // and is not allowed to install while this process is alive.
            if (fs::is_regular_file(session / L"ready") && WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT)
                return true;
            if (WaitForSingleObject(child.value, 50) == WAIT_OBJECT_0) {
                std::ifstream stream(session / L"error.txt", std::ios::binary);
                std::string detail(4096, '\0');
                stream.read(detail.data(), static_cast<std::streamsize>(detail.size()));
                detail.resize(static_cast<size_t>(stream.gcount()));
                throw std::runtime_error(detail.empty() ? "The updater could not start. You can keep playing and try again later." : detail);
            }
            if (GetTickCount64() >= deadline) {
                TerminateProcess(child.value, 1);
                WaitForSingleObject(child.value, 5000);
                throw std::runtime_error("The updater did not start in time. You can keep playing and try again later.");
            }
        }
    } catch (const std::exception& ex) {
        if (!session.empty()) RemoveSession(session);
        if (error) *error = ex.what();
        return false;
    }
}
}  // namespace

std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif

bool StartUpdate(const std::string& tag, std::string* error) {
#ifdef _WIN32
    if (AvailableRelease("2000 01 01 A", "https://github.com/GitFist/hotd2-re/releases/tag/" + tag) != tag || tag.empty()) {
        if (error) *error = "This release cannot be installed automatically.";
        return false;
    }
    return Launch(false, tag, error);
#else
    (void)tag;
    if (error) *error = "Automatic installation is only available on Windows.";
    return false;
#endif
}

Recovery RecoverInterruptedUpdate(std::string* error) {
#ifdef _WIN32
    try {
        const auto pending = Executable().parent_path() / L".hotd2-update/transaction.json";
        if (!fs::exists(pending)) return Recovery::None;
        CheckPath(pending);
        return Launch(true, {}, error) ? Recovery::Started : Recovery::Failed;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return Recovery::Failed;
    }
#else
    (void)error;
    return Recovery::None;
#endif
}
}  // namespace port::update
