// probe_scoop.cpp — diagnostic: what Scoop discovery and reading really do here.
//
// Not part of the plugin. Proves the root search and a real `scoop export` run
// against this machine, including under a simulated elevated environment (no
// user profile, USERPROFILE pointing at C:\Users\Default).
//
//   packages_probe_scoop.exe            root + a real export
//   packages_probe_scoop.exe locate     root only (no process)
#include <cstdio>
#include <string>
#include <vector>

#include "../src/adapters/scoop_adapter.h"
#include "../src/platform/process.h"

int main(int argc, char** argv)
{
    const bool locateOnly = argc > 1 && std::string(argv[1]) == "locate";

    const pkg::ScoopLocation location = pkg::locateScoop();
    std::printf("--- locateScoop ---\n");
    std::printf("found      = %s\n", location.found ? "yes" : "no");
    std::printf("root       = %s\n", location.root.c_str());
    std::printf("shim       = %s\n", location.shim.c_str());
    std::printf("powershell = %s\n", location.powershell.c_str());
    std::printf("detail     = %s\n", location.detail.c_str());
    if (locateOnly || !location.found)
        return location.found ? 0 : 1;

    // The one command line the adapter would run, so the argv the plugin builds
    // can be checked against the real shim without involving the plugin.
    std::vector<std::string> argvOut;
    std::string refusal;
    const bool ok = pkg::ScoopAdapter::buildShimArgs(location.shim, {"export"}, argvOut, refusal);
    std::printf("--- argv ---\n");
    std::printf("built      = %s\n", ok ? "yes" : "no");
    if (!ok) {
        std::printf("refusal    = %s\n", refusal.c_str());
        return 1;
    }
    std::printf("%s", location.powershell.c_str());
    for (const auto& a : argvOut)
        std::printf(" %s", a.c_str());
    std::printf("\n");

    // The same argv, run: proves the adapter's command line works against this
    // machine's real Scoop, which `locateScoop` finding a shim alone does not.
    pkg::platform::RunOptions options;
    options.application = location.powershell;
    options.arguments = argvOut;
    options.timeoutMs = 60000;
    const auto result = pkg::platform::run(options);
    std::printf("--- run ---\n");
    std::printf("launched   = %s\n", result.launched ? "yes" : "no");
    std::printf("exitCode   = %d\n", result.exitCode);
    std::printf("bytes      = %zu\n", result.output.size());
    if (!result.launched)
        std::printf("errorText  = %s\n", result.errorText.c_str());

    // And the parser, which is what turns those bytes into rows.
    std::string parseFailure;
    const pkg::Snapshot snapshot =
        pkg::ScoopAdapter::fromExportJson(result.output, "probe", true, &parseFailure);
    std::printf("--- fromExportJson ---\n");
    std::printf("state      = %s\n", pkg::backendStateId(snapshot.scoop));
    std::printf("detail     = %s\n", snapshot.scoopDetail.c_str());
    std::printf("packages   = %zu\n", snapshot.packages.size());
    int shown = 0;
    for (const auto& p : snapshot.packages) {
        if (shown++ >= 8)
            break;
        std::printf("  %-20s %-12s %s\n", p.key.id.c_str(), p.installedVersion.c_str(), p.key.source.c_str());
    }
    return snapshot.packages.empty() ? 1 : 0;
}
