// task_test [capture with K lines]
// 1. Scheduler semantics of game::TaskSystem (mirror of 0x004A6F50..0x004A73D0), synthetic checks:
//    creation order = execution order, tasks created during a frame run in that frame, Task_Kill ends the call and
//    unlinks without breaking the sibling loop, dead tasks are freed at frame end, the 0xFF skip byte, serials.
// 2. With a capture (HOTD2_SCENECAP_TASKS K lines "serial:fn:parent", in list order): every frame's live list must
//    equal the previous frame's survivors in the same order followed by the newborn tasks in serial order, all
//    with parent 0 (the root) - i.e. the original's task list behaves exactly like TaskSystem's flat root list.

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/task.h"

namespace {
int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("FAIL line %d: %s\n", __LINE__, #c);           \
            ++g_fail;                                             \
        }                                                         \
    } while (0)

game::TaskSystem* g_ts;
std::vector<std::string> g_log;

struct Named : game::TaskData {
    std::string name;
    int calls = 0;
    int dieAt = -1;     // kill itself on this call
    int spawnAt = -1;   // create a "child" (sibling) on this call
};

void NamedFn(game::Task& t) {
    Named& d = t.As<Named>();
    ++d.calls;
    g_log.push_back(d.name);
    if (d.calls == d.spawnAt) {
        Named& n = g_ts->Create<Named>(NamedFn);
        n.name = d.name + "'";
    }
    if (d.calls == d.dieAt) {
        g_ts->KillCurrent();
        g_log.push_back("NOT REACHED");
    }
    g_log.back() += "";  // still in the call
}

std::string Frame() {
    g_log.clear();
    g_ts->RunFrame();
    g_ts->FreeDead();
    std::string s;
    for (auto& x : g_log) s += x + " ";
    return s;
}

void Synthetic() {
    game::TaskSystem ts;
    g_ts = &ts;
    auto add = [&](const char* n, int spawnAt = -1, int dieAt = -1) {
        Named& d = ts.Create<Named>(NamedFn);
        d.name = n;
        d.spawnAt = spawnAt;
        d.dieAt = dieAt;
    };
    add("A", 1);
    add("B", -1, 2);
    add("C");
    CHECK(ts.NextSerial() == 0x10003);
    std::string f1 = Frame();
    printf("frame 1: %s\n", f1.c_str());
    CHECK(f1 == "A B C A' ");  // A' created by A during the frame runs at the tail of the same frame
    std::string f2 = Frame();
    printf("frame 2: %s\n", f2.c_str());
    CHECK(f2 == "A B C A' ");  // B dies in its 2nd call; C still runs
    std::string f3 = Frame();
    printf("frame 3: %s\n", f3.c_str());
    CHECK(f3 == "A C A' ");
    auto live = ts.Live();
    CHECK(live.size() == 3);
    CHECK(live.size() == 3 && live[0]->serial == 0x10000 && live[1]->serial == 0x10002 && live[2]->serial == 0x10003);
    for (auto* t : live) CHECK(t->parent == &ts.Root());
    live[1]->word32 = 0x12FF;  // skip C once
    std::string f4 = Frame();
    CHECK(f4 == "A A' ");
    CHECK(live[1]->word32 == 0x1200);
    std::string f5 = Frame();
    CHECK(f5 == "A C A' ");
}

int Capture(const char* path) {
    std::ifstream cap(path);
    if (!cap) {
        printf("cannot open %s\n", path);
        return 1;
    }
    std::vector<std::pair<int, int>> prev;  // serial, fn
    bool havePrev = false;
    int frames = 0, okFrames = 0, born = 0, died = 0, badParent = 0, fnSwitch = 0;
    std::vector<std::pair<int, int>> cur;
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2) continue;
        if (line[0] == 'K' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            for (std::string e; ss >> e;) {
                int s = 0, par = 0;
                unsigned fn = 0;
                if (sscanf(e.c_str(), "%d:%x:%d", &s, &fn, &par) == 3) {
                    cur.push_back({ s, static_cast<int>(fn) });
                    if (par != 0) ++badParent;
                }
            }
        } else if (line[0] == 'F' && line[1] == ' ') {
            if (!cur.empty() || havePrev) {
                if (havePrev && !cur.empty()) {
                    std::set<int> now;
                    for (auto& p : cur) now.insert(p.first);
                    std::set<int> before;
                    for (auto& p : prev) before.insert(p.first);
                    std::vector<std::pair<int, int>> expect;
                    for (auto& p : prev)
                        if (now.count(p.first)) expect.push_back(p);
                        else ++died;
                    std::vector<std::pair<int, int>> newborn;
                    int maxPrev = prev.empty() ? 0 : prev.back().first;
                    for (auto& p : prev) maxPrev = std::max(maxPrev, p.first);
                    for (auto& p : cur)
                        if (!before.count(p.first)) newborn.push_back(p);
                    std::sort(newborn.begin(), newborn.end());
                    bool serialOk = true;
                    for (auto& p : newborn) {
                        if (p.first <= maxPrev) serialOk = false;
                        expect.push_back(p);
                    }
                    born += static_cast<int>(newborn.size());
                    ++frames;
                    // a task may switch its function (task word 0), so compare serials only
                    bool same = expect.size() == cur.size();
                    for (size_t i = 0; same && i < cur.size(); ++i) {
                        same = expect[i].first == cur[i].first;
                        if (same && expect[i].second != cur[i].second) ++fnSwitch;
                    }
                    if (same && serialOk) ++okFrames;
                    else printf("frame list mismatch near line: %s\n", line.c_str());
                }
                if (!cur.empty()) {
                    prev = cur;
                    havePrev = true;
                }
            }
            cur.clear();
        }
    }
    printf("capture: %d frame transitions, %d consistent with the flat root list (born %d, died %d), "
           "non-root parents %d, function switches %d\n",
           frames, okFrames, born, died, badParent, fnSwitch);
    return (frames > 0 && okFrames == frames && badParent == 0) ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    Synthetic();
    printf("synthetic: %s\n", g_fail ? "FAILED" : "ok");
    int rc = g_fail ? 1 : 0;
    if (argc > 1) rc |= Capture(argv[1]);
    return rc;
}
