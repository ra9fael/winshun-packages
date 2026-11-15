// search_cache.h — the on-disk cache for Discover searches.
//
// A search is not cheap: `winget search` plus `scoop search` is seconds of
// process time, and the previous build kept the answer only in memory. Typing
// the same word twice, or switching tabs and back, re-ran both queries. This
// stores one file per query so a repeat is a file read.
//
// Why one file per query rather than one big file: a query's result is exactly
// a Snapshot, and cache.h already has a tested serialiser/parser for that. The
// file name is a stable hash of the normalised query, so no index is needed and
// a corrupt entry can be dropped without rewriting the others.
//
// Pure text logic where it matters (normalisation and the file name), with the
// caller owning the directory and the staleness clock — the same contract as
// cache.h and info_cache.h, so the rules stay unit-testable.
#pragma once

#include <cstddef>
#include <string>

#include "model.h"

namespace pkg::search_cache {

// The key a query is stored under: trimmed, internal runs of spaces collapsed,
// and lower-cased (WinGet and Scoop both match case-insensitively, so "Vim" and
// "vim" are the same query and must not cost two searches).
std::string normalizeQuery(const std::string& query);

// Stable file stem for a query (FNV-1a over the normalised text, hex). Stable
// across runs and versions: a cache written yesterday is still found today.
std::string fileStem(const std::string& query);

// Full path of a query's cache file inside `dir`.
std::string pathFor(const std::string& dir, const std::string& query);

// Serialises a search snapshot (cache.h's format, scope "search").
std::string serialize(const Snapshot& snapshot);

// Parses a cached search snapshot. False with `error` when the text is not a
// file this build understands; the caller then simply searches again.
bool parse(const std::string& text, Snapshot& out, std::string& error);

// Whether an answer is still worth showing: a failed or empty scan must never be
// served from the cache, and an answer without a timestamp cannot be judged.
bool worthWriting(const Snapshot& snapshot);

// How many query files the cache may hold before the oldest are removed.
constexpr std::size_t kMaxEntries = 60;

} // namespace pkg::search_cache
