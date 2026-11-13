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

class Adapter {
public:
    virtual ~Adapter() = default;

    virtual Manager manager() const = 0;

    // Detection only. Must not accept agreements, refresh sources, update the
    // manager, or install anything.
    virtual ProbeResult probe() = 0;

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
    // an exit code.
    virtual bool verifyInstalled(const ProbeResult& probe, const PackageKey& key, std::string& installedVersion,
                                 std::string& detail) = 0;
};

} // namespace pkg
