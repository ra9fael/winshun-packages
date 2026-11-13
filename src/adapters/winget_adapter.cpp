// winget_adapter.cpp — see winget_adapter.h.
#include "winget_adapter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

#include "winget_table.h"
#include "../core/version.h"
#include "../platform/process.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pkg {
namespace {

std::string nowText()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

// Runs winget. `withInteractivity` appends --disable-interactivity for the data
// commands, so a source prompt can never block a query. Pure probes (--version,
// --help) take no flags: some winget builds reject the global switch outside a
// verb, and a failed probe would deny the whole backend unnecessarily.
platform::RunOptions baseOptions(const std::string& executable, std::vector<std::string> args,
                                 std::uint32_t timeoutMs, bool withInteractivity = true)
{
    platform::RunOptions options;
    options.application = executable;
    options.arguments = std::move(args);
    options.timeoutMs = timeoutMs;
    if (withInteractivity)
        options.arguments.push_back("--disable-interactivity");
    return options;
}

} // namespace

#ifdef _WIN32

WingetLocation locateWinget()
{
    WingetLocation location;

    // Prefer the real WindowsApps package path. The bare `winget.exe` on PATH is
    // an app-execution alias (a reparse point under %LOCALAPPDATA%\Microsoft\WindowsApps); resolving through it is fine, but the location is worth
    // recording because policy/tampering concerns live there.
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const std::wstring aliasRoot = std::wstring(localAppData) + L"\\Microsoft\\WindowsApps\\winget.exe";
        if (::GetFileAttributesW(aliasRoot.c_str()) != INVALID_FILE_ATTRIBUTES) {
            location.found = true;
            location.viaAppExecutionAlias = true;
            // The alias is a link; the target is under the DesktopAppInstaller
            // package directory. Resolving the final path gives the real exe.
            HANDLE file = ::CreateFileW(aliasRoot.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                wchar_t target[MAX_PATH * 2] = {};
                const DWORD got = ::GetFinalPathNameByHandleW(file, target, MAX_PATH * 2, FILE_NAME_NORMALIZED);
                ::CloseHandle(file);
                if (got > 0 && got < MAX_PATH * 2) {
                    std::wstring resolved(target, got);
                    const std::wstring prefix = L"\\\\?\\";
                    if (resolved.rfind(prefix, 0) == 0)
                        resolved.erase(0, prefix.size());
                    location.path = platform::toUtf8(resolved);
                }
            }
            if (location.path.empty())
                location.path = platform::toUtf8(aliasRoot);
            return location;
        }
    }

    // Fall back to a PATH search restricted to executable files.
    wchar_t found[MAX_PATH] = {};
    const DWORD n = ::SearchPathW(nullptr, L"winget.exe", nullptr, MAX_PATH, found, nullptr);
    if (n > 0 && n < MAX_PATH) {
        location.found = true;
        location.path = platform::toUtf8(std::wstring(found, n));
        return location;
    }

    location.detail = "winget.exe was not found (no WindowsApps alias and nothing named winget.exe on PATH)";
    return location;
}

#else

WingetLocation locateWinget()
{
    WingetLocation location;
    location.detail = "WinGet is Windows-only";
    return location;
}

#endif

WingetAdapter::WingetAdapter(std::string executablePath) : m_executable(std::move(executablePath))
{
    if (m_executable.empty()) {
        const auto location = locateWinget();
        m_executable = location.found ? location.path : std::string("winget.exe");
    }
}

const ProbeResult& WingetAdapter::probeCached()
{
    // The backend's location and version cannot change for the life of the
    // process: probing again would spawn two more winget processes and seconds
    // of waiting for the same answer. A Denied result is NOT cached — a
    // transient failure (a lock, a busy installer service) should be retried on
    // the next refresh, so only Ready/Unsupported stick.
    if (m_probed)
        return m_probeCache;
    m_probeCache = probe();
    if (m_probeCache.state == BackendState::Ready || m_probeCache.state == BackendState::Unsupported)
        m_probed = true;
    return m_probeCache;
}

ProbeResult WingetAdapter::probe()
{
    ProbeResult result;
    const auto location = locateWinget();
    if (!location.found) {
        result.state = BackendState::Missing;
        result.detail = location.detail;
        return result;
    }
    result.programPath = location.path;

    // A child process may be blocked entirely when the host did not grant
    // `processes`; that is a Denied backend, not a missing one.
    std::uint32_t childError = 0;
    if (!platform::childProcessesAllowed(childError)) {
        result.state = BackendState::Denied;
        result.detail = childError == 1816
            ? "the host did not grant `processes`: child processes are blocked (error 1816)"
            : "could not create a child process (error " + std::to_string(childError) + ")";
        return result;
    }

    // Pure probes take no flags: some winget builds reject --disable-interactivity
    // outside a verb, and the probe failing would report the whole backend as
    // denied even though the data commands would have worked.
    const auto version = platform::run(baseOptions(m_executable, {"--version"}, 15000, false));
    if (!version.launched) {
        result.state = BackendState::Denied;
        result.detail = "winget --version could not be launched: " + version.errorText;
        return result;
    }
    std::string versionText = version.output;
    while (!versionText.empty() && (versionText.back() == '\n' || versionText.back() == '\r' || versionText.back() == ' '))
        versionText.pop_back();
    while (!versionText.empty() && (versionText.front() == '\n' || versionText.front() == '\r' || versionText.front() == ' '))
        versionText.erase(0, 1);
    if (version.exitCode != 0 || versionText.empty()) {
        result.state = BackendState::Denied;
        result.detail = "winget --version failed (exit " + std::to_string(version.exitCode) + ")";
        return result;
    }
    result.version = versionText;

    result.capability.manager = Manager::Winget;
    result.capability.backendVersion = result.version;
    result.capability.os = "Windows";
    result.capability.evidence = Evidence::None;   // nothing is proven by probing
    result.capability.note = "probing proves presence only; each command below needs its own fixture";

    // Confirm the read-only commands this adapter relies on are actually known to
    // this build. `--output` is deliberately not attempted: 1.29.380 rejects it.
    const auto help = platform::run(baseOptions(m_executable, {"--help"}, 15000, false));
    if (help.launched && help.output.find("list") != std::string::npos &&
        help.output.find("upgrade") != std::string::npos && help.output.find("pin") != std::string::npos) {
        result.state = BackendState::Ready;
        result.capability.evidence = Evidence::L3;   // verified against the real tool on this machine
        result.capability.note = "list/upgrade/pin verbs confirmed present; --output is not supported";
    } else {
        result.state = BackendState::Unsupported;
        result.detail = "winget --help did not list the verbs this adapter needs";
        return result;
    }
    return result;
}

void WingetAdapter::invalidateCaches()
{
    std::lock_guard<std::mutex> lock(m_pinMutex);
    m_pinsFetched = false;
    m_pinsCache.clear();
    // The probe result is kept: a missing winget will not appear mid-session, and
    // re-probing costs two processes.
}

std::vector<std::pair<std::string, std::string>> WingetAdapter::pinsCached()
{
    std::lock_guard<std::mutex> lock(m_pinMutex);
    if (m_pinsFetched)
        return m_pinsCache;
    // recorded even when the call fails: a read-only scan must not retry a
    // failing pin list once per query.
    m_pinsFetched = true;
    std::string pinsFailure;
    const auto pinRun = platform::run(baseOptions(m_executable, {"pin", "list"}, 30000));
    if (pinRun.launched)
        parsePinList(pinRun.output, m_pinsCache, &pinsFailure);
    return m_pinsCache;
}

Snapshot WingetAdapter::readInstalled(const ProbeResult& probe, bool includeObserved)
{
    Snapshot snapshot;
    snapshot.winget = probe.state;
    snapshot.wingetDetail = probe.detail;
    if (probe.state != BackendState::Ready) {
        snapshot.stale = true;
        snapshot.staleReason = probe.detail;
        return snapshot;
    }

    const auto pins = pinsCached();

    const auto run = platform::run(baseOptions(m_executable, {"list"}, 120000));
    if (!run.launched) {
        snapshot.stale = true;
        snapshot.staleReason = "winget list could not be launched: " + run.errorText;
        return snapshot;
    }
    std::string observedAt = nowText();
    std::string failure;
    snapshot = fromListText(run.output, observedAt, includeObserved, {}, &failure);
    snapshot.winget = probe.state;
    snapshot.wingetDetail = probe.detail;
    if (!failure.empty()) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = failure;
    }
    // Apply pins read-only: a pinned id reports its policy and can never be
    // upgraded by a batch.
    for (auto& p : snapshot.packages) {
        for (const auto& [id, policy] : pins) {
            if (!id.empty() && id == p.key.id)
                p.holdPolicy = policy;
        }
    }
    return snapshot;
}

Snapshot WingetAdapter::search(const ProbeResult& probe, const std::string& text, const bool* cancelRequested)
{
    Snapshot snapshot;
    snapshot.winget = probe.state;
    if (probe.state != BackendState::Ready) {
        snapshot.wingetDetail = probe.detail;
        return snapshot;
    }
    // An empty query is not a search: WinGet would enumerate a source and the
    // user asked for nothing yet.
    if (text.empty())
        return snapshot;

    auto options = baseOptions(m_executable, {"search", "--query", text}, 120000);
    options.cancelRequested = cancelRequested;
    const auto run = platform::run(options);
    if (!run.launched) {
        snapshot.winget = BackendState::Denied;
        snapshot.wingetDetail = run.errorText;
        return snapshot;
    }
    std::string failure;
    snapshot = fromSearchText(run.output, nowText(), text, &failure);
    if (!failure.empty()) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = failure;
    }
    return snapshot;
}

Snapshot WingetAdapter::readUpdates(const ProbeResult& probe)
{
    Snapshot snapshot;
    snapshot.winget = probe.state;
    if (probe.state != BackendState::Ready) {
        snapshot.wingetDetail = probe.detail;
        return snapshot;
    }

    const auto pins = pinsCached();

    const auto run = platform::run(baseOptions(m_executable, {"list", "--upgrade-available"}, 120000));
    if (!run.launched) {
        snapshot.winget = BackendState::Denied;
        snapshot.wingetDetail = run.errorText;
        return snapshot;
    }
    std::string failure;
    snapshot = fromUpgradeText(run.output, nowText(), &failure);
    if (!failure.empty()) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = failure;
    }
    for (auto& p : snapshot.packages)
        for (const auto& [id, policy] : pins)
            if (!id.empty() && id == p.key.id)
                p.holdPolicy = policy;
    return snapshot;
}

CommandSpec WingetAdapter::buildMutation(const Plan& plan, const std::vector<std::string>& agreementsAccepted)
{
    CommandSpec spec;

    // The identity must already be exact; the adapter never widens it. Only an
    // install passes `--scope`, so only an install requires a known scope:
    // `upgrade`/`uninstall` match by exact id, and `winget list` prints no scope
    // column at all, so demanding one here made every real mutation impossible.
    const bool scopeMandatory = plan.action == Action::Install;
    if (!isResolvable(plan.key, scopeMandatory)) {
        spec.refusal = scopeMandatory
            ? "identity is not exactly resolvable (manager, source, id and scope must all be known)"
            : "identity is not exactly resolvable (manager, source and id must all be known)";
        return spec;
    }
    if (plan.action == Action::Reinstall) {
        // There is no `winget reinstall`, and `repair` is a different operation,
        // so a reinstall here is only the explicit two-phase plan.
        spec.refusal = "WinGet has no reinstall verb; a two-phase uninstall/install plan must be run as two tasks";
        return spec;
    }

    std::vector<std::string> args;
    switch (plan.action) {
    case Action::Install:
        args = {"install", "--id", plan.key.id, "--exact", "--source", plan.key.source};
        break;
    case Action::Uninstall:
        args = {"uninstall", "--id", plan.key.id, "--exact"};
        // A source is not accepted by uninstall in every version; only pass it
        // when the plan actually carries one and the id came from that source.
        break;
    case Action::Upgrade:
        args = {"upgrade", "--id", plan.key.id, "--exact", "--source", plan.key.source};
        break;
    case Action::None:
    case Action::Reinstall:
        spec.refusal = "no WinGet command for this action";
        return spec;
    }

    if (plan.key.scope != Scope::Unknown) {
        args.push_back("--scope");
        args.push_back(scopeId(plan.key.scope));
    }

    // Agreements are opt-in, per agreement, and only when the user checked them.
    for (const auto& agreement : agreementsAccepted) {
        if (agreement == "source")
            args.push_back("--accept-source-agreements");
        else if (agreement == "installer")
            args.push_back("--accept-package-agreements");
    }

    spec.ok = true;
    // A mutation is the one case where interactivity must be allowed through.
    // `winget upgrade` on a package whose installer cannot upgrade in place
    // (Pandoc is one) hands over to the old version's own uninstaller UI, and with
    // --disable-interactivity plus CREATE_NO_WINDOW that UI cannot appear: the
    // uninstaller either failed or hung, and the upgrade was then torn down
    // mid-install. The window is the backend's own, so nothing is being delegated
    // to this plugin; it only has to stop suppressing it.
    spec.options = baseOptions(m_executable, args, 3600000, /*withInteractivity=*/false);
    spec.options.allowWindow = true;
    spec.options.interactiveAllowed = true;
    spec.redactedPreview = "winget";
    for (const auto& a : spec.options.arguments)
        spec.redactedPreview += " " + a;
    return spec;
}

bool WingetAdapter::verifyInstalled(const ProbeResult& probe, const PackageKey& key, std::string& installedVersion,
                                    std::string& detail)
{
    installedVersion.clear();
    if (probe.state != BackendState::Ready) {
        detail = probe.detail;
        return false;
    }
    const auto run = platform::run(baseOptions(m_executable, {"list", "--id", key.id, "--exact"}, 120000));
    if (!run.launched) {
        detail = "verification could not run: " + run.errorText;
        return false;
    }
    std::string failure;
    const auto snapshot = fromListText(run.output, nowText(), true, {}, &failure);
    if (!failure.empty()) {
        detail = "verification output was unsupported: " + failure;
        return false;
    }
    // `winget list --id X --exact` is case-insensitive and can report the id with
    // different casing than the plan stored, so compare case-insensitively. An
    // exact string compare here reported "not found after the run" for a package
    // the output had just listed.
    const auto sameId = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    };
    for (const auto& p : snapshot.packages) {
        if (!sameId(p.key.id, key.id))
            continue;
        installedVersion = p.installedVersion;
        detail = "the package is installed at " + installedVersion;
        return true;
    }
    detail = "the package was not found after the run";
    return false;
}

bool WingetAdapter::verifyReachedTarget(const PackageKey& key, const std::string& targetVersion,
                                        std::string& installedVersion, std::string& detail)
{
    installedVersion.clear();
    std::string version, why;
    if (!verifyInstalled(probeCached(), key, version, why)) {
        detail = why;
        return false;
    }
    installedVersion = version;
    return versionReached(version, targetVersion, detail);
}

bool WingetAdapter::versionReached(const std::string& installed, const std::string& target,
                                   std::string& detail)
{
    if (target.empty()) {
        detail = "installed at " + installed + "; no target version was recorded to compare against";
        return true;
    }
    // Exit code 0 says the installer ran, not that the machine now has the
    // version the plan promised. Comparing the reported version is the only
    // evidence that separates "upgraded" from "the backend reported success but
    // did nothing" — the exact case a DBX upgrade hit.
    const Order order = compareVersionStrings(installed, target);
    if (order == Order::Same) {
        detail = "installed at " + installed + ", which matches the target " + target;
        return true;
    }
    if (order == Order::Older) {
        detail = "still at " + installed + " after the run; the target was " + target;
        return false;
    }
    if (order == Order::Unknown) {
        // Two unorderable strings: report both rather than claim success. A version
        // that cannot be compared is not evidence of reaching the target.
        detail = "installed version reads \"" + installed + "\"; the target \"" + target +
                 "\" could not be compared, so success is not claimed";
        return false;
    }
    detail = "installed version " + installed + " is newer than the target " + target +
             "; treated as reached";
    return true;
}

// ---------------------------------------------------------------- pure helpers

Provenance WingetAdapter::provenanceFor(const std::string& id, bool installedMatchFound)
{
    if (winget::isArpId(id))
        return Provenance::Observed;  // registry entry WinGet enumerated only
    return installedMatchFound ? Provenance::BackendRecorded : Provenance::Observed;
}

Snapshot WingetAdapter::fromListText(const std::string& text, const std::string& observedAt,
                                     bool includeObserved, const std::string&, std::string* failure)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "winget list (local inventory)";

    const auto table = winget::parseTable(text);
    if (!table.failure.ok) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = "winget list output is unsupported here: " + table.failure.reason;
        if (failure)
            *failure = snapshot.wingetDetail;
        return snapshot;
    }
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        const std::string id = table.value(r, "Id");
        const std::string name = table.value(r, "Name");
        const std::string version = table.value(r, "Version");
        const std::string available = table.value(r, "Available");
        // A row whose id still contains whitespace means the geometry did not
        // hold for this output: refuse the whole result instead of guessing.
        if (!id.empty() && !winget::isWingetPackageId(id) && !winget::isArpId(id)) {
            snapshot.packages.clear();
            snapshot.winget = BackendState::Unsupported;
            snapshot.wingetDetail = "winget list output is unsupported here: a parsed id contains whitespace";
            if (failure)
                *failure = snapshot.wingetDetail;
            return snapshot;
        }
        if (id.empty())
            continue;

        Package p;
        p.key.manager = Manager::Winget;
        // `list` does not print the source; the id itself came from the default
        // source set. Record it as the configured source name only when the id
        // is a managed id, otherwise leave it explicit-unknown.
        p.key.source = winget::isArpId(id) ? std::string() : std::string("winget");
        p.key.id = id;
        // `list` prints no scope either. Scope stays Unknown on purpose: an
        // unknown scope must not be turned into a --scope argument later, and the
        // plugin never retries other scopes automatically.
        p.key.scope = Scope::Unknown;
        p.name = name.empty() ? id : name;
        p.installedVersion = version;
        p.availableVersion = available;
        p.availableKind = available.empty() ? "unknown" : "version";
        p.arch = "unknown";
        p.provenance = provenanceFor(id, true);
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = "not inspected";
        p.canInstall = false;
        // Uninstall needs no scope: `winget uninstall --id X --exact` resolves the
        // instance by its exact id, which is why an unknown scope must not
        // disable it. It is only refused for an ARP id WinGet does not manage.
        p.canUninstall = !winget::isArpId(id);
        // Upgrade is offered when a newer version is evidenced, so an installed
        // row can be acted on even before the Updates tab is opened.
        p.canUpgrade = !available.empty() &&
                       compareVersionStrings(available, version) == Order::Newer;
        if (winget::isArpId(id)) {
            p.limits = "enumerated from the uninstall registry; WinGet does not manage it, so no WinGet action "
                       "is offered";
            p.canUpgrade = false;
        } else if (p.key.scope == Scope::Unknown) {
            // Informational only: `winget list` prints no scope column, and
            // upgrade/uninstall match by exact id, so this never blocks them.
            p.limits = "scope is not reported by `winget list`; only install needs one, so upgrade and uninstall "
                       "use the exact id";
        }
        if (!includeObserved && p.provenance == Provenance::Observed)
            continue;
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.winget = BackendState::Ready;
    return snapshot;
}

Snapshot WingetAdapter::fromUpgradeText(const std::string& text, const std::string& observedAt,
                                        std::string* failure)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "winget list --upgrade-available";
    const auto table = winget::parseTable(text);
    if (!table.failure.ok) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = "winget upgrade-available output is unsupported here: " + table.failure.reason;
        if (failure)
            *failure = snapshot.wingetDetail;
        return snapshot;
    }
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        const std::string id = table.value(r, "Id");
        if (id.empty() || !winget::isWingetPackageId(id))
            continue;
        Package p;
        p.key.manager = Manager::Winget;
        p.key.source = "winget";
        p.key.id = id;
        p.key.scope = Scope::Unknown;
        p.name = table.value(r, "Name");
        p.installedVersion = table.value(r, "Version");
        p.availableVersion = table.value(r, "Available");
        // WinGet prints "< 2026.3.1" or "< nightly" for a target it cannot
        // order. That is evidence of an upgrade but not a provable version, so
        // the row carries the text and cannot be upgraded in bulk.
        const bool orderable =
            !p.availableVersion.empty() &&
            compareVersionStrings(p.availableVersion, p.installedVersion) != Order::Unknown;
        p.availableKind = orderable ? "version" : "unknown";
        p.provenance = Provenance::BackendRecorded;
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = "not inspected";
        p.canUpgrade = orderable;
        // Uninstall needs no scope (exact id resolves it), so an update row is
        // still removable even when the scope is unknown.
        p.canUninstall = orderable || !p.installedVersion.empty();
        p.limits = orderable
            ? "scope is not reported by WinGet; only install needs one, so upgrade and uninstall use the exact id"
            : "the target version cannot be ordered (\"" + p.availableVersion +
                  "\"); upgrade needs manual review";
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.winget = BackendState::Ready;
    return snapshot;
}

Snapshot WingetAdapter::fromSearchText(const std::string& text, const std::string& observedAt,
                                       const std::string& query, std::string* failure)
{
    Snapshot snapshot;
    snapshot.observedAt = observedAt;
    snapshot.scanScope = "winget search " + query;
    const auto table = winget::parseTable(text);
    if (!table.failure.ok) {
        snapshot.winget = BackendState::Unsupported;
        snapshot.wingetDetail = "winget search output is unsupported here: " + table.failure.reason;
        if (failure)
            *failure = snapshot.wingetDetail;
        return snapshot;
    }
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        const std::string id = table.value(r, "Id");
        if (id.empty() || !winget::isWingetPackageId(id))
            continue;
        Package p;
        p.key.manager = Manager::Winget;
        p.key.source = table.hasColumn("Source") ? table.value(r, "Source") : std::string("winget");
        if (p.key.source.empty())
            p.key.source = "winget";
        p.key.id = id;
        p.key.scope = Scope::Unknown;
        p.name = table.value(r, "Name");
        p.availableVersion = table.value(r, "Version");
        p.availableKind = p.availableVersion.empty() ? "unknown" : "version";
        p.provenance = Provenance::None;  // a search hit says nothing about being installed
        p.metadataAt = observedAt;
        p.dependencies = "unknown";
        p.material = "not inspected";
        p.canInstall = false;             // scope still unknown
        p.limits = "scope is not reported by WinGet; install stays disabled until it is resolved";
        snapshot.packages.push_back(std::move(p));
    }
    snapshot.winget = BackendState::Ready;
    return snapshot;
}

bool WingetAdapter::parsePinList(const std::string& text, std::vector<std::pair<std::string, std::string>>& pins,
                                 std::string* failure)
{
    // Verified fixture: "There are no pins configured." plus a trailing newline.
    // Only that exact sentence means "no pins": treating unparsable output as
    // "no pins" would let an upgrade ignore a real pin.
    if (text.find("There are no pins configured") != std::string::npos)
        return true;

    const auto table =
        winget::parseTableWithColumns(text, {"Name", "Id", "Version", "Source", "Pinned Version", "Blocking"});
    if (!table.failure.ok) {
        if (failure)
            *failure = "pin list output is unsupported here: " + table.failure.reason;
        return false;
    }
    for (std::size_t r = 0; r < table.rows.size(); ++r) {
        const std::string id = table.value(r, "Id");
        if (id.empty())
            continue;
        const std::string pinned = table.value(r, "Pinned Version");
        const std::string blocking = table.value(r, "Blocking");
        std::string policy = "pinned";
        if (!blocking.empty())
            policy = "held (" + blocking + ")";
        if (!pinned.empty())
            policy += " @" + pinned;
        pins.emplace_back(id, policy);
    }
    return true;
}

} // namespace pkg
