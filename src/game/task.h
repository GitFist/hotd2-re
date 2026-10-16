#pragma once

// HOTD2 task (coroutine-less "process") system, see notes/agents/props.md. From Task_InitRoot 0x004A6F50,
// Task_Create 0x004A6FA0, Task_Kill 0x004A7040, Task_FreeDead 0x004A70D0, Task_Unlink 0x004A7120,
// Task_Run 0x004A71A0, Task_RunFrom 0x004A7240, Task_Clear 0x004A73D0.
//
// Every task function is called once per frame from the start (no saved stack): state lives in the task block.
// A task ends by calling Task_Kill, which unlinks it and longjmps back to the scheduler; here that is a C++
// exception caught in Run, so the rest of the task function is skipped exactly like the original.
// The execution order is part of the game state (rand() call order), so this model mirrors it exactly:
//   Run(t): for c in t.children: Run(c);  t.fn(t);  for c in t.post: Run(c)
// and Create() makes the new task a SIBLING of the current one (child of current's parent), appended at the
// tail, so a task created during the frame still runs in that frame (the loop reads next after each call).

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace game {

struct Task;
using TaskFn = void (*)(Task&);

// Per-type state (the original's raw block above the 0x34-byte header).
struct TaskData {
    virtual ~TaskData() = default;
};

struct Task {
    TaskFn fn = nullptr;       // +0x00 function called every frame (a task may switch it)
    uint32_t serial = 0;       // +0x08 creation counter (0x00598AA8), 0 once dead
    bool alive = false;        // +0x0C 1 while linked
    void* user = nullptr;      // +0x10 set by 0x004A7260 (owner pointer of some HUD tasks)
    Task* nextFree = nullptr;  // +0x14 dead list link (0x007DF8AC)
    Task* prev = nullptr;      // +0x18
    Task* next = nullptr;      // +0x1C
    Task* parent = nullptr;    // +0x20
    Task* post = nullptr;      // +0x24 list run AFTER the task (never non-zero in the captures; see notes)
    Task* first = nullptr;     // +0x28 children, run BEFORE the task
    Task* last = nullptr;      // +0x2C
    uint16_t word30 = 0;       // +0x30
    uint16_t word32 = 0;       // +0x32: low byte 0xFF = skip the next call once (Run clears it)
    std::unique_ptr<TaskData> data;

    template <class T> T& As() { return static_cast<T&>(*data); }
};

class TaskSystem {
public:
    // Serial counter starts at 0x10000 (initial value of 0x00598AA8) and is never reset by the game.
    explicit TaskSystem(uint32_t firstSerial = 0x10000);

    // Task_InitRoot 0x004A6F50: current = root, dead list cleared, root fn = 0, then init(*this).
    // (Mem_Init 0x004A7310, which every caller runs first, wipes the heap and the root's links: Reset() does both.)
    void Reset();

    // Task_Create 0x004A6FA0: parent = current's parent (root if current is root or none).
    Task* Create(TaskFn fn, std::unique_ptr<TaskData> data = nullptr);
    template <class T> T& Create(TaskFn fn) {
        auto d = std::make_unique<T>();
        T& r = *d;
        Create(fn, std::move(d));
        return r;
    }

    // Task_Kill 0x004A7040: unlink the current task, put it on the dead list, end its call. Never returns
    // (throws TaskEnded, caught by Run). On the root it only unlinks and returns, like the original.
    [[noreturn]] void KillCurrent();

    // Task_RunFrom 0x004A7240 (root) -> Task_Run 0x004A71A0.
    void RunFrame() { Run(&root_); }
    void Run(Task* t);

    // Task_FreeDead 0x004A70D0 (end of frame, after rendering): release the dead list.
    void FreeDead();

    Task* Current() const { return current_; }
    Task& Root() { return root_; }
    uint32_t NextSerial() const { return serial_; }
    void SetNextSerial(uint32_t s) { serial_ = s; }

    // Live tasks in execution order (depth first, children before parents).
    std::vector<Task*> Live();

    // A freed block (FreeDead / Reset) for which this returns true stays readable for kGhostFrames more FreeDead calls:
    // the original's freed heap blocks keep their fields until reused, and pointers kept elsewhere (the hit system's
    // target lists 0x0059F4D0 / 0x005A4C90) still read them.
    std::function<bool(const TaskData*)> keepFreed;
    static constexpr int kGhostFrames = 3;

    struct TaskEnded {};

private:
    void Unlink(Task* t);
    void Collect(Task* t, std::vector<Task*>& out);

    Task root_;
    Task* current_ = &root_;
    Task* dead_ = nullptr;
    uint32_t serial_;
    std::vector<std::unique_ptr<Task>> storage_;
    std::vector<std::pair<std::unique_ptr<TaskData>, int>> ghosts_;  // (block, FreeDead calls since freed)
    void Retire(Task& t);
    void AgeGhosts();
};

}  // namespace game
