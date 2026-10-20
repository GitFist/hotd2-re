#include "update/update_check.h"

#include <atomic>
#include <string_view>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace port::update {
namespace {

constexpr std::string_view kReleasePrefix = "https://github.com/GitFist/hotd2-re/releases/tag/";
constexpr size_t kMaxRevision = 16;

struct Version {
    int year = 0, month = 0, day = 0;
    std::string revision;
};

int Number(std::string_view text, size_t at, size_t count) {
    int value = 0;
    for (size_t i = at; i < at + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        value = value * 10 + text[i] - '0';
    }
    return value;
}

bool ParseVersion(std::string_view text, bool tag, Version& out) {
    const size_t offset = tag ? 1 : 0;
    if (text.size() < offset + 12 || text.size() > offset + 11 + kMaxRevision) return false;
    if (tag && text[0] != 'v') return false;
    const char separator = tag ? '.' : ' ';
    if (text[offset + 4] != separator || text[offset + 7] != separator ||
        text[offset + 10] != (tag ? '-' : ' ')) return false;
    out.year = Number(text, offset, 4);
    out.month = Number(text, offset + 5, 2);
    out.day = Number(text, offset + 8, 2);
    if (out.year < 2000 || out.year > 9999 || out.month < 1 || out.month > 12 || out.day < 1) return false;
    constexpr int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    const bool leap = out.year % 4 == 0 && (out.year % 100 != 0 || out.year % 400 == 0);
    if (out.day > days[out.month - 1] + (out.month == 2 && leap ? 1 : 0)) return false;
    const char first = tag ? 'a' : 'A', last = tag ? 'z' : 'Z';
    out.revision.clear();
    for (const char c : text.substr(offset + 11)) {
        if (c < first || c > last) return false;
        out.revision += static_cast<char>('a' + c - first);
    }
    return !out.revision.empty();
}

bool Newer(const Version& candidate, const Version& current) {
    if (candidate.year != current.year) return candidate.year > current.year;
    if (candidate.month != current.month) return candidate.month > current.month;
    if (candidate.day != current.day) return candidate.day > current.day;
    // Alphabetic release revisions use a..z, aa..az, ba..bz, etc. Comparing the
    // digit count first avoids integer overflow while retaining base-26 ordering.
    if (candidate.revision.size() != current.revision.size()) return candidate.revision.size() > current.revision.size();
    return candidate.revision > current.revision;
}

#ifdef _WIN32
class HttpHandle {
public:
    explicit HttpHandle(HINTERNET value) : value_(value) {}
    ~HttpHandle() { if (value_) WinHttpCloseHandle(value_); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    HINTERNET Get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
private:
    HINTERNET value_ = nullptr;
};

std::string FetchReleaseRedirect() {
    HttpHandle session(WinHttpOpen(L"HOTD2RE update check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.Get(), 2500, 2500, 2500, 2500)) return {};
    // This option counts attempts, including the initial connection (not retries after it).
    DWORD attempts = 1;
    if (!WinHttpSetOption(session.Get(), WINHTTP_OPTION_CONNECT_RETRIES, &attempts, sizeof(attempts))) return {};
    HttpHandle connection(WinHttpConnect(session.Get(), L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) return {};
    HttpHandle request(WinHttpOpenRequest(connection.Get(), L"HEAD", L"/GitFist/hotd2-re/releases/latest", nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) return {};
    // Inspect only GitHub's first response. Never follow a server-supplied URL.
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disabledFeatures = WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_COOKIES;
    DWORD maxHeaders = 16 * 1024;
    DWORD responseTimeout = 2500;
    if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
        !WinHttpSetOption(request.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabledFeatures, sizeof(disabledFeatures)) ||
        !WinHttpSetOption(request.Get(), WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &maxHeaders, sizeof(maxHeaders)) ||
        !WinHttpSetOption(request.Get(), WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &responseTimeout, sizeof(responseTimeout))) return {};
    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr)) return {};
    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) || status != 302) return {};
    wchar_t location[256] = {};
    DWORD locationSize = sizeof(location);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                             location, &locationSize, WINHTTP_NO_HEADER_INDEX) ||
        locationSize == 0 || locationSize >= sizeof(location) || locationSize % sizeof(wchar_t) != 0) return {};
    std::string result;
    for (size_t i = 0; i < locationSize / sizeof(wchar_t); ++i) {
        if (location[i] < 0x21 || location[i] > 0x7e) return {};
        result += static_cast<char>(location[i]);
    }
    return result;
}
#endif

}  // namespace

std::string AvailableRelease(const std::string& currentBuild, const std::string& redirectLocation) {
    if (!std::string_view(redirectLocation).starts_with(kReleasePrefix)) return {};
    const std::string_view tag = std::string_view(redirectLocation).substr(kReleasePrefix.size());
    Version current, release;
    if (!ParseVersion(currentBuild, false, current) || !ParseVersion(tag, true, release) || !Newer(release, current)) return {};
    return std::string(tag);
}

struct UpdateCheck::State {
    std::atomic<bool> ready = false;
    std::string available;
};

UpdateCheck::UpdateCheck() : state_(std::make_shared<State>()) {}

void UpdateCheck::Start(std::string currentBuild) {
#ifdef _WIN32
    static std::atomic<bool> started = false;
    Version current;
    if (!ParseVersion(currentBuild, false, current) || started.exchange(true)) return;
    try {
        std::thread([state = state_, currentBuild = std::move(currentBuild)] {
            try {
                state->available = AvailableRelease(currentBuild, FetchReleaseRedirect());
            } catch (...) {  // update checks are optional, including allocation / network failure
            }
            state->ready.store(true, std::memory_order_release);
        }).detach();
    } catch (...) {  // thread creation failure must never stop the game
    }
#else
    (void)currentBuild;
#endif
}

std::string UpdateCheck::TakeAvailable() {
    if (taken_ || !state_->ready.load(std::memory_order_acquire)) return {};
    taken_ = true;
    return std::move(state_->available);
}

}  // namespace port::update
