// winget_adapter.h — WinGet detection, read-only queries and argv building.
//
// Facts this adapter is written against (verified on this machine, WinGet
// v1.29.380 / zh-CN / Windows.Desktop 10.0.26300):
//   * `--output` is NOT accepted by list/search/upgrade, so the console table is
//     the only evidence and parsing is fixture-gated;
//   * `list` enumerates uninstall-registry entries as "ARP\Machine\X64\…".
//     Those are real installed software that WinGet does not manage: they are
//     Observed, never managed, and never removed through WinGet;
//   * `install` does accept --scope/--exact/--accept-package-agreements/
//     --accept-source-agreements/--force;
//   * there is no `reinstall` verb, and `repair` is a different operation.
#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "adapter.h"
#include "../core/model.h"

namespace pkg {

// Where the WinGet executable lives, and whether that location is trustworthy.
struct WingetLocation {
    bool found = false;
    std::string path;        // resolved absolute path
    bool viaAppExecutionAlias = false;  // resolved through an alias reparse point
    std::string detail;      // why it is missing/untrusted
};

WingetLocation locateWinget();

class WingetAdapter : public Adapter {
public:
    explicit WingetAdapter(std::string executablePath = {});

    Manager manager() const override { return Manager::Winget; }

    ProbeResult probe() override;
    // probe() spawns two processes (--version, --help) and takes seconds; the
    // answer cannot change while the plugin lives, so the adapter caches it and
    // callers that probe repeatedly reuse the first result.
    const ProbeResult& probeCached();

    // Drops the per-scan caches. The controller calls this once at the start of
    // every refresh: within one scan the cached answers are shared by the
    // parallel queries, but a run may have changed them, so the next scan must
    // not reuse them.
    void invalidateCaches();

    Snapshot readInstalled(const ProbeResult& probe, bool includeObserved) override;
    Snapshot search(const ProbeResult& probe, const std::string& text, const bool* cancelRequested) override;
    Snapshot readUpdates(const ProbeResult& probe) override;
    CommandSpec buildMutation(const Plan& plan, const std::vector<std::string>& agreementsAccepted) override;
    bool verifyInstalled(const ProbeResult& probe, const PackageKey& key, std::string& installedVersion,
                         std::string& detail) override;
    // Verification for an action that promised a specific version. Exit code 0 is
    // not success: an upgrade can run cleanly and leave the old version in place,
    // which is exactly what a DBX upgrade did. Comparing the reported installed
    // version against the plan's target is what separates the two.
    bool verifyReachedTarget(const PackageKey& key, const std::string& targetVersion,
                             std::string& installedVersion, std::string& detail);
    // Same rule for a plain installed-check, exposed so it can be unit tested
    // without spawning a process.
    static bool versionReached(const std::string& installed, const std::string& target, std::string& detail);

    // --- pure helpers, unit tested with fixtures -------------------------------

    // Turns one captured `winget list` table into packages. Anything the table
    // cannot prove becomes Unsupported with the reason, never a guess.
    static Snapshot fromListText(const std::string& text, const std::string& observedAt, bool includeObserved,
                                 const std::string& pinnedPolicy = {}, std::string* failure = nullptr);

    static Snapshot fromUpgradeText(const std::string& text, const std::string& observedAt,
                                    std::string* failure = nullptr);

    static Snapshot fromSearchText(const std::string& text, const std::string& observedAt, const std::string& query,
                                   std::string* failure = nullptr);

    // Parses `winget pin list`. Returns {"id -> "blocking"|"pinned"}. The text
    // "There are no pins configured." is not an error.
    static bool parsePinList(const std::string& text, std::vector<std::pair<std::string, std::string>>& pins,
                             std::string* failure);

    // Whether a given list row should be reported as managed. Only a non-ARP id
    // does that; an ARP id is observed.
    static Provenance provenanceFor(const std::string& id, bool installedMatchFound);

private:
    // `winget pin list` is one process per query, and the installed and updates
    // queries both need it. They now run in parallel on separate threads, so
    // without a cache the same 0.8 s call would run twice concurrently. The pin
    // state cannot change during a read-only scan, so one fetch is shared.
    // Guarded by a mutex because the two callers really are on different threads.
    std::vector<std::pair<std::string, std::string>> pinsCached();

    std::string m_executable;

public:
    // The resolved path, so a caller that must start winget itself (the metadata
    // lookup) uses the same executable the probe validated instead of a bare
    // "winget.exe" that may not resolve under the host's environment.
    const std::string& executablePath() const { return m_executable; }
    bool m_probed = false;
    ProbeResult m_probeCache;

    std::mutex m_pinMutex;
    bool m_pinsFetched = false;
    std::vector<std::pair<std::string, std::string>> m_pinsCache;
};
} // namespace pkg
