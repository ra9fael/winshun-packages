// process_tests.cpp — L2: the process layer against a real child.
//
// The bug this exists for: `run()` drained the pipe, then waited, and broke out
// of the loop as soon as the child exited — leaving a fast child's output in the
// pipe. `winget --version` is exactly that shape, and the plugin reported
// "winget --version failed (exit 0)" because the text arrived after the last
// drain. A final drain after the loop is what fixed it, and only a real child
// can prove it.
#ifdef _WIN32

#include <cstdio>
#include <string>

#include <windows.h>

#include "../src/platform/process.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void section(const char* name) { std::printf("-- %s\n", name); }

std::string comspec()
{
    wchar_t buffer[512] = {};
    const DWORD n = ::GetEnvironmentVariableW(L"ComSpec", buffer, 512);
    return n > 0 && n < 512 ? pkg::platform::toUtf8(std::wstring(buffer, n)) : std::string();
}

pkg::platform::RunOptions shell(const std::string& arguments, std::uint32_t timeoutMs = 15000)
{
    pkg::platform::RunOptions options;
    options.application = comspec();
    options.arguments = {"/c", arguments};
    options.timeoutMs = timeoutMs;
    return options;
}

// A child that exits immediately: the case that lost its output.
void fastExitOutput()
{
    section("a fast child's output is not lost");
    if (comspec().empty()) {
        std::printf("skip: no ComSpec\n");
        return;
    }
    const auto result = pkg::platform::run(shell("echo hello-from-a-fast-child"));
    CHECK(result.launched);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("hello-from-a-fast-child") != std::string::npos);
}

// Output written right before exit, with no trailing newline.
void noTrailingNewline()
{
    section("output without a trailing newline");
    if (comspec().empty())
        return;
    const auto result = pkg::platform::run(shell("echo|set /p=no-newline-here"));
    CHECK(result.launched);
    CHECK(result.output.find("no-newline-here") != std::string::npos);
}

// A non-zero exit is reported as such, and its output is still captured.
void nonZeroExit()
{
    section("a non-zero exit keeps its output");
    if (comspec().empty())
        return;
    const auto result = pkg::platform::run(shell("echo failing-output & exit /b 3"));
    CHECK(result.launched);
    CHECK(result.exitCode == 3);
    CHECK(result.output.find("failing-output") != std::string::npos);
}

// A missing executable is a launch failure, not an empty success.
void missingProgram()
{
    section("a missing program is a launch failure");
    pkg::platform::RunOptions options;
    options.application = "definitely-not-a-real-program-xyz.exe";
    options.timeoutMs = 5000;
    const auto result = pkg::platform::run(options);
    CHECK(!result.launched);
    CHECK(result.launchError != 0);
    CHECK(!result.errorText.empty());
}

// Cancelling kills the tree and says it is not a rollback.
void cancelIsNotRollback()
{
    section("cancel terminates and says so");
    if (comspec().empty())
        return;
    bool cancel = true;   // set before the run, so the first poll cancels
    auto options = shell("ping -n 30 127.0.0.1 >nul", 30000);
    options.cancelRequested = &cancel;
    const auto result = pkg::platform::run(options);
    CHECK(result.launched);
    CHECK(!result.timedOut);
    CHECK(result.errorText.find("nothing is rolled back") != std::string::npos);
}

// A child that outlives its timeout is killed and marked as timed out.
void timeoutIsNotSuccess()
{
    section("a timeout is never success");
    if (comspec().empty())
        return;
    const auto result = pkg::platform::run(shell("ping -n 30 127.0.0.1 >nul", 600));
    CHECK(result.launched);
    CHECK(result.timedOut);
    CHECK(result.errorText.find("not success") != std::string::npos);
}

// Elevation is a boolean fact, whatever this machine currently is.
void elevationIsReported()
{
    section("elevation is reported as a fact");
    const bool elevated = pkg::platform::isElevated();
    std::printf("   running elevated: %s\n", elevated ? "yes" : "no");
    // Nothing to assert about the value, only that asking does not crash and is
    // stable within a process.
    CHECK(pkg::platform::isElevated() == elevated);
}

} // namespace

int main()
{
    std::printf("ra9fael.packages L2 process tests\n");
    fastExitOutput();
    noTrailingNewline();
    nonZeroExit();
    missingProgram();
    cancelIsNotRollback();
    timeoutIsNotSuccess();
    elevationIsReported();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else

#include <cstdio>

int main()
{
    std::printf("the process layer is Windows-only; nothing to test here\n");
    return 0;
}

#endif
