// queue.h — the mutation scheduler.
//
// One serial lane per manager. A two-phase reinstall occupies its lane for both
// phases so nothing can slip between the uninstall and the install. Dedup is per
// exact identity (scope included): two rows that only share a display name are
// different packages. A suspected shared instance across managers parks both
// tasks for review instead of guessing.
//
// Pure logic: tasks are advanced by explicit events, so L1 tests can drive any
// interleaving without starting a process.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "plan.h"

namespace pkg {

enum class TaskState {
    Queued,
    Preflight,
    NeedsReview,
    Blocked,
    Running,          // acquire / uninstall / install / verify
    InteractivePending,
    CancelRequested,
    RebootRequired,
    Succeeded,
    Failed,
    PartialFailure,   // reinstall removed the package and the install failed
    Cancelled,
    Interrupted,      // the host exited; the real state is unknown
};

const char* taskStateId(TaskState s);

// One line of a task's per-step log. `kind` drives the colour in the UI and is
// one of "info" | "ok" | "warning" | "error". `exitCode` is -1 when the entry is
// not a process result. `text` is always redacted before it is stored.
struct TaskLogEntry {
    std::string at;          // wall clock, "HH:MM:SS"
    std::string phase;       // the phase the entry belongs to
    std::string kind;
    std::string text;
    int exitCode = -1;
};

// Keeps a task log bounded: an installer can print for minutes.
constexpr std::size_t kMaxTaskLogEntries = 200;

// True for states a terminal result should never be inferred from a process
// exit code alone.
bool isTerminal(TaskState s);
bool isActive(TaskState s);

struct Task {
    unsigned long long id = 0;
    Plan plan;
    TaskState state = TaskState::Queued;
    std::string phase;              // "acquire" | "uninstall" | "install" | "verify"
    std::string detail;             // redacted diagnostic fragment
    int exitCode = -1;              // -1 until the process reported one
    bool verifiedAfterRun = false;  // the post-run inventory check ran
    std::string blockedReason;
    // The agreements the user checked for this task. Copied from the review at
    // enqueue time: reading the controller's single "latest review" field from the
    // executor meant a second confirmation could change the flags of a task that
    // was already queued, which is consent recorded for the wrong task.
    std::vector<std::string> agreements;
    // Set when the backend's own installer took over interactively (an uninstall
    // UI that must finish before the upgrade can install). The plugin cannot see
    // into that window; it waits, and says so.
    bool interactive = false;
    // Per-step log: what was run, what came back, why it stopped. Kept in memory
    // only (never written to disk: it can contain local paths) and capped, so a
    // long queue cannot grow without bound.
    std::vector<TaskLogEntry> log;
};

struct EnqueueResult {
    bool accepted = false;
    unsigned long long id = 0;
    std::string reason;   // why it was refused, when it was
};

class Queue {
public:
    // Accepts a confirmed plan. Refuses a plan that is not confirmed, whose
    // token is empty, or that duplicates an active task on the same identity.
    // Accepts a confirmed plan. `agreements` are the consents the user gave for
    // this plan; they are stored on the task so the executor never has to read
    // mutable review state from another thread.
    EnqueueResult enqueue(const Plan& plan, const std::vector<std::string>& agreements = {});

    // Cancels a task that has not started. Returns false when it is already
    // running (which only requests cancellation elsewhere).
    bool removePending(unsigned long long id);

    // Removes a task record by id, whatever its state, except a task that is
    // running: removing that would hide what is happening on the machine. The
    // record disappears from the Tasks page; machine state is untouched.
    bool removeFinished(unsigned long long id);

    // True when the task may be removed from the list (queued or terminal).
    bool canRemove(unsigned long long id) const;

    // True when the task may be cancelled right now (running).
    bool canCancel(unsigned long long id) const;

    // Requests cancellation of one specific task. For a running task this
    // records intent; it does not promise that the installer stopped or that
    // anything rolled back.
    bool requestCancel(unsigned long long id);

    // A paused queue keeps its running task but starts nothing new. Pausing is
    // not cancelling: the user can resume without re-confirming anything.
    void pause() { m_paused = true; }
    void resume() { m_paused = false; }
    bool paused() const { return m_paused; }

    // Appends one redacted log line. Over the cap the oldest entries are dropped
    // so the tail (the failure) always survives.
    void appendLog(unsigned long long id, const char* phase, const char* kind,
                   const std::string& text, int exitCode = -1);

    // Drops every terminal task record. Active work is never touched.
    int clearFinished();

    // Which task may start now: the head of each manager's lane. Reports the
    // reason a head is waiting when it cannot start.
    struct Next {
        bool found = false;
        unsigned long long id = 0;
        std::string waitingReason;
    };
    Next nextRunnable() const;

    // Moves a queued task to running. Only valid for the id nextRunnable gave.
    bool start(unsigned long long id);

    void setPhase(unsigned long long id, const std::string& phase);
    void setInteractivePending(unsigned long long id, bool pending);
    void setExitCode(unsigned long long id, int code);
    void setBlocked(unsigned long long id, const std::string& reason);
    void setNeedsReview(unsigned long long id, const std::string& reason);
    void setVerified(unsigned long long id, bool ok);

    // Terminal transitions. `finished` decides success from a verified
    // post-run inventory check and the exit code, never from the exit code
    // alone; a two-phase reinstall that removed but failed to install becomes a
    // partial failure with its own message.
    void finish(unsigned long long id, bool installedCheckOk, int exitCode);

    // The host went away. Everything active becomes Interrupted and nothing is
    // ever replayed automatically.
    void markInterrupted();

    const std::vector<Task>& tasks() const { return m_tasks; }
    const Task* find(unsigned long long id) const;

    // Replaces the whole task list. The development harness uses it to install its
    // deterministic rows into the queue itself: feeding only the model left the
    // Tasks header and the status line reporting an empty queue above a list of
    // four tasks, so the page contradicted itself.
    void setTasks(std::vector<Task> tasks) { m_tasks = std::move(tasks); }

    // A different manager touched a package that looks like the same software as
    // one of ours: park our task for review rather than racing it.
    bool parkForSharedInstance(unsigned long long id, const std::string& reason);

private:
    std::vector<Task> m_tasks;
    unsigned long long m_nextId = 1;
    bool m_paused = false;

    bool laneBusy(Manager m) const;
    bool sameIdentityActive(const PackageKey& key, unsigned long long exceptId) const;
};

} // namespace pkg
