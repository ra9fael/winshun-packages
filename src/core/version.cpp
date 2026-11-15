// version.cpp — see version.h.
#include "version.h"

#include "identity.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace pkg {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "unknown", "latest", "-" and friends are placeholders, not versions.
bool isPlaceholder(const std::string& s)
{
    const std::string l = lower(s);
    return l.empty() || l == "unknown" || l == "n/a" || l == "-" || l == "?" || l == "latest" || l == "none";
}

// SemVer §11 pre-release identifiers: dot separated; numeric identifiers
// compare numerically, alphanumeric ones in ASCII order, and a numeric
// identifier always sorts below an alphanumeric one. A malformed identifier
// (empty part) makes the pair unorderable rather than guessed.
struct PreReleaseId {
    bool numeric = false;
    unsigned long long number = 0;
    std::string text;   // already lowercased by parseVersion
};

bool parsePreRelease(const std::string& suffix, std::vector<PreReleaseId>& out)
{
    std::size_t i = 0;
    while (true) {
        const std::size_t dot = suffix.find('.', i);
        const std::string part =
            suffix.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
        if (part.empty())
            return false;   // "alpha.", "a..b": not a compliant identifier, do not guess
        PreReleaseId id;
        bool allDigits = true;
        for (const char c : part) {
            if (!isDigit(c)) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            id.numeric = true;
            // Saturate like the numeric segments do. Leading zeroes are
            // non-compliant but harmless once parsed ("rc.02" == "rc.2").
            unsigned long long n = 0;
            for (const char c : part) {
                const unsigned long long digit = static_cast<unsigned long long>(c - '0');
                if (n > (0xFFFFFFFFFFFFFFFFull - digit) / 10)
                    n = 0xFFFFFFFFFFFFFFFFull;
                else
                    n = n * 10 + digit;
            }
            id.number = n;
        } else {
            id.text = part;
        }
        out.push_back(std::move(id));
        if (dot == std::string::npos)
            return true;
        i = dot + 1;
    }
}

Order comparePreRelease(const std::string& a, const std::string& b)
{
    std::vector<PreReleaseId> idsA, idsB;
    if (!parsePreRelease(a, idsA) || !parsePreRelease(b, idsB))
        return Order::Unknown;
    for (std::size_t i = 0;; ++i) {
        if (i >= idsA.size() && i >= idsB.size())
            return Order::Same;
        if (i >= idsA.size())
            return Order::Older;    // 1.0.0-alpha < 1.0.0-alpha.1: the longer set is greater
        if (i >= idsB.size())
            return Order::Newer;
        const PreReleaseId& x = idsA[i];
        const PreReleaseId& y = idsB[i];
        if (x.numeric && y.numeric) {
            if (x.number != y.number)
                return x.number < y.number ? Order::Older : Order::Newer;
        } else if (!x.numeric && !y.numeric) {
            if (x.text != y.text)
                return x.text < y.text ? Order::Older : Order::Newer;   // ASCII order
        } else {
            // §11.4: numeric identifiers always have lower precedence.
            return x.numeric ? Order::Older : Order::Newer;
        }
    }
}

} // namespace

Version parseVersion(const std::string& raw)
{
    Version v;
    v.raw = raw;
    if (isPlaceholder(raw))
        return v;

    std::string text = raw;
    // A leading 'v' is common ("v1.2.3") and carries no ordering meaning.
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V') && text.size() > 1 && isDigit(text[1]))
        text.erase(0, 1);

    // SemVer §10: build metadata ("+…") attaches to a version but never
    // affects precedence, so it is dropped before parsing. `raw` keeps the
    // original text for display and for the backend argv.
    if (const std::size_t plus = text.find('+'); plus != std::string::npos)
        text.resize(plus);

    if (text.empty() || !isDigit(text[0]))
        return v; // "beta-3", "head", a date-less tag: cannot be ordered

    std::size_t i = 0;
    while (i < text.size()) {
        if (!isDigit(text[i]))
            break;
        unsigned long long n = 0;
        while (i < text.size() && isDigit(text[i])) {
            const unsigned long long digit = static_cast<unsigned long long>(text[i] - '0');
            // Saturate instead of overflowing on absurd input.
            if (n > (0xFFFFFFFFFFFFFFFFull - digit) / 10)
                n = 0xFFFFFFFFFFFFFFFFull;
            else
                n = n * 10 + digit;
            ++i;
        }
        v.numbers.push_back(n);
        if (i >= text.size())
            break;
        // A single separator between numeric segments.
        if (text[i] == '.' || text[i] == '-' || text[i] == '_' || text[i] == '+') {
            ++i;
            if (i < text.size() && isDigit(text[i]))
                continue;
        }
        break;
    }

    if (i < text.size())
        v.suffix = lower(text.substr(i));
    return v;
}

Order compareVersions(const Version& a, const Version& b)
{
    if (!a.valid() || !b.valid())
        return Order::Unknown;

    const std::size_t n = std::max(a.numbers.size(), b.numbers.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned long long x = i < a.numbers.size() ? a.numbers[i] : 0;
        const unsigned long long y = i < b.numbers.size() ? b.numbers[i] : 0;
        if (x != y)
            return x < y ? Order::Older : Order::Newer;
    }

    // Equal numbers: a suffix makes a pre-release, which sorts below the plain
    // release. Two suffixes follow the SemVer §11 identifier rules — the
    // convention winget manifests and scoop recipes quote — instead of declaring
    // every differing pair unorderable ("0.2.2-rc.2 → 0.2.2-rc.3" was blocked
    // by the old rule). Unparseable identifiers stay Unknown rather than
    // guessed.
    if (a.suffix == b.suffix)
        return Order::Same;
    if (a.suffix.empty())
        return Order::Newer;
    if (b.suffix.empty())
        return Order::Older;
    return comparePreRelease(a.suffix, b.suffix);
}

Order compareVersionStrings(const std::string& a, const std::string& b)
{
    return compareVersions(parseVersion(a), parseVersion(b));
}

bool isNewer(const std::string& installed, const std::string& available)
{
    return compareVersionStrings(available, installed) == Order::Newer;
}

PinnedRef splitPinnedRef(const std::string& text)
{
    PinnedRef ref;
    const auto at = text.rfind('@');
    // "@" at position 0 is not a pin; it is a malformed id and the id gate
    // rejects it later.
    if (at == std::string::npos || at == 0 || at + 1 >= text.size()) {
        ref.id = text;
        return ref;
    }
    ref.id = text.substr(0, at);
    ref.version = text.substr(at + 1);
    return ref;
}

std::optional<std::string> buildPinnedRef(const std::string& id, const std::string& version)
{
    if (!isSafePackageId(id) || version.empty())
        return std::nullopt;
    // Version text is passed to the backend too, so it gets the same treatment.
    // It may contain dots, plus signs and interior dashes, but a leading dash
    // would be read as an option and nothing here may break out of the argument.
    if (version.front() == '-')
        return std::nullopt;
    for (const char ch : version) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c > 0x7f)
            return std::nullopt;
        switch (ch) {
        case '"': case '\'': case '`': case '\\': case '/': case '$': case ';': case '&':
        case '|': case '<': case '>': case '(': case ')': case '{': case '}': case '[':
        case ']': case '*': case '?': case '!': case '^': case '%': case '~': case '=':
        case ',': case '@': case '#': case ':':
            return std::nullopt;
        default:
            break;
        }
    }
    return id + "@" + version;
}

} // namespace pkg
