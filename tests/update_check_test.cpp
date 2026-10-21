// Release ordering and redirect validation use no network or game data.
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>

#include "port/update/update_check.h"

namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
std::string Release(const std::string& current, const std::string& tag) {
    return port::update::AvailableRelease(current, "https://github.com/GitFist/hotd2-re/releases/tag/" + tag);
}
}

int main(int argc, char** argv) {
    Check(Release("2026 10 19 W", "v2026.10.19-x") == "v2026.10.19-x", "newer revision is available");
    Check(Release("2026 10 19 W", "v2026.10.19-w").empty(), "equal release is ignored");
    Check(Release("2026 10 19 W", "v2026.10.19-v").empty(), "older revision is ignored");
    Check(Release("2026 10 19 W", "v2026.10.16-r").empty(), "published release older than development build is ignored");
    Check(Release("2026 10 19 Z", "v2026.10.19-aa") == "v2026.10.19-aa", "aa follows z");
    Check(Release("2026 10 19 Z", "v2026.10.21-a") == "v2026.10.21-a",
          "published build Z is offered the Night Shift release on the later date");
    Check(Release("2026 10 21 A", "v2026.10.21-a").empty(), "current Night Shift build is not offered its own release");
    Check(Release("2026 10 21 A", "v2026.10.19-z").empty(), "current Night Shift build ignores the previous latest build Z");
    Check(Release("2026 10 21 A", "v2026.10.21-b") == "v2026.10.21-b", "current Night Shift build still detects a later revision");
    Check(Release("2026 10 19 AA", "v2026.10.19-z").empty(), "z precedes aa");
    Check(Release("2026 10 19 AA", "v2026.10.19-ab") == "v2026.10.19-ab", "multi-letter revisions are ordered");
    Check(Release("2026 10 19 AZ", "v2026.10.19-ba") == "v2026.10.19-ba", "alphabetic revision carries into next prefix");
    Check(Release("2026 10 19 ZZ", "v2026.10.19-aaa") == "v2026.10.19-aaa", "longer revisions sort after shorter ones");
    Check(Release("2026 10 19 ZZ", "v2026.10.20-a") == "v2026.10.20-a", "new day precedes revision comparison");
    Check(Release("2026 10 31 Z", "v2026.11.01-a") == "v2026.11.01-a", "month rollover");
    Check(Release("2026 12 31 Z", "v2027.01.01-a") == "v2027.01.01-a", "year rollover");
    Check(Release("2026 11 01 A", "v2026.10.31-zzz").empty(), "older month is ignored regardless of revision");
    Check(Release("2027 01 01 A", "v2026.12.31-zzz").empty(), "older year is ignored regardless of revision");
    Check(Release("2028 02 28 A", "v2028.02.29-a") == "v2028.02.29-a", "valid leap day");
    Check(Release("2399 12 31 Z", "v2400.02.29-a") == "v2400.02.29-a", "400-year leap day");
    Check(Release("2026 10 19 W", "v2026.10.19-aaaaaaaaaaaaaaaa") == "v2026.10.19-aaaaaaaaaaaaaaaa",
          "maximum supported revision length is accepted");
    Check(Release("9999 12 30 A", "v9999.12.31-z") == "v9999.12.31-z", "maximum supported date is accepted");

    for (const char* invalid : {"", "latest", "v1.2.3", "v2026.10.20", "v2026.10.20-", "v2026.10.20-A",
                               "v2026.10.20-a1", "v2026.10.20-a-beta", "v2026.1.20-a", "v2026.10.2-a",
                               "v2026.00.20-a", "v2026.13.20-a", "v2026.10.00-a", "v2026.10.32-a",
                               "v2026.11.31-a", "v2027.02.29-a", "v2100.02.29-a", "v1999.12.31-a",
                               "v2026.10.20-aaaaaaaaaaaaaaaaa", "v2026.10.20-a/extra", "v2026.10.20-a?x=1",
                               "v2026.10.20-a#fragment", "v2026.10.20-a%0a", "v2026.10.20-a\r\n", " v2026.10.20-a"}) {
        Check(Release("2026 10 19 W", invalid).empty(), "invalid / unknown release tag is ignored");
    }
    for (const char* invalid : {"", "development", "2026 10 19", "2026 10 19 w", "2026.10.19-W",
                               "2026 13 19 W", "2027 02 29 W", "2026 10 19 W extra", "2026 10 19 W\n",
                               "2026 10 19 W ", "2026 10 19 AAAAAAAAAAAAAAAAA"}) {
        Check(Release(invalid, "v2027.10.20-a").empty(), "unknown current build cannot cause false update");
    }
    for (const char* location : {"http://github.com/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://github.com.evil.test/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://github.com@evil.test/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://evil.test/https://github.com/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://github.com/Other/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://github.com/GitFist/other/releases/tag/v2026.10.20-a",
                                "https://github.com/GitFist/hotd2-re/releases/tag/v2026.10.20-a?download=1",
                                "https://github.com/GitFist/hotd2-re/releases/tag/v2026.10.20-a#fragment",
                                "https://github.com:443/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://GITHUB.COM/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "/GitFist/hotd2-re/releases/tag/v2026.10.20-a",
                                "https://github.com/GitFist/hotd2-re/releases/tag/../v2026.10.20-a"}) {
        Check(port::update::AvailableRelease("2026 10 19 W", location).empty(), "untrusted redirect is ignored");
    }
    std::string embedded = "https://github.com/GitFist/hotd2-re/releases/tag/v2026.10.20-a";
    embedded += '\0'; embedded += "evil";
    Check(port::update::AvailableRelease("2026 10 19 W", embedded).empty(), "embedded null in redirect is rejected");
    std::string embeddedBuild = "2026 10 19 W";
    embeddedBuild += '\0'; embeddedBuild += "A";
    Check(Release(embeddedBuild, "v2026.10.20-a").empty(), "embedded null in current build is rejected");
    port::update::UpdateCheck check;
    Check(check.TakeAvailable().empty() && check.TakeAvailable().empty(), "poll without starting is empty and repeatable");
    // Explicit integration probe only; ordinary test runs remain fully offline.
    if (argc == 2 && std::string(argv[1]) == "--live") {
#ifdef _WIN32
        check.Start("2000 01 01 A");
        std::string available;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (available.empty() && std::chrono::steady_clock::now() < deadline) {
            available = check.TakeAvailable();
            if (available.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        Check(!available.empty(), "live WinHTTP check returns an official release within ten seconds");
        if (!available.empty()) std::printf("Live release: %s\n", available.c_str());
        Check(check.TakeAvailable().empty(), "live result is delivered once");
#else
        std::printf("Live update check is unavailable on this platform\n");
#endif
    }
    std::printf("update_check_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
