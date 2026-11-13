// cache.h — the on-disk inventory cache.
//
// A resident plugin is not a long-lived process: the host may end it between
// window opens, so an in-memory snapshot alone would make every launch re-scan
// (a winget list is seconds). This writes the last successful scan to the
// plugin's own data folder and reads it back at startup, so the window has rows
// to show immediately and only re-queries once the data is actually old.
//
// Pure text logic (no Qt, no OS headers): the caller supplies the directory and
// does the file I/O, so the format is unit-testable.
#pragma once

#include <string>

#include "model.h"

namespace pkg::cache {

// Serialises one snapshot, optionally with the update listing beside it. The
// format is deliberately flat JSON so an older build can ignore fields it does
// not know instead of failing to parse. `updates` is a separate snapshot because
// it has its own timestamp: the update listing is a different query from the
// inventory and the two must not claim to have been observed at the same moment.
std::string serialize(const Snapshot& snapshot, const Snapshot* updates = nullptr);

// Parses a snapshot. Returns false and fills `error` when the text is not a
// cache file this build understands; the caller then simply re-queries. When
// `updates` is given it receives the cached update listing, which may legitimately
// be empty (a cache written before updates were stored).
bool parse(const std::string& text, Snapshot& out, std::string& error,
           Snapshot* updates = nullptr);

// Whether a snapshot is worth writing: an empty or failed scan must not
// overwrite a good cache with nothing.
bool worthWriting(const Snapshot& snapshot);

} // namespace pkg::cache
