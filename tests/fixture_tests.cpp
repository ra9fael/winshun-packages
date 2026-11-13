// fixture_tests.cpp — L1: adapters parsed against captured backend output.
//
// The fixtures under tests/fixtures/ were captured from real backends and are
// evidence of output shape, not of behaviour: a passing parse proves this parser
// reads that output correctly, never that an install works.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/adapters/winget_adapter.h"
#include "../src/adapters/winget_table.h"
#include "../src/core/version.h"

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
    // Fixtures were written by PowerShell's Out-File, which may add a BOM.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return text;
}

using namespace pkg;

void wingetTableGeometry()
{
    section("winget table geometry");
    // Minimal table: header, rule, two rows, then a blank line and a notice.
    const std::string text =
        "Name        Id         Version  Available\n"
        "-------------------------------------------\n"
        "jq          jqlang.jq  1.7.1    1.8.2\n"
        "7-Zip       7zip.7zip  24.08\n"
        "\n"
        "2 upgrades available.\n";
    const auto table = winget::parseTable(text);
    CHECK(table.failure.ok);
    CHECK(table.rows.size() == 2);
    CHECK(table.value(0, "Name") == "jq");
    CHECK(table.value(0, "Id") == "jqlang.jq");
    CHECK(table.value(0, "Version") == "1.7.1");
    CHECK(table.value(0, "Available") == "1.8.2");
    CHECK(table.value(1, "Available").empty()); // a row without the last column
    CHECK(table.hasColumn("Available"));

    // Banner before the header is normal for the real tool.
    const std::string withBanner =
        "Windows Package Manager v1.29.380\n"
        "\n"
        "Name    Id         Version\n"
        "-------------------------\n"
        "git     Git.Git    2.47\n";
    CHECK(winget::parseTable(withBanner).failure.ok);
    CHECK(winget::parseTable(withBanner).rows.size() == 1);

    // A name wider than its column spills across the gap; it must be merged, not
    // misread as extra columns.
    const std::string spill =
        "Name                                 Id               Version\n"
        "--------------------------------------------------------------\n"
        "AMD Adaptive SoC and FPGA Tools 2025.2  AMD.SoC          2025.2\n";
    const auto spilled = winget::parseTable(spill);
    CHECK(spilled.failure.ok);
    CHECK(spilled.rows.size() == 1);
    CHECK(spilled.value(0, "Id") == "AMD.SoC");
    CHECK(spilled.value(0, "Version") == "2025.2");

    // Real ARP registry ids contain spaces and must still be read as one id.
    const std::string arp =
        "Name              Id                                    Version\n"
        "------------------------------------------------------------------\n"
        "Steam App 274190  ARP\\Machine\\X64\\Steam App 274190      Unknown\n";
    const auto arpTable = winget::parseTable(arp);
    CHECK(arpTable.failure.ok);
    CHECK(arpTable.rows.size() == 1);
    CHECK(winget::isArpId(arpTable.value(0, "Id")));
    CHECK(arpTable.value(0, "Id") == "ARP\\Machine\\X64\\Steam App 274190");

    // A row that fits no reading is counted, never guessed at.
    const std::string hopeless =
        "Name        Id         Version\n"
        "--------------------------------\n"
        "(03/30/2010 1.0.0.1)\n";
    const auto bad = winget::parseTable(hopeless);
    CHECK(bad.failure.ok);              // the table itself is fine
    CHECK(bad.unreadableRows == 1);     // the row is reported, not invented
    CHECK(bad.rows.empty());

    // No rule line: refused, not guessed.
    CHECK(!winget::parseTable("Name   Id   Version\njq   jqlang.jq   1.7.1\n").failure.ok);
    CHECK(!winget::parseTable("").failure.ok);

    // Unknown locale signature: refused.
    const std::string german =
        "Name   Kennung   Version\n"
        "------------------------\n"
        "jq     jqlang.jq 1.7.1\n";
    const auto de = winget::parseTable(german);
    CHECK(!de.failure.ok);
    CHECK(de.failure.reason.find("locale") != std::string::npos);
}

void wingetIdClassification()
{
    section("winget id classification");
    CHECK(winget::isWingetPackageId("jqlang.jq"));
    CHECK(winget::isWingetPackageId("Microsoft.PowerToys"));
    CHECK(!winget::isWingetPackageId("jq lang"));
    CHECK(!winget::isWingetPackageId(""));
    // The real tool prints registry entries this way; they are not managed.
    CHECK(winget::isArpId("ARP\\Machine\\X64\\Altium Designer Agile {12AD1C37-60DE-4E80-A2F5-D18410E73AE0}"));
    CHECK(!winget::isWingetPackageId("ARP\\Machine\\X64\\Altium Designer Agile {12AD1C37}"));
    CHECK(WingetAdapter::provenanceFor("ARP\\Machine\\X64\\x", true) == Provenance::Observed);
    CHECK(WingetAdapter::provenanceFor("jqlang.jq", true) == Provenance::BackendRecorded);
    CHECK(WingetAdapter::provenanceFor("jqlang.jq", false) == Provenance::Observed);
}

void wingetListFixture()
{
    section("winget list fixture (real v1.29.380 zh-CN)");
    const std::string text = readFile("tests/fixtures/winget/list.zh-CN.v1.29.380.txt");
    if (text.empty()) {
        std::printf("SKIP: fixture missing (run tools/capture_fixtures.ps1)\n");
        return;
    }
    std::string failure;
    const auto snapshot = WingetAdapter::fromListText(text, "2026-11-10T08:40:00Z", true, {}, &failure);
    CHECK(failure.empty());
    CHECK(snapshot.winget == BackendState::Ready);
    CHECK(snapshot.packages.size() > 50);   // the real machine has hundreds

    // Every row must have a plausible identity and a real version.
    bool sawManaged = false, sawObserved = false, sawUninstallable = false;
    for (const auto& p : snapshot.packages) {
        CHECK(!p.key.id.empty());
        CHECK(winget::isWingetPackageId(p.key.id) || winget::isArpId(p.key.id));
        CHECK(!p.installedVersion.empty());
        // `winget list` never reports scope; scope must stay Unknown so no
        // mutation is ever built with a guessed --scope.
        CHECK(p.key.scope == Scope::Unknown);
        CHECK(!p.canInstall);   // an install would have to pass a --scope
        // Removal needs no scope: `winget uninstall --id X --exact` resolves the
        // instance itself, which is why an unknown scope must not disable it.
        // An ARP id is the one exception: WinGet does not manage those.
        if (winget::isArpId(p.key.id)) {
            CHECK(!p.canUninstall);
            CHECK(!p.canUpgrade);
        } else {
            CHECK(p.canUninstall);
            if (p.canUninstall)
                sawUninstallable = true;
        }
        if (p.provenance == Provenance::BackendRecorded)
            sawManaged = true;
        if (p.provenance == Provenance::Observed)
            sawObserved = true;
    }
    CHECK(sawManaged);
    CHECK(sawObserved);        // the real output really does mix both kinds
    CHECK(sawUninstallable);   // and the real output really can be acted on

    // A specific known row: 7-Zip is a managed package with an available update.
    bool found7zip = false;
    for (const auto& p : snapshot.packages) {
        if (p.key.id == "7zip.7zip") {
            found7zip = true;
            CHECK(p.provenance == Provenance::BackendRecorded);
            CHECK(!p.installedVersion.empty());
        }
    }
    CHECK(found7zip);

    // Hiding observed rows must actually remove them.
    const auto managedOnly = WingetAdapter::fromListText(text, "t", false, {}, &failure);
    CHECK(managedOnly.packages.size() < snapshot.packages.size());
    for (const auto& p : managedOnly.packages)
        CHECK(p.provenance == Provenance::BackendRecorded);
}

void wingetUpgradeFixture()
{
    section("winget upgrade-available fixture");
    const std::string text = readFile("tests/fixtures/winget/list-upgrade-available.zh-CN.v1.29.380.txt");
    if (text.empty()) {
        std::printf("SKIP: fixture missing\n");
        return;
    }
    std::string failure;
    const auto snapshot = WingetAdapter::fromUpgradeText(text, "2026-11-10T08:40:00Z", &failure);
    CHECK(failure.empty());
    CHECK(snapshot.winget == BackendState::Ready);
    CHECK(snapshot.packages.size() >= 25);

    int orderable = 0, unorderable = 0;
    for (const auto& p : snapshot.packages) {
        CHECK(winget::isWingetPackageId(p.key.id));
        CHECK(!p.installedVersion.empty());
        CHECK(!p.availableVersion.empty());
        const Order order = compareVersionStrings(p.availableVersion, p.installedVersion);
        if (order == Order::Unknown) {
            // The real output contains rows like "< 2026.3.1" and "< nightly":
            // WinGet says an upgrade exists but the versions cannot be ordered.
            // Those must not be presented as a provable upgrade.
            ++unorderable;
            CHECK(!p.canUpgrade);
            CHECK(p.availableKind == "unknown");
        } else {
            CHECK(order == Order::Newer);
            ++orderable;
            CHECK(p.canUpgrade);
        }
    }
    CHECK(orderable > 20);
    // Evidence of the real shape: at least one unorderable row exists, and it is
    // reported as unknown rather than guessed.
    CHECK(unorderable >= 1);

    bool found7zip = false;
    for (const auto& p : snapshot.packages) {
        if (p.key.id == "7zip.7zip") {
            found7zip = true;
            CHECK(p.installedVersion == "26.05.00.0");
            CHECK(p.availableVersion == "26.06");
            CHECK(p.canUpgrade);
        }
    }
    CHECK(found7zip);
}

void wingetSearchFixture()
{
    section("winget search fixture");
    const std::string text = readFile("tests/fixtures/winget/search-jq.zh-CN.v1.29.380.txt");
    if (text.empty()) {
        std::printf("SKIP: fixture missing\n");
        return;
    }
    std::string failure;
    const auto snapshot = WingetAdapter::fromSearchText(text, "2026-11-10T08:40:00Z", "jq", &failure);
    CHECK(failure.empty());
    CHECK(!snapshot.packages.empty());
    bool foundJq = false;
    for (const auto& p : snapshot.packages) {
        CHECK(winget::isWingetPackageId(p.key.id));
        // A search hit says nothing about being installed, and install stays
        // disabled because the scope is unknown.
        CHECK(p.provenance == Provenance::None);
        CHECK(!p.canInstall);
        if (p.key.id == "jqlang.jq")
            foundJq = true;
    }
    CHECK(foundJq);
}

void wingetPinFixture()
{
    section("winget pin list fixture");
    const std::string text = readFile("tests/fixtures/winget/pin-list.zh-CN.v1.29.380.txt");
    if (text.empty()) {
        std::printf("SKIP: fixture missing\n");
        return;
    }
    std::vector<std::pair<std::string, std::string>> pins;
    std::string failure;
    const bool ok = WingetAdapter::parsePinList(text, pins, &failure);
    CHECK(ok);
    CHECK(pins.empty());   // "There are no pins configured." is not an error

    // A real pin table must be read, not ignored: ignoring a pin would let an
    // upgrade bypass it.
    const std::string withPins =
        "Name   Id          Version   Source   Pinned Version   Blocking\n"
        "-----------------------------------------------------------------\n"
        "jq     jqlang.jq   1.8.2     winget   1.7.1            pin\n";
    std::vector<std::pair<std::string, std::string>> parsed;
    CHECK(WingetAdapter::parsePinList(withPins, parsed, &failure));
    CHECK(parsed.size() == 1);
    CHECK(parsed[0].first == "jqlang.jq");
    CHECK(parsed[0].second.find("1.7.1") != std::string::npos);

    // Unrecognised output must be reported, never silently treated as "no pins".
    std::vector<std::pair<std::string, std::string>> bad;
    std::string badFailure;
    CHECK(!WingetAdapter::parsePinList("something entirely different", bad, &badFailure));
    CHECK(!badFailure.empty());
}

void wingetCommandBuilding()
{
    section("winget command building");
    WingetAdapter adapter("C:\\fake\\winget.exe");

    Package p;
    p.key = PackageKey{Manager::Winget, "winget", "jqlang.jq", Scope::User};
    p.name = "jq";
    p.availableVersion = "1.7.1";
    p.canInstall = true;
    p.metadataAt = "t";
    Plan plan = makePlan(p, Action::Install);
    applyPreflight(plan, true, "verified", "");
    CHECK(confirmPlan(plan, "t"));

    // No agreement flag unless the user explicitly accepted one.
    auto spec = adapter.buildMutation(plan, {});
    CHECK(spec.ok);
    bool hasAgreement = false;
    for (const auto& a : spec.options.arguments)
        if (a.find("--accept") != std::string::npos)
            hasAgreement = true;
    CHECK(!hasAgreement);
    // The id and scope are passed as separate argv entries, never concatenated.
    bool sawId = false, sawScope = false;
    for (std::size_t i = 0; i < spec.options.arguments.size(); ++i) {
        if (spec.options.arguments[i] == "--id" && i + 1 < spec.options.arguments.size() &&
            spec.options.arguments[i + 1] == "jqlang.jq")
            sawId = true;
        if (spec.options.arguments[i] == "user")
            sawScope = true;
    }
    CHECK(sawId);
    CHECK(sawScope);

    // Accepting only the source agreement must not add the package agreement.
    auto sourceOnly = adapter.buildMutation(plan, {"source"});
    int sourceFlags = 0, packageFlags = 0;
    for (const auto& a : sourceOnly.options.arguments) {
        if (a == "--accept-source-agreements")
            ++sourceFlags;
        if (a == "--accept-package-agreements")
            ++packageFlags;
    }
    CHECK(sourceFlags == 1);
    CHECK(packageFlags == 0);

    // A reinstall is refused: WinGet has no such verb and repair is not one.
    Plan reinstall = makePlan(p, Action::Reinstall);
    const auto refused = adapter.buildMutation(reinstall, {});
    CHECK(!refused.ok);
    CHECK(refused.refusal.find("no reinstall verb") != std::string::npos);

    // An unknown scope blocks an install (it would have to pass --scope) but not
    // an upgrade or uninstall: those match by exact id, and `winget list` never
    // reports a scope at all. Demanding one here made every real mutation fail
    // with "identity is not exactly resolvable".
    Plan unknownScopeInstall = plan;
    unknownScopeInstall.key.scope = Scope::Unknown;
    CHECK(!adapter.buildMutation(unknownScopeInstall, {}).ok);

    Package installed;
    installed.key = PackageKey{Manager::Winget, "winget", "isOfficeAI.OfficeCLI", Scope::Unknown};
    installed.name = "OfficeCLI";
    installed.installedVersion = "1.0.158";
    installed.canUninstall = true;
    Plan uninstall = makePlan(installed, Action::Uninstall);
    CHECK(uninstall.state != PlanState::Blocked);
    applyPreflight(uninstall, true, "not required", "");
    CHECK(confirmPlan(uninstall, "t"));
    const auto uninstallSpec = adapter.buildMutation(uninstall, {});
    CHECK(uninstallSpec.ok);
    bool sawUninstall = false, sawExact = false, sawUninstallScope = false;
    for (std::size_t i = 0; i < uninstallSpec.options.arguments.size(); ++i) {
        const std::string& a = uninstallSpec.options.arguments[i];
        if (a == "uninstall")
            sawUninstall = true;
        if (a == "--exact")
            sawExact = true;
        if (a == "--scope")
            sawUninstallScope = true;
    }
    CHECK(sawUninstall);
    CHECK(sawExact);
    CHECK(!sawUninstallScope);   // uninstall never passes a scope it cannot know

    // The same identity with an evidenced newer version is an upgrade, also
    // without a scope.
    Package upgradable = installed;
    upgradable.availableVersion = "1.0.200";
    upgradable.canUpgrade = true;
    Plan upgrade = makePlan(upgradable, Action::Upgrade);
    CHECK(upgrade.state != PlanState::Blocked);
    applyPreflight(upgrade, true, "backend-fetched", "");
    CHECK(confirmPlan(upgrade, "t"));
    const auto upgradeSpec = adapter.buildMutation(upgrade, {});
    CHECK(upgradeSpec.ok);
    bool sawUpgrade = false, sawUpgradeScope = false;
    for (const auto& a : upgradeSpec.options.arguments) {
        if (a == "upgrade")
            sawUpgrade = true;
        if (a == "--scope")
            sawUpgradeScope = true;
    }
    CHECK(sawUpgrade);
    CHECK(!sawUpgradeScope);
}

void scoopPinnedRefRules()
{
    section("scoop @version handling");
    // This is the pure half of the Scoop adapter's version choice.
    CHECK(buildPinnedRef("vscode", "1.94.2").has_value());
    CHECK(buildPinnedRef("vscode", "1.94.2").value() == "vscode@1.94.2");
    CHECK(!buildPinnedRef("vscode", "1.94.2 && calc").has_value());
    CHECK(!buildPinnedRef("vscode", "1;cmd").has_value());
    CHECK(!buildPinnedRef("vscode", "-o").has_value());
    const auto ref = splitPinnedRef("vscode@1.94.2");
    CHECK(ref.id == "vscode" && ref.version == "1.94.2");
}

void wingetShowFixture()
{
    section("winget show (real shape)");
    const std::string text = readFile("tests/fixtures/winget/show-docker.user.win11.txt");
    if (text.empty()) {
        std::printf("SKIP: show fixture missing\n");
        return;
    }
    const auto fields = pkg::winget::parseShowOutput(text);
    // Top-level fields, keyed lowercase without spaces.
    CHECK(fields.at("version") == "4.99.0");
    CHECK(fields.at("publisher") == "Docker Inc.");
    CHECK(fields.at("license") == "Proprietary");
    CHECK(fields.at("moniker") == "docker");
    CHECK(fields.at("publisherurl") == "https://www.docker.com/");
    // The homepage is the whole point of this parse: it is what the project-page
    // action opens instead of a search guess.
    CHECK(pkg::winget::homepageFromShow(fields) == "https://www.docker.com/products/docker-desktop");
    // A grouped field keeps its own key, so it can never shadow "homepage".
    CHECK(fields.at("installertype") == "exe");
    CHECK(fields.at("installersha256") ==
          "a5e299131c9c53d60f3f752b162078945eb5faf6eedcf0be20a214c6e6cea444");
    // A long description must survive as one value, not be lost at the wrap.
    CHECK(fields.at("description").find("Docker Desktop is an application") == 0);
    CHECK(fields.at("description").size() > 100);

    // Only an absolute http(s) URL is ever opened: the field is third-party data.
    std::map<std::string, std::string> hostile;
    hostile["homepage"] = "file:///C:/Windows/System32/calc.exe";
    CHECK(pkg::winget::homepageFromShow(hostile).empty());
    hostile["homepage"] = "javascript:alert(1)";
    CHECK(pkg::winget::homepageFromShow(hostile).empty());
    hostile["homepage"] = "https://example.com/x";
    CHECK(pkg::winget::homepageFromShow(hostile) == "https://example.com/x");
    // No homepage at all is a real state: the caller falls back, it does not open
    // an empty URL.
    CHECK(pkg::winget::homepageFromShow({}).empty());
}

void wingetVerifyReachedTarget()
{
    section("verify: exit 0 is not the verdict");
    std::string detail;
    // The DBX case: an upgrade ran cleanly and the machine still has the old
    // version. Reporting success here is what made the plugin claim a failure
    // later, out of context; the verify step has to be the one that says it.
    CHECK(!WingetAdapter::versionReached("0.6.69", "0.6.70", detail));
    CHECK(detail.find("0.6.69") != std::string::npos);
    CHECK(detail.find("0.6.70") != std::string::npos);
    CHECK(WingetAdapter::versionReached("0.6.70", "0.6.70", detail));
    // A newer version than the target still means the promise is met: the user
    // asked for at least the target, and the machine has more.
    CHECK(WingetAdapter::versionReached("0.7.0", "0.6.70", detail));
    // No recorded target is not evidence of failure; the install was verified.
    CHECK(WingetAdapter::versionReached("1.0.0", "", detail));
    // Two unorderable strings must never be claimed as success.
    CHECK(!WingetAdapter::versionReached("garbage", "also-garbage", detail));
    CHECK(detail.find("could not be compared") != std::string::npos);
    // Pre-release ordering still counts: 0.6.70-rc1 is below 0.6.70.
    CHECK(!WingetAdapter::versionReached("0.6.70-rc1", "0.6.70", detail));
}

} // namespace

int main()
{
    std::printf("ra9fael.packages L1 fixture tests\n");
    wingetTableGeometry();
    wingetIdClassification();
    wingetListFixture();
    wingetUpgradeFixture();
    wingetSearchFixture();
    wingetPinFixture();
    wingetShowFixture();
    wingetVerifyReachedTarget();
    wingetCommandBuilding();
    scoopPinnedRefRules();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
