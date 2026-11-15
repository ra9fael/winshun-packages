// probe_cache.h — the on-disk result of the backend probe.
//
// A probe is the cheapest way to learn "can this backend run at all, and does
// this build know the verbs we use". For WinGet that costs two processes
// (`--version`, `--help`), for Scoop one PowerShell start; measured on this
// machine that is ~275 ms before the first `winget list` of every plugin
// process. The plugin is started lazily, so a window opened after the host
// recycled it pays that again even though nothing changed.
//
// This caches the verdict together with a stamp of the executable that was
// probed. At startup the verdict is reused only when the stamp still matches,
// i.e. the same file, unchanged, that produced the answer. Anything else — a
// different path, a new build, a verdict that was not Ready/Unsupported — falls
// back to a real probe.
//
// Why a separate file rather than cache.json: the inventory cache is a snapshot
// of the machine and is replaced wholesale on every scan; a verdict about the
// binary has a different lifetime and must survive a failed scan.
//
// Pure text logic (the caller owns the file), like cache.h and info_cache.h.
#pragma once

#include <string>

#include "model.h"

namespace pkg::probe_cache {

struct Record {
    BackendState state = BackendState::Unknown;
    std::string detail;      // why it is not Ready, as shown in the UI
    std::string version;     // exact version text (WinGet) or "unknown" (Scoop)
    std::string program;     // resolved executable that was probed
    std::string stamp;       // size+write-time fingerprint of that file
    std::string observedAt;  // ISO 8601
};

struct Store {
    Record winget;
    Record scoop;
    bool hasWinget = false;
    bool hasScoop = false;
};

// A verdict is only reused for the exact file that produced it, and only when
// it says something a real query can build on. A failure is never reused: the
// next launch should try again rather than trust yesterday's refusal.
bool reusable(const Record& record, const std::string& currentStamp);

std::string serialize(const Store& store);
bool parse(const std::string& text, Store& out, std::string& error);

} // namespace pkg::probe_cache
