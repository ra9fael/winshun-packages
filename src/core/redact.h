// redact.h — log redaction.
//
// Task records and diagnostics are persisted under WS_DATA_DIR and may be
// exported by the user, so they are minimal by construction: no environment
// dump, no tokens, no full personal paths, no shell text that could be replayed.
// Pure logic so the rules are testable.
#pragma once

#include <string>
#include <vector>

namespace pkg {

struct RedactRules {
    // Replace the user's profile directory with %USERPROFILE% and other known
    // roots with their variable name, so a log is useful without naming a person.
    std::vector<std::pair<std::string, std::string>> roots; // {path, "%USERPROFILE%"}

    // Any of these substrings makes the whole line drop (case-insensitive).
    std::vector<std::string> dropTokens; // e.g. "token=", "password", "authorization:"

    std::size_t maxLineLength = 512;
};

// Redacts one line: strips control characters, replaces known roots, drops the
// line entirely when it looks like a secret, and truncates with a marker.
std::string redactLine(const std::string& line, const RedactRules& rules);

// Redacts a multi-line blob, keeping at most `maxLines` lines and appending a
// truncation marker. The design forbids unbounded buffering.
std::string redactBlock(const std::string& text, const RedactRules& rules, std::size_t maxLines);

// Builds the default rule set for a given profile directory. Other roots are
// added by the platform layer once it knows them.
RedactRules defaultRules(const std::string& userProfile);

} // namespace pkg
