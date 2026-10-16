#include "task.h"

#include <algorithm>

namespace game {

TaskSystem::TaskSystem(uint32_t firstSerial) : serial_(firstSerial) { Reset(); }

// 0x004A6F50 (with 0x004A7310's root wipe)
void TaskSystem::Reset() {
    for (auto& t : storage_) Retire(*t);
    storage_.clear();
    root_ = Task{};
    root_.alive = true;  // root[3] = 1, root[2] = 0, root[0] = 0
    current_ = &root_;
    dead_ = nullptr;
}

// 0x004A6FA0
Task* TaskSystem::Create(TaskFn fn, std::unique_ptr<TaskData> data) {
    storage_.push_back(std::make_unique<Task>());
    Task* t = storage_.back().get();
    t->parent = (current_ == &root_ || current_ == nullptr) ? &root_ : current_->parent;
    t->serial = serial_++;
    t->alive = true;
    t->prev = t->parent->last;
    if (t->prev) t->prev->next = t;
    else t->parent->first = t;
    t->parent->last = t;
    t->fn = fn;
    t->data = std::move(data);
    return t;
}

void TaskSystem::Unlink(Task* t) {
    if (!t->prev) {
        if (t->parent) t->parent->first = t->next;
    } else {
        t->prev->next = t->next;
    }
    if (!t->next) {
        if (t->parent) t->parent->last = t->prev;
    } else {
        t->next->prev = t->prev;
    }
    // t->next is left intact: Run's sibling loop continues from it after the killed task returns.
}

// 0x004A7040
void TaskSystem::KillCurrent() {
    Task* t = current_;
    Unlink(t);
    current_ = t->parent ? t->parent : &root_;
    if (t != &root_) {
        t->nextFree = dead_;
        dead_ = t;
        t->serial = 0;
        t->alive = false;
    }
    throw TaskEnded{};  // longjmp(0x007DF838, -1)
}

// 0x004A71A0
void TaskSystem::Run(Task* t) {
    for (Task* c = t->first; c; c = c->next) {
        current_ = c;
        Run(c);
    }
    current_ = t;
    if (t->fn) {
        if ((t->word32 & 0xFF) == 0xFF) {
            t->word32 &= 0xFF00;
        } else {
            try {
                t->fn(*t);
            } catch (const TaskEnded&) {
            }
        }
    }
    current_ = t;
    for (Task* c = t->post; c; c = c->next) {
        current_ = c;
        Run(c);
    }
    current_ = nullptr;
}

// 0x004A70D0
void TaskSystem::Retire(Task& t) {
    if (t.data && keepFreed && keepFreed(t.data.get())) ghosts_.emplace_back(std::move(t.data), 0);
}

void TaskSystem::AgeGhosts() {
    for (auto& g : ghosts_) ++g.second;
    ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const auto& g) { return kGhostFrames < g.second; }),
                  ghosts_.end());
}

void TaskSystem::FreeDead() {
    AgeGhosts();
    for (Task* t = dead_; t;) {
        Task* n = t->nextFree;
        Retire(*t);
        storage_.erase(std::remove_if(storage_.begin(), storage_.end(),
                                      [t](const std::unique_ptr<Task>& p) { return p.get() == t; }),
                       storage_.end());
        t = n;
    }
    dead_ = nullptr;
}

void TaskSystem::Collect(Task* t, std::vector<Task*>& out) {
    for (Task* c = t->first; c; c = c->next) Collect(c, out);
    if (t != &root_) out.push_back(t);
    for (Task* c = t->post; c; c = c->next) Collect(c, out);
}

std::vector<Task*> TaskSystem::Live() {
    std::vector<Task*> out;
    Collect(&root_, out);
    return out;
}

}  // namespace game
