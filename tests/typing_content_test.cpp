#include "port/update/typing_content.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
void Require(bool value,const std::string& why) {
    if (!value) {std::fprintf(stderr,"FAIL: %s\n",why.c_str());std::exit(1);}
}
std::string Utf8(const std::filesystem::path& path) {
    const auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
void Write(const std::filesystem::path& path,const char* value) {std::ofstream(path,std::ios::binary)<<value;}
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
}
int main(int argc,char** argv) {
    using namespace port::update;
#ifdef _WIN32
    namespace fs=std::filesystem;
    // Explicit opt-in only: exercise the real HTTPS/progress path after the
    // pinned release asset is published. The caller supplies an isolated folder.
    if (argc==3 && std::string(argv[1])=="--download") {
        const fs::path directory=fs::absolute(fs::u8path(argv[2]));
        std::string error;
        if (!EnsureTypingContent(Utf8(directory),&error)) {
            if (error.empty()) {std::puts("CANCELLED: Typing content download");return 2;}
            std::fprintf(stderr,"FAIL: %s\n",error.c_str());return 1;
        }
        Require(detail::ValidateTypingContentBank(Utf8(directory/L"typing_reference.bank"),&error),
                "downloaded bank matches its pinned size and SHA-256: "+error);
        std::puts("PASS live Typing content: HTTPS progress completed and the installed bank matches its pinned size and SHA-256");
        return 0;
    }
    Require(argc==2,"usage: typing_content_test <actual-pinned-reference-bank> | --download <isolated-directory>");
    const fs::path source=fs::absolute(fs::u8path(argv[1]));
    std::string error;
    Require(detail::ValidateTypingContentBank(Utf8(source),&error),"actual pinned bank validates: "+error);
    const auto root=fs::temp_directory_path()/
        ("hotd2-typing-content-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(fs::create_directory(root),"create isolated transaction directory");
    const auto game=root/L"game data";fs::create_directory(game);
    const auto bank=game/L"typing_reference.bank";
    Write(bank,"existing player data");
    error="stale error";
    Require(EnsureTypingContent(Utf8(game),&error) && error.empty() && Read(bank)=="existing player data",
            "existing bank takes the zero-HTTP fast path and remains byte-identical");
    Require(!detail::ValidateTypingContentBank(Utf8(bank),&error),"wrong-size bank fails offline verification");
    Require(!EnsureTypingContent(Utf8(root/L"missing"),&error) && !error.empty(),
            "missing installation directory fails before creating a window or HTTP request");
    std::atomic<bool> cancelled{false};
    Write(root/L"outside.part","outside");
    Require(!detail::CommitTypingContent(Utf8(game),"../outside.part",cancelled,&error) &&
            Read(root/L"outside.part")=="outside","parent traversal cannot reach an outside staging file");
    const auto stage=game/L".typing-reference-offline.part";
    auto commit=[&] {error.clear();return detail::CommitTypingContent(Utf8(game),Utf8(stage.filename()),cancelled,&error);};
    Write(stage,"partial");
    Require(!commit() && !fs::exists(stage) && Read(bank)=="existing player data",
            "truncated downloads are removed without touching existing data");
    fs::copy_file(source,stage);
    cancelled=true;
    Require(!commit() && !fs::exists(stage) && Read(bank)=="existing player data",
            "cancelled complete download is removed and existing bank survives");
    cancelled=false;
    fs::copy_file(source,stage);
    Require(!commit() && !fs::exists(stage) && Read(bank)=="existing player data",
            "verified install atomically refuses replacement of an existing bank");
    fs::remove(bank);
    fs::copy_file(source,stage);
    {std::fstream corrupt(stage,std::ios::in|std::ios::out|std::ios::binary);char byte=0;
        corrupt.read(&byte,1);byte^=1;corrupt.seekp(0);corrupt.write(&byte,1);}
    Require(!commit() && error.find("SHA-256")!=std::string::npos && !fs::exists(stage) && !fs::exists(bank),
            "same-length corrupt data fails the pinned SHA-256 and cleans staging");
    fs::copy_file(source,stage);
    Require(commit() && !fs::exists(stage) && fs::exists(bank),"valid pinned bank installs through the atomic handle rename");
    Require(detail::ValidateTypingContentBank(Utf8(bank),&error),"installed bank retains exact pinned bytes and hash");
    const auto linked=root/L"linked game";
    bool checkedLink=false;
    if (CreateSymbolicLinkW(linked.c_str(),game.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)) {
        Require(!EnsureTypingContent(Utf8(linked),&error),"linked game directory is rejected before HTTP");
        fs::remove(linked);checkedLink=true;
    }
    const auto linkedSource=root/L"owned-link-source.bank";
    fs::rename(bank,linkedSource);
    Require(CreateHardLinkW(stage.c_str(),linkedSource.c_str(),nullptr)!=0,"create hardlink rejection fixture");
    Require(!commit() && error.find("linked")!=std::string::npos && !fs::exists(bank) && fs::exists(linkedSource),
            "linked staging file cannot be published or delete its source");
    // Cleanup is restricted to this test's freshly created absolute temporary root.
    fs::remove_all(root);
    std::printf("PASS Typing content: actual bank SHA-256, existing-data fast path, traversal/link refusal, truncated/corrupt/cancel cleanup and atomic no-replace install (directory symlink %s)\n",
                checkedLink?"checked":"unavailable");
#else
    Require(EnsureTypingContent("unused"),"non-Windows bootstrap is a quiet no-op");
    std::puts("PASS Typing content: non-Windows no-op");
#endif
}
