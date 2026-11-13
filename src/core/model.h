// model.h — the domain model shared by the adapters, the plan service and the UI.
//
// Pure data. No OS headers, no Qt, so every layer below the UI can be tested
// with fixtures (L1/L2).
#pragma once

#include <string>
#include <vector>

#include "identity.h"

namespace pkg {

// A backend's adaptation state. A backend that is missing, refuses to run, or
// whose output did not match any fixture is reported as its own state; it never
// silently becomes "0 packages installed".
enum class BackendState {
    Unknown,       // not probed yet
    Missing,       // executable not found, or resolved to an untrusted alias
    Denied,        // present but refused to run (policy, permission, lock)
    Unsupported,   // ran, but this version/command/locale has no verified parser
    Unavailable,   // network or source problem
    Ready,
};

const char* backendStateId(BackendState s);

// Capability record. One row per backend + exact version/revision + OS +
// command/option help digest + invocation form + scope + locale + installer
// kind + evidence level. A candidate is not a capability: every entry starts
// with evidence == None until a test proves it.
enum class Evidence { None, L1, L2, L3, L4 };

const char* evidenceId(Evidence e);

struct Capability {
    Manager manager = Manager::Unknown;
    std::string backendVersion;   // exact version or revision probe text
    std::string os;               // e.g. "Windows 11 26100"
    std::string helpDigest;       // digest of the parsed help for this verb
    std::string invocation;       // e.g. "winget --id <id> --exact --scope user"
    Scope scope = Scope::Unknown;
    std::string locale;           // "en-US", "de-DE", ...
    std::string installerKind;    // "wix", "msi", "zip", "unknown"
    Evidence evidence = Evidence::None;
    bool reinstallable = false;   // only true with L3/L4 evidence for this row
    std::string note;             // why it is or is not usable
};

// What an adapter produced for one installed or discoverable package.
struct Package {
    PackageKey key;
    std::string name;              // display only
    std::string installedVersion;  // empty when not installed
    std::string availableVersion;  // empty when the backend gave none
    std::string availableKind;     // "version" | "unknown"
    std::string arch;              // "x64" | "arm64" | "unknown"
    Provenance provenance = Provenance::None;
    std::string holdPolicy;        // e.g. "pinned @1.94.2", "held", ""
    std::vector<std::string> agreements;   // "source", "installer"
    std::string metadataAt;        // when this row's evidence was observed
    std::string dependencies;      // human-readable, "none" when verified none
    std::string material;          // availability of the install material
    std::string limits;            // why an action is unavailable
    bool canInstall = false;
    bool canUninstall = false;
    bool canUpgrade = false;
};

// Timestamped query result. The UI keeps showing the previous snapshot, marked
// stale, instead of blanking into an empty state while a new query runs.
struct Snapshot {
    std::vector<Package> packages;
    std::string observedAt;      // backend-reported or plugin-measured time
    bool stale = false;
    std::string staleReason;
    BackendState winget = BackendState::Unknown;
    BackendState scoop = BackendState::Unknown;
    std::string wingetDetail;
    std::string scoopDetail;
    std::string scanScope;       // what this snapshot actually covers
};

enum class Tab { Installed, Discover, Updates, Tasks };

const char* tabId(Tab t);

// Which rows belong on a page. Installed keeps anything the backend enumerates
// (provenance is shown per row); Updates only keeps rows with real update
// evidence; Discover filters the candidate list by text.
std::vector<Package> rowsForTab(const Snapshot& snapshot, Tab tab, const std::string& filter,
                                Manager onlyManager);

// Cross-manager duplicate hints for one row, as indices into the snapshot.
std::vector<std::size_t> duplicateHints(const Snapshot& snapshot, std::size_t index);

} // namespace pkg
