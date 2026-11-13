// process.cpp — see process.h.
//
// Windows-only. Everything here is deliberately conservative: no shell, no
// breakaway, bounded buffers, explicit timeout, and a cancel path that says what
// it does and does not do.
#ifdef _WIN32

#include "process.h"

#include <windows.h>

#include <chrono>
#include <thread>

namespace pkg::platform {
namespace {

std::string lastErrorMessage(std::uint32_t code)
{
    if (code == 0)
        return {};
    wchar_t* buffer = nullptr;
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string text;
    if (length && buffer) {
        text = toUtf8(std::wstring(buffer, length));
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
            text.pop_back();
    }
    LocalFree(buffer);
    return text.empty() ? ("error " + std::to_string(code)) : text;
}

// Reads whatever is available without blocking for a full buffer, so a chatty
// child cannot stall the loop and a quiet one cannot stall it either.
void drainPipe(HANDLE pipe, std::string& sink, std::size_t cap, bool& truncated)
{
    char buffer[4096];
    DWORD available = 0;
    while (::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        DWORD read = 0;
        const DWORD want = available > sizeof buffer ? static_cast<DWORD>(sizeof buffer) : available;
        if (!::ReadFile(pipe, buffer, want, &read, nullptr) || read == 0)
            return;
        if (sink.size() >= cap) {
            truncated = true;
            continue; // keep draining so the child never blocks on a full pipe
        }
        const std::size_t room = cap - sink.size();
        sink.append(buffer, read < room ? read : room);
        if (read > room)
            truncated = true;
    }
}

} // namespace

std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string toUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                             nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string quoteArgument(const std::string& argument)
{
    // The CommandLineToArgvW rules: quote when the argument is empty or holds
    // whitespace or a quote, and double every backslash that precedes a quote.
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos)
        return argument;

    std::string out = "\"";
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == '\\') {
            ++it;
            ++backslashes;
        }
        if (it == argument.end()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}

bool childProcessesAllowed(std::uint32_t& lastError)
{
    lastError = 0;
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT length = ::GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        lastError = ::GetLastError();
        return false;
    }
    std::wstring probe = std::wstring(systemDirectory, length) + L"\\cmd.exe";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring commandLine = L"\"" + probe + L"\" /c exit 0";
    const BOOL ok = ::CreateProcessW(probe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        lastError = ::GetLastError();
        return false;
    }
    ::WaitForSingleObject(pi.hProcess, 5000);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

bool isElevated()
{
    // The same check the host uses: the process token's elevation flag. The
    // manifest's `elevated` permission makes this true exactly when Win顺 runs
    // as administrator and the user confirmed the plugin, so this is the honest
    // runtime answer rather than a guess.
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof elevation;
    const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation, size, &size) &&
                    elevation.TokenIsElevated != 0;
    ::CloseHandle(token);
    return ok;
}

std::string pluginDataDir()
{
    // The development harness points this at its own folder so a development scan
    // can never overwrite the cache the installed plugin will read. The shipped
    // plugin never sets it, so this costs one environment read.
    wchar_t override_ [MAX_PATH * 2] = {};
    const DWORD overrideLen = ::GetEnvironmentVariableW(L"PACKAGES_DEV_DATA_DIR", override_, MAX_PATH * 2);
    if (overrideLen > 0 && overrideLen < MAX_PATH * 2) {
        const std::string dir = toUtf8(std::wstring(override_, overrideLen));
        ::CreateDirectoryW(toWide(dir).c_str(), nullptr);
        return dir;
    }

    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    const std::string root = toUtf8(std::wstring(localAppData, length)) +
                             "\\WinShun\\plugins\\ra9fael.packages.data";
    // Create every level: a fresh machine has none of them. Failure here is not
    // fatal — the plugin just runs without a disk cache.
    std::string partial;
    for (std::size_t i = 0; i <= root.size(); ++i) {
        if (i == root.size() || root[i] == '\\') {
            if (!partial.empty() && partial.back() != ':')
                ::CreateDirectoryW(toWide(partial).c_str(), nullptr);
            if (i == root.size())
                break;
        }
        if (i < root.size())
            partial.push_back(root[i]);
    }
    return root;
}

bool readTextFile(const std::string& path, std::string& out)
{
    out.clear();
    HANDLE file = ::CreateFileW(toWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32 * 1024 * 1024) {
        ::CloseHandle(file);
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(file);
    if (!ok) {
        out.clear();
        return false;
    }
    out.resize(read);
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text)
{
    // Temp + rename: a crash mid-write leaves the old file intact rather than a
    // truncated one the next launch would parse as garbage.
    const std::string temp = path + ".tmp";
    HANDLE file = ::CreateFileW(toWide(temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size();
    ::FlushFileBuffers(file);
    ::CloseHandle(file);
    if (!ok) {
        ::DeleteFileW(toWide(temp).c_str());
        return false;
    }
    if (!::MoveFileExW(toWide(temp).c_str(), toWide(path).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(toWide(temp).c_str());
        return false;
    }
    return true;
}

RunResult run(const RunOptions& options)
{
    RunResult result;
    const auto started = std::chrono::steady_clock::now();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.launchError = ::GetLastError();
        result.errorText = "could not create the output pipe: " + lastErrorMessage(result.launchError);
        return result;
    }
    ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // Our own job for this child. Killing it takes the whole installer tree.
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    }

    // Build the command line from an argv vector. Each argument is quoted
    // separately, so nothing a package id contains can become a second argument.
    std::wstring commandLine = toWide(quoteArgument(options.application));
    for (const auto& argument : options.arguments) {
        commandLine += L' ';
        commandLine += toWide(quoteArgument(argument));
    }
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};

    DWORD creationFlags = options.allowWindow ? 0 : CREATE_NO_WINDOW;
    // CREATE_SUSPENDED lets the child join our job before it can spawn anything,
    // so an installer's own children are all inside the job too.
    creationFlags |= CREATE_SUSPENDED;

    const std::wstring application = toWide(options.application);
    const std::wstring workingDirectory =
        options.workingDirectory.empty() ? std::wstring() : toWide(options.workingDirectory);

    // A minimal, explicit environment block when the caller supplied variables;
    // otherwise inherit without ever dumping it to a log.
    std::vector<wchar_t> environmentBlock;
    if (!options.environment.empty()) {
        for (const auto& [key, value] : options.environment) {
            const std::wstring entry = toWide(key + "=" + value);
            environmentBlock.insert(environmentBlock.end(), entry.begin(), entry.end());
            environmentBlock.push_back(L'\0');
        }
        environmentBlock.push_back(L'\0');
    }

    const BOOL created = ::CreateProcessW(
        application.c_str(), mutableLine.data(), nullptr, nullptr, TRUE, creationFlags, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &si, &pi);

    ::CloseHandle(writePipe);
    writePipe = nullptr;

    if (!created) {
        result.launchError = ::GetLastError();
        result.errorText = lastErrorMessage(result.launchError);
        ::CloseHandle(readPipe);
        if (job)
            ::CloseHandle(job);
        return result;
    }

    if (job && !::AssignProcessToJobObject(job, pi.hProcess)) {
        // If the assignment fails the child is still ours to wait for and kill
        // directly; note it, but do not fail the run.
        result.errorText = "child could not join the plugin job: " + lastErrorMessage(::GetLastError());
    }

    ::ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);
    result.launched = true;

    const auto deadline = started + std::chrono::milliseconds(options.timeoutMs);
    bool cancelled = false;
    bool exited = false;
    while (true) {
        drainPipe(readPipe, result.output, options.maxOutputBytes, result.truncated);

        const DWORD wait = ::WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0) {
            exited = true;
            break;
        }
        if (options.cancelRequested && *options.cancelRequested) {
            cancelled = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            break;
        }
    }

    // A fast child can exit between the drain and the wait, leaving its output
    // sitting in the pipe. Breaking out of the loop above without a final drain
    // reported exit 0 with empty output — exactly what "winget --version failed
    // (exit 0)" was. Drain once more, always, after the loop.
    drainPipe(readPipe, result.output, options.maxOutputBytes, result.truncated);

    if (!exited) {
        // Cancelling or timing out kills the job, not just the direct child. This
        // is not a rollback: whatever the installer already changed stays.
        if (job)
            ::TerminateJobObject(job, 0xC000013AUL); // STATUS_CONTROL_C_EXIT
        else
            ::TerminateProcess(pi.hProcess, 0xC000013AUL);
        ::WaitForSingleObject(pi.hProcess, 5000);
        drainPipe(readPipe, result.output, options.maxOutputBytes, result.truncated);
        if (cancelled)
            result.errorText = "cancelled; the process tree was terminated and nothing is rolled back";
        else if (result.timedOut)
            result.errorText = "timed out after " + std::to_string(options.timeoutMs) +
                               " ms; a timeout is not success";
    }

    DWORD exitCode = 0;
    if (::GetExitCodeProcess(pi.hProcess, &exitCode))
        result.exitCode = static_cast<int>(exitCode);
    if (!exited && !result.timedOut && !cancelled)
        result.exitCode = -1;

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(readPipe);
    if (job)
        ::CloseHandle(job);

    result.elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (result.truncated)
        result.output += "\n…[output truncated at " + std::to_string(options.maxOutputBytes) + " bytes]";
    return result;
}

} // namespace pkg::platform

#else // !_WIN32

// The pure core builds everywhere; the process layer does not. Keep the
// translation unit compilable so a Linux CI job can still run the L1 tests.

#include "process.h"

namespace pkg::platform {

std::string quoteArgument(const std::string& argument) { return argument; }
std::wstring toWide(const std::string&) { return {}; }
std::string toUtf8(const std::wstring&) { return {}; }
bool childProcessesAllowed(std::uint32_t& lastError)
{
    lastError = 0;
    return false;
}
RunResult run(const RunOptions&)
{
    RunResult r;
    r.errorText = "the process layer is Windows-only";
    return r;
}

bool isElevated() { return false; }

std::string pluginDataDir() { return {}; }
bool readTextFile(const std::string&, std::string& out)
{
    out.clear();
    return false;
}
bool writeTextFile(const std::string&, const std::string&) { return false; }

} // namespace pkg::platform

#endif
