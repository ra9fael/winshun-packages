// redact.cpp — see redact.h.
#include "redact.h"

#include <algorithm>
#include <cctype>

namespace pkg {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& haystackLower, const std::string& needleLower)
{
    return !needleLower.empty() && haystackLower.find(needleLower) != std::string::npos;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    std::size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) {
        s.replace(at, from.size(), to);
        at += to.size();
    }
}

} // namespace

std::string redactLine(const std::string& line, const RedactRules& rules)
{
    // Control characters (including ANSI escapes) are removed: a log must not
    // contain text that acts on a terminal.
    std::string out;
    out.reserve(line.size());
    for (const char ch : line) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == 0x1b) // ESC: drop the whole escape introducer, the rest reads as noise anyway
            continue;
        if (c < 0x20 && ch != '\t')
            continue;
        if (c == 0x7f)
            continue;
        out.push_back(ch);
    }

    const std::string lowerOut = lower(out);
    for (const auto& token : rules.dropTokens) {
        if (contains(lowerOut, lower(token)))
            return "[redacted: possible secret]";
    }

    for (const auto& [path, variable] : rules.roots) {
        if (path.empty())
            continue;
        replaceAll(out, path, variable);
        // The same path may appear with forward slashes in backend output.
        std::string forward = path;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        replaceAll(out, forward, variable);
    }

    if (out.size() > rules.maxLineLength) {
        const std::size_t keep = rules.maxLineLength > 24 ? rules.maxLineLength - 24 : rules.maxLineLength;
        out = out.substr(0, keep) + "…[truncated " + std::to_string(line.size() - keep) + " chars]";
    }
    return out;
}

std::string redactBlock(const std::string& text, const RedactRules& rules, std::size_t maxLines)
{
    std::string out;
    std::size_t start = 0, lines = 0, dropped = 0;
    bool full = false;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!full) {
            if (lines < maxLines) {
                if (!out.empty())
                    out.push_back('\n');
                out += redactLine(line, rules);
                ++lines;
            } else {
                full = true;
            }
        } else {
            ++dropped;
        }
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (full)
        out += "\n…[truncated " + std::to_string(dropped) + " further lines]";
    return out;
}

RedactRules defaultRules(const std::string& userProfile)
{
    RedactRules rules;
    if (!userProfile.empty())
        rules.roots.emplace_back(userProfile, "%USERPROFILE%");
    rules.dropTokens = {"token=", "password", "passwd", "authorization:", "api_key", "apikey", "secret",
                        "bearer ", "cookie:"};
    rules.maxLineLength = 512;
    return rules;
}

} // namespace pkg
