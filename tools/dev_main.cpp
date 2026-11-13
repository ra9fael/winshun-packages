// dev_main.cpp — the development harness: opens the real window, no plugin host.
//
// The shipped plugin is a resident process driven over stdio by WinShun, and every
// change therefore costs a build, a package, a reinstall and a host restart — one
// data point per round. None of the bugs found so far were of the kind that flow
// would catch: the elevated environment's USERPROFILE, `winget list` printing no
// scope, a hidden item reserving layout width, a theme token that does not exist.
// They are all runtime facts, and this harness is what makes a runtime fact one
// command away.
//
// It links the same Controller, models, adapters, platform layer and QML scene as
// the plugin. The only differences are the entry point (no protocol loop, no
// manifest) and the switches below. It is not shipped and not part of the package.
//
// Usage (see run-dev.ps1):
//   packages_dev.exe [--style f|brand|tactile|liquid] [--theme light|dark|system]
//                    [--size dense|compact|standard|roomy] [--locale zh|en]
//                    [--tab installed|discover|updates|tasks]
//                    [--qml <dir>] [--watch] [--fake] [--data-dir isolated|real]
//                    [--simulate-elevated] [--dry-run] [--dump-plan <id>]
//                    [--shot <png>] [--dump-tree] [--log-level quiet|info|debug]
//                    [--quit-after <seconds>]
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include "src/adapters/scoop_adapter.h"
#include "src/adapters/winget_adapter.h"
#include "src/core/model.h"
#include "src/core/plan.h"
#include "src/core/queue.h"
#include "src/ui/models.h"
#include "src/ui/theme_resolver.h"
#include "src/ui/window.h"
#include "look_fixture.h"

namespace {

// ---- command line ------------------------------------------------------------

struct Options {
    QString style = QStringLiteral("fluent");
    QString theme = QStringLiteral("system");
    QString size = QStringLiteral("standard");
    QString locale = QStringLiteral("zh");
    QString tab;
    QString reviewId;
    QString searchText;
    QString qmlDir;
    QString shotPath;
    QString dumpPlanId;
    QString logLevel = QStringLiteral("info");
    bool watch = false;
    bool fake = false;
    bool realDataDir = false;
    bool simulateElevated = false;
    bool dryRun = false;
    bool dumpTree = false;
    bool lint = false;
    bool forceRefresh = false;
    bool tabCycleCheck = false;
    // Selects (not checks) one row before the capture. The details pane is the
    // only surface that shows the metadata lookup, and it needs a selected row.
    int focusRow = -1;
    bool showInfoWindow = false;
    int showInfoRow = -1;
    // Holds the "searching" state for N ms so the in-box spinner can be captured.
    // A fake run deliberately starts no backend, so the spinner otherwise never
    // appears and the one visual it exists for could not be checked.
    int holdSearchingMs = 0;
    int shotDelayMs = 700;
    int expectRows = -1;
    QString selectRows;
    QString discoverFilter;
    QString installedFilter;
    QString detailAction;
    int detailRow = -1;
    double quitAfter = 0.0;
};

void usage()
{
    std::printf(
        "packages_dev — development harness for ra9fael.packages\n"
        "\n"
        "  --style   fluent|brand|tactile|liquid   (default fluent)\n"
        "  --theme   light|dark|system             (default system)\n"
        "  --size    dense|compact|standard|roomy  (default standard)\n"
        "  --locale  zh|en                          (default zh)\n"
        "  --tab     installed|discover|updates|tasks\n"
        "  --qml DIR          load QML from DIR instead of the qrc resource\n"
        "  --watch            re-create the scene when a .qml file changes\n"
        "  --fake             use a deterministic snapshot instead of the backends\n"
        "  --data-dir real    read and write the plugin's real cache\n"
        "                     (default: ra9fael.packages.dev.data)\n"
        "  --simulate-elevated  set USERPROFILE to C:\\Users\\Default and drop every\n"
        "                     scoop entry from PATH, for this process only\n"
        "  --dry-run          drive plan/queue/executor and print every argv without\n"
        "                     starting a mutating backend process\n"
        "  --dump-plan ID     print the argv the plan for ID would build, then exit\n"
        "  --shot FILE        write a PNG of the window and exit\n"
        "  --shot-delay MS    wait MS before the capture (real backends need seconds)\n"
        "  --dump-tree        print the item tree geometry (layout regressions)\n"
        "  --lint             fail (exit 1) if the layout has a known defect\n"
        "  --refresh          scan even when the disk cache is fresh\n"
    "  --tab-cycle-check  check that Discover text and rows survive tab switching\n"
        "                     (requires --fake and --search)\n"
        "  --focus-row N      select row N without checking it (shows the metadata pane)\n"
        "  --show-info-window open the full metadata window for the focused row\n"
        "  --hold-searching MS hold the search spinner for MS so it can be captured\n"
        "  --review INDEX     select row INDEX, open the review page, and continue\n"
        "                     (add :uninstall to also choose the uninstall action)\n"
        "  --search TEXT      type TEXT into the Discover search box and run the query\n"
        "  --installed-filter TEXT  type TEXT into the Installed tab's filter box\n"
        "  --select ROWS      check the given comma-separated row indexes (0,2,5)\n"
        "  --discover-filter  new|all|installed (Discover's result filter)\n"
        "  --detail-action A  ROW:ACT  click the detail pane action for row ROW, as a\n"
        "                     user would: select the row (no checkbox) then act\n"
        "  --expect-rows N    fail (exit 1) unless the model holds exactly N rows\n"
        "                     right after the window opens, before any backend reply\n"
        "  --log-level quiet|info|debug\n"
        "  --quit-after SEC   exit after SEC seconds (smoke checks)\n");
}

QString valueOf(int argc, char** argv, int& i, const char* flag){
    if (i + 1 >= argc) {
        std::printf("error: %s needs a value\n", flag);
        std::exit(2);
    }
    return QString::fromLocal8Bit(argv[++i]);
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {            usage();
            return false;
        } else if (arg == QStringLiteral("--style")) {
            options.style = valueOf(argc, argv, i, "--style");
        } else if (arg == QStringLiteral("--theme")) {
            options.theme = valueOf(argc, argv, i, "--theme");
        } else if (arg == QStringLiteral("--size")) {
            options.size = valueOf(argc, argv, i, "--size");
        } else if (arg == QStringLiteral("--locale")) {
            options.locale = valueOf(argc, argv, i, "--locale");
        } else if (arg == QStringLiteral("--tab")) {
            options.tab = valueOf(argc, argv, i, "--tab");
        } else if (arg == QStringLiteral("--review")) {
            options.reviewId = valueOf(argc, argv, i, "--review");
        } else if (arg == QStringLiteral("--search")) {
            options.searchText = valueOf(argc, argv, i, "--search");
        } else if (arg == QStringLiteral("--select")) {
            options.selectRows = valueOf(argc, argv, i, "--select");
        } else if (arg == QStringLiteral("--discover-filter")) {
            options.discoverFilter = valueOf(argc, argv, i, "--discover-filter");
        } else if (arg == QStringLiteral("--installed-filter")) {
            options.installedFilter = valueOf(argc, argv, i, "--installed-filter");
        } else if (arg == QStringLiteral("--detail-action")) {
            const QString spec = valueOf(argc, argv, i, "--detail-action");
            const int colon = spec.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                options.detailRow = spec.left(colon).toInt();
                options.detailAction = spec.mid(colon + 1);
            }
        } else if (arg == QStringLiteral("--qml")) {
            options.qmlDir = valueOf(argc, argv, i, "--qml");
        } else if (arg == QStringLiteral("--watch")) {
            options.watch = true;
        } else if (arg == QStringLiteral("--fake")) {
            options.fake = true;
        } else if (arg == QStringLiteral("--data-dir")) {
            options.realDataDir = valueOf(argc, argv, i, "--data-dir") == QStringLiteral("real");
        } else if (arg == QStringLiteral("--simulate-elevated")) {
            options.simulateElevated = true;
        } else if (arg == QStringLiteral("--dry-run")) {
            options.dryRun = true;
        } else if (arg == QStringLiteral("--dump-plan")) {
            options.dumpPlanId = valueOf(argc, argv, i, "--dump-plan");
        } else if (arg == QStringLiteral("--shot")) {
            options.shotPath = valueOf(argc, argv, i, "--shot");
        } else if (arg == QStringLiteral("--shot-delay")) {
            options.shotDelayMs = valueOf(argc, argv, i, "--shot-delay").toInt();
        } else if (arg == QStringLiteral("--expect-rows")) {
            options.expectRows = valueOf(argc, argv, i, "--expect-rows").toInt();
        } else if (arg == QStringLiteral("--dump-tree")) {
            options.dumpTree = true;
        } else if (arg == QStringLiteral("--lint")) {
            options.lint = true;
        } else if (arg == QStringLiteral("--refresh")) {
            options.forceRefresh = true;
        } else if (arg == QStringLiteral("--tab-cycle-check")) {
            options.tabCycleCheck = true;
        } else if (arg == QStringLiteral("--focus-row")) {
            options.focusRow = valueOf(argc, argv, i, "--focus-row").toInt();
        } else if (arg == QStringLiteral("--show-info-window")) {
            options.showInfoWindow = true;
        } else if (arg == QStringLiteral("--hold-searching")) {
            options.holdSearchingMs = valueOf(argc, argv, i, "--hold-searching").toInt();
        } else if (arg == QStringLiteral("--log-level")) {
            options.logLevel = valueOf(argc, argv, i, "--log-level");
        } else if (arg == QStringLiteral("--quit-after")) {
            options.quitAfter = valueOf(argc, argv, i, "--quit-after").toDouble();
        } else {
            std::printf("error: unknown argument %s\n", argv[i]);
            usage();
            return false;
        }
    }
    return true;
}

// ---- logging -----------------------------------------------------------------

enum class LogLevel { Quiet, Info, Debug };

LogLevel g_logLevel = LogLevel::Info;

// The exit code that asks the supervisor script for a restart (a .qml change). It
// is outside the range a crash or a normal failure uses, so `run-dev.ps1` can tell
// "reload" apart from "the user closed the window" and from "it broke".
constexpr int kRestartRequested = 75;

void say(const char* tag, const QString& message)
{
    if (g_logLevel == LogLevel::Quiet)
        return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    std::fprintf(stderr, "[%s] %-5s %s\n", stamp.toUtf8().constData(), tag, message.toUtf8().constData());
}

void debug(const QString& message)
{
    if (g_logLevel == LogLevel::Debug)
        say("debug", message);
}

// ---- environment simulation --------------------------------------------------

// The host starts an elevated plugin with a clean environment: system variables
// plus USERPROFILE filled from FOLDERID_Profile, which is C:\Users\Default when no
// user profile is loaded. Reproducing that here is what makes the class of bug we
// already hit once reachable in one command.
void simulateElevatedEnvironment()
{
    qputenv("USERPROFILE", "C:\\Users\\Default");
    qunsetenv("SCOOP");
    const QStringList path = qEnvironmentVariable("PATH").split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList kept;
    for (const QString& entry : path) {
        if (!entry.contains(QStringLiteral("scoop"), Qt::CaseInsensitive))
            kept.append(entry);
    }
    qputenv("PATH", kept.join(QLatin1Char(';')).toUtf8());
    say("env", QStringLiteral("simulated elevated: USERPROFILE=C:\\Users\\Default, %1 PATH entries dropped")
                   .arg(path.size() - kept.size()));
}

// The plugin's cache lives under the real plugin data folder. The harness defaults
// to its own folder so a development scan can never overwrite what the installed
// plugin will read.
void useIsolatedDataDir()
{
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    const QString dir = base + QStringLiteral("\\WinShun\\plugins\\ra9fael.packages.dev.data");
    QDir().mkpath(dir);
    qputenv("PACKAGES_DEV_DATA_DIR", dir.toUtf8());
    say("env", QStringLiteral("data dir: %1").arg(dir));
}

// ---- deterministic snapshot --------------------------------------------------

pkg::Package fakeRow(pkg::Manager manager, const char* source, const char* id, pkg::Scope scope,
                     const char* installed, const char* available, pkg::Provenance provenance)
{
    pkg::Package p;
    p.key.manager = manager;
    p.key.source = source;
    p.key.id = id;
    p.key.scope = scope;
    p.name = id;
    p.installedVersion = installed;
    p.availableVersion = available;
    p.provenance = provenance;
    p.arch = "x64";
    p.dependencies = "none";
    p.material = "not inspected";
    p.metadataAt = "2026-11-12T00:00:00Z";
    const bool installedNow = !std::string(installed).empty();
    p.canInstall = !installedNow && scope != pkg::Scope::Unknown;
    p.canUninstall = installedNow;
    p.canUpgrade = installedNow && !std::string(available).empty();
    return p;
}

// Search results for --fake. A fake run never starts a backend, so without this a
// --search screenshot showed the idle state; these rows have the shape the real
// backends return (winget has no installed version, scoop has a bucket), which is
// what the Discover actions depend on.
std::vector<pkg::Package> fakeSearchResults()
{
    std::vector<pkg::Package> rows;
    auto searchRow = [](pkg::Manager manager, const char* bucket, const char* id, const char* version) {
        pkg::Package p;
        p.key.manager = manager;
        p.key.source = bucket;
        p.key.id = id;
        p.key.scope = pkg::Scope::Unknown;
        p.name = id;
        p.availableVersion = version;
        p.availableKind = "version";
        p.arch = "unknown";
        p.provenance = pkg::Provenance::BackendRecorded;
        p.metadataAt = "2026-11-13T12:00:00Z";
        p.dependencies = "unknown";
        p.material = std::string("bucket ") + bucket;
        p.canInstall = true;
        return p;
    };
    rows.push_back(searchRow(pkg::Manager::Winget, "winget", "vim.vim", "9.2.1310"));
    rows.push_back(searchRow(pkg::Manager::Winget, "winget", "neovim.neovim", "0.12.6"));
    rows.push_back(searchRow(pkg::Manager::Scoop, "main", "vim", "9.2.1320"));
    rows.push_back(searchRow(pkg::Manager::Scoop, "extras", "fvim", "0.3.549"));
    rows.push_back(searchRow(pkg::Manager::Scoop, "versions", "vim-nightly", "9.2.1332"));
    return rows;
}

std::vector<pkg::Task> fakeTasks()
{    std::vector<pkg::Package> rows;
    using namespace pkg;
    std::vector<Task> tasks;
    auto make = [](unsigned long long id, Manager manager, const char* target, const char* installed,
                   const char* available, TaskState state, Action action) {
        Task t;
        t.id = id;
        t.plan.key.manager = manager;
        t.plan.key.id = target;
        t.plan.key.scope = Scope::User;
        t.plan.key.source = manager == Manager::Scoop ? "main" : "winget";
        t.plan.facts.installedVersion = installed;
        t.plan.facts.targetVersion = available;
        t.plan.action = action;
        t.state = state;
        t.phase = "acquire";
        return t;
    };
    tasks.push_back(make(1, Manager::Winget, "git", "2.47.0", "2.47.1", TaskState::Running, Action::Upgrade));
    tasks.push_back(make(2, Manager::Scoop, "nodejs-lts", "22.9.0", "22.11.0", TaskState::Queued, Action::Upgrade));
    tasks.push_back(make(3, Manager::Scoop, "aria2", "1.37.0-1", "1.37.0-1", TaskState::Succeeded, Action::Upgrade));
    tasks.push_back(make(4, Manager::Winget, "Microsoft.PowerToys", "0.86.0", "0.87.1", TaskState::Failed,
                         Action::Upgrade));
    tasks.back().detail = "installer failed with exit code 0x8A15002B";
    // Give the fake rows a per-step log so the log panel and its copy button can be
    // checked without a real mutation: a fake run never executes anything, and an
    // empty log made both look absent.
    for (auto& t : tasks) {
        t.log.push_back({"14:32:07", "plan", "ok",
                         std::string("plan: ") + actionId(t.plan.action) + " " + t.plan.key.id, -1});
        t.log.push_back({"14:32:08", t.phase, "info",
                         std::string("argv: winget ") + actionId(t.plan.action) + " --id " + t.plan.key.id +
                             " --exact", -1});
        if (t.state == TaskState::Failed) {
            t.log.push_back({"14:32:09", "verify", "warning", "the package was not found after the run", 0});
            t.log.push_back({"14:32:09", "finish", "error", t.detail, 0});
        } else if (t.state == TaskState::Succeeded) {
            t.log.push_back({"14:32:09", "verify", "ok", "installed at 1.37.0-1", 0});
            t.log.push_back({"14:32:09", "finish", "ok", "succeeded", 0});
        }
    }
    return tasks;
}

std::vector<pkg::Package> fakeInventory()
{    std::vector<pkg::Package> rows;
    rows.push_back(fakeRow(pkg::Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", pkg::Scope::Unknown, "14.1.0",
                           "14.1.2", pkg::Provenance::BackendRecorded));
    rows.push_back(fakeRow(pkg::Manager::Scoop, "main", "git", pkg::Scope::User, "2.47.0", "2.47.1",
                           pkg::Provenance::BackendRecorded));
    rows.push_back(fakeRow(pkg::Manager::Winget, "winget", "Microsoft.PowerToys", pkg::Scope::Unknown, "0.86.0",
                           "0.87.1", pkg::Provenance::BackendRecorded));
    rows.push_back(fakeRow(pkg::Manager::Scoop, "extras", "vscode", pkg::Scope::User, "1.94.2", "",
                           pkg::Provenance::BackendRecorded));
    // Mirrors the real machine report: Neovim is installed from WinGet and also
    // appears in search results. Discover must decorate the candidate with this
    // installed version rather than presenting it as a fresh install candidate.
    rows.push_back(fakeRow(pkg::Manager::Winget, "winget", "Neovim.Neovim", pkg::Scope::Unknown, "0.12.6", "",
                           pkg::Provenance::BackendRecorded));
    // Exactly the reported shape: an installed row whose scope winget does not
    // report, and which therefore must still be selectable and removable.
    pkg::Package unknownScope = fakeRow(pkg::Manager::Winget, "winget", "isOfficeAI.OfficeCLI", pkg::Scope::Unknown,
                                        "1.0.158", "", pkg::Provenance::BackendRecorded);
    unknownScope.limits = "scope is not reported by `winget list`; only install needs one, so upgrade and "
                          "uninstall use the exact id";
    rows.push_back(unknownScope);
    rows.push_back(fakeRow(pkg::Manager::Scoop, "main", "nodejs-lts", pkg::Scope::User, "22.9.0", "22.11.0",
                           pkg::Provenance::Matched));
    return rows;
}

// ---- --dump-plan -------------------------------------------------------------

// Prints the argv the adapter would build for the current inventory, without
// starting anything. This is the check that answers "would uninstall really send
// `--id X --exact` and no `--scope`" outside a unit test.
int dumpPlan(const QString& id, bool elevated)
{
    const auto rows = fakeInventory();
    for (const auto& p : rows) {
        if (p.key.id != id.toStdString())
            continue;
        for (pkg::Action action : {pkg::Action::Uninstall, pkg::Action::Upgrade, pkg::Action::Install}) {
            pkg::Plan plan = pkg::makePlan(p, action, elevated);
            std::printf("action %-9s state %s", pkg::actionId(action), pkg::planStateId(plan.state));
            if (!plan.blockedReason.empty())
                std::printf("  blocked: %s", plan.blockedReason.c_str());
            std::printf("\n");
            if (plan.state == pkg::PlanState::Blocked)
                continue;
            pkg::applyPreflight(plan, true, action == pkg::Action::Uninstall ? "not required" : "backend-fetched", "");
            if (!pkg::confirmPlan(plan, "dev"))
                continue;
            pkg::CommandSpec spec;
            // A local adapter instance: the shared ones are inside window.cpp and
            // this tool must not reach into another translation unit's statics.
            if (plan.key.manager == pkg::Manager::Scoop) {
                pkg::ScoopAdapter adapter;
                spec = adapter.buildMutation(plan, {});
            } else {
                pkg::WingetAdapter adapter;
                spec = adapter.buildMutation(plan, {});
            }
            if (!spec.ok) {
                std::printf("  refused: %s\n", spec.refusal.c_str());
                continue;
            }
            std::printf("  %s", spec.options.application.c_str());
            for (const auto& a : spec.options.arguments)
                std::printf(" %s", a.c_str());
            std::printf("\n");
        }
        return 0;
    }
    std::printf("error: no row with id %s in the fake inventory\n", id.toUtf8().constData());
    return 2;
}

// ---- window lifetime ---------------------------------------------------------

// One scene instance, and the teardown order that makes a rebuild safe.
//
// `initializeUi` parents the QQmlEngine to the controller and hands the created
// Theme object to the controller as a raw pointer. Deleting the window destroys the
// engine and with it the Theme, so the controller would then hold a dangling
// pointer — and the next applyAppearance would write through it and crash (access
// violation 0xC0000005). Clearing the controller's theme reference first is what
// makes --watch's rebuild safe.
struct Scene {
    QQuickWindow* window = nullptr;
    pkg::ui::Controller* controller = nullptr;

    void reset()
    {
        if (controller)
            controller->setThemeObject(nullptr);
        if (window)
            delete window;
        window = nullptr;
    }

    ~Scene() { reset(); }
};

// ---- layout lint -------------------------------------------------------------
//
// The geometry dump already iterates every item, so the same walk can check the
// layout instead of leaving it to someone squinting at a screenshot. Only the
// rules that correspond to bugs actually seen are encoded: a checker that guesses
// would either miss the real cases or report noise until it is ignored.
//
// QtQuick's item types are private headers, so nothing here casts to them; the
// facts a rule needs are read through QObject properties/metaobject instead, which
// is also what keeps the tool compiling against the public Quick API only.
struct LintIssue {
    QString rule;
    QString detail;
};

QString classNameOf(QQuickItem* item)
{
    return QString::fromLatin1(item->metaObject()->className());
}

bool isRectangle(QQuickItem* item)
{
    const QString name = classNameOf(item);
    if (name.contains(QStringLiteral("Rectangle")))
        return true;
    // A QML file whose root is a Rectangle is registered under the file's own type
    // name (TasksPage_QMLTYPE_5), so the class name does not say "Rectangle". The
    // properties do: a Rectangle is the only item with a writable `color`.
    const QMetaObject* meta = item->metaObject();
    const int index = meta->indexOfProperty("color");
    if (index >= 0 && meta->property(index).isWritable())
        return true;
    return false;
}

// The text a Text item shows, or empty. QML's Text exposes `text` as a property.
QString textOf(QQuickItem* item)
{
    if (!classNameOf(item).contains(QStringLiteral("Text")))
        return {};
    const QVariant value = item->property("text");
    return value.isValid() ? value.toString() : QString();
}

class LayoutLint {
public:
    explicit LayoutLint(QQuickItem* root) : m_root(root) {}

    QVector<LintIssue> run()
    {
        walk(m_root);
        return m_issues;
    }

private:
    static QString rectText(const QRectF& r)
    {
        return QStringLiteral("%1x%2@%3,%4")
            .arg(qRound(r.width()))
            .arg(qRound(r.height()))
            .arg(qRound(r.x()))
            .arg(qRound(r.y()));
    }

    // The nearest ancestor-or-self that paints an opaque background. A delegate
    // inside a ListView is painted by the view's own background, and a ScrollBar
    // by the control it belongs to, so "no Rectangle ancestor" alone is not a
    // defect — the rule only fires for a *container* that is expected to paint.
    static bool hasPaintedAncestor(QQuickItem* item)
    {
        for (QQuickItem* p = item; p; p = p->parentItem())
            if (isRectangle(p))
                return true;
        return false;
    }

    // True for items that are supposed to paint their own background: a layout
    // container or an explicit pane. Delegates, text, scrollbars and mouse areas
    // are content, not containers, and are skipped.
    static bool isContainer(QQuickItem* item)
    {
        const QString name = classNameOf(item);
        return name.contains(QStringLiteral("Page")) || name.contains(QStringLiteral("Pane")) ||
               name.contains(QStringLiteral("SplitView"));
    }

    void walk(QQuickItem* item)
    {
        if (isContainer(item) && item->isVisible() && item->width() > 4 && item->height() > 4 &&
            !hasPaintedAncestor(item)) {
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            m_issues.append({QStringLiteral("unpainted"),
                             QStringLiteral("%1 %2 has no painted ancestor")
                                 .arg(classNameOf(item), rectText(r))});
        }

        // A visible child wider than its parent by a real margin is clipped or
        // overlaps. Delegates are sized to the viewport, so a 1 px difference is
        // normal there; 4 px is the smallest real overflow worth reporting.
        if (QQuickItem* parent = item->parentItem()) {
            if (item->isVisible() && item->width() > 0 && parent->width() > 0 &&
                item->width() > parent->width() + 4) {
                m_issues.append({QStringLiteral("overflow"),
                                 QStringLiteral("%1 is %2 wide inside a parent %3 wide")
                                     .arg(classNameOf(item))
                                     .arg(qRound(item->width()))
                                     .arg(qRound(parent->width()))});
            }
        }

        for (QQuickItem* child : item->childItems())
            walk(child);
    }

    QQuickItem* m_root = nullptr;
    QVector<LintIssue> m_issues;
};

// The specific regression this round is about: the manager label and the segmented
// buttons must sit next to each other. A hidden-but-space-reserving sibling (the
// search field, before its fillWidth became conditional) pushed them 137 px apart.
QVector<LintIssue> checkFilterRow(QQuickItem* root)
{
    QVector<LintIssue> issues;
    QQuickItem* label = nullptr;
    QQuickItem* segmented = nullptr;
    QList<QQuickItem*> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeFirst();
        if (!label && item->isVisible() && textOf(item) == QStringLiteral("管理器"))
            label = item;
        // The segmented control: the only ~24 px tall wide rectangle on the filter
        // row (below the tab strip, above the evidence strip).
        if (!segmented && item->isVisible() && isRectangle(item) && qAbs(item->height() - 24) < 1.5 &&
            item->width() > 150 && item->width() < 260) {
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            if (r.y() > 60 && r.y() < 220)
                segmented = item;
        }
        for (QQuickItem* child : item->childItems())
            pending.append(child);
    }
    if (label && segmented) {
        const QRectF l = label->mapRectToScene(QRectF(0, 0, label->width(), label->height()));
        const QRectF s = segmented->mapRectToScene(QRectF(0, 0, segmented->width(), segmented->height()));
        const qreal gap = s.left() - l.right();
        // Diagnostic: name every visible sibling between the two, so a phantom gap
        // names its cause instead of being guessed at from pixel offsets.
        if (qEnvironmentVariableIsSet("PACKAGES_LINT_VERBOSE") && label->parentItem()) {
            for (QQuickItem* sibling : label->parentItem()->childItems()) {
                const QRectF r = sibling->mapRectToScene(QRectF(0, 0, sibling->width(), sibling->height()));
                std::fprintf(stderr, "  sibling %s vis=%d %s text=\"%s\"\n", qPrintable(classNameOf(sibling)),
                             sibling->isVisible() ? 1 : 0,
                             qPrintable(QStringLiteral("%1x%2@%3,%4")
                                            .arg(qRound(r.width()))
                                            .arg(qRound(r.height()))
                                            .arg(qRound(r.x()))
                                            .arg(qRound(r.y()))),
                             qPrintable(textOf(sibling)));
            }
        }
        // 8 px is the row's spacing; 40 leaves room for a wider label at a larger
        // uiSize without letting a 137 px phantom gap through.
        if (gap > 40) {
            issues.append({QStringLiteral("filter-gap"),
                           QStringLiteral("管理器 ends at %1 but the manager buttons start at %2 (gap %3 px); "
                                          "a hidden sibling is probably still reserving width")
                               .arg(qRound(l.right()))
                               .arg(qRound(s.left()))
                               .arg(qRound(gap))});
        }
    }
    return issues;
}

} // namespace

int main(int argc, char** argv)
{
    // Arguments are parsed BEFORE the QGuiApplication is constructed: Qt consumes
    // and reorders the argv it recognises (and rebuilds it from its own list), so
    // by the time the event loop starts, `--style brand` can be gone from argv.
    // That is exactly what happened: the harness kept opening in fluent.
    Options options;
    if (!parse(argc, argv, options))
        return 1;

    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setQuitOnLastWindowClosed(false);

    if (options.logLevel == QStringLiteral("quiet"))
        g_logLevel = LogLevel::Quiet;
    else if (options.logLevel == QStringLiteral("debug"))
        g_logLevel = LogLevel::Debug;

    if (options.simulateElevated)
        simulateElevatedEnvironment();
    if (!options.realDataDir)
        useIsolatedDataDir();

    const bool elevated = pkg::platform::isElevated();

    if (!options.dumpPlanId.isEmpty())
        return dumpPlan(options.dumpPlanId, elevated);

    // ---- controller and models, exactly as the plugin builds them ------------
    pkg::ui::PackagesModel packagesModel;
    pkg::ui::TasksModel tasksModel;
    pkg::ui::Controller controller;
    controller.attachModels(&packagesModel, &tasksModel);

    pkg::ui::UiSettings settings;
    settings.language = options.locale == QStringLiteral("en") ? "en-US" : "zh-CN";
    settings.refreshMinutes = 30;
    controller.applySettings(settings);

    // ---- appearance ----------------------------------------------------------
    const bool dark = options.theme == QStringLiteral("dark") ||
                      (options.theme == QStringLiteral("system") && false);
    const QVariantMap appearance = pkg::ui::dev::appearanceFor(options.style, dark, options.size);

    // The harness always loads the scene from src/ui/qml: that is what makes an
    // edited .qml file take effect without a rebuild. The path is compiled in as
    // the default and `--qml` overrides it.
    if (options.qmlDir.isEmpty())
        options.qmlDir = QStringLiteral(PACKAGES_DEV_QML_DIR);

    say("init", QStringLiteral("style=%1 theme=%2 size=%3 elevated=%4 qml=%5")
                    .arg(options.style,
                         options.theme,
                         options.size,
                         elevated ? QStringLiteral("yes") : QStringLiteral("no"),
                         options.qmlDir));

    // ---- scene ---------------------------------------------------------------
    auto scene = std::make_unique<Scene>();
    scene->controller = &controller;
    auto buildScene = [&]() -> bool {
        std::string error;
        QQuickWindow* created = nullptr;
        if (!pkg::ui::initializeUi(&controller, created, error, options.qmlDir)) {
            std::fprintf(stderr, "scene error: %s\n", error.c_str());
            return false;
        }
        scene->window = created;
        created->show();
        created->raise();
        created->requestActivate();
        return true;
    };

    if (!buildScene())
        return 1;

    // Appearance must be applied AFTER the scene exists: applyAppearance pushes the
    // map onto the Theme object, which initializeUi creates, so applying earlier
    // silently does nothing and the window keeps the fallback palette.
    controller.applyAppearance(options.locale, dark, appearance);

    // ---- fake inventory ------------------------------------------------------
    if (options.fake) {
        // The models are fed directly so the window paints deterministic rows with
        // no backend process: UI work should not be gated on winget taking 1.6 s.
        // setFakeMode() must come first, or openWindow() below starts a real scan
        // and the status line reports "Querying…" over the surface being captured.
        controller.setFakeMode(true);
        const auto rows = fakeInventory();
        controller.applyFakeInventory(rows);
        say("fake", QStringLiteral("inventory: %1 rows").arg(packagesModel.rowCount()));
        // The Tasks page is part of what a UI round has to check, and a fake run
        // with an empty queue cannot show it. These tasks mirror the shapes the
        // queue produces: running, queued, succeeded, failed. They go into the
        // queue as well as the model: the header and the status line count
        // m_queue.tasks(), so a model-only injection made the page report an empty
        // queue above a list of four tasks.
        const auto tasks = fakeTasks();
        controller.applyFakeTasks(tasks);
        say("fake", QStringLiteral("tasks: %1").arg(tasksModel.rowCount()));
        // Opened here, with the snapshot already in place, so nothing is queried.
        controller.openWindow();
    }

    if (!options.tab.isEmpty()) {
        controller.setTab(options.tab);
        debug(QStringLiteral("tab -> %1").arg(options.tab));
    }

    if (!options.fake) {
        // The plugin opens its window when the host asks; the harness opens it at
        // once. In fake mode the window is not opened here: the injected snapshot
        // must be in place first, and openWindow() would try to refresh a stale
        // cache. It is opened below, after applyFakeInventory().
        controller.openWindow();
        if (options.forceRefresh)
            controller.refreshAll();
    }

    // ---- --search ------------------------------------------------------------
    // Discover is the only tab whose content depends on a query, so a screenshot
    // of it without one shows the installed inventory instead of search results.
    if (!options.searchText.isEmpty()) {
        controller.setTab(QStringLiteral("discover"));
        if (!options.discoverFilter.isEmpty())
            controller.setDiscoverFilter(options.discoverFilter);
        controller.setSearchText(options.searchText);
        say("search", QStringLiteral("query=%1 filter=%2")
                          .arg(options.searchText,
                               options.discoverFilter.isEmpty() ? QStringLiteral("all")
                                                               : options.discoverFilter));
        // A fake run starts no backend, so the search would never produce rows and
        // the Discover actions could not be checked. The rows below have the shape
        // the real backends return.
        if (options.fake) {
            pkg::Snapshot winget;
            pkg::Snapshot scoop;
            winget.winget = pkg::BackendState::Ready;
            scoop.scoop = pkg::BackendState::Ready;
            const std::string observed = "2026-11-13T12:00:00Z";
            winget.observedAt = observed;
            scoop.observedAt = observed;
            for (const auto& p : fakeSearchResults()) {
                if (p.key.manager == pkg::Manager::Scoop)
                    scoop.packages.push_back(p);
                else
                    winget.packages.push_back(p);
            }
            controller.applyFakeSearch(options.searchText, winget, scoop);
            if (options.tabCycleCheck) {
                controller.setDiscoverFilter(QStringLiteral("all"));
                const int beforeRows = packagesModel.rowCount();
                const QString beforeSearch = controller.searchText();
                const auto neovim = std::find_if(packagesModel.rows().begin(), packagesModel.rows().end(),
                                                  [](const pkg::Package& p) {
                                                      return p.key.manager == pkg::Manager::Winget &&
                                                             QString::fromStdString(p.key.id).compare(
                                                                 QStringLiteral("Neovim.Neovim"),
                                                                 Qt::CaseInsensitive) == 0;
                                                  });
                if (neovim == packagesModel.rows().end() || neovim->installedVersion != "0.12.6" ||
                    neovim->canInstall || !neovim->canUninstall) {
                    std::fprintf(stderr, "tab-cycle-check FAIL: installed Neovim was not merged into Discover\n");
                    return 2;
                }
                controller.setTab(QStringLiteral("installed"));
                controller.setTab(QStringLiteral("discover"));
                if (controller.searchText() != beforeSearch || packagesModel.rowCount() != beforeRows) {
                    std::fprintf(stderr,
                                 "tab-cycle-check FAIL: Discover did not restore (%d -> %d rows, query '%s' -> '%s')\n",
                                 beforeRows, packagesModel.rowCount(), qPrintable(beforeSearch),
                                 qPrintable(controller.searchText()));
                    return 2;
                }
                say("tab-cycle-check", QStringLiteral("PASS: %1 rows, query '%2', Neovim installed 0.12.6")
                                           .arg(beforeRows)
                                           .arg(beforeSearch));
            }
        }
    }

    // ---- --installed-filter --------------------------------------------------
    if (!options.installedFilter.isEmpty()) {
        controller.setInstalledFilter(options.installedFilter);
        say("filter", QStringLiteral("installed filter=%1 rows=%2")
                          .arg(options.installedFilter)
                          .arg(packagesModel.rowCount()));
    }

    // ---- --select ------------------------------------------------------------
    // The selection bar only exists while something is checked, so a screenshot of
    // it needs a way to check rows without a mouse. When a search is running the
    // rows are not in the model yet, so the checks are deferred until the search
    // result lands.
    auto applySelect = [&]() {
        if (options.selectRows.isEmpty())
            return;
        for (const QString& part : options.selectRows.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const int index = part.trimmed().toInt();
            if (index < 0 || index >= packagesModel.rowCount()) {
                std::fprintf(stderr, "select error: row %d is out of range (0..%d)\n",
                             index, packagesModel.rowCount() - 1);
                continue;
            }
            controller.toggleRow(index);
        }
        say("select", QStringLiteral("checked %1 row(s)").arg(packagesModel.checkedCount()));
    };
    if (!options.selectRows.isEmpty()) {
        if (options.searchText.isEmpty()) {
            applySelect();
        } else {
            // A search result lands asynchronously, so the checks run on a timer
            // instead of a signal: a fake-data run never starts a search at all, and
            // the screenshot is what needs the delay, not the harness.
            QTimer::singleShot(1200, &app, [&, applySelect]() { applySelect(); });
        }
    }

    // ---- --hold-searching ----------------------------------------------------
    if (options.holdSearchingMs > 0)
        controller.holdSearching(options.holdSearchingMs);

    // ---- --focus-row ---------------------------------------------------------
    // Selects a row without checking it, exactly like a click. The details pane
    // only has content for a selected row, and it is the surface the metadata
    // lookup renders into.
    if (options.focusRow >= 0) {
        auto applyFocus = [&]() -> bool {
            if (options.focusRow >= packagesModel.rowCount())
                return false;
            controller.selectRow(options.focusRow);
            say("focus", QStringLiteral("row %1 selected (model=%2, rows=%3)")
                               .arg(options.focusRow)
                               .arg(packagesModel.selected())
                               .arg(packagesModel.rowCount()));
            if (options.showInfoWindow) {
                QTimer::singleShot(250, &app, [&] {
                    if (auto* window = scene->window->findChild<QQuickWindow*>("packageInfoWindow")) {
                        window->setProperty("packageIndex", options.focusRow);
                        window->show();
                        window->raise();
                        window->requestActivate();
                    } else {
                        std::fprintf(stderr, "show-info-window error: metadata window not found\n");
                    }
                });
            }
            return true;
        };
        if (!applyFocus()) {
            auto* timer = new QTimer(&app);
            timer->setInterval(250);
            auto waited = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, &app, [&, timer, waited, applyFocus]() {
                if (applyFocus()) {
                    timer->stop();
                    timer->deleteLater();
                    return;
                }
                *waited += 250;
                if (*waited >= 60000) {
                    std::fprintf(stderr, "focus-row error: row %d never appeared\n", options.focusRow);
                    timer->stop();
                    timer->deleteLater();
                    QCoreApplication::exit(2);
                }
            });
            timer->start();
        }
    }

    // ---- --detail-action -----------------------------------------------------
    // Reproduces the user's path exactly: click a row (which only selects it, it
    // does not check the checkbox), then press an action in the details pane.
    //
    // With a real backend the rows arrive seconds after the window opens, so a
    // harness that only acted immediately reported "row out of range" whenever the
    // scan had not finished — the check could not run against real data at all.
    // The action therefore retries until the model has that row, up to a deadline.
    auto applyDetailAction = [&]() -> bool {
        if (options.detailRow < 0 || options.detailAction.isEmpty())
            return true;
        if (options.detailRow >= packagesModel.rowCount())
            return false;
        controller.selectRow(options.detailRow);
        say("detail", QStringLiteral("row %1 action=%2").arg(options.detailRow).arg(options.detailAction));
        controller.openReviewFor(options.detailRow, options.detailAction);
        say("detail", QStringLiteral("review opened, actions=%1 action=%2 confirm=%3")
                          .arg(controller.reviewActions().size())
                          .arg(controller.reviewAction())
                          .arg(controller.reviewConfirmEnabled() ? "yes" : "no"));
        return true;
    };
    if (options.detailRow >= 0 && !options.detailAction.isEmpty()) {
        if (!applyDetailAction()) {
            // Wait for the backend, bounded so a hung scan still fails loudly.
            auto* timer = new QTimer(&app);
            timer->setInterval(250);
            auto waited = std::make_shared<int>(0);
            QObject::connect(timer, &QTimer::timeout, &app, [&, timer, waited, applyDetailAction]() {
                if (applyDetailAction()) {
                    timer->stop();
                    timer->deleteLater();
                    return;
                }
                *waited += 250;
                if (*waited >= 60000) {
                    std::fprintf(stderr, "detail-action error: row %d never appeared (model has %d rows)\n",
                                 options.detailRow, packagesModel.rowCount());
                    timer->stop();
                    timer->deleteLater();
                    QCoreApplication::exit(2);
                }
            });
            timer->start();
        }
    }

    // ---- --review ------------------------------------------------------------
    // The review page is a dialog driven by controller state, so it cannot be
    // reached from a screenshot of a list. Selecting and opening it here is what a
    // user click does; the layout check then sees the page that actually ships.
    if (!options.reviewId.isEmpty()) {
        QString spec = options.reviewId;
        bool uninstall = false;
        const int colon = spec.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
            uninstall = spec.mid(colon + 1) == QStringLiteral("uninstall");
            spec = spec.left(colon);
        }
        int index = spec.toInt();
        if (index < 0 || index >= packagesModel.rowCount()) {
            std::fprintf(stderr, "review error: row %d is out of range (0..%d)\n",
                         index, packagesModel.rowCount() - 1);
            return 2;
        }
        packagesModel.setSelected(index);
        controller.selectRow(index);
        controller.toggleRow(index);
        controller.openReview();
        if (uninstall)
            controller.setReviewAction(QStringLiteral("uninstall"));
        say("review", QStringLiteral("row %1 action=%2")
                          .arg(index)
                          .arg(uninstall ? QStringLiteral("uninstall") : QStringLiteral("default")));
    }

    // ---- --expect-rows -------------------------------------------------------
    // A check that runs before the event loop starts, so no backend reply can
    // have arrived: whatever is in the model now came from the disk cache. This
    // is the regression that made a warm launch paint an empty list — the cache
    // was loaded in the constructor, when there was no model to rebuild.
    if (options.expectRows >= 0) {
        const int actual = packagesModel.rowCount();
        if (actual != options.expectRows) {
            std::fprintf(stderr, "expect-rows FAIL: wanted %d rows before any backend reply, found %d\n",
                         options.expectRows, actual);
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(1);
        }
        say("rows", QStringLiteral("%1 rows from the cache, before any backend reply").arg(actual));
    }

    // ---- --watch -------------------------------------------------------------
    //
    // Rebuilding the scene in-process is not safe: initializeUi parents the engine
    // to the controller and the scene's context owns the models, so destroying the
    // window leaves several dangling references and the next build crashes with an
    // access violation. Rewriting all of that lifetime handling to gain a reload
    // would trade a known-good path for a fragile one.
    //
    // So --watch restarts the process instead. A harness launch is about two
    // seconds, the edited .qml is picked up, and nothing about the plugin's own
    // lifetime model is bent for a development convenience.
    //
    // The watcher is heap-allocated and parented to the application: a stack object
    // would be destroyed at the end of this block, which is before app.exec() runs.
    std::unique_ptr<QFileSystemWatcher> watcher;
    if (options.watch) {
        watcher = std::make_unique<QFileSystemWatcher>();
        const QString dir = options.qmlDir;
        auto rearm = [w = watcher.get(), dir]() {
            for (const QString& file : QDir(dir).entryList({QStringLiteral("*.qml")}, QDir::Files)) {
                const QString absolute = QDir(dir).absoluteFilePath(file);
                if (!w->files().contains(absolute))
                    w->addPath(absolute);
            }
        };
        rearm();
        watcher->addPath(dir);
        say("watch", QStringLiteral("watching %1 for changes (%2 files)")
                        .arg(dir)
                        .arg(QDir(dir).entryList({QStringLiteral("*.qml")}, QDir::Files).size()));

        QObject::connect(watcher.get(), &QFileSystemWatcher::fileChanged, &app, [rearm](const QString& path) {
            say("watch", QStringLiteral("%1 changed, restarting").arg(QFileInfo(path).fileName()));
            // A distinct code so the supervisor restarts, while closing the window
            // (exit 0) ends the session. The rearm call is for an editor that
            // replaces the file (delete + rename) instead of writing in place.
            rearm();
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(kRestartRequested);
        });
        QObject::connect(watcher.get(), &QFileSystemWatcher::directoryChanged, &app, [&](const QString&) {
            say("watch", QStringLiteral("a .qml file was added, restarting"));
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(kRestartRequested);
        });
    }

    // ---- --dump-tree / --shot ------------------------------------------------
    auto finishWithCapture = [&]() {
        QTimer::singleShot(options.shotDelayMs, &app, [&] {
            if (options.dumpTree && scene->window) {
                std::function<void(QQuickItem*, int)> dump = [&](QQuickItem* item, int depth) {
                    const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                    std::printf("%*s%s %.0fx%.0f @%.0f,%.0f vis=%d\n", depth * 2, "",
                                item->metaObject()->className(), r.width(), r.height(), r.x(), r.y(),
                                item->isVisible() ? 1 : 0);
                    for (QQuickItem* child : item->childItems())
                        dump(child, depth + 1);
                };
                if (auto* root = scene->window->contentItem())
                    for (QQuickItem* child : root->childItems())
                        dump(child, 0);
            }
            int code = 0;
            if (options.lint && scene->window) {
                QVector<LintIssue> issues = LayoutLint(scene->window->contentItem()).run();
                issues += checkFilterRow(scene->window->contentItem());
                for (const auto& issue : issues)
                    std::fprintf(stderr, "LINT %s: %s\n", qPrintable(issue.rule), qPrintable(issue.detail));
                if (!issues.isEmpty())
                    code = 1;
                else
                    say("lint", QStringLiteral("no layout defects"));
            }
            if (!options.shotPath.isEmpty() && scene->window) {
                const QImage image = scene->window->grabWindow();
                if (image.save(options.shotPath)) {
                    say("shot", QStringLiteral("wrote %1 (%2x%3)")
                                    .arg(options.shotPath)
                                    .arg(image.width())
                                    .arg(image.height()));
                } else {
                    std::fprintf(stderr, "shot error: could not write %s\n", qPrintable(options.shotPath));
                    code = 1;
                }
            }
            // A capture run must always exit. Tearing the scene down here can block
            // on the local Qt/offscreen environment after the event loop has
            // stopped (a minimal Qt repro does the same), and a harness that hangs
            // is useless in a script. The image is already on disk, so the process
            // ends now and the OS reclaims the rest.
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(code);
        });
    };
    const bool captureRequested =
        !options.shotPath.isEmpty() || options.dumpTree || options.lint;
    if (captureRequested)
        finishWithCapture();

    if (options.quitAfter > 0) {
        QTimer::singleShot(static_cast<int>(options.quitAfter * 1000), &app, [&, captureRequested] {
            // A capture run exits through finishWithCapture(), so reaching this
            // timer means the capture never happened — most likely --shot-delay was
            // longer than --quit-after. Exiting 0 here would report success with no
            // file on disk, so a capture run that gets here is a failure.
            if (captureRequested) {
                std::fprintf(stderr,
                             "quit-after: the capture did not run within %gs; "
                             "raise --quit-after above --shot-delay\n", options.quitAfter);
                std::fflush(stdout);
                std::fflush(stderr);
                std::_Exit(1);
            }
            say("exit", QStringLiteral("--quit-after reached"));
            // Exit here rather than through app.quit(): in this environment exec()
            // does not return after quit() (a minimal Qt repro behaves the same),
            // so a smoke-check run would hang and a script would never see the exit
            // code. Everything the harness needed is already on stdout or disk.
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(0);
        });
    }

    const int rc = app.exec();
    scene.reset();

    // The window thread can leave the process waiting on a Qt dispatcher that has
    // already shut down (a minimal Qt repro does the same here, so it is the local
    // Qt/offscreen environment, not this code). A harness that will not exit is
    // useless in a script, so the exit is explicit: everything that needed a
    // destructor has been destroyed above, and the OS reclaims the rest.
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(rc);
}
