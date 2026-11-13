// identity.cpp — see identity.h.
#include "identity.h"

#include <algorithm>
#include <cctype>

namespace pkg {

const char* managerId(Manager m)
{
    switch (m) {
    case Manager::Winget: return "winget";
    case Manager::Scoop: return "scoop";
    case Manager::Unknown: break;
    }
    return "unknown";
}

const char* provenanceId(Provenance p)
{
    switch (p) {
    case Provenance::Observed: return "observed";
    case Provenance::Matched: return "matched";
    case Provenance::BackendRecorded: return "backend-recorded";
    case Provenance::None: break;
    }
    return "none";
}

const char* scopeId(Scope s)
{
    switch (s) {
    case Scope::User: return "user";
    case Scope::Machine: return "machine";
    case Scope::Global: return "global";
    case Scope::Unknown: break;
    }
    return "unknown";
}

Manager managerFromId(const std::string& s)
{
    if (s == "winget") return Manager::Winget;
    if (s == "scoop") return Manager::Scoop;
    return Manager::Unknown;
}

Scope scopeFromId(const std::string& s)
{
    if (s == "user") return Scope::User;
    if (s == "machine") return Scope::Machine;
    if (s == "global") return Scope::Global;
    return Scope::Unknown;
}

Provenance provenanceFromId(const std::string& s)
{
    if (s == "observed") return Provenance::Observed;
    if (s == "matched") return Provenance::Matched;
    if (s == "backend-recorded") return Provenance::BackendRecorded;
    return Provenance::None;
}

bool operator==(const PackageKey& a, const PackageKey& b)
{
    // An unknown source compares by its explicit "unknown" text, so a key with
    // an unknown source never matches a key that names a real source.
    return a.manager == b.manager && a.sourceOrUnknown() == b.sourceOrUnknown() && a.id == b.id && a.scope == b.scope;
}

bool operator!=(const PackageKey& a, const PackageKey& b) { return !(a == b); }

bool operator<(const PackageKey& a, const PackageKey& b) { return a.text() < b.text(); }

namespace {

bool isSpaceOrControl(unsigned char c) { return c <= 0x20 || c == 0x7f; }

// Characters that only ever make sense as shell / option syntax. None of them
// appear in a legitimate package id or source name. '@' is rejected here too:
// a pinned "app@version" is built from two separately validated parts by the
// Scoop adapter, never by letting '@' through an id.
bool isMeta(char c)
{
    switch (c) {
    case '"': case '\'': case '`': case '\\': case '/': case '$': case ';':
    case '&': case '|': case '<': case '>': case '(': case ')': case '{':
    case '}': case '[': case ']': case '*': case '?': case '!': case '^':
    case '%': case '~': case '=': case ',': case '@': case '#': case ':':
        return true;
    default:
        return false;
    }
}

bool allSafe(const std::string& s, std::size_t maxLen)
{
    if (s.empty() || s.size() > maxLen)
        return false;
    if (s.front() == '-') // option-like, never an id
        return false;
    if (s.find("://") != std::string::npos) // a URL is never a package id
        return false;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (isSpaceOrControl(c))
            return false;
        if (c > 0x7f) // non-ASCII ids do exist, but never for the backends we drive
            return false;
        if (isMeta(ch))
            return false;
    }
    return true;
}

} // namespace

bool isSafePackageId(const std::string& id) { return allSafe(id, 128); }

bool isSafeSourceName(const std::string& source) { return allSafe(source, 64); }

bool isResolvable(const PackageKey& key, bool scopeMandatory)
{
    if (key.manager == Manager::Unknown)
        return false;
    if (key.source.empty() || !isSafeSourceName(key.source))
        return false;
    if (!isSafePackageId(key.id))
        return false;
    // An unknown scope cannot become a concrete --scope argument. Winget's
    // upgrade/uninstall never takes one (the exact id resolves the instance),
    // so only the actions that would pass --scope are blocked by it; the design
    // forbids retrying other scopes automatically either way.
    if (key.scope == Scope::Unknown && scopeMandatory)
        return false;
    return true;
}

std::string displayName(const std::string& name, const PackageKey& key)
{
    if (!name.empty())
        return name;
    return key.id.empty() ? std::string("(unknown)") : key.id;
}

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "BurntSushi.ripgrep.MSVC" and scoop's "ripgrep" should be recognisable as the
// same software; ".msvc", "-lts" style qualifiers must not defeat the match.
std::string coreName(const std::string& id)
{
    std::string s = lower(id);
    for (const char* suffix : {".msvc", ".gnu", ".musl", ".exe", "-lts", "-msvc", "-gnu", ".cli", "-cli"}) {
        const std::string suf(suffix);
        if (s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0)
            s.erase(s.size() - suf.size());
    }
    // Keep the last dotted segment: "BurntSushi.ripgrep" -> "ripgrep".
    const auto dot = s.rfind('.');
    if (dot != std::string::npos && dot + 1 < s.size())
        s = s.substr(dot + 1);
    for (auto& c : s)
        if (c == '_' || c == ' ')
            c = '-';
    return s;
}

} // namespace

std::optional<Manager> possibleDuplicate(const PackageKey& a, const PackageKey& b)
{
    if (a.manager == b.manager || a.manager == Manager::Unknown || b.manager == Manager::Unknown)
        return std::nullopt;
    if (a.id.empty() || b.id.empty())
        return std::nullopt;
    const std::string ca = coreName(a.id), cb = coreName(b.id);
    if (ca.empty() || cb.empty() || ca != cb)
        return std::nullopt;
    // Two different scopes can legitimately exist side by side; the hint is
    // still useful, the caller just phrases it more cautiously.
    return b.manager;
}

std::vector<std::vector<PackageKey>> groupByManager(const std::vector<PackageKey>& keys)
{
    std::vector<std::vector<PackageKey>> groups;
    for (const auto& k : keys) {
        bool placed = false;
        for (auto& g : groups) {
            if (!g.empty() && g.front().manager == k.manager) {
                g.push_back(k);
                placed = true;
                break;
            }
        }
        if (!placed)
            groups.push_back({k});
    }
    return groups;
}

} // namespace pkg
