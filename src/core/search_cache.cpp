// search_cache.cpp — see search_cache.h.
#include "search_cache.h"

#include <cstdint>
#include <cstdio>

#include "cache.h"

namespace pkg::search_cache {
namespace {

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

char lowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

std::string normalizeQuery(const std::string& query)
{
    std::string out;
    out.reserve(query.size());
    bool pendingSpace = false;
    for (const char c : query) {
        if (isSpace(c)) {
            // Leading whitespace is dropped; a run of whitespace becomes the one
            // separator a query legitimately has ("visual studio code").
            if (!out.empty())
                pendingSpace = true;
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(lowerAscii(c));
    }
    return out;
}

std::string fileStem(const std::string& query)
{
    // FNV-1a 64-bit over the normalised query: short, stable, and filesystem
    // safe (hex only), so a query containing slashes or colons cannot escape the
    // cache directory.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : normalizeQuery(query)) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::string pathFor(const std::string& dir, const std::string& query)
{
    return dir + "\\search\\" + fileStem(query) + ".json";
}

std::string serialize(const Snapshot& snapshot)
{
    // cache.cpp owns the inventory format; a search snapshot is the same shape
    // with scanScope "search", so there is one serialiser to keep correct.
    return cache::serialize(snapshot, nullptr);
}

bool parse(const std::string& text, Snapshot& out, std::string& error)
{
    return cache::parse(text, out, error, nullptr);
}

bool worthWriting(const Snapshot& snapshot)
{
    // Same rule as the inventory cache: never replace a good answer with the
    // failure of a moment ago. An empty result IS worth keeping (a query that
    // matched nothing is a real answer), but only from a backend that ran.
    if (snapshot.observedAt.empty())
        return false;
    if (snapshot.winget != BackendState::Ready && snapshot.scoop != BackendState::Ready)
        return false;
    return true;
}

} // namespace pkg::search_cache
