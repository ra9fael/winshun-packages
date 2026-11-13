// identity.h — package identity, provenance and scope.
//
// Pure logic: no OS headers, no Qt. Builds on every platform so the identity
// rules can be unit-tested (L1) without a host or a package manager.
//
// The design's rule: PackageKey = manager + source/bucket + exact id + scope,
// and an unknown part is never a wildcard. Two rows that differ in any part are
// two different packages even when their display name is identical, and a
// cross-manager "possible duplicate" is only ever a hint — it must never make
// one row's uninstall touch the other.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pkg {

enum class Manager { Unknown, Winget, Scoop };

// Where the row's evidence comes from. Only BackendRecorded may be shown as
// "managed"; Observed means a list command enumerated it and nothing else, and
// Matched means a directory layout happened to line up.
enum class Provenance { None, Observed, Matched, BackendRecorded };

// Scope is a backend-supported value plus the evidence behind it. An empty or
// unrecognised scope becomes Unknown and must not be treated as "any scope".
enum class Scope { Unknown, User, Machine, Global };

const char* managerId(Manager m);
const char* provenanceId(Provenance p);
const char* scopeId(Scope s);

Manager managerFromId(const std::string& s);
Scope scopeFromId(const std::string& s);
Provenance provenanceFromId(const std::string& s);

// True only when the backend itself can prove it owns the installed instance.
// This still does not prove the manager performed the first install, so the UI
// keeps that sentence next to the badge.
inline bool countsAsManaged(Provenance p) { return p == Provenance::BackendRecorded; }

struct PackageKey {
    Manager manager = Manager::Unknown;
    std::string source;   // source (WinGet) or bucket (Scoop); "" == unknown
    std::string id;       // exact id only; a display name is never an id
    Scope scope = Scope::Unknown;

    std::string sourceOrUnknown() const { return source.empty() ? std::string("unknown") : source; }

    // Stable, canonical text form. Deliberately includes the scope so that two
    // installs of the same id in different scopes cannot collide in a set, a
    // queue de-duplication check or a persisted task record.
    std::string text() const
    {
        return std::string(managerId(manager)) + "|" + sourceOrUnknown() + "|" + id + "|" + scopeId(scope);
    }
};

bool operator==(const PackageKey& a, const PackageKey& b);
bool operator!=(const PackageKey& a, const PackageKey& b);
bool operator<(const PackageKey& a, const PackageKey& b);

// A key is actionable when the id passed the conservative check below and the
// parts a mutation needs are known. Which parts those are depends on the action:
// a winget upgrade/uninstall matches by exact id alone (the backend resolves the
// installed instance), so an Unknown scope from `winget list` — which prints no
// scope column — does not block them. Only installs take an explicit --scope.
// `scopeMandatory` is true for installs, false for upgrade/uninstall.
bool isResolvable(const PackageKey& key, bool scopeMandatory = true);

// Whether a string is safe to hand to a backend as a package id: no leading
// dash (would be read as an option), no path separators, no URL scheme, no
// whitespace or shell metacharacters, no control characters. This is the gate
// that keeps user text from ever becoming an argument.
bool isSafePackageId(const std::string& id);

// Whether a source/bucket name is safe to use as an argument.
bool isSafeSourceName(const std::string& source);

// Only for display: a short human label. Never use it as an identity.
std::string displayName(const std::string& name, const PackageKey& key);

// Cross-manager duplicate hint. Returns the other manager when the two rows
// look like the same software, and nullopt otherwise. The caller may only show
// a hint; it must not merge rows or chain a removal.
std::optional<Manager> possibleDuplicate(const PackageKey& a, const PackageKey& b);

// Groups a list of keys by manager, preserving order inside each group. The
// scheduler uses this because each manager has its own serial lane.
std::vector<std::vector<PackageKey>> groupByManager(const std::vector<PackageKey>& keys);

} // namespace pkg
