// process.h — the only place that starts a process.
//
// Every backend invocation goes through here. The rules the design requires:
//   * no shell: CreateProcessW with an argv vector, never a concatenated string;
//   * the child joins our own job object with KILL_ON_JOB_CLOSE, so closing the
//     plugin (or the host ending the job) takes the tree with it. We never pass
//     CREATE_BREAKAWAY_FROM_JOB, so the host's job still contains us;
//   * output is captured as UTF-8 through pipes with a bounded buffer, and a
//     truncated capture is marked instead of silently dropping the tail;
//   * cancelling terminates the job and reports that this is not a rollback.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace pkg::platform {

struct RunResult {
    bool launched = false;
    std::uint32_t launchError = 0;   // Win32 error when launched == false
    int exitCode = -1;               // -1 when the process never reported one
    bool timedOut = false;
    bool truncated = false;          // output hit the cap
    std::string output;              // combined stdout (UTF-8, already bounded)
    std::string errorText;           // human-readable launch/timeout explanation
    double elapsedSeconds = 0.0;
};

struct RunOptions {
    std::string application;          // absolute path to the executable
    std::vector<std::string> arguments;
    std::string workingDirectory;      // empty: inherit
    std::uint32_t timeoutMs = 120000;  // hard cap; a timeout is never success
    std::size_t maxOutputBytes = 4u * 1024u * 1024u;
    // Extra environment variables; the inherited block is not dumped anywhere.
    std::vector<std::pair<std::string, std::string>> environment;
    // When false the child gets no window; an interactive installer may need one.
    bool allowWindow = false;
    // The backend may show its own UI (an uninstaller that must be clicked
    // through). When true, run() does not treat long silences on the pipe as a
    // stall, because an interactive installer produces no output while it waits.
    bool interactiveAllowed = false;

    // A callback invoked roughly every 250 ms so the UI can show "installer
    // running" without guessing progress. Returning false requests a cancel.
    const bool* cancelRequested = nullptr;
    // Same signal, safe to set from another thread. The UI's cancel action runs
    // on the GUI thread while run() polls on a worker; a plain bool there was a
    // data race, which is a plausible cause of an intermittent crash rather than
    // anything to do with memory limits.
    const std::atomic_bool* cancelAtomic = nullptr;
};

// Runs one process to completion (or timeout) and returns its captured output.
// Never throws; inspect RunResult.launched / exitCode.
RunResult run(const RunOptions& options);

// Quotes one argument for the Windows command line. Exposed so a test can prove
// a hostile argument stays a single argument after a round trip.
std::string quoteArgument(const std::string& argument);

// Encodes UTF-8 bytes into a wide string for the wide-char Win32 API.
std::wstring toWide(const std::string& utf8);

// Decodes a wide string to UTF-8.
std::string toUtf8(const std::wstring& wide);

// True when the current process can create a child at all. When the host did not
// grant `processes`, the job's ActiveProcessLimit makes CreateProcess fail with
// error 1816; this is reported instead of a generic failure.
bool childProcessesAllowed(std::uint32_t& lastError);

// True when this process actually holds elevated (administrator) rights. The
// manifest's `elevated` permission means the host runs the plugin as
// administrator exactly when Win顺 does and the user confirmed it, so this is
// the runtime answer to "can a machine-scope action proceed".
bool isElevated();

// The plugin's own data folder: %LOCALAPPDATA%\WinShun\plugins\
// ra9fael.packages.data, created if missing. Empty on failure. This is the only
// place the plugin writes, matching the manifest's files:write entry.
std::string pluginDataDir();

// Reads a whole file; returns false when it cannot be opened or read.
bool readTextFile(const std::string& path, std::string& out);

// Writes a file atomically (temp + rename) so a crash mid-write cannot leave a
// half-written cache for the next launch to trust.
bool writeTextFile(const std::string& path, const std::string& text);

} // namespace pkg::platform
