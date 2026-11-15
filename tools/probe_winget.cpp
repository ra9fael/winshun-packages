// probe_winget.cpp — диагностика: чем именно заканчивается probe WinGet.
// Development-only tool (не входит в пакет). Повторяет путь адаптера:
// locateWinget() -> platform::run("--version").
#include <cstdio>
#include <string>

#include "../src/adapters/winget_adapter.h"
#include "../src/platform/process.h"

int main()
{
    const auto location = pkg::locateWinget();
    std::printf("found=%d alias=%d\n", location.found ? 1 : 0, location.viaAppExecutionAlias ? 1 : 0);
    std::printf("path=%s\n", location.path.c_str());
    std::printf("detail=%s\n", location.detail.c_str());
    if (!location.found)
        return 1;

    pkg::platform::RunOptions options;
    options.application = location.path;
    options.arguments = {"--version"};
    options.timeoutMs = 15000;
    const auto result = pkg::platform::run(options);
    std::printf("launched=%d launchError=%u timedOut=%d exit=%d\n",
                result.launched ? 1 : 0, result.launchError, result.timedOut ? 1 : 0, result.exitCode);
    std::printf("errorText=%s\n", result.errorText.c_str());
    std::printf("output=[%s]\n", result.output.c_str());
    return 0;
}
