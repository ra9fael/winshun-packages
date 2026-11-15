// info_cache.h — the on-disk `winget show` metadata cache.
//
// Selecting a row runs `winget show` (seconds, one process per call), and the
// result lived only in memory: going back to a row re-ran the whole query.
// This caches each answer by exact id, same contract as cache.h — pure text
// logic, the caller owns the file and the staleness clock.
//
// The id is compared case-insensitively everywhere: `winget list` and
// `winget show` may recase an id between calls (the same reason plan matching
// lowercases). Entries accumulate per click, unlike the inventory whose row
// count is naturally bounded, so an LRU cap keeps the file small.
#pragma once

#include <cstddef>

#include <map>
#include <string>
#include <vector>

namespace pkg::info_cache {

struct Entry {
    std::string id;          // exact id as fetched
    std::string observedAt;  // ISO 8601; the caller decides freshness
    // The version the package was installed at when this was fetched. When the
    // installed version still equals it, the answer still describes this package
    // and does not expire by age (a package installed at 1.2.3 for a month has
    // the same metadata today). Empty for rows that are not installed, which
    // fall back to the time-based TTL.
    std::string version;
    std::map<std::string, std::string> fields;  // the parsed `winget show` map
};

struct Store {
    std::vector<Entry> entries;  // oldest first; upsert refreshes to the back
};

// Newest entry for `id`, compared case-insensitively. False when absent.
bool find(const Store& store, const std::string& id, Entry& out);

// True when the entry was fetched for the version the package is still
// installed at, so its content still describes that package regardless of age.
// False when either side is empty (a non-installed row, or an entry written by
// an older build), which makes the caller fall back to the time-based TTL.
bool stillMatches(const Entry& entry, const std::string& installedVersion);

// Inserts or refreshes (moving the entry to the back of the LRU order) and
// evicts the oldest while the store holds more than `cap` entries.
void upsert(Store& store, Entry&& entry, std::size_t cap = 200);

// Removes the entry for `id` (case-insensitively). Returns true when something
// was removed — the caller then persists the store. A mutation invalidates the
// entry for the package it changed; a refresh drops entries for packages that
// are no longer installed at all.
bool drop(Store& store, const std::string& id);

std::string serialize(const Store& store);

// Returns false and fills `error` for text this build does not understand; the
// caller then starts from an empty store and refetches on demand.
bool parse(const std::string& text, Store& out, std::string& error);

} // namespace pkg::info_cache
