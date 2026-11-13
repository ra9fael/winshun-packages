// version.h — version comparison and update decisions.
//
// Pure logic. The design forbids treating a string comparison as "a newer
// version", so this parses dotted numeric segments (and suffix tags) and
// reports Unknown instead of guessing when a version cannot be ordered.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pkg {

enum class Order { Unknown, Older, Same, Newer };

struct Version {
    // Numeric segments, left to right. "1.2.0.3" -> {1,2,0,3}. Pre-release
    // suffixes are kept separately so "1.0.0-beta" sorts below "1.0.0".
    std::vector<unsigned long long> numbers;
    std::string suffix;   // "", "beta", "rc1", "20240101", ...
    std::string raw;      // as reported by the backend, never re-formatted

    bool valid() const { return !numbers.empty(); }
};

// Parses a backend version string. Returns an invalid Version (valid() ==
// false) for empty text, "unknown", "latest" and anything without a leading
// number — those must never be ordered.
Version parseVersion(const std::string& raw);

// Orders two parsed versions. Unknown when either side is invalid.
Order compareVersions(const Version& a, const Version& b);

// Convenience wrapper that parses first.
Order compareVersionStrings(const std::string& a, const std::string& b);

// True only when the backend gave a real target version that is strictly newer
// than what is installed. An unknown target is not an update.
bool isNewer(const std::string& installed, const std::string& available);

// Scoop and WinGet both allow "id@version" pin syntax in places. Splits it
// without ever letting '@' into an id handed to a process: the caller validates
// each half separately.
struct PinnedRef {
    std::string id;
    std::string version;   // empty when the reference carried no version
};
PinnedRef splitPinnedRef(const std::string& text);

// Builds the "id@version" text only after both halves were validated by the
// identity rules; used by the Scoop adapter for an explicitly chosen version.
std::optional<std::string> buildPinnedRef(const std::string& id, const std::string& version);

} // namespace pkg
