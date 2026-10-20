#pragma once

#include <memory>
#include <string>

namespace port::update {

// Pure validation: returns a newer vYYYY.MM.DD-revision tag only for an exact official
// GitHub release redirect. currentBuild uses the game's "YYYY MM DD REVISION" label.
// Unknown formats, invalid dates, and equal / older versions return an empty string.
std::string AvailableRelease(const std::string& currentBuild, const std::string& redirectLocation);

// One optional background request per process. No network access occurs until Start.
// Destruction never waits: the worker retains its own state and bounded WinHTTP handles.
// Use Start and TakeAvailable on the main thread; non-Windows builds are a quiet no-op.
class UpdateCheck {
public:
    UpdateCheck();
    UpdateCheck(const UpdateCheck&) = delete;
    UpdateCheck& operator=(const UpdateCheck&) = delete;

    void Start(std::string currentBuild);
    std::string TakeAvailable();  // nonblocking; returns the available tag at most once

private:
    struct State;
    std::shared_ptr<State> state_;
    bool taken_ = false;
};

}  // namespace port::update
