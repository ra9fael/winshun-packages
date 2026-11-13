// queue.cpp — see queue.h.
#include "queue.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace pkg {

const char* taskStateId(TaskState s)
{
    switch (s) {
    case TaskState::Queued: return "queued";
    case TaskState::Preflight: return "preflight";
    case TaskState::NeedsReview: return "needs-review";
    case TaskState::Blocked: return "blocked";
    case TaskState::Running: return "running";
    case TaskState::InteractivePending: return "interactive-pending";
    case TaskState::CancelRequested: return "cancel-requested";
    case TaskState::RebootRequired: return "reboot-required";
    case TaskState::Succeeded: return "succeeded";
    case TaskState::Failed: return "failed";
    case TaskState::PartialFailure: return "partial-failure";
    case TaskState::Cancelled: return "cancelled";
    case TaskState::Interrupted: return "interrupted";
    }
    return "queued";
}

bool isTerminal(TaskState s)
{
    switch (s) {
    case TaskState::Succeeded:
    case TaskState::Failed:
    case TaskState::PartialFailure:
    case TaskState::Cancelled:
    case TaskState::Interrupted:
        return true;
    default:
        return false;
    }
}

bool isActive(TaskState s) { return !isTerminal(s); }

namespace {

Task* findMutable(std::vector<Task>& tasks, unsigned long long id)
{
    for (auto& t : tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

// Local wall clock as "HH:MM:SS". Only used for log display, so a monotonic
// clock would be less readable and no more correct here.
std::string nowClock()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

} // namespace

EnqueueResult Queue::enqueue(const Plan& plan)
{
    EnqueueResult r;
    if (!canRun(plan)) {
        r.reason = "the plan is not confirmed (state " + std::string(planStateId(plan.state)) + ")";
        return r;
    }
    if (sameIdentityActive(plan.key, 0)) {
        // De-duplication is per exact identity including scope, so a queued
        // user-scope install never suppresses a machine-scope one.
        r.reason = "an active task already targets this exact identity";
        return r;
    }
    Task t;
    t.id = m_nextId++;
    t.plan = plan;
    t.state = TaskState::Queued;
    m_tasks.push_back(t);
    r.accepted = true;
    r.id = t.id;
    return r;
}

bool Queue::removePending(unsigned long long id)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return false;
    // Only tasks that have not started may be removed. A running task only gets
    // a cancel request, because removing it would hide what is happening.
    if (t->state != TaskState::Queued && t->state != TaskState::Preflight &&
        t->state != TaskState::NeedsReview && t->state != TaskState::Blocked)
        return false;
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                 [id](const Task& x) { return x.id == id; }),
                  m_tasks.end());
    return true;
}

void Queue::appendLog(unsigned long long id, const char* phase, const char* kind,
                      const std::string& text, int exitCode)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return;
    TaskLogEntry e;
    e.at = nowClock();
    e.phase = phase ? phase : "";
    e.kind = kind ? kind : "info";
    e.text = text;
    e.exitCode = exitCode;
    t->log.push_back(std::move(e));
    if (t->log.size() > kMaxTaskLogEntries) {
        // Keep the tail: the failure and the exit code are what explain a run.
        const std::size_t excess = t->log.size() - kMaxTaskLogEntries;
        t->log.erase(t->log.begin(), t->log.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}

int Queue::clearFinished()
{
    const auto before = m_tasks.size();
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                 [](const Task& t) { return isTerminal(t.state); }),
                  m_tasks.end());
    return static_cast<int>(before - m_tasks.size());
}

bool Queue::removeFinished(unsigned long long id)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return false;
    // A running task is never removed: the record is the only thing telling the
    // user that a process is still touching the machine.
    if (t->state == TaskState::Running || t->state == TaskState::InteractivePending ||
        t->state == TaskState::CancelRequested)
        return false;
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                 [id](const Task& x) { return x.id == id; }),
                  m_tasks.end());
    return true;
}

bool Queue::canRemove(unsigned long long id) const
{
    const Task* t = find(id);
    if (!t)
        return false;
    return t->state != TaskState::Running && t->state != TaskState::InteractivePending &&
           t->state != TaskState::CancelRequested;
}

bool Queue::canCancel(unsigned long long id) const
{
    const Task* t = find(id);
    if (!t)
        return false;
    return t->state == TaskState::Running || t->state == TaskState::InteractivePending;
}

bool Queue::requestCancel(unsigned long long id)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return false;
    if (t->state == TaskState::Queued || t->state == TaskState::Preflight) {
        t->state = TaskState::Cancelled;
        return true;
    }
    if (t->state == TaskState::Running || t->state == TaskState::InteractivePending) {
        // Records intent only: the installer may keep running, and no rollback
        // is promised. The real state is re-checked afterwards.
        t->state = TaskState::CancelRequested;
        t->detail = "cancel requested; the installer may still be running and nothing is rolled back";
        return true;
    }
    return false;
}

bool Queue::laneBusy(Manager m) const
{
    for (const auto& t : m_tasks) {
        if (t.plan.key.manager != m)
            continue;
        if (t.state == TaskState::Running || t.state == TaskState::InteractivePending ||
            t.state == TaskState::CancelRequested || t.state == TaskState::Queued ||
            t.state == TaskState::Preflight)
            return true;
    }
    return false;
}

bool Queue::sameIdentityActive(const PackageKey& key, unsigned long long exceptId) const
{
    for (const auto& t : m_tasks) {
        if (t.id == exceptId)
            continue;
        if (isActive(t.state) && t.plan.key == key)
            return true;
    }
    return false;
}

Queue::Next Queue::nextRunnable() const
{
    Next n;
    // A paused queue hands out nothing new; the task that is already running
    // keeps running, because pause is not cancel.
    if (m_paused) {
        for (const auto& t : m_tasks) {
            if (t.state == TaskState::Running || t.state == TaskState::InteractivePending) {
                n.waitingReason = "the queue is paused by the user and a task is still running";
                return n;
            }
        }
        n.waitingReason = "the queue is paused by the user";
        return n;
    }
    // Managers that already have a running task are skipped: one mutation at a
    // time per manager, and a reinstall's second phase stays in the same slot.
    for (const auto& t : m_tasks) {
        if (t.state != TaskState::Queued)
            continue;
        bool aheadInLane = false;
        for (const auto& other : m_tasks) {
            if (other.id == t.id)
                break; // only earlier entries can block
            if (other.plan.key.manager != t.plan.key.manager)
                continue;
            if (isActive(other.state)) {
                aheadInLane = true;
                break;
            }
        }
        if (aheadInLane)
            continue;
        n.found = true;
        n.id = t.id;
        return n;
    }
    // Report why a head is waiting, for the Tasks page.
    for (const auto& t : m_tasks) {
        if (t.state == TaskState::InteractivePending) {
            n.waitingReason = "an interactive installer is still waiting";
            return n;
        }
        if (t.state == TaskState::Running) {
            n.waitingReason = "the lane is running another task";
            return n;
        }
    }
    return n;
}

bool Queue::start(unsigned long long id)
{
    Task* t = findMutable(m_tasks, id);
    if (!t || t->state != TaskState::Queued)
        return false;
    t->state = TaskState::Running;
    t->phase = "acquire";
    return true;
}

void Queue::setPhase(unsigned long long id, const std::string& phase)
{
    if (Task* t = findMutable(m_tasks, id))
        t->phase = phase;
}

void Queue::setInteractivePending(unsigned long long id, bool pending)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return;
    if (pending && (t->state == TaskState::Running || t->state == TaskState::CancelRequested))
        t->state = TaskState::InteractivePending;
    else if (!pending && t->state == TaskState::InteractivePending)
        t->state = TaskState::Running;
}

void Queue::setExitCode(unsigned long long id, int code)
{
    if (Task* t = findMutable(m_tasks, id))
        t->exitCode = code;
}

void Queue::setBlocked(unsigned long long id, const std::string& reason)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return;
    t->state = TaskState::Blocked;
    t->blockedReason = reason;
}

void Queue::setNeedsReview(unsigned long long id, const std::string& reason)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return;
    t->state = TaskState::NeedsReview;
    t->blockedReason = reason;
    t->plan.token.clear();
}

void Queue::setVerified(unsigned long long id, bool ok)
{
    if (Task* t = findMutable(m_tasks, id))
        t->verifiedAfterRun = ok;
}

void Queue::finish(unsigned long long id, bool installedCheckOk, int exitCode)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return;
    t->exitCode = exitCode;

    // A cancel request that reached the end is still reported by what actually
    // happened: if the package is gone and the install never ran, that is not a
    // clean cancellation.
    const bool removedPhaseDone = t->plan.action == Action::Reinstall && t->phase != "uninstall";

    if (exitCode != 0) {
        if (removedPhaseDone) {
            t->state = TaskState::PartialFailure;
            t->detail = "removed successfully but the install failed; the package may be absent and data may be "
                        "affected";
        } else if (!installedCheckOk && t->plan.action == Action::Uninstall &&
                   t->state == TaskState::CancelRequested) {
            t->state = TaskState::PartialFailure;
            t->detail = "cancelled during uninstall; the real state is uncertain";
        } else {
            t->state = TaskState::Failed;
        }
        return;
    }

    if (!installedCheckOk) {
        // Exit code 0 is not success: the post-run inventory check decides.
        if (removedPhaseDone) {
            t->state = TaskState::PartialFailure;
            t->detail = "the install reported success but the package was not found afterwards";
        } else {
            t->state = TaskState::Failed;
            t->detail = "the backend reported success but the resulting state did not match";
        }
        return;
    }

    if (t->state == TaskState::CancelRequested) {
        t->state = TaskState::Cancelled;
        t->detail = "cancelled; the real state was refreshed afterwards";
        return;
    }

    t->state = TaskState::Succeeded;
}

void Queue::markInterrupted()
{
    for (auto& t : m_tasks) {
        if (isActive(t.state)) {
            t.state = TaskState::Interrupted;
            t.detail = "the host exited or was disabled; the real state is unknown and nothing was replayed";
        }
    }
}

const Task* Queue::find(unsigned long long id) const
{
    for (const auto& t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

bool Queue::parkForSharedInstance(unsigned long long id, const std::string& reason)
{
    Task* t = findMutable(m_tasks, id);
    if (!t)
        return false;
    if (t->state == TaskState::Running || t->state == TaskState::InteractivePending)
        return false; // already under way: report, do not pretend to stop it
    t->state = TaskState::NeedsReview;
    t->blockedReason = reason;
    t->plan.token.clear();
    return true;
}

} // namespace pkg
