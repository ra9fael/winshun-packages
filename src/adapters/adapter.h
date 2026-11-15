// adapter.h — the backend contract the plan service talks to.
//
// An adapter is responsible for three things and nothing else:
//   1. probe: report a BackendState plus its exact version, without installing
//      anything or accepting any agreement;
//   2. read: produce Package rows for installed / search / update-list, with
//      provenance and a timestamp, or an explicit failure;
//   3. describe: build the exact argv for a mutation, or refuse.
//
// Adapters never run a mutation themselves. The queue asks for argv and the
// process layer runs it, so the "no shell, validated ids only" rule lives in one
// place and every adapter is testable with fakes.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../core/model.h"
#include "../core/plan.h"
#include "../platform/process.h"

namespace pkg {

struct ProbeResult {
    BackendState state = BackendState::Unknown;
    std::string programPath;      // resolved absolute path, when trusted
    std::string version;          // exact version or revision text
    std::string detail;           // why it is not Ready, in words the UI can show
    Capability capability;
};

// One exact command an adapter wants run. The queue records it in the task so a
// later review can see what was actually requested.
struct CommandSpec {
    bool ok = false;
    std::string refusal;          // why no command could be built
    platform::RunOptions options;
    std::string redactedPreview;  // for the task log and the review page
};

// What a post-run verification actually established. The old pair of booleans
// ("found" and "knowable") could be combined into a wrong claim: an uninstall
// inverted "found == false" into "success", which also matched a query that
// simply failed. These outcomes say what the evidence supports.
enum class VerifyOutcome {
    // The identity was read and is present, with its installed version.
    Present,
    // The identity was read and is provably gone: the backend answered a
    // complete list that does not contain it.
    Absent,
    // The identity was read and is present, but not at the requested version.
    PresentWrongVersion,
    // The query could not answer: it failed, timed out, or the output could not
    // be read. Nothing about the machine state follows from that.
    Unknown,
};

// The evidence behind a verification, so the task log can say why.
struct VerifyResult {
    VerifyOutcome outcome = VerifyOutcome::Unknown;
    std::string installedVersion;   // empty when absent or unknown
    std::string detail;             // human-readable, already free of secrets
    int exitCode = -1;              // the backend's exit code for the query
    int checks = 0;                 // how many queries were made
    double elapsedSeconds = 0.0;

    bool provedPresent() const { return outcome == VerifyOutcome::Present; }
    bool provedAbsent() const { return outcome == VerifyOutcome::Absent; }
    bool unknown() const { return outcome == VerifyOutcome::Unknown; }
};

class Adapter {
public:
    virtual ~Adapter() = default;

    virtual Manager manager() const = 0;

    // Detection only. Must not accept agreements, refresh sources, update the
    // manager, or install anything.
    virtual ProbeResult probe() = 0;

    // Reuses a verdict recorded by an earlier process (probe_cache.h). Only
    // called with a record whose executable stamp still matches, so this must
    // not re-validate; it installs the answer as if probe() had just returned it.
    virtual void seedProbe(const ProbeResult&) {}
    // The executable whose fingerprint decides whether a seeded verdict is still
    // about this build. Empty when the adapter cannot say.
    virtual std::string probeStampPath() const { return {}; }

    // Installed inventory. `includeObserved` lets the UI hide rows that only
    // appeared in an enumeration.
    virtual Snapshot readInstalled(const ProbeResult& probe, bool includeObserved) = 0;

    // Remote/local candidate search. Never installs, and must stop on cancel.
    virtual Snapshot search(const ProbeResult& probe, const std::string& text,
                            const bool* cancelRequested) = 0;

    // Rows with update evidence, honouring hold/pin read-only.
    virtual Snapshot readUpdates(const ProbeResult& probe) = 0;

    // Builds the exact command for a plan, or refuses with a reason. Never adds
    // an agreement flag; consent is supplied by the caller through
    // `agreementsAccepted`, which the plan service fills only after the user
    // explicitly checked each one.
    virtual CommandSpec buildMutation(const Plan& plan, const std::vector<std::string>& agreementsAccepted) = 0;

    // Re-reads one identity after a run so success is decided by state, not by
    // an exit code. The outcome says what the evidence supports: present,
    // absent, present at the wrong version, or unknown.
    virtual VerifyResult verifyInstalled(const ProbeResult& probe, const PackageKey& key) = 0;
};

} // namespace pkg
