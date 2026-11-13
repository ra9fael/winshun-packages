// plan.cpp — see plan.h.
#include "plan.h"

#include "version.h"

#include <atomic>

namespace pkg {
namespace {

std::atomic<unsigned long long> g_tokenSequence{0};

std::string nextToken()
{
    return "pkg-plan-" + std::to_string(++g_tokenSequence);
}

bool actionChangesInstalledState(Action a)
{
    return a == Action::Install || a == Action::Uninstall || a == Action::Upgrade || a == Action::Reinstall;
}

} // namespace

const char* actionId(Action a)
{
    switch (a) {
    case Action::Install: return "install";
    case Action::Uninstall: return "uninstall";
    case Action::Upgrade: return "upgrade";
    case Action::Reinstall: return "reinstall";
    case Action::None: break;
    }
    return "none";
}

Action actionFromId(const std::string& id)
{
    if (id == "install")
        return Action::Install;
    if (id == "uninstall")
        return Action::Uninstall;
    if (id == "upgrade")
        return Action::Upgrade;
    if (id == "reinstall")
        return Action::Reinstall;
    return Action::None;
}

const char* planStateId(PlanState s)
{
    switch (s) {
    case PlanState::Draft: return "draft";
    case PlanState::Blocked: return "blocked";
    case PlanState::NeedsReview: return "needs-review";
    case PlanState::Ready: return "ready";
    case PlanState::Confirmed: return "confirmed";
    }
    return "draft";
}

bool PlanFacts::operator==(const PlanFacts& o) const
{
    return installedVersion == o.installedVersion && targetVersion == o.targetVersion && source == o.source &&
           scope == o.scope && holdPolicy == o.holdPolicy && agreements == o.agreements &&
           dependencies == o.dependencies && materialDigest == o.materialDigest && metadataAt == o.metadataAt;
}

std::vector<std::string> compareFacts(const PlanFacts& confirmed, const PlanFacts& fresh)
{
    std::vector<std::string> changed;
    if (confirmed.installedVersion != fresh.installedVersion)
        changed.push_back("installedVersion");
    if (confirmed.targetVersion != fresh.targetVersion)
        changed.push_back("targetVersion");
    if (confirmed.source != fresh.source)
        changed.push_back("source");
    if (confirmed.scope != fresh.scope)
        changed.push_back("scope");
    if (confirmed.holdPolicy != fresh.holdPolicy)
        changed.push_back("holdPolicy");
    if (confirmed.agreements != fresh.agreements)
        changed.push_back("agreements");
    if (confirmed.dependencies != fresh.dependencies)
        changed.push_back("dependencies");
    if (confirmed.materialDigest != fresh.materialDigest)
        changed.push_back("materialDigest");
    if (confirmed.metadataAt != fresh.metadataAt)
        changed.push_back("metadataAt");
    return changed;
}

bool needsElevation(Scope scope) { return scope == Scope::Machine || scope == Scope::Global; }

Plan makePlan(const Package& package, Action action, bool elevated)
{
    Plan plan;
    plan.key = package.key;
    plan.action = action;
    plan.facts.installedVersion = package.installedVersion;
    plan.facts.targetVersion = action == Action::Uninstall ? std::string() : package.availableVersion;
    plan.facts.source = package.key.sourceOrUnknown();
    plan.facts.scope = package.key.scope;
    plan.facts.holdPolicy = package.holdPolicy;
    plan.facts.agreements = package.agreements;
    plan.facts.dependencies = package.dependencies;
    plan.facts.materialDigest = "unknown";
    plan.facts.metadataAt = package.metadataAt;
    plan.state = PlanState::Draft;
    plan.atomicOk = true;

    if (action == Action::None || !actionChangesInstalledState(action)) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = "no action selected";
        return plan;
    }

    // The identity gate first: an id that cannot be handed to a process safely
    // is never actionable, whatever the row says. An install needs a scope to
    // pass explicitly; an upgrade or uninstall matches by exact id alone, which
    // is why winget rows with an unknown scope (winget list prints no scope
    // column) can still be upgraded or removed.
    if (!isResolvable(package.key, action == Action::Install)) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = action == Action::Install
            ? "identity is not exactly resolvable (manager, source, id and scope must all be known "
              "and the id must be a plain package id)"
            : "identity is not exactly resolvable (manager, source and id must all be known "
              "and the id must be a plain package id)";
        return plan;
    }

    if (needsElevation(package.key.scope) && !elevated) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = "this scope needs an elevated installer path, and the plugin is running without "
                             "administrator rights right now; confirm the plugin's 跟随管理员权限 in the host "
                             "settings first";
        return plan;
    }

    // Held / pinned packages are never silently upgraded: an explicit policy
    // review is a separate, deliberate step.
    if (action == Action::Upgrade && !package.holdPolicy.empty()) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = "held or pinned: upgrade requires an explicit policy review";
        return plan;
    }

    if (action == Action::Upgrade) {
        // "unknown" target versions are not upgrades and must be reviewed one by
        // one rather than swept into a batch.
        if (package.availableVersion.empty() || package.availableVersion == "unknown" ||
            compareVersionStrings(package.availableVersion, package.installedVersion) == Order::Unknown) {
            plan.state = PlanState::Blocked;
            plan.blockedReason = "target version is unknown or not orderable: manual review required";
            return plan;
        }
        if (!isNewer(package.installedVersion, package.availableVersion)) {
            plan.state = PlanState::Blocked;
            plan.blockedReason = "no newer version is evidenced";
            return plan;
        }
    }

    if (action == Action::Install && !package.installedVersion.empty()) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = "already installed at this exact identity";
        return plan;
    }
    if (action == Action::Install && !package.canInstall) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = package.limits.empty() ? "backend reports install is unavailable" : package.limits;
        return plan;
    }

    if ((action == Action::Uninstall || action == Action::Reinstall)) {
        if (package.installedVersion.empty()) {
            plan.state = PlanState::Blocked;
            plan.blockedReason = "not installed at this exact identity";
            return plan;
        }
        // Removing anything requires the adapter to have computed that the
        // installed instance is uniquely located. An observed-only row is only
        // allowed once that verification passed, which is what canUninstall
        // encodes; matched rows never reach it.
        if (!package.canUninstall) {
            plan.state = PlanState::Blocked;
            plan.blockedReason = package.limits.empty()
                ? "the installed instance is not uniquely located, so removal is disabled"
                : package.limits;
            return plan;
        }
    }

    if (action == Action::Upgrade && !package.canUpgrade) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = package.limits.empty() ? "backend reports upgrade is unavailable" : package.limits;
        return plan;
    }

    if (action == Action::Reinstall) {
        // Two explicit phases, never atomic. The steps list is what the review
        // page and the task record both show.
        plan.steps = {"uninstall", "install"};
        plan.atomicOk = false;
    } else {
        plan.steps = {actionId(action)};
    }

    return plan;
}

void applyPreflight(Plan& plan, bool materialAvailable, const std::string& materialDigest,
                    const std::string& detail)
{
    if (plan.state == PlanState::Blocked)
        return;

    plan.facts.materialDigest = materialDigest;

    if (!materialAvailable) {
        plan.state = PlanState::Blocked;
        plan.blockedReason = detail.empty()
            ? "install material for the exact target version is not available, so nothing may be removed first"
            : detail;
        return;
    }

    plan.state = PlanState::Ready;
}

bool invalidateIfChanged(Plan& plan, const PlanFacts& fresh)
{
    if (plan.state != PlanState::Confirmed && plan.state != PlanState::Ready)
        return false;
    const auto changed = compareFacts(plan.facts, fresh);
    if (changed.empty())
        return false;
    plan.state = PlanState::NeedsReview;
    std::string reason = "the plan changed since it was confirmed:";
    for (const auto& c : changed)
        reason += " " + c + ";";
    plan.reviewReason = reason;
    plan.token.clear();
    // The facts now describe reality, so a re-review starts from what is true.
    plan.facts = fresh;
    return true;
}

bool confirmPlan(Plan& plan, const std::string& confirmedAt)
{
    if (plan.state != PlanState::Ready)
        return false;
    // Pending agreements must be empty: consent is recorded per agreement by the
    // UI, and the plan may only be confirmed once nothing is outstanding.
    if (!plan.facts.agreements.empty())
        return false;
    plan.state = PlanState::Confirmed;
    plan.token = nextToken();
    (void)confirmedAt;
    return true;
}

bool canRun(const Plan& plan)
{
    return plan.state == PlanState::Confirmed && !plan.token.empty() && !plan.steps.empty();
}

} // namespace pkg
