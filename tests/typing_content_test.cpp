#include "port/update/typing_content.h"
#include "port/update/naomi_content.h"
#include "port/update/naomi_audio_content.h"

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
    const bool naomi=argc>1 && std::string(argv[1])=="--naomi";
    const bool audio=argc>1 && std::string(argv[1])=="--naomi-audio";
    if (naomi || audio) {--argc;++argv;}
    const auto ensure=audio?EnsureNaomiAudioContent:naomi?EnsureNaomiContent:EnsureTypingContent;
    const auto validate=audio?detail::ValidateNaomiAudioContentBank:naomi?detail::ValidateNaomiContentBank:detail::ValidateTypingContentBank;
    const auto install=audio?detail::CommitNaomiAudioContent:naomi?detail::CommitNaomiContent:detail::CommitTypingContent;
    const auto bankName=audio?L"naomi_audio.bank":naomi?L"naomi_visuals.bank":L"typing_reference.bank";
    const auto stageName=audio?L".naomi-audio-offline.part":naomi?L".naomi-visuals-offline.part":L".typing-reference-offline.part";
    std::printf("Testing %s content\n",audio?"Naomi audio":naomi?"Naomi":"Typing");
    // Explicit opt-in only: exercise the real HTTPS/progress path after the
    // pinned release asset is published. The caller supplies an isolated folder.
    if (argc==3 && std::string(argv[1])=="--download") {
        const fs::path directory=fs::absolute(fs::u8path(argv[2]));
        std::string error;
        if (!ensure(Utf8(directory),&error)) {
            if (error.empty()) {std::puts("CANCELLED: Typing content download");return 2;}
            std::fprintf(stderr,"FAIL: %s\n",error.c_str());return 1;
        }
        Require(validate(Utf8(directory/bankName),&error),
                "downloaded bank matches its pinned size and SHA-256: "+error);
        std::puts("PASS live game content: HTTPS progress completed and the installed bank matches its pinned size and SHA-256");
        return 0;
    }
    Require(argc==2 || (naomi && argc==3),"usage: typing_content_test [--naomi | --naomi-audio] <actual-pinned-bank> [previous-naomi-bank] | --download <isolated-directory>");
    const fs::path source=fs::absolute(fs::u8path(argv[1]));
    std::string error;
    Require(validate(Utf8(source),&error),"actual pinned bank validates: "+error);
    const auto root=fs::temp_directory_path()/
        ("hotd2-typing-content-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(fs::create_directory(root),"create isolated transaction directory");
    const auto game=root/L"game data";fs::create_directory(game);
    const auto bank=game/bankName;
    Write(bank,"existing player data");
    error="stale error";
    Require(ensure(Utf8(game),&error) && error.empty() && Read(bank)=="existing player data",
            "existing bank takes the zero-HTTP fast path and remains byte-identical");
    Require(!validate(Utf8(bank),&error),"wrong-size bank fails offline verification");
    Require(!ensure(Utf8(root/L"missing"),&error) && !error.empty(),
            "missing installation directory fails before creating a window or HTTP request");
    std::atomic<bool> cancelled{false};
    Write(root/L"outside.part","outside");
    Require(!install(Utf8(game),"../outside.part",cancelled,&error) &&
            Read(root/L"outside.part")=="outside","parent traversal cannot reach an outside staging file");
    const auto stage=game/stageName;
    auto commit=[&] {error.clear();return install(Utf8(game),Utf8(stage.filename()),cancelled,&error);};
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
    Require(validate(Utf8(bank),&error),"installed bank retains exact pinned bytes and hash");
    Require(ensure(Utf8(game),&error) && error.empty(),"current official bank needs no download");
    if (naomi && argc==3) {
        const auto previous=fs::absolute(fs::u8path(argv[2]));
        fs::remove(bank);fs::copy_file(previous,bank);
        fs::copy_file(source,stage);cancelled=true;
        Require(!commit() && !fs::exists(stage) && Read(bank)==Read(previous),
                "cancelled graphics upgrade preserves the exact previous official bank");
        cancelled=false;
        // Matching length alone must never authorize replacing player data.
        {std::fstream corrupt(bank,std::ios::in|std::ios::out|std::ios::binary);char byte=0;
            corrupt.read(&byte,1);byte^=1;corrupt.seekp(0);corrupt.write(&byte,1);}
        fs::copy_file(source,stage);
        Require(!commit() && !fs::exists(stage) && Read(bank)!=Read(previous),
                "same-length modified prior bank is preserved instead of upgraded");
        fs::remove(bank);fs::copy_file(previous,bank);fs::copy_file(source,stage);
        Require(commit() && !fs::exists(stage) && validate(Utf8(bank),&error),
                "verified previous official bank upgrades atomically to the current pinned bank");
        Require(ensure(Utf8(game),&error) && error.empty(),"upgraded bank takes the no-download path");
        std::puts("PASS Naomi graphics migration: verified old bank replaced, cancellation and modified data preserved");
    }
    const auto linked=root/L"linked game";
    bool checkedLink=false;
    if (CreateSymbolicLinkW(linked.c_str(),game.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)) {
        Require(!ensure(Utf8(linked),&error),"linked game directory is rejected before HTTP");
        fs::remove(linked);checkedLink=true;
    }
    const auto linkedSource=root/L"owned-link-source.bank";
    fs::rename(bank,linkedSource);
    Require(CreateHardLinkW(stage.c_str(),linkedSource.c_str(),nullptr)!=0,"create hardlink rejection fixture");
    Require(!commit() && error.find("linked")!=std::string::npos && !fs::exists(bank) && fs::exists(linkedSource),
            "linked staging file cannot be published or delete its source");
    // Cleanup is restricted to this test's freshly created absolute temporary root.
    fs::remove_all(root);
    std::printf("PASS game content: actual bank SHA-256, existing-data fast path, traversal/link refusal, truncated/corrupt/cancel cleanup and atomic no-replace install (directory symlink %s)\n",
                checkedLink?"checked":"unavailable");
#else
    Require(EnsureTypingContent("unused"),"non-Windows bootstrap is a quiet no-op");
    std::puts("PASS Typing content: non-Windows no-op");
#endif
}
