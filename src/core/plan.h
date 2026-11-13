// plan.h — the operation plan and the confirmation token.
//
// The design's flow is refresh -> resolve -> preflight -> review -> confirm ->
// queue, and the queue re-checks before it runs. A plan is an immutable value:
// once confirmed, any change to the facts it was built from invalidates the
// confirmation and the plan must be reviewed again. There is no "close enough".
#pragma once

#include <string>
#include <vector>

#include "model.h"

namespace pkg {

enum class Action { None, Install, Uninstall, Upgrade, Reinstall };

const char* actionId(Action a);

// The inverse of actionId. Returns Action::None for anything unrecognised, so a
// bad string from QML can never silently become a mutation.
Action actionFromId(const std::string& id);

// The facts a confirmation is bound to. A plan carries its own copy; the queue
// compares a fresh copy against it before running and refuses on any difference.
struct PlanFacts {
    std::string installedVersion;
    std::string targetVersion;
    std::string source;
    Scope scope = Scope::Unknown;
    std::string holdPolicy;
    std::vector<std::string> agreements;   // still-pending agreements
    std::string dependencies;
    std::string materialDigest;            // "verified", "missing", "hash-mismatch", "unknown"
    std::string metadataAt;

    bool operator==(const PlanFacts& o) const;
    bool operator!=(const PlanFacts& o) const { return !(*this == o); }
};

enum class PlanState {
    Draft,        // preflight not finished: confirm stays disabled
    Blocked,      // a hard stop (unsafe id, unknown scope, machine scope, held policy)
    NeedsReview,  // an earlier confirmation went stale
    Ready,        // preflight passed; still nothing runs until the user confirms
    Confirmed,    // the user confirmed; waiting for its lane
};

const char* planStateId(PlanState s);

struct Plan {
    PackageKey key;
    Action action = Action::None;
    PlanFacts facts;
    PlanState state = PlanState::Draft;
    std::string blockedReason;     // shown when state == Blocked
    std::string reviewReason;      // shown when state == NeedsReview
    std::vector<std::string> steps;  // e.g. {"uninstall", "install"} for reinstall
    bool atomicOk = true;          // false whenever the plan is a two-phase reinstall
    std::string token;             // opaque; a confirmation is not reusable
};

// Builds a draft plan for one action against one row. The result is Draft or
// Blocked; it never becomes Ready without a preflight. `elevated` is whether the
// plugin actually has an elevated path right now: machine/global scope is only
// blocked when it does not, so the manifest's `elevated` permission (and the
// host's confirmation) decide rather than a hardcoded refusal.
Plan makePlan(const Package& package, Action action, bool elevated = false);

// Applies preflight results. Reinstall additionally requires the material to be
// available for the exact installed version, because the design forbids
// uninstalling before it can reinstall.
void applyPreflight(Plan& plan, bool materialAvailable, const std::string& materialDigest,
                    const std::string& detail);

// Reusable-check before running: compares the facts captured at confirmation
// with freshly observed facts. Any difference downgrades the plan and returns
// the list of changed field names for the review page.
std::vector<std::string> compareFacts(const PlanFacts& confirmed, const PlanFacts& fresh);

// Marks a confirmed plan stale when anything changed; returns true when it did.
bool invalidateIfChanged(Plan& plan, const PlanFacts& fresh);

// Confirmation. Refuses unless the plan is Ready, records the confirmation time
// and produces a fresh token; the token of a previous confirmation never works
// twice.
bool confirmPlan(Plan& plan, const std::string& confirmedAt);

// Whether the queue may run this plan right now.
bool canRun(const Plan& plan);

// Whether the action needs an elevated installer path. The manifest declares
// `elevated`, so whether that path is available is decided at runtime (see
// makePlan's `elevated`): this only names the scopes that would need it.
bool needsElevation(Scope scope);

} // namespace pkg
