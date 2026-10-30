#include "update/typing_content.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace port::update {
namespace {
namespace fs = std::filesystem;
constexpr wchar_t kBank[] = L"typing_reference.bank";
constexpr wchar_t kDownload[] = L"/GitFist/hotd2-re/releases/download/updates-v2026.10.30-a/hotd2re-typing-reference-v5.bank";
struct File {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit File(HANDLE handle) : value(handle) {}
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    File(const File&) = delete;
};
struct Http {
    HINTERNET value;
    explicit Http(HINTERNET handle) : value(handle) {}
    ~Http() { if (value) WinHttpCloseHandle(value); }
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
            BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) {
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
            throw std::runtime_error("Windows could not initialize SHA-256 verification.");
        }
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    void Add(uint8_t* bytes,DWORD count) {
        if (BCryptHashData(hash,bytes,count,0)<0) throw std::runtime_error("Typing data verification failed.");
    }
    bool Matches() {
        std::array<uint8_t,32> digest{};
        if (BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)<0) return false;
        constexpr char hex[] = "0123456789abcdef";
        std::string text;
        for (auto byte:digest) { text+=hex[byte>>4];text+=hex[byte&15]; }
        return text==kTypingContentSha256;
    }
};
struct Directory {
    fs::path path;
    std::vector<std::unique_ptr<File>> locks;
    explicit Directory(const std::string& input) {
        const auto supplied=fs::u8path(input);
        for (const auto& part:supplied) if (part==L"..")
            throw std::runtime_error("The Typing data folder contains a parent traversal.");
        path=fs::absolute(supplied).lexically_normal();
        const auto drive=path.root_name().wstring();
        if (drive.size()!=2 || drive[1]!=L':' || path.root_directory()!=L"\\")
            throw std::runtime_error("Typing data requires an ordinary local game folder.");
        fs::path current=path.root_path();
        Lock(current);
        for (const auto& part:path.relative_path()) {
            if (part.wstring().find(L':')!=std::wstring::npos)
                throw std::runtime_error("The Typing data folder has an invalid path component.");
            current/=part;Lock(current);
        }
    }
    void Lock(const fs::path& part) {
        auto file=std::make_unique<File>(CreateFileW(part.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (file->value==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file->value,&info) ||
            !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Typing data requires an accessible game folder without linked folders.");
        locks.push_back(std::move(file));
    }
    bool Exists() const {
        const DWORD attributes=GetFileAttributesW((path/kBank).c_str());
        if (attributes==INVALID_FILE_ATTRIBUTES) {
            if (GetLastError()==ERROR_FILE_NOT_FOUND) return false;
            throw std::runtime_error("The existing Typing data cannot be inspected.");
        }
        if (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("The Typing data path is a linked file or a directory.");
        return true;
    }
};
std::string Utf8(const fs::path& path) {
    const auto bytes=path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
struct OwnedStage {
    File file;
    bool committed=false;
    explicit OwnedStage(HANDLE handle) : file(handle) {}
    ~OwnedStage() {
        if (!committed && file.value!=INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO remove{TRUE};
            SetFileInformationByHandle(file.value,FileDispositionInfo,&remove,sizeof(remove));
        }
    }
};
void OrdinaryFile(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks!=1)
        throw std::runtime_error("The Typing staging file is unavailable or linked.");
}
void Validate(HANDLE file,const std::atomic<bool>* cancelled=nullptr) {
    OrdinaryFile(file);
    LARGE_INTEGER size{},start{};
    if (!GetFileSizeEx(file,&size) || uint64_t(size.QuadPart)!=kTypingContentBytes ||
        !SetFilePointerEx(file,start,nullptr,FILE_BEGIN))
        throw std::runtime_error("The downloaded Typing data has the wrong size.");
    Hash hash;
    std::array<uint8_t,65536> bytes{};
    uint64_t total=0;
    for (;;) {
        if (cancelled && cancelled->load()) throw std::runtime_error("Typing data installation was cancelled.");
        DWORD count=0;
        if (!ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr))
            throw std::runtime_error("The downloaded Typing data could not be read.");
        if (!count) break;
        total+=count;hash.Add(bytes.data(),count);
    }
    if (total!=kTypingContentBytes || !hash.Matches())
        throw std::runtime_error("The downloaded Typing data failed SHA-256 verification.");
}
void Commit(Directory& directory,OwnedStage& stage,const std::atomic<bool>& cancelled) {
    Validate(stage.file.value,&cancelled);
    if (cancelled.load()) throw std::runtime_error("Typing data installation was cancelled.");
    const std::wstring target=(directory.path/kBank).wstring();
    std::vector<uint8_t> storage(sizeof(FILE_RENAME_INFO)+target.size()*sizeof(wchar_t));
    auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists=FALSE;rename->RootDirectory=nullptr;
    rename->FileNameLength=DWORD(target.size()*sizeof(wchar_t));
    std::copy(target.begin(),target.end(),rename->FileName);
    if (!SetFileInformationByHandle(stage.file.value,FileRenameInfo,rename,DWORD(storage.size())))
        throw std::runtime_error("Typing data could not be installed; any existing bank was preserved.");
    stage.committed=true;
}
struct Progress {
    std::atomic<bool> cancelled{false},done{false};
    std::atomic<uint64_t> downloaded{0};
    bool success=false;
    std::string error;
    HWND label=nullptr,button=nullptr;
};
void Download(Directory& directory,Progress& progress) {
    std::array<uint8_t,16> random{};
    if (BCryptGenRandom(nullptr,random.data(),DWORD(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
        throw std::runtime_error("Windows could not create a private Typing staging file.");
    std::wstring name=L".typing-reference-";
    for (auto byte:random) {name+=L"0123456789abcdef"[byte>>4];name+=L"0123456789abcdef"[byte&15];}
    name+=L".part";
    OwnedStage stage(CreateFileW((directory.path/name).c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,
        CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    OrdinaryFile(stage.file.value);
    Http session(WinHttpOpen(L"HOTD2RE Typing data/1.6",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    DWORD attempts=1,protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!session.value || !WinHttpSetTimeouts(session.value,5000,5000,5000,5000) ||
        !WinHttpSetOption(session.value,WINHTTP_OPTION_CONNECT_RETRIES,&attempts,sizeof(attempts)) ||
        !WinHttpSetOption(session.value,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols)))
        throw std::runtime_error("Windows could not open a secure Typing data connection.");
    Http connection(WinHttpConnect(session.value,L"github.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    Http request(connection.value?WinHttpOpenRequest(connection.value,L"GET",kDownload,nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr);
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    DWORD disabled=WINHTTP_DISABLE_AUTHENTICATION|WINHTTP_DISABLE_COOKIES,headers=16384,redirectLimit=8;
    DWORD authentication=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH,responseTimeout=5000;
    if (!request.value ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)) ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,&redirectLimit,sizeof(redirectLimit)) ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&authentication,sizeof(authentication)) ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,&responseTimeout,sizeof(responseTimeout)) ||
        !WinHttpSetOption(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headers,sizeof(headers)))
        throw std::runtime_error("The secure Typing data request could not be configured.");
    if (progress.cancelled.load()) throw std::runtime_error("Typing data installation was cancelled.");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    if (!WinHttpSendRequest(request.value,L"Accept-Encoding: identity\r\n",DWORD(-1L),
        WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request.value,nullptr))
        throw std::runtime_error("Typing data could not be downloaded. Check your connection and try again.");
    DWORD status=0,length=0,count=sizeof(DWORD);
    if (!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&count,WINHTTP_NO_HEADER_INDEX) || status!=200)
        throw std::runtime_error("The Typing data server did not return the expected download.");
    count=sizeof(DWORD);
    if (!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&length,&count,WINHTTP_NO_HEADER_INDEX) || length!=kTypingContentBytes)
        throw std::runtime_error("The Typing data server returned an unexpected download size.");
    std::array<uint8_t,65536> bytes{};
    uint64_t total=0;
    for (;;) {
        if (progress.cancelled.load()) throw std::runtime_error("Typing data installation was cancelled.");
        if (std::chrono::steady_clock::now()>deadline) throw std::runtime_error("The Typing data download timed out.");
        DWORD received=0,written=0;
        if (!WinHttpReadData(request.value,bytes.data(),DWORD(bytes.size()),&received))
            throw std::runtime_error("The Typing data download was interrupted. Please try again.");
        if (!received) break;
        if (received>kTypingContentBytes-total) throw std::runtime_error("The Typing data download exceeded its expected size.");
        if (!WriteFile(stage.file.value,bytes.data(),received,&written,nullptr) || written!=received)
            throw std::runtime_error("Typing data could not be saved. Check free disk space and folder permissions.");
        total+=received;progress.downloaded=total;
    }
    if (total!=kTypingContentBytes || !FlushFileBuffers(stage.file.value))
        throw std::runtime_error("The Typing data download is incomplete or could not be saved.");
    Commit(directory,stage,progress.cancelled);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM word,LPARAM parameter) {
    auto* progress=reinterpret_cast<Progress*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        progress=static_cast<Progress*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(progress));
    }
    if (progress && (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(word)==IDCANCEL))) {
        progress->cancelled=true;EnableWindow(progress->button,FALSE);
        SetWindowTextW(progress->label,L"Cancelling. Waiting for the current network operation...");return 0;
    }
    return DefWindowProcW(window,message,word,parameter);
}
bool Fail(std::string* error,const std::exception& exception) { if (error)*error=exception.what();return false; }
} // namespace

namespace detail {
bool ValidateTypingContentBank(const std::string& path,std::string* error) {
    try {
        const fs::path filePath=fs::absolute(fs::u8path(path));
        Directory directory(Utf8(filePath.parent_path()));
        File file(CreateFileW(filePath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        Validate(file.value);return true;
    } catch (const std::exception& exception) { return Fail(error,exception); }
}
bool CommitTypingContent(const std::string& gameDirectory,const std::string& stagingFilename,
                         const std::atomic<bool>& cancelled,std::string* error) {
    try {
        Directory directory(gameDirectory);
        const auto name=fs::u8path(stagingFilename);
        if (name.has_parent_path() || stagingFilename.rfind(".typing-reference-",0)!=0 || name.extension()!=L".part" ||
            stagingFilename.find(':')!=std::string::npos)
            throw std::runtime_error("The Typing staging filename is outside its game folder.");
        OwnedStage stage(CreateFileW((directory.path/name).c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        OrdinaryFile(stage.file.value);Commit(directory,stage,cancelled);return true;
    } catch (const std::exception& exception) { return Fail(error,exception); }
}
} // namespace detail

bool EnsureTypingContent(const std::string& gameDirectory,std::string* error) {
    if (error) error->clear();
    try {
        Directory directory(gameDirectory);
        if (directory.Exists()) return true;
        Progress progress;
        const HINSTANCE instance=GetModuleHandleW(nullptr);
        WNDCLASSW type{};type.lpfnWndProc=WindowProc;type.hInstance=instance;
        type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));type.hbrBackground=HBRUSH(COLOR_WINDOW+1);
        type.lpszClassName=L"HOTD2RETypingContent";
        if (!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("The Typing data progress window could not be opened.");
        HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,type.lpszClassName,L"Typing of the Dead data",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,500,184,
            nullptr,nullptr,instance,&progress);
        if (!window) throw std::runtime_error("The Typing data progress window could not be opened.");
        CreateWindowW(L"STATIC",L"Installing the one-time Typing game data download (90 MB).",
            WS_CHILD|WS_VISIBLE,18,16,460,25,window,nullptr,instance,nullptr);
        progress.label=CreateWindowW(L"STATIC",L"Connecting securely...",WS_CHILD|WS_VISIBLE,
            18,48,460,40,window,nullptr,instance,nullptr);
        progress.button=CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            380,99,88,28,window,HMENU(IDCANCEL),instance,nullptr);
        ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);
        std::thread worker;
        try {
            worker=std::thread([&] {
                try { Download(directory,progress);progress.success=true; }
                catch (const std::exception& exception) { progress.error=exception.what(); }
                progress.done=true;
            });
        } catch (...) { DestroyWindow(window);throw; }
        bool quit=false;
        try {
            while (!progress.done.load()) {
                MSG message{};
                while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if (message.message==WM_QUIT) {quit=true;progress.cancelled=true;continue;}
                    if (!IsDialogMessageW(window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
                }
                if (!progress.cancelled.load()) {
                    const auto count=progress.downloaded.load();
                    const auto text=count==kTypingContentBytes?L"Verifying the complete download...":
                        std::to_wstring(count*100/kTypingContentBytes)+L"% downloaded ("+
                        std::to_wstring(count/1000000)+L" / 90 MB)";
                    SetWindowTextW(progress.label,text.c_str());
                }
                MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);
            }
        } catch (...) {
            progress.cancelled=true;worker.join();DestroyWindow(window);throw;
        }
        worker.join();DestroyWindow(window);if (quit)PostQuitMessage(0);
        if (!progress.success && error && !progress.cancelled.load())*error=progress.error;
        return progress.success;
    } catch (const std::exception& exception) { return Fail(error,exception); }
}
} // namespace port::update
#else
namespace port::update {
bool EnsureTypingContent(const std::string&,std::string*) { return true; }
namespace detail {
bool ValidateTypingContentBank(const std::string&,std::string*) { return false; }
bool CommitTypingContent(const std::string&,const std::string&,const std::atomic<bool>&,std::string*) { return false; }
}
}
#endif
