// pins_cache.h — the on-disk `winget pin list` answer.
//
// Reading pins costs one WinGet process. The list changes only when a pin is
// added or removed (rare, and never by this plugin), yet it was re-read on every
// refresh and on every new plugin process. This keeps the answer beside the
// inventory cache, with its own timestamp so the caller can apply the same
// freshness rule it uses elsewhere.
//
// The policy per id is what the upgrade path needs: a pinned package must not be
// offered for upgrade, and that decision must be available before the first
// query finishes.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pkg::pins_cache {

struct Store {
    std::string observedAt;  // ISO 8601
    std::vector<std::pair<std::string, std::string>> entries;  // id -> "pinned" | "blocking"
};

std::string serialize(const Store& store);
// False with `error` for text this build does not understand; the caller then
// reads the pins again instead of guessing.
bool parse(const std::string& text, Store& out, std::string& error);

} // namespace pkg::pins_cache
