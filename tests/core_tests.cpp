// core_tests.cpp — L1: pure logic, no OS headers, no host, no backend.
//
// Each case names the design section and the TEST-PLAN item it covers, so a
// failure points at the requirement it broke rather than at a function name.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/core/identity.h"
#include "../src/core/cache.h"
#include "../src/core/info_cache.h"
#include "../src/core/pins_cache.h"
#include "../src/core/probe_cache.h"
#include "../src/core/search_cache.h"
#include "../src/core/model.h"
#include "../src/core/plan.h"
#include "../src/core/queue.h"
#include "../src/core/redact.h"
#include "../src/core/version.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void section(const char* name) { std::printf("-- %s\n", name); }

using namespace pkg;

// A braced initializer list cannot be parenthesised, so the cases that build a
// PackageKey inline spell the comparison out instead of going through CHECK.
PackageKey key(Manager m, const char* source, const char* id, Scope s)
{
    PackageKey k;
    k.manager = m;
    k.source = source;
    k.id = id;
    k.scope = s;
    return k;
}

Package makePackage(Manager manager, const std::string& source, const std::string& id, Scope scope,
                    const std::string& installed, const std::string& available,
                    Provenance provenance = Provenance::BackendRecorded)
{
    Package p;
    p.key.manager = manager;
    p.key.source = source;
    p.key.id = id;
    p.key.scope = scope;
    p.name = id;
    p.installedVersion = installed;
    p.availableVersion = available;
    p.provenance = provenance;
    p.canInstall = installed.empty();
    p.canUninstall = !installed.empty();
    p.canUpgrade = !installed.empty() && !available.empty();
    p.dependencies = "none";
    p.material = "verified";
    p.metadataAt = "2026-11-10T08:40:00Z";
    return p;
}

// T03: identity, scope and unknown handling.
void identityRules()
{
    section("identity (T03)");
    const PackageKey a{Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", Scope::User};
    const PackageKey b{Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", Scope::Machine};
    CHECK(a != b);                                     // scope is part of identity
    CHECK(a.text() != b.text());
    CHECK(a == key(Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", Scope::User));

    // Same id, different source: two packages, never merged.
    const PackageKey c{Manager::Winget, "msstore", "BurntSushi.ripgrep.MSVC", Scope::User};
    CHECK(a != c);

    // An unknown source stays an explicit unknown and never matches a real one.
    const PackageKey unknownSource{Manager::Scoop, "", "ripgrep", Scope::User};
    CHECK(unknownSource.sourceOrUnknown() == "unknown");
    CHECK(unknownSource != key(Manager::Scoop, "main", "ripgrep", Scope::User));

    // Unknown scope is not a wildcard. An install needs a concrete --scope, so
    // it stays blocked; an upgrade or uninstall matches by exact id alone (the
    // backend resolves the instance), so winget rows whose scope `winget list`
    // never printed are still actionable for those two.
    const PackageKey unknownScope{Manager::Scoop, "main", "ripgrep", Scope::Unknown};
    CHECK(!isResolvable(unknownScope, true));    // install: scope mandatory
    CHECK(isResolvable(unknownScope, false));    // upgrade/uninstall: id alone
    CHECK(isResolvable(key(Manager::Scoop, "main", "ripgrep", Scope::User)));

    // Managed only with backend evidence.
    CHECK(countsAsManaged(Provenance::BackendRecorded));
    CHECK(!countsAsManaged(Provenance::Observed));
    CHECK(!countsAsManaged(Provenance::Matched));
    CHECK(!countsAsManaged(Provenance::None));

    // Cross-manager duplicate is only a hint.
    const PackageKey scoopRg{Manager::Scoop, "main", "ripgrep", Scope::User};
    CHECK(possibleDuplicate(a, scoopRg).has_value());
    CHECK(!possibleDuplicate(a, key(Manager::Winget, "winget", "jqlang.jq", Scope::User)).has_value());
    CHECK(!possibleDuplicate(scoopRg, key(Manager::Scoop, "extras", "ripgrep", Scope::User)).has_value());
}

// T11: ids and sources are validated before ever becoming an argument.
void argumentSafety()
{
    section("argument safety (T11)");
    CHECK(isSafePackageId("BurntSushi.ripgrep.MSVC"));
    CHECK(isSafePackageId("nodejs-lts"));
    CHECK(isSafePackageId("7zip.7zip"));
    CHECK(isSafePackageId("Microsoft.PowerToys"));

    // Option injection.
    CHECK(!isSafePackageId("--force"));
    CHECK(!isSafePackageId("-e"));
    // Shell metacharacters and whitespace.
    CHECK(!isSafePackageId("rip grep"));
    CHECK(!isSafePackageId("rip;grep"));
    CHECK(!isSafePackageId("rip|grep"));
    CHECK(!isSafePackageId("rip$grep"));
    CHECK(!isSafePackageId("rip`grep"));
    CHECK(!isSafePackageId("rip&&grep"));
    CHECK(!isSafePackageId("rip>out"));
    CHECK(!isSafePackageId("rip%PATH%"));
    // Paths and URLs are never ids.
    CHECK(!isSafePackageId("..\\..\\windows\\system32"));
    CHECK(!isSafePackageId("C:\\Windows\\notepad.exe"));
    CHECK(!isSafePackageId("https://evil.example/payload.ps1"));
    // Control characters, empties, absurd length.
    CHECK(!isSafePackageId(""));
    CHECK(!isSafePackageId(std::string("rip\0grep", 8)));
    CHECK(!isSafePackageId(std::string(129, 'a')));
    // Non-ASCII ids are rejected: the backends we drive take ASCII ids only.
    CHECK(!isSafePackageId("软件包"));

    CHECK(isSafeSourceName("main"));
    CHECK(isSafeSourceName("extras"));
    CHECK(!isSafeSourceName("main bucket"));
    CHECK(!isSafeSourceName("-x"));
}

// PKG-F05/F10/F29: version ordering, never string comparison.
void versionRules()
{
    section("versions (T07)");
    CHECK(compareVersionStrings("1.10.0", "1.9.0") == Order::Newer);      // string compare would be wrong
    CHECK(compareVersionStrings("14.1.2", "14.1.0") == Order::Newer);
    CHECK(compareVersionStrings("1.0.0", "1.0") == Order::Same);            // trailing zeros
    CHECK(compareVersionStrings("1.0.0-beta", "1.0.0") == Order::Older);    // pre-release sorts below release
    CHECK(compareVersionStrings("v2.0", "1.9") == Order::Newer);            // leading v
    CHECK(compareVersionStrings("unknown", "1.0") == Order::Unknown);
    CHECK(compareVersionStrings("latest", "1.0") == Order::Unknown);
    CHECK(compareVersionStrings("", "1.0") == Order::Unknown);
    // Different pre-release identifiers now follow SemVer §11: alphanumeric
    // identifiers compare in ASCII order, so alpha < beta and this pair is a
    // provable upgrade. Unorderable pairs need a malformed identifier.
    CHECK(compareVersionStrings("2.0.0-alpha", "2.0.0-beta") == Order::Older);

    // SemVer §11 pre-release identifiers: rc.2 < rc.3 (numeric identifiers
    // compare numerically — a DeepSeek-style "0.2.2-rc.2 → 0.2.2-rc.3" row
    // used to be blocked as unorderable), alpha < alpha.1 (a shorter set
    // sorts below a longer one), and a numeric identifier sorts below an
    // alphanumeric one at the same position.
    CHECK(compareVersionStrings("0.2.2-rc.1", "0.2.2-rc.2") == Order::Older);
    CHECK(compareVersionStrings("0.2.2-rc.2", "0.2.2-rc.3") == Order::Older);
    CHECK(compareVersionStrings("0.2.2-rc.2", "0.2.2-rc.2") == Order::Same);
    CHECK(compareVersionStrings("1.0.0-alpha", "1.0.0-alpha.1") == Order::Older);
    CHECK(compareVersionStrings("1.0.0-alpha.1", "1.0.0-alpha.beta") == Order::Older);
    CHECK(compareVersionStrings("1.0.0-alpha.beta", "1.0.0-beta") == Order::Older);
    CHECK(compareVersionStrings("1.0.0-beta.2", "1.0.0-beta.11") == Order::Older);
    CHECK(compareVersionStrings("1.0.0-rc.2", "1.0.0-rc.10") == Order::Older);
    CHECK(compareVersionStrings("2.0.0+build.1", "2.0.0+build.2") == Order::Same);   // build metadata ignored
    CHECK(compareVersionStrings("2.0.0+build.1", "2.0.0") == Order::Same);
    CHECK(compareVersionStrings("0.2.2-rc.2", "0.3") == Order::Older);   // numeric part first
    // A malformed identifier ("alpha..b") stays unorderable rather than guessed.
    CHECK(compareVersionStrings("1.0.0-alpha..b", "1.0.0-alpha.2") == Order::Unknown);

    CHECK(isNewer("1.9.0", "1.10.0"));
    CHECK(!isNewer("1.10.0", "1.9.0"));
    CHECK(!isNewer("1.0.0", "unknown"));

    // "@version" is split, never waved through as part of an id.
    const auto pinned = splitPinnedRef("vscode@1.94.2");
    CHECK(pinned.id == "vscode" && pinned.version == "1.94.2");
    const auto plain = splitPinnedRef("vscode");
    CHECK(plain.id == "vscode" && plain.version.empty());
    CHECK(!isSafePackageId("vscode@1.94.2"));   // the raw form is not an id
    CHECK(buildPinnedRef("vscode", "1.94.2").has_value());
    CHECK(!buildPinnedRef("vscode", "--force").has_value());
    CHECK(!buildPinnedRef("-vscode", "1.0").has_value());
}

// T04: provenance decides what the UI may claim and what may be removed.
void provenanceRules()
{
    section("provenance (T04)");
    Snapshot s;
    s.packages.push_back(makePackage(Manager::Winget, "winget", "7zip.7zip", Scope::User, "24.08", "",
                                     Provenance::Observed));
    s.packages.push_back(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1",
                                     Provenance::Matched));
    s.packages.push_back(makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::User, "", "1.7.1",
                                     Provenance::None));

    const auto installed = rowsForTab(s, Tab::Installed, "", Manager::Unknown);
    CHECK(installed.size() == 2);   // observed and matched still belong in Installed
    const auto discover = rowsForTab(s, Tab::Discover, "", Manager::Unknown);
    CHECK(discover.size() == 3);    // Discover is a candidate search, not installed-only
    const auto wingetOnly = rowsForTab(s, Tab::Installed, "", Manager::Winget);
    CHECK(wingetOnly.size() == 1);
    const auto filtered = rowsForTab(s, Tab::Discover, "jq", Manager::Unknown);
    CHECK(filtered.size() == 1 && filtered[0].key.id == "jqlang.jq");

    // An observed row is not removable even though it appears installed.
    const Package observed = makePackage(Manager::Winget, "winget", "7zip.7zip", Scope::User, "24.08", "",
                                         Provenance::Observed);
    Package unremovable = observed;
    unremovable.canUninstall = false;
    unremovable.limits = "the installed instance is not uniquely located";
    const Plan p = makePlan(unremovable, Action::Uninstall);
    CHECK(p.state == PlanState::Blocked);
    CHECK(!p.blockedReason.empty());
}

// T06: a confirmation is bound to facts and dies when they change.
void planFactsRules()
{
    section("plan confirmation (T06)");
    Package p = makePackage(Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", Scope::User, "14.1.0", "14.1.2");
    Plan plan = makePlan(p, Action::Upgrade);
    CHECK(plan.state == PlanState::Draft);
    CHECK(!canRun(plan));                      // draft never runs

    applyPreflight(plan, true, "verified", "");
    CHECK(plan.state == PlanState::Ready);
    CHECK(!canRun(plan));                      // ready still needs confirmation

    CHECK(confirmPlan(plan, "2026-11-10T08:50:00Z"));
    CHECK(plan.state == PlanState::Confirmed);
    CHECK(canRun(plan));
    const std::string token = plan.token;
    CHECK(!token.empty());

    // A second confirmation of the same plan must not mint a reusable token.
    const bool again = confirmPlan(plan, "2026-11-10T08:51:00Z");
    CHECK(!again);
    CHECK(plan.token == token);

    // Any changed fact downgrades to needs-review and clears the token.
    PlanFacts fresh = plan.facts;
    fresh.targetVersion = "14.2.0";
    CHECK(invalidateIfChanged(plan, fresh));
    CHECK(plan.state == PlanState::NeedsReview);
    CHECK(plan.token.empty());
    CHECK(!canRun(plan));

    // Nothing changed: the plan stays confirmed.
    Plan stable = makePlan(p, Action::Upgrade);
    applyPreflight(stable, true, "verified", "");
    CHECK(confirmPlan(stable, "t"));
    CHECK(!invalidateIfChanged(stable, stable.facts));
    CHECK(stable.state == PlanState::Confirmed);

    // Pending agreements block confirmation: consent is recorded separately.
    Package withAgreement = makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::User, "", "1.7.1");
    withAgreement.agreements = {"source"};
    Plan gated = makePlan(withAgreement, Action::Install);
    applyPreflight(gated, true, "verified", "");
    CHECK(gated.state == PlanState::Ready);
    CHECK(!confirmPlan(gated, "t"));           // outstanding agreement

    // The action round-trips through its id, and an unknown id can never become
    // a mutation: the review page's selector is driven by these strings.
    CHECK(actionFromId("install") == Action::Install);
    CHECK(actionFromId("uninstall") == Action::Uninstall);
    CHECK(actionFromId("upgrade") == Action::Upgrade);
    CHECK(actionFromId("reinstall") == Action::Reinstall);
    CHECK(actionFromId("none") == Action::None);
    CHECK(actionFromId("") == Action::None);
    CHECK(actionFromId("--force") == Action::None);
    for (Action a : {Action::Install, Action::Uninstall, Action::Upgrade, Action::Reinstall})
        CHECK(actionFromId(actionId(a)) == a);
}

// T08: preflight gates a reinstall before anything is removed.
void reinstallRules()
{
    section("reinstall preflight (T08)");
    Package p = makePackage(Manager::Scoop, "extras", "vscode", Scope::User, "1.94.2", "1.95.1");
    Plan plan = makePlan(p, Action::Reinstall);
    CHECK(plan.action == Action::Reinstall);
    CHECK(!plan.atomicOk);                     // never atomic
    CHECK(plan.steps.size() == 2);
    CHECK(plan.steps[0] == "uninstall" && plan.steps[1] == "install");
    CHECK(plan.state == PlanState::Draft);

    // Material missing for the exact version: blocked before the uninstall.
    applyPreflight(plan, false, "missing", "material for 1.94.2 is not available");
    CHECK(plan.state == PlanState::Blocked);
    CHECK(!confirmPlan(plan, "t"));

    // Verified material: proceeds, still two phases.
    Plan ok = makePlan(p, Action::Reinstall);
    applyPreflight(ok, true, "verified", "");
    CHECK(ok.state == PlanState::Ready);
    CHECK(ok.steps.size() == 2);
    CHECK(confirmPlan(ok, "t"));
    CHECK(ok.facts.targetVersion == "1.95.1");
}

// T07: held/pinned and unknown versions never become a plan.
void policyRules()
{
    section("hold/pin policy (T07)");
    Package pinned = makePackage(Manager::Scoop, "extras", "vscode", Scope::User, "1.94.2", "1.95.1");
    pinned.holdPolicy = "pinned @1.94.2";
    const Plan p = makePlan(pinned, Action::Upgrade);
    CHECK(p.state == PlanState::Blocked);

    Package unknownVersion = makePackage(Manager::Winget, "winget", "7zip.7zip", Scope::User, "24.08", "unknown");
    const Plan u = makePlan(unknownVersion, Action::Upgrade);
    CHECK(u.state == PlanState::Blocked);

    // An unorderable target is not an upgrade. "alpha..b" is a malformed
    // SemVer identifier, so the pair stays Unknown and the plan blocks.
    Package weird = makePackage(Manager::Winget, "winget", "x.y", Scope::User, "1.0.0-alpha..b", "1.0.0-alpha.2");
    CHECK(makePlan(weird, Action::Upgrade).state == PlanState::Blocked);

    // An unknown scope (winget list prints no scope column) blocks an install
    // — nothing to pass as --scope — but not an upgrade or an uninstall, which
    // match by exact id alone. The upgrade plan is a Draft still needing
    // material and a preflight, exactly like a scoped one.
    Package wingetUnknown = makePackage(Manager::Winget, "winget", "NetEase.UURemote", Scope::Unknown,
                                        "4.45.14372", "4.46.0.4716");
    const Plan upgradeUnknown = makePlan(wingetUnknown, Action::Upgrade);
    CHECK(upgradeUnknown.state == PlanState::Draft);
    CHECK(upgradeUnknown.blockedReason.empty());
    const Plan installUnknown = makePlan(wingetUnknown, Action::Install);
    CHECK(installUnknown.state == PlanState::Blocked);
    const Plan uninstallUnknown = makePlan(wingetUnknown, Action::Uninstall);
    CHECK(uninstallUnknown.state == PlanState::Draft);

    // WinGet owns installer UAC; planning must not require an elevated UI.
    Package machine = makePackage(Manager::Winget, "winget", "Microsoft.PowerToys", Scope::Machine, "", "0.87.1");
    const Plan notElevated = makePlan(machine, Action::Install, false);
    CHECK(notElevated.state == PlanState::Draft);
    CHECK(notElevated.blockedReason.empty());
    auto global = makePackage(Manager::Scoop, "main", "git", Scope::Global, "", "2.0");
    CHECK(makePlan(global, Action::Install, false).state == PlanState::Blocked);
    // Elevation is not a licence to skip the rest: the plan is still a Draft
    // that needs material and a preflight, never Ready on its own.
    const Plan elevated = makePlan(machine, Action::Install, true);
    CHECK(elevated.state == PlanState::Draft);
    CHECK(needsElevation(Scope::Machine));
    CHECK(needsElevation(Scope::Global));
    CHECK(!needsElevation(Scope::User));
}

// T09: one serial lane per manager, no interleaving inside a reinstall.
void queueOrdering()
{
    section("queue ordering (T09)");
    Queue q;

    auto confirmed = [](Package p, Action a) {
        Plan plan = makePlan(p, a);
        applyPreflight(plan, true, "verified", "");
        confirmPlan(plan, "t");
        return plan;
    };

    const Plan scoopGit = confirmed(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"),
                                    Action::Upgrade);
    const Plan scoopVscode =
        confirmed(makePackage(Manager::Scoop, "extras", "vscode", Scope::User, "1.94.2", "1.94.2"), Action::Reinstall);
    const Plan wingetJq =
        confirmed(makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::User, "", "1.7.1"), Action::Install);

    const auto a = q.enqueue(scoopGit);
    const auto b = q.enqueue(scoopVscode);
    const auto c = q.enqueue(wingetJq);
    CHECK(a.accepted && b.accepted && c.accepted);

    // First ready task is the head of the first lane.
    auto next = q.nextRunnable();
    CHECK(next.found && next.id == a.id);
    CHECK(q.start(next.id));

    // While Scoop is busy, the other manager may still work; Scoop's second
    // task must wait even though it is second overall.
    next = q.nextRunnable();
    CHECK(next.found);
    CHECK(next.id == c.id);                    // WinGet jumps ahead: lanes are independent

    // Reinstall occupies its lane for both phases: after phase 1 the task is
    // still active, so nothing else in that lane may start.
    CHECK(q.start(c.id));
    q.finish(c.id, true, 0);
    q.requestCancel(a.id);
    q.finish(a.id, true, 0);
    next = q.nextRunnable();
    CHECK(next.found && next.id == b.id);
    CHECK(q.start(b.id));
    q.setPhase(b.id, "uninstall");
    q.setVerified(b.id, true);
    // Simulate the uninstall phase completing by moving to the install phase;
    // the task stays Running and its lane stays busy.
    q.setPhase(b.id, "install");
    CHECK(q.nextRunnable().found == false);

    // De-duplication is per exact identity: the same id in another scope is a
    // different package and must be accepted.
    Queue dedup;
    const Plan userScope =
        confirmed(makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::User, "", "1.7.1"), Action::Install);
    const Plan machineScope =
        confirmed(makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::Machine, "", "1.7.1"), Action::Install);
    // machine scope plans are blocked by policy, so build the second one by hand
    // to prove the identity rule itself rather than the policy rule.
    CHECK(dedup.enqueue(userScope).accepted);
    CHECK(!dedup.enqueue(userScope).accepted);                       // same identity twice: refused
    Plan sameIdOtherScope = userScope;
    sameIdOtherScope.key.scope = Scope::Machine;
    sameIdOtherScope.token = "forced-token";
    CHECK(dedup.enqueue(sameIdOtherScope).accepted);                 // different scope: different package
    (void)machineScope;
}

// T10: cancellation, interruption and partial failure are distinct outcomes.
void queueOutcomes()
{
    section("queue outcomes (T10)");
    auto confirmed = [](Package p, Action a) {
        Plan plan = makePlan(p, a);
        applyPreflight(plan, true, "verified", "");
        confirmPlan(plan, "t");
        return plan;
    };

    {
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        CHECK(q.removePending(r.id));          // not started: removable
        CHECK(q.tasks().empty());
    }
    {
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        CHECK(q.start(r.id));
        CHECK(!q.removePending(r.id));         // running: not removable
        CHECK(q.requestCancel(r.id));          // only a request
        CHECK(q.find(r.id)->state == TaskState::CancelRequested);
        // The installer may have finished the work anyway; the real state decides.
        q.finish(r.id, true, 0);
        CHECK(q.find(r.id)->state == TaskState::Cancelled);
    }
    {
        // Reinstall: uninstall succeeded, install failed => partial failure.
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "extras", "vscode", Scope::User, "1.94.2", "1.95.1"), Action::Reinstall));
        CHECK(q.start(r.id));
        q.setPhase(r.id, "uninstall");
        q.setVerified(r.id, true);
        q.setPhase(r.id, "install");
        q.finish(r.id, false, 1603);
        CHECK(q.find(r.id)->state == TaskState::PartialFailure);
        CHECK(q.find(r.id)->detail.find("removed") != std::string::npos);
    }
    {
        // Exit code 0 is not success when the inventory check disagrees.
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Winget, "winget", "jqlang.jq", Scope::User, "", "1.7.1"), Action::Install));
        CHECK(q.start(r.id));
        q.finish(r.id, false, 0);
        CHECK(q.find(r.id)->state == TaskState::Failed);
    }
    {
        // Host exit: everything active becomes interrupted and is never replayed.
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "main", "nodejs-lts", Scope::User, "22.9.0", "22.11.0"), Action::Upgrade));
        CHECK(q.start(r.id));
        q.markInterrupted();
        CHECK(q.find(r.id)->state == TaskState::Interrupted);
        CHECK(q.nextRunnable().found == false);
    }
    {
        // A shared instance found in the other manager parks the task.
        Queue q;
        const auto r = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "main", "ripgrep", Scope::User, "14.1.0", "14.1.0"), Action::Uninstall));
        CHECK(q.parkForSharedInstance(r.id, "possible shared instance"));
        CHECK(q.find(r.id)->state == TaskState::NeedsReview);
        CHECK(q.find(r.id)->plan.token.empty());
    }
}

// T21: per-task operations address one id, and pause/resume gates new starts
// without touching the task that is already running.
void queueIntervention()
{
    section("queue intervention (T21)");
    auto confirmed = [](Package p, Action a) {
        Plan plan = makePlan(p, a);
        applyPreflight(plan, true, "verified", "");
        confirmPlan(plan, "t");
        return plan;
    };

    {
        // Removing by id must hit that id, not "the last removable one".
        Queue q;
        const auto first = q.enqueue(
            confirmed(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        const auto second = q.enqueue(
            confirmed(makePackage(Manager::Scoop, "extras", "fzf", Scope::User, "0.57.0", "0.62.0"), Action::Upgrade));
        CHECK(first.accepted && second.accepted);
        CHECK(q.removeFinished(first.id));
        CHECK(q.find(first.id) == nullptr);
        CHECK(q.find(second.id) != nullptr);          // the other record is untouched
        CHECK(!q.removeFinished(9999));                // unknown id is a no-op
    }
    {
        // A running task cannot be removed: that record is the only evidence a
        // process is still touching the machine.
        Queue q;
        const auto r = q.enqueue(
            confirmed(makePackage(Manager::Winget, "winget", "Git.Git", Scope::User, "2.47.1", "2.51.0"),
                      Action::Upgrade));
        CHECK(q.start(r.id));
        CHECK(!q.canRemove(r.id));
        CHECK(q.canCancel(r.id));
        CHECK(!q.removeFinished(r.id));
        CHECK(!q.removePending(r.id));
        CHECK(q.find(r.id) != nullptr);
    }
    {
        // A terminal record can be removed; a queued one too.
        Queue q;
        const auto done = q.enqueue(
            confirmed(makePackage(Manager::Winget, "winget", "Git.Git", Scope::User, "2.47.1", "2.51.0"),
                      Action::Upgrade));
        CHECK(q.start(done.id));
        q.finish(done.id, true, 0);
        CHECK(q.find(done.id)->state == TaskState::Succeeded);
        CHECK(q.canRemove(done.id));
        CHECK(q.removeFinished(done.id));
        CHECK(!q.canRemove(done.id));
    }
    {
        // Pause hands out nothing new but does not disturb the running task.
        Queue q;
        const auto running = q.enqueue(
            confirmed(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        const auto queued = q.enqueue(
            confirmed(makePackage(Manager::Winget, "winget", "Git.Git", Scope::User, "2.47.1", "2.51.0"),
                      Action::Upgrade));
        CHECK(q.start(running.id));
        q.pause();
        CHECK(q.paused());
        const auto blocked = q.nextRunnable();
        CHECK(!blocked.found);
        CHECK(!blocked.waitingReason.empty());
        // The running task is still running: pause is not cancel.
        CHECK(q.find(running.id)->state == TaskState::Running);
        q.resume();
        CHECK(!q.paused());
        const auto next = q.nextRunnable();
        CHECK(next.found && next.id == queued.id);
    }
    {
        // Clearing finished work never removes active work.
        Queue q;
        const auto a = q.enqueue(
            confirmed(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        const auto b = q.enqueue(confirmed(
            makePackage(Manager::Scoop, "extras", "vscode", Scope::User, "1.94.2", "1.94.2"), Action::Reinstall));
        CHECK(q.start(a.id));
        q.finish(a.id, true, 0);
        CHECK(q.clearFinished() == 1);
        CHECK(q.find(a.id) == nullptr);
        CHECK(q.find(b.id) != nullptr);              // queued, still here
        CHECK(q.clearFinished() == 0);               // nothing left to clear
    }
    {
        // The log is bounded and keeps the tail, because the tail is the failure.
        Queue q;
        const auto r = q.enqueue(
            confirmed(makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1"), Action::Upgrade));
        for (std::size_t i = 0; i < kMaxTaskLogEntries + 50; ++i)
            q.appendLog(r.id, "install", "info", "line " + std::to_string(i));
        const Task* t = q.find(r.id);
        CHECK(t->log.size() == kMaxTaskLogEntries);
        CHECK(t->log.front().text == "line 50");
        CHECK(t->log.back().text == "line " + std::to_string(kMaxTaskLogEntries + 49));
        // Every entry carries the clock and phase, so a log line is self-describing.
        CHECK(t->log.back().at.size() == 8);
        CHECK(t->log.back().phase == "install");
    }
}

// PKG-T18: logs are redacted and bounded.
void redaction()
{
    section("redaction (T18)");
    RedactRules rules = defaultRules("C:\\Users\\someone");
    CHECK(redactLine("installed C:\\Users\\someone\\scoop\\apps\\git", rules).find("%USERPROFILE%") !=
          std::string::npos);
    CHECK(redactLine("Authorization: Bearer abc", rules).find("redacted") != std::string::npos);
    CHECK(redactLine("password=hunter2", rules).find("hunter2") == std::string::npos);
    CHECK(redactLine(std::string("x") + "\x1b[31m" + "y", rules).find('\x1b') == std::string::npos);
    // A long line is truncated with an explicit marker, never silently cut.
    const std::string longLine(2000, 'a');
    const std::string redacted = redactLine(longLine, rules);
    CHECK(redacted.size() < longLine.size());
    CHECK(redacted.find("truncated") != std::string::npos);
    // Bounded block: 100 input lines, 5 kept.
    std::string block;
    for (int i = 0; i < 100; ++i)
        block += "line " + std::to_string(i) + "\n";
    const std::string out = redactBlock(block, rules, 5);
    CHECK(out.find("line 0") != std::string::npos);
    CHECK(out.find("line 99") == std::string::npos);
    CHECK(out.find("truncated") != std::string::npos);
}

// Grouping for the scheduler.
void grouping()
{
    section("manager grouping (T09)");
    const std::vector<PackageKey> keys{
        {Manager::Scoop, "main", "git", Scope::User},
        {Manager::Winget, "winget", "jqlang.jq", Scope::User},
        {Manager::Scoop, "extras", "vscode", Scope::User},
    };
    const auto groups = groupByManager(keys);
    CHECK(groups.size() == 2);
    CHECK(groups[0].size() == 2 && groups[0][0].id == "git" && groups[0][1].id == "vscode");
    CHECK(groups[1].size() == 1 && groups[1][0].id == "jqlang.jq");
}

// The disk cache: the last scan must survive a process restart without being
// reinterpreted.
void diskCache()
{
    section("disk cache (T20)");

    Snapshot original;
    original.observedAt = "2026-11-12T09:30:00Z";
    original.scanScope = "installed";
    original.winget = BackendState::Ready;
    original.wingetDetail = "winget 1.29.380";
    original.wingetObservedAt = "2026-11-12T09:28:00Z";
    original.scoopObservedAt = "2026-11-12T09:30:00Z";
    original.scoop = BackendState::Denied;
    // A detail string with the characters that break naive JSON writers.
    original.scoopDetail = "quote \" backslash \\ tab \t and 中文";

    Package p = makePackage(Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", Scope::User, "14.1.0", "14.1.2");
    p.name = "ripgrep";
    p.arch = "x64";
    p.holdPolicy = "pinned @14.1.0";
    p.limits = "enumerated from the uninstall registry";
    original.packages.push_back(p);
    original.packages.push_back(
        makePackage(Manager::Scoop, "main", "git", Scope::User, "2.47.0", "2.47.1", Provenance::Observed));

    const std::string text = cache::serialize(original);
    CHECK(text.find("\"cacheVersion\":1") != std::string::npos);

    Snapshot restored;
    std::string error;
    CHECK(cache::parse(text, restored, error));
    CHECK(error.empty());
    CHECK(restored.observedAt == original.observedAt);
    // Each backend's own stamp survives, which is what lets a later refresh
    // re-query only the part that is actually old.
    CHECK(restored.wingetObservedAt == original.wingetObservedAt);
    CHECK(restored.scoopObservedAt == original.scoopObservedAt);
    CHECK(restored.winget == BackendState::Ready);
    CHECK(restored.scoop == BackendState::Denied);
    // The escaping round-trips: a detail with quotes, a backslash and UTF-8 is
    // the case a hand-written serialiser gets wrong.
    CHECK(restored.scoopDetail == original.scoopDetail);
    CHECK(restored.packages.size() == 2);
    CHECK(restored.packages[0].key.id == "BurntSushi.ripgrep.MSVC");
    CHECK(restored.packages[0].key.scope == Scope::User);
    CHECK(restored.packages[0].name == "ripgrep");
    CHECK(restored.packages[0].installedVersion == "14.1.0");
    CHECK(restored.packages[0].availableVersion == "14.1.2");
    CHECK(restored.packages[0].holdPolicy == "pinned @14.1.0");
    CHECK(restored.packages[0].limits == p.limits);
    CHECK(restored.packages[0].canUpgrade == p.canUpgrade);
    CHECK(restored.packages[1].provenance == Provenance::Observed);    CHECK(restored.packages[1].key.source == "main");

    // A cache written before per-backend stamps existed still loads: both stamps
    // fall back to the whole-snapshot time, so freshness behaves as it did then.
    {
        std::string legacy = text;
        for (const std::string key : {std::string("\"wingetAt\":\"2026-11-12T09:28:00Z\","),
                                      std::string("\"scoopAt\":\"2026-11-12T09:30:00Z\",")}) {
            const auto at = legacy.find(key);
            if (at != std::string::npos)
                legacy.erase(at, key.size());
        }
        Snapshot legacyParsed;
        std::string legacyError;
        CHECK(cache::parse(legacy, legacyParsed, legacyError));
        CHECK(legacyParsed.wingetObservedAt == legacyParsed.observedAt);
        CHECK(legacyParsed.scoopObservedAt == legacyParsed.observedAt);
    }

    // The update listing is cached beside the inventory. It is the most expensive
    // query in a scan, and leaving it out meant every restart showed an empty
    // Updates tab that had to be waited for all over again.
    Snapshot updates;
    updates.observedAt = "2026-11-12T09:30:05Z";
    updates.scanScope = "updates";
    updates.winget = BackendState::Ready;
    updates.scoop = BackendState::Ready;
    updates.packages.push_back(
        makePackage(Manager::Winget, "winget", "Microsoft.PowerToys", Scope::Unknown, "0.86.0", "0.87.1"));

    const std::string withUpdates = cache::serialize(original, &updates);
    Snapshot restoredUpdates;
    CHECK(cache::parse(withUpdates, restored, error, &restoredUpdates));
    CHECK(restoredUpdates.packages.size() == 1);
    CHECK(restoredUpdates.packages[0].key.id == "Microsoft.PowerToys");
    CHECK(restoredUpdates.packages[0].availableVersion == "0.87.1");
    // Its own timestamp: the update listing is a different query from the
    // inventory, so the two must not claim to have been observed together.
    CHECK(restoredUpdates.observedAt == updates.observedAt);
    CHECK(restoredUpdates.packages[0].key.scope == Scope::Unknown);

    // A cache written before updates were stored has no array at all. That is not
    // an error: the inventory still loads and the Updates tab starts empty.
    Snapshot legacyUpdates;
    CHECK(cache::parse(text, restored, error, &legacyUpdates));
    CHECK(legacyUpdates.packages.empty());
    CHECK(restored.packages.size() == 2);

    // A file this build cannot understand is refused, never half-read.
    std::string why;
    Snapshot ignored;
    CHECK(!cache::parse("not json at all", ignored, why));
    CHECK(!why.empty());
    why.clear();
    CHECK(!cache::parse("{\"cacheVersion\":99,\"packages\":[]}", ignored, why));
    CHECK(why.find("different version") != std::string::npos);
    why.clear();
    CHECK(!cache::parse("{\"cacheVersion\":1}", ignored, why));   // no packages array

    // An empty or failed scan must never replace a good cache.
    Snapshot empty;
    CHECK(!cache::worthWriting(empty));
    Snapshot allFailed;
    allFailed.observedAt = "2026-11-12T09:30:00Z";
    allFailed.winget = BackendState::Denied;
    allFailed.scoop = BackendState::Denied;
    CHECK(!cache::worthWriting(allFailed));
    CHECK(cache::worthWriting(original));
}

// The `winget show` metadata cache: keyed by exact id (compared
// case-insensitively — list and show recase freely), LRU-capped, and the same
// atomic-file contract as the inventory cache.
void infoCacheRules()
{
    section("info cache (T20)");
    info_cache::Store store;
    info_cache::Entry a;
    a.id = "Docker.DockerDesktop";
    a.observedAt = "2026-11-14T06:00:00Z";
    a.fields["homepage"] = "https://docker.com";
    a.fields["publisher"] = "Docker Inc.";
    info_cache::upsert(store, std::move(a));

    // Case-insensitive lookup: list and show recase ids freely.
    info_cache::Entry hit;
    CHECK(info_cache::find(store, "docker.dockerdesktop", hit));
    CHECK(hit.fields.at("homepage") == "https://docker.com");
    CHECK(!info_cache::find(store, "Other.Package", hit));
    CHECK(!info_cache::find(store, "", hit));

    // An upsert refreshes in place (no duplicate) and moves to the back.
    info_cache::Entry b;
    b.id = "7zip.7zip";
    b.observedAt = "2026-11-14T06:30:00Z";
    info_cache::upsert(store, std::move(b));
    info_cache::Entry refreshed = hit;
    refreshed.observedAt = "2026-11-14T07:00:00Z";
    info_cache::upsert(store, std::move(refreshed));
    CHECK(store.entries.size() == 2);
    CHECK(store.entries.back().id == "Docker.DockerDesktop");
    CHECK(store.entries.front().id == "7zip.7zip");

    // The LRU cap evicts the oldest first: after the fill the two originals
    // and the earliest fills are gone, the newest fills remain.
    for (int i = 0; i < 205; ++i) {
        info_cache::Entry fill;
        fill.id = "Fill.Package." + std::to_string(i);
        fill.observedAt = "2026-11-14T08:00:00Z";
        fill.fields["homepage"] = "fill://" + fill.id;
        info_cache::upsert(store, std::move(fill), 200);
    }
    CHECK(store.entries.size() == 200);
    CHECK(!info_cache::find(store, "7zip.7zip", hit));              // oldest, evicted
    CHECK(!info_cache::find(store, "Docker.DockerDesktop", hit));   // second oldest, evicted too
    CHECK(!info_cache::find(store, "Fill.Package.0", hit));
    CHECK(info_cache::find(store, "Fill.Package.204", hit));

    // The fetch is pinned to the version it was made for: the metadata of a
    // package installed at 1.2.3 does not change just because it is a day old,
    // so an entry for that same version stays usable while the version matches.
    info_cache::Entry versioned;
    versioned.id = "Version.Pinned";
    versioned.observedAt = "2026-11-14T08:00:00Z";
    versioned.version = "1.2.3";
    versioned.fields["version"] = "1.2.3";
    info_cache::upsert(store, std::move(versioned));
    CHECK(info_cache::find(store, "version.pinned", hit));
    CHECK(info_cache::stillMatches(hit, "1.2.3"));
    CHECK(!info_cache::stillMatches(hit, "1.2.4"));   // the package changed
    CHECK(!info_cache::stillMatches(hit, ""));        // not an installed row
    info_cache::Entry unversioned;
    unversioned.id = "Old.Entry";
    unversioned.observedAt = "2026-11-14T08:00:00Z";
    CHECK(!info_cache::stillMatches(unversioned, "1.2.3"));   // entry from an older build

    // Round trip; failures are the caller's problem and are never stored.
    const std::string text = info_cache::serialize(store);
    info_cache::Store restored;
    std::string error;
    CHECK(info_cache::parse(text, restored, error));
    CHECK(restored.entries.size() == store.entries.size());
    info_cache::Entry roundtrip;
    if (info_cache::find(restored, "Fill.Package.204", roundtrip)) {
        CHECK(roundtrip.observedAt == "2026-11-14T08:00:00Z");
        CHECK(roundtrip.fields.at("homepage") == "fill://Fill.Package.204");
    } else {
        CHECK(false);   // the newest entry must survive the round trip
    }
    if (info_cache::find(restored, "Version.Pinned", roundtrip))
        CHECK(roundtrip.version == "1.2.3");
    else
        CHECK(false);

    // A file this build does not understand is refused, never half-read.
    CHECK(!info_cache::parse("not json", restored, error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!info_cache::parse("{\"infoVersion\":99,\"entries\":[]}", restored, error));
    CHECK(error.find("different version") != std::string::npos);
    error.clear();
    CHECK(!info_cache::parse("{\"infoVersion\":1}", restored, error));
    // An entry without an id can never be looked up, so it is skipped.
    CHECK(info_cache::parse(
        "{\"infoVersion\":1,\"entries\":[{\"observedAt\":\"x\",\"fields\":{}},"
        "{\"id\":\"Ok.Package\",\"observedAt\":\"y\",\"fields\":{\"k\":\"v\"}}]}",
        restored, error));
    CHECK(restored.entries.size() == 1);
    CHECK(restored.entries[0].id == "Ok.Package");
}

// Discover search cache: the query key must be stable and the entry must
// round-trip through the inventory format, because that is what a repeat
// search reads instead of spawning two backends.
void searchCacheRules()
{
    section("search cache");

    // Case and whitespace must not create two entries: both backends match
    // case-insensitively, so "Vim" and "  vim  " are the same query.
    CHECK(search_cache::normalizeQuery("Vim") == "vim");
    CHECK(search_cache::normalizeQuery("  vim  ") == "vim");
    CHECK(search_cache::normalizeQuery("Visual   Studio\tCode") == "visual studio code");
    CHECK(search_cache::normalizeQuery("") == "");
    CHECK(search_cache::normalizeQuery("   ") == "");

    // The file name is stable across runs and safe for the filesystem.
    const std::string a = search_cache::fileStem("Vim");
    const std::string b = search_cache::fileStem("  vim ");
    CHECK(a == b);
    CHECK(a.size() == 16);
    for (const char c : a)
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    CHECK(search_cache::fileStem("vim") != search_cache::fileStem("emacs"));
    // A query with path characters cannot escape the cache folder.
    CHECK(search_cache::fileStem("..\\..\\evil").find('\\') == std::string::npos);

    CHECK(search_cache::pathFor("C:\\data", "Vim") ==
          "C:\\data\\search\\" + search_cache::fileStem("Vim") + ".json");

    // Round trip through the inventory format used for search snapshots.
    Snapshot snapshot;
    snapshot.observedAt = "2026-11-15T06:30:00Z";
    snapshot.scanScope = "search";
    snapshot.winget = BackendState::Ready;
    snapshot.scoop = BackendState::Ready;
    Package p;
    p.key.manager = Manager::Winget;
    p.key.source = "winget";
    p.key.id = "jqlang.jq";
    p.key.scope = Scope::Unknown;
    p.name = "jq";
    p.availableVersion = "1.7.1";
    p.availableKind = "version";
    p.canInstall = true;
    snapshot.packages.push_back(p);

    CHECK(search_cache::worthWriting(snapshot));
    const std::string text = search_cache::serialize(snapshot);
    Snapshot restored;
    std::string error;
    CHECK(search_cache::parse(text, restored, error));
    CHECK(restored.winget == BackendState::Ready);
    CHECK(restored.observedAt == snapshot.observedAt);
    CHECK(restored.packages.size() == 1);
    if (!restored.packages.empty()) {
        CHECK(restored.packages[0].key.id == "jqlang.jq");
        CHECK(restored.packages[0].canInstall);
    }

    // A failed scan must never overwrite a good entry, and an entry without a
    // timestamp cannot be judged fresh.
    Snapshot failed;
    failed.observedAt = "2026-11-15T06:30:00Z";
    failed.winget = BackendState::Denied;
    failed.scoop = BackendState::Unsupported;
    CHECK(!search_cache::worthWriting(failed));
    Snapshot untimed;
    untimed.winget = BackendState::Ready;
    CHECK(!search_cache::worthWriting(untimed));

    // An empty result from a backend that ran IS an answer worth caching.
    Snapshot empty;
    empty.observedAt = "2026-11-15T06:30:00Z";
    empty.winget = BackendState::Ready;
    CHECK(search_cache::worthWriting(empty));

    Snapshot junk;
    CHECK(!search_cache::parse("not json", junk, error));
}

// A remembered probe verdict is only reused for the very file that produced it,
// and only when it says something a query can build on.
void probeCacheRules()
{
    section("probe cache");

    probe_cache::Record ready;
    ready.state = BackendState::Ready;
    ready.version = "v1.29.380";
    ready.program = "C:\\Users\\x\\AppData\\Local\\Microsoft\\WindowsApps\\winget.exe";
    ready.stamp = "1234:638000000000000000";
    ready.detail = "";
    ready.observedAt = "2026-11-15T07:00:00Z";

    // Same file: reusable. Different size/write-time: must probe again.
    CHECK(probe_cache::reusable(ready, ready.stamp));
    CHECK(!probe_cache::reusable(ready, "1234:638000000000000001"));
    CHECK(!probe_cache::reusable(ready, ""));          // the file vanished
    CHECK(!probe_cache::reusable(ready, ready.stamp) == false);

    // A refusal is never reused: it is the case most likely to be transient.
    probe_cache::Record denied = ready;
    denied.state = BackendState::Denied;
    CHECK(!probe_cache::reusable(denied, denied.stamp));
    probe_cache::Record missing = ready;
    missing.state = BackendState::Missing;
    CHECK(!probe_cache::reusable(missing, missing.stamp));

    // Unsupported is reused: running the probe again would give the same answer
    // for the same binary, and the UI already explains it.
    probe_cache::Record unsupported = ready;
    unsupported.state = BackendState::Unsupported;
    CHECK(probe_cache::reusable(unsupported, unsupported.stamp));

    // Round trip, including text that has to be escaped.
    probe_cache::Store store;
    store.winget = ready;
    store.hasWinget = true;
    store.scoop.state = BackendState::Ready;
    store.scoop.version = "unknown";
    store.scoop.program = "C:\\Users\\x\\scoop\\shims\\scoop.ps1";
    store.scoop.stamp = "99:638000000000000099";
    store.scoop.detail = "say \"hi\"\nsecond line";
    store.hasScoop = true;

    const std::string text = probe_cache::serialize(store);
    probe_cache::Store restored;
    std::string error;
    CHECK(probe_cache::parse(text, restored, error));
    CHECK(restored.hasWinget && restored.hasScoop);
    CHECK(restored.winget.state == BackendState::Ready);
    CHECK(restored.winget.version == "v1.29.380");
    CHECK(restored.winget.stamp == ready.stamp);
    CHECK(restored.scoop.detail == store.scoop.detail);

    // Files this build does not understand are refused, never half-read.
    CHECK(!probe_cache::parse("not json", restored, error));
    error.clear();
    CHECK(!probe_cache::parse("{\"probeVersion\":2,\"winget\":{}}", restored, error));
    // A record without a stamp cannot be matched to a file, so it is ignored.
    error.clear();
    CHECK(!probe_cache::parse(
        "{\"probeVersion\":1,\"winget\":{\"state\":\"ready\",\"program\":\"x\"}}", restored, error));
}

// The pin list is cached like the inventory: policy data whose loss would let a
// pinned package be offered for upgrade.
void pinsCacheRules()
{
    section("pin cache");

    pins_cache::Store store;
    store.observedAt = "2026-11-15T07:10:00Z";
    store.entries = {{"Microsoft.PowerToys", "pinned"}, {"7zip.7zip", "blocking"}};

    const std::string text = pins_cache::serialize(store);
    pins_cache::Store restored;
    std::string error;
    CHECK(pins_cache::parse(text, restored, error));
    CHECK(restored.observedAt == store.observedAt);
    CHECK(restored.entries.size() == 2);
    CHECK(restored.entries[0].first == "Microsoft.PowerToys");
    CHECK(restored.entries[0].second == "pinned");

    // An empty list is a real answer ("nothing is pinned") and must round-trip.
    pins_cache::Store empty;
    empty.observedAt = "2026-11-15T07:10:00Z";
    pins_cache::Store emptyRestored;
    CHECK(pins_cache::parse(pins_cache::serialize(empty), emptyRestored, error));
    CHECK(emptyRestored.entries.empty());
    CHECK(emptyRestored.observedAt == empty.observedAt);

    // Unknown shapes are refused rather than read as "nothing is pinned", which
    // would silently allow upgrading a held package.
    CHECK(!pins_cache::parse("not json", restored, error));
    error.clear();
    CHECK(!pins_cache::parse("{\"pinsVersion\":2,\"entries\":[]}", restored, error));
    error.clear();
    CHECK(!pins_cache::parse("{\"pinsVersion\":1}", restored, error));
    error.clear();
    CHECK(pins_cache::parse(
        "{\"pinsVersion\":1,\"observedAt\":\"t\",\"entries\":[{\"policy\":\"pinned\"},"
        "{\"id\":\"Ok.Package\",\"policy\":\"pinned\"}]}", restored, error));
    CHECK(restored.entries.size() == 1);   // the entry without an id is skipped
}

} // namespace

int main()
{
    std::printf("ra9fael.packages L1 core tests\n");
    identityRules();
    argumentSafety();
    versionRules();
    provenanceRules();
    planFactsRules();
    reinstallRules();
    policyRules();
    queueOrdering();
    queueOutcomes();
    queueIntervention();
    redaction();
    diskCache();
    infoCacheRules();
    searchCacheRules();
    probeCacheRules();
    pinsCacheRules();
    grouping();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
