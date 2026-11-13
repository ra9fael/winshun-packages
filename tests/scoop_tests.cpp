// scoop_tests.cpp — L1: Scoop parsing, argv safety and JSON reading.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/adapters/scoop_adapter.h"
#include "../src/core/json_lite.h"

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

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream out;
    out << in.rdbuf();
    std::string text = out.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return text;
}

using namespace pkg;

void jsonReader()
{
    section("json reader");
    std::string error;
    const auto root = jsonlite::parse(R"({"a": 1, "b": "x", "c": [true, null, 2.5], "d": {"e": "f"}})", error);
    CHECK(error.empty());
    CHECK(root && root->isObject());
    CHECK(root->get("a")->isNumber());
    CHECK(root->stringAt("b") == "x");
    CHECK(root->get("c")->isArray() && root->get("c")->array.size() == 3);
    CHECK(root->get("d")->stringAt("e") == "f");

    // Escapes, including the \/ that PowerShell's JSON writer emits.
    const auto escaped = jsonlite::parse(R"({"v": "\/Date(1)\/", "t": "a\tb", "u": "\u00e9"})", error);
    CHECK(error.empty());
    CHECK(escaped->stringAt("v") == "/Date(1)/");
    CHECK(escaped->stringAt("t") == "a\tb");
    CHECK(escaped->stringAt("u") == "\xC3\xA9");   // é as UTF-8

    // Malformed input is reported, never guessed.
    CHECK(jsonlite::parse("{", error) == nullptr);
    CHECK(!error.empty());
    CHECK(jsonlite::parse("{\"a\": }", error) == nullptr);
    CHECK(jsonlite::parse("[1,2", error) == nullptr);
    CHECK(jsonlite::parse("{} trailing", error) == nullptr);
    // Depth cap protects the stack.
    std::string deep;
    for (int i = 0; i < 60; ++i)
        deep += "[";
    CHECK(jsonlite::parse(deep, error) == nullptr);
}

void scoopShimArgs()
{
    section("scoop argv safety (PKG-F18)");
    std::vector<std::string> out;
    std::string refusal;

    // The fixed invocation: profile off, non-interactive, the shim, then the verb.
    CHECK(ScoopAdapter::buildShimArgs("C:\\Users\\u\\scoop\\shims\\scoop.ps1", {"export"}, out, refusal));
    CHECK(out.size() == 8);
    CHECK(out[0] == "-NoProfile");
    CHECK(out[1] == "-NonInteractive");
    CHECK(out[2] == "-NoLogo");
    CHECK(out[3] == "-ExecutionPolicy");
    CHECK(out[4] == "Bypass");
    // -Command, not -File: under -File PowerShell's formatter is not attached, so
    // a command that emits objects (scoop-search emits a table) prints only its
    // header and the rows are lost. That made every Scoop search parse to zero.
    CHECK(out[5] == "-Command");
    CHECK(out[6] == "& 'C:\\Users\\u\\scoop\\shims\\scoop.ps1'");
    CHECK(out[7] == "export");

    // Option-like arguments are refused: they would become Scoop options.
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"update", "--force"}, out, refusal));
    CHECK(!refusal.empty());
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", "-g"}, out, refusal));

    // Shell / PowerShell metacharacters are refused.
    for (const char* hostile : {"a;calc", "a&&b", "a|b", "a`b", "a$(b)", "a${b}", "a&&calc",
                                "$(calc.exe)", "a>b", "a<b", "a&b", "a%b%", "a`nStart"}) {
        CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", hostile}, out, refusal));
    }
    // Whitespace is refused (it would split into two arguments).
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", "two words"}, out, refusal));
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", ""}, out, refusal));
    // A URL is never a package id.
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", "https://evil/x.ps1"}, out, refusal));
    CHECK(!ScoopAdapter::buildShimArgs("s.ps1", {"install", "http://evil"}, out, refusal));

    // Legitimate ids pass.
    for (const char* ok : {"git", "nodejs-lts", "vscode-insiders", "7zip", "scoop-search"}) {
        CHECK(ScoopAdapter::buildShimArgs("s.ps1", {"install", ok}, out, refusal));
    }
}

void scoopExport()
{
    section("scoop export fixture (real)");
    const std::string text = readFile("tests/fixtures/scoop/export.user.win11.txt");
    if (text.empty()) {
        std::printf("SKIP: fixture missing\n");
        return;
    }
    std::string failure;
    const auto snapshot = ScoopAdapter::fromExportJson(text, "2026-11-10T08:40:00Z", true, &failure);
    CHECK(failure.empty());
    CHECK(snapshot.scoop == BackendState::Ready);
    CHECK(!snapshot.packages.empty());

    for (const auto& p : snapshot.packages) {
        CHECK(!p.key.id.empty());
        CHECK(isSafePackageId(p.key.id));
        CHECK(!p.installedVersion.empty());
        CHECK(p.key.manager == Manager::Scoop);
        // Scoop export is Scoop's own record, so it is always backend-recorded.
        CHECK(p.provenance == Provenance::BackendRecorded);
        // The bucket must be recorded, otherwise the identity is incomplete.
        CHECK(!p.key.source.empty());
    }

    // A known real entry.
    bool found = false;
    for (const auto& p : snapshot.packages) {
        if (p.key.id == "scoop-search") {
            found = true;
            CHECK(p.key.source == "main");
            CHECK(!p.installedVersion.empty());
        }
    }
    CHECK(found);

    // Unparsable output is reported, never silently treated as "nothing installed".
    std::string badFailure;
    const auto bad = ScoopAdapter::fromExportJson("not json at all", "t", true, &badFailure);
    CHECK(!badFailure.empty());
    CHECK(bad.scoop == BackendState::Unsupported);
    CHECK(bad.packages.empty());
}

void scoopSearch()
{
    section("scoop search text (real shape)");
    const std::string real = readFile("tests/fixtures/scoop/scoop-search-vim.user.win11.txt");
    std::string failure;
    if (!real.empty()) {
        const auto parsed = ScoopAdapter::fromSearchText(real, "t", "vim", &failure);
        CHECK(parsed.scoop == BackendState::Ready);
        CHECK(!parsed.packages.empty());
        bool sawFvim = false, sawNeovim = false;
        for (const auto& p : parsed.packages) {
            // The bucket comes from the section header, and every row is
            // installable because its bucket is known.
            CHECK(!p.key.source.empty());
            CHECK(p.canInstall);
            CHECK(p.provenance == Provenance::None);
            CHECK(p.availableVersion.find(' ') == std::string::npos);
            if (p.key.id == "fvim") {
                sawFvim = true;
                CHECK(p.key.source == "extras");
                CHECK(p.availableVersion == "0.3.549");
            }
            if (p.key.id == "neovim-qt") {
                sawNeovim = true;
                CHECK(p.key.source == "extras");
            }
        }
        CHECK(sawFvim);
        CHECK(sawNeovim);
        // The "--> includes 'vim.exe'" comment must not become a bucket.
        for (const auto& p : parsed.packages)
            CHECK(p.key.source.find("includes") == std::string::npos);
    } else {
        std::printf("SKIP: search fixture missing\n");
    }

    // The empty-index answer is a real state, not an error: it must stay empty.
    const std::string emptyIndex = readFile("tests/fixtures/scoop/search-git-empty-index.user.win11.txt");
    if (!emptyIndex.empty()) {
        const auto empty = ScoopAdapter::fromSearchText(emptyIndex, "t", "git", &failure);
        CHECK(empty.scoop == BackendState::Ready);
        CHECK(empty.packages.empty());
    }

    // A row without a section bucket has an incomplete identity and cannot install.
    const std::string noBucket = "    orphan (1.0.0)\n";
    const auto orphan = ScoopAdapter::fromSearchText(noBucket, "t", "orphan", &failure);
    CHECK(orphan.packages.size() == 1);
    CHECK(!orphan.packages[0].canInstall);
    CHECK(!orphan.packages[0].limits.empty());

    // Scoop 0.6.0 replaced the grouped sections with a table. The old parser
    // recognised only the section shape, so on 0.6.0 every search produced zero
    // rows and Discover showed nothing for Scoop at all.
    section("scoop search table (0.6.0 shape)");
    const std::string table = readFile("tests/fixtures/scoop/search-vim-table.scoop060.win11.txt");
    if (!table.empty()) {
        CHECK(ScoopAdapter::looksLikeSearchTable(table));
        const auto parsed = ScoopAdapter::fromSearchText(table, "t", "vim", &failure);
        CHECK(parsed.scoop == BackendState::Ready);
        CHECK(parsed.packages.size() == 14);
        bool sawVim = false, sawFvim = false, sawNightly = false;
        for (const auto& p : parsed.packages) {
            CHECK(!p.key.source.empty());
            CHECK(p.canInstall);
            CHECK(p.key.manager == Manager::Scoop);
            // The version column must not swallow the bucket, and the bucket must
            // not swallow the trailing Binaries column.
            CHECK(p.availableVersion.find(' ') == std::string::npos);
            CHECK(p.key.source.find(' ') == std::string::npos);
            if (p.key.id == "vim") {
                sawVim = true;
                CHECK(p.key.source == "main");
                CHECK(p.availableVersion == "9.2.1320");
            }
            if (p.key.id == "fvim") {
                sawFvim = true;
                CHECK(p.key.source == "extras");
                CHECK(p.availableVersion == "0.3.549");
            }
            if (p.key.id == "vim-tux") {
                sawNightly = true;
                CHECK(p.key.source == "versions");
                CHECK(p.availableVersion == "9.2.0699_20260622");
            }
        }
        CHECK(sawVim);
        CHECK(sawFvim);
        CHECK(sawNightly);
        // A row whose Binaries column is populated must still parse: the last
        // column runs to end of line and is not part of the bucket.
        for (const auto& p : parsed.packages)
            CHECK(p.key.source != "main   vim.exe");
    } else {
        std::printf("SKIP: 0.6.0 table fixture missing\n");
    }

    // The section shape must keep working: a user on 0.5.x still gets rows.
    CHECK(!ScoopAdapter::looksLikeSearchTable(
        "'main' bucket:\n    vim (9.1.0)\n"));
}

void scoopHeld()
{
    section("scoop held apps");
    CHECK(ScoopAdapter::parseHeldApps("").empty());
    CHECK(ScoopAdapter::parseHeldApps("WARN  buckets out of date").empty());
    const auto held = ScoopAdapter::parseHeldApps("WARN  x\nHeld apps: git, vscode\nother line\n");
    CHECK(held.size() == 2);
    CHECK(held[0] == "git");
    CHECK(held[1] == "vscode");
}

void scoopCommands()
{
    section("scoop command building");
    // Constructing the adapter with explicit paths avoids probing the machine.
    ScoopAdapter adapter("C:\\www\\scoop", "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");

    Package p;
    p.key = PackageKey{Manager::Scoop, "main", "git", Scope::User};
    p.name = "git";
    p.installedVersion = "2.47.0";
    p.availableVersion = "2.47.1";
    p.canUpgrade = true;
    p.metadataAt = "t";
    Plan plan = makePlan(p, Action::Upgrade);
    applyPreflight(plan, true, "verified", "");
    CHECK(confirmPlan(plan, "t"));

    const auto spec = adapter.buildMutation(plan, {});
    CHECK(spec.ok);
    // upgrade must be `update <app>`, never a bare `update` that syncs buckets.
    bool sawUpdate = false, sawApp = false;
    for (const auto& a : spec.options.arguments) {
        if (a == "update")
            sawUpdate = true;
        if (a == "git")
            sawApp = true;
    }
    CHECK(sawUpdate);
    CHECK(sawApp);
    // The bare form would sync Scoop and every bucket; it must not appear.
    int updateCount = 0;
    for (const auto& a : spec.options.arguments)
        if (a == "update")
            ++updateCount;
    CHECK(updateCount == 1);

    // A reinstall is refused: force install and reset are both wrong.
    Plan reinstall = makePlan(p, Action::Reinstall);
    const auto refused = adapter.buildMutation(reinstall, {});
    CHECK(!refused.ok);
    CHECK(refused.refusal.find("explicit uninstall") != std::string::npos);

    // A machine-scope plan is refused: this plugin never elevates.
    Plan machine = plan;
    machine.key.scope = Scope::Machine;
    CHECK(!adapter.buildMutation(machine, {}).ok);

    // Uninstall passes the exact id.
    Package installed = p;
    installed.canUninstall = true;
    Plan uninstall = makePlan(installed, Action::Uninstall);
    applyPreflight(uninstall, true, "verified", "");
    CHECK(confirmPlan(uninstall, "t"));
    const auto un = adapter.buildMutation(uninstall, {});
    CHECK(un.ok);
    bool sawUninstall = false;
    for (const auto& a : un.options.arguments)
        if (a == "uninstall")
            sawUninstall = true;
    CHECK(sawUninstall);
}

void scoopRootResolution()
{
    section("scoop root resolution (PKG-F19)");

    using Env = std::vector<std::pair<std::string, std::string>>;

    // An explicit SCOOP always wins: Scoop itself reads it, so honouring it can
    // never select a different installation than the one the user runs.
    {
        const ScoopRootChoice c = resolveScoopRoot(
            Env{{"SCOOP", "D:\\Scoop"}, {"USERPROFILE", "C:\\Users\\me"}, {"ProgramData", "C:\\ProgramData"}}, {});
        CHECK(c.root == "D:\\Scoop");
        CHECK(std::string(c.source) == "SCOOP");
    }
    // The user's profile is the documented default.
    {
        const ScoopRootChoice c =
            resolveScoopRoot(Env{{"USERPROFILE", "C:\\Users\\me"}, {"ProgramData", "C:\\ProgramData"}}, {});
        CHECK(c.root == "C:\\Users\\me\\scoop");
        CHECK(std::string(c.source) == "profile");
    }
    // This is the reported bug: an elevated plugin gets no user profile, and the
    // environment it does get may point at C:\Users\Default. With the profile
    // gone, the shim on PATH is what still identifies the installation.
    {
        const ScoopRootChoice c = resolveScoopRoot(
            Env{{"ProgramData", "C:\\ProgramData"}}, "C:\\Users\\me\\scoop\\shims\\scoop.ps1");
        CHECK(c.root == "C:\\Users\\me\\scoop");
        CHECK(std::string(c.source) == "path-shim");
    }
    // The shim path is accepted with forward slashes too (PATH entries can), and
    // the root is reported in the platform's own separator form.
    {
        const ScoopRootChoice c = resolveScoopRoot(Env{}, "C:/Users/me/scoop/shims/scoop.ps1");
        CHECK(c.root == "C:\\Users\\me\\scoop");
        CHECK(std::string(c.source) == "path-shim");
    }
    // A machine-wide install is the last resort that needs no profile.
    {
        const ScoopRootChoice c = resolveScoopRoot(Env{{"ProgramData", "C:\\ProgramData"}}, {});
        CHECK(c.root == "C:\\ProgramData\\scoop");
        CHECK(std::string(c.source) == "common");
    }
    // Nothing known: reported, never guessed into a path that looks valid.
    {
        const ScoopRootChoice c = resolveScoopRoot(Env{}, {});
        CHECK(c.root.empty());
        CHECK(std::string(c.source) == "none");
        CHECK(!c.detail.empty());
    }
    // A shim path without a \shims\ segment is not a Scoop root.
    {
        const ScoopRootChoice c = resolveScoopRoot(Env{}, "C:\\tools\\scoop.ps1");
        CHECK(c.root != "C:\\tools");
    }
    // Case: the resolver copies the value verbatim, so a path with mixed case is
    // preserved for the existence check that follows.
    {
        const ScoopRootChoice c = resolveScoopRoot(Env{{"SCOOP", "C:\\Users\\Me\\Scoop"}}, {});
        CHECK(c.root == "C:\\Users\\Me\\Scoop");
    }
}

} // namespace

int main()
{
    std::printf("ra9fael.packages L1 Scoop tests\n");
    jsonReader();
    scoopShimArgs();
    scoopExport();
    scoopSearch();
    scoopHeld();
    scoopCommands();
    scoopRootResolution();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
