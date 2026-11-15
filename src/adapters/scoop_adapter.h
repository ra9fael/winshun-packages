// scoop_adapter.h — Scoop detection, read-only queries and argv building.
//
// Scoop is a PowerShell implementation, not an exe, so it cannot be handed an
// argv array the way WinGet can. The design therefore requires one trusted entry
// point and nothing else:
//
//     powershell.exe -NoProfile -NonInteractive -NoLogo -ExecutionPolicy Bypass
//                    -File <scoop root>\shims\scoop.ps1 <verb> <args…>
//
// and every argument after the shim is built here from validated parts. User text
// is never concatenated into a command line and never reaches a shell.
//
// Facts verified by reading the Scoop source at revision
// e6aa3b366bdee8ed138c1e0f7b85192ebdd35d0f (not by running it):
//   * `update` with no arguments updates Scoop itself and the buckets. It is NOT
//     an update list and must never be called behind a query button.
//   * `update <app>` upgrades one app, and may sync Scoop/buckets first.
//   * `install` has no force option, and accepts app@version in help — which does
//     not prove that any historical version is downloadable.
//   * `update --force` re-resolves the current manifest; with an @version pin it
//     can move to bucket HEAD, so it is never used as a reinstall.
//   * `reset` rebuilds links/shims/env; it is not a reinstall.
#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "adapter.h"
#include "../core/model.h"

namespace pkg {

struct ScoopLocation {
    bool found = false;
    std::string root;        // %SCOOP% or %USERPROFILE%\scoop
    std::string shim;        // <root>\shims\scoop.ps1
    std::string powershell;  // absolute path to powershell.exe
    std::string detail;      // why it is not usable
};

// Which root to use and why. Exposed so a test can prove the search order
// without a real Scoop, and so the UI can say where the root came from.
struct ScoopRootChoice {
    std::string root;
    const char* source = "none";   // "SCOOP" | "profile" | "path-shim" | "common"
    std::string detail;
};

// Resolves Scoop's root from facts a plugin may rely on, in this order:
//   1. %SCOOP%                        — an explicit override always wins;
//   2. the current user's real profile — the registry ProfileList entry for this
//      process token, NOT %USERPROFILE%, which an elevated plugin does not get;
//   3. `scoop.ps1` on PATH            — the installed shim knows its own root;
//   4. %ProgramData%\scoop            — a machine-wide install.
// `env` is the caller's environment (a map), so the order is testable.
ScoopRootChoice resolveScoopRoot(const std::vector<std::pair<std::string, std::string>>& env,
                                const std::string& pathShim);

ScoopLocation locateScoop();

class ScoopAdapter : public Adapter {
public:
    explicit ScoopAdapter(std::string root = {}, std::string powershellPath = {});

    Manager manager() const override { return Manager::Scoop; }

    ProbeResult probe() override;
    // probe() starts a PowerShell for the shim probe and takes a second or two;
    // the root cannot change while the plugin lives, so only Ready/Unsupported
    // stick, and a Denied probe is retried on the next refresh.
    const ProbeResult& probeCached();
    void seedProbe(const ProbeResult& probe) override;
    // The shim, not powershell.exe: Scoop removed from the machine deletes it,
    // while PowerShell stays and would make a stale verdict look current.
    std::string probeStampPath() const override { return m_shim; }
    bool isProbeSeeded() const { return m_probed; }
    void invalidateProbe() { m_probed = false; m_probeCache = ProbeResult{}; }

    // Drops the per-scan cache. The controller calls this at the start of every
    // refresh so the parallel installed/updates queries share one `scoop export`
    // inside a scan without reusing it across scans.
    void invalidateCaches();

    Snapshot readInstalled(const ProbeResult& probe, bool includeObserved) override;
    Snapshot search(const ProbeResult& probe, const std::string& text, const bool* cancelRequested) override;
    Snapshot readUpdates(const ProbeResult& probe) override;
    CommandSpec buildMutation(const Plan& plan, const std::vector<std::string>& agreementsAccepted) override;
    VerifyResult verifyInstalled(const ProbeResult& probe, const PackageKey& key) override;

    // --- pure helpers, unit tested ---------------------------------------------

    // Parses `scoop export`-style JSON, which is the only structured Scoop output
    // available without a module: [{"Name":"git","Version":"2.47.0",
    // "Source":"main","Info":""}]. Anything unparsable is reported, not guessed.
    static Snapshot fromExportJson(const std::string& text, const std::string& observedAt,
                                  bool includeObserved, std::string* failure);

    // Compares installed Scoop packages against the bucket manifests on disk
    // (%SCOOP%\buckets\<bucket>\bucket\<app>.json). Pure file reading: the only
    // update evidence that needs no process, and the same evidence `scoop update
    // <app>` would act on. Packages with a newer manifest version gain
    // availableVersion and canUpgrade; a manifest that is missing or unparsable
    // leaves the row unchanged (no evidence is not a downgrade).
    static void fromBucketManifests(const std::string& scoopRoot, std::vector<Package>& installed,
                                    const std::string& observedAt, Snapshot& out, std::string* failure);

    // Parses `scoop search` plain text. Two real shapes exist and both are
    // handled, because which one appears depends on the Scoop version:
    //   - 0.6.0+ prints a table (Name / Version / Source / Binaries)
    //   - 0.5.x and scoop-search print grouped sections ("'main' bucket:")
    static Snapshot fromSearchText(const std::string& text, const std::string& observedAt,
                                  const std::string& query, std::string* failure);

    // True when the text is the 0.6.0 table shape: a "Name ... Version ... Source"
    // header followed by a dashed rule.
    static bool looksLikeSearchTable(const std::string& text);

    // Parses the 0.6.0 table shape. Column starts come from the header, so a
    // wider name column in one result set does not shift the next.
    static Snapshot fromSearchTable(const std::string& text, const std::string& observedAt,
                                    const std::string& query);

    // Builds the powershell argv for one shim invocation. Returns false when any
    // part is unsafe. This is the single place a Scoop command line is formed.
    static bool buildShimArgs(const std::string& shimPath, const std::vector<std::string>& scoopArgs,
                              std::vector<std::string>& out, std::string& refusal);

    // A scoop status line names held apps; used read-only to explain why a row
    // cannot be upgraded.
    static std::vector<std::string> parseHeldApps(const std::string& text);

private:
    // One `scoop export` PowerShell run, shared by every query in a scan. The
    // definition lives in the .cpp; only this class uses it.
    struct ExportResult;
    ExportResult exportCached();

    std::string m_root;
    std::string m_powershell;
    std::string m_shim;
    bool m_probed = false;
    ProbeResult m_probeCache;

    // `scoop export` is one PowerShell run (about 1.1 s) and both the installed
    // and updates queries need it. They run in parallel now, so one fetch is
    // shared per scan; guarded because the callers are on different threads.
    std::mutex m_exportMutex;
    bool m_exportFetched = false;
    bool m_exportLaunched = false;
    std::string m_exportOutput;
    std::string m_exportRefusal;
};

} // namespace pkg
