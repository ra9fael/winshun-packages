// grab_window.cpp — renders the real QML scene offscreen and writes a PNG.
//
// This exists so the delivered window can be checked against the confirmed mockup
// without a desktop session. It is a development tool, not part of the plugin: it
// links the same scene and the same controller, so what it captures is what the
// plugin shows.
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQml> // qmlContext()
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QThread>

#include <cstdio>
#include <functional>
#include <string>

#include "src/core/model.h"
#include "src/core/plan.h"
#include "src/core/queue.h"
#include "src/ui/models.h"
#include "src/ui/theme_resolver.h"
#include "src/ui/window.h"

namespace {

pkg::Package makeRow(pkg::Manager manager, const char* source, const char* id, pkg::Scope scope, const char* installed,
                     const char* available, pkg::Provenance provenance)
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
    p.material = "verified";
    p.metadataAt = "2026-11-10T08:40:00Z";
    // Mirrors what the real adapters now produce: removal never needs a scope
    // (the exact id resolves the instance), so an installed row is actionable
    // even when the scope is unknown.
    const bool isInstalled = !std::string(installed).empty();
    p.canInstall = !isInstalled && pkg::scopeFromId(pkg::scopeId(scope)) != pkg::Scope::Unknown;
    p.canUninstall = isInstalled;
    p.canUpgrade = isInstalled && !std::string(available).empty();
    return p;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    const QString outPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("window.png");
    // "tasks" and "review" capture the other two states the mockup confirmed.
    const QString mode = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("list");

    pkg::ui::PackagesModel packages;
    pkg::ui::TasksModel tasks;
    pkg::ui::Controller controller;
    controller.attachModels(&packages, &tasks);

    // The style under test comes from argv[3] ("fluent" | "brand" | "tactile" |
    // "liquid") and the theme from argv[4] ("light" | "dark"), so the delivered
    // window can be checked against every style the host offers.
    const QString styleName = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QStringLiteral("fluent");
    const QString themeName = argc > 4 ? QString::fromLocal8Bit(argv[4]) : QStringLiteral("light");
    const bool dark = themeName == QStringLiteral("dark");

    // Per-style nativeLook values, taken from the host's own Look.qml files so the
    // capture shows what Win顺 would really send for that style.
    QVariantMap nativeLook;
    nativeLook.insert(QStringLiteral("style"), styleName);
    nativeLook.insert(QStringLiteral("dark"), dark);
    nativeLook.insert(QStringLiteral("rounded"), true);
    if (styleName == QStringLiteral("brand")) {
        nativeLook.insert(QStringLiteral("cardRadius"), 10);
        nativeLook.insert(QStringLiteral("controlRadius"), 6);
        nativeLook.insert(QStringLiteral("largeControlRadius"), 8);
        nativeLook.insert(QStringLiteral("smallControlRadius"), 4);
        nativeLook.insert(QStringLiteral("menuRadius"), 8);
        nativeLook.insert(QStringLiteral("menuItemRadius"), 4);
        nativeLook.insert(QStringLiteral("menuPadding"), 4);
        nativeLook.insert(QStringLiteral("background"), dark ? "#FF202024" : "#FFFDFCFD");
        nativeLook.insert(QStringLiteral("layer"), dark ? "#FF202024" : "#FFFAF9FA");
        nativeLook.insert(QStringLiteral("page"), dark ? "#FF202024" : "#FFFAF9FA");
        nativeLook.insert(QStringLiteral("windowEdge"), dark ? "#FF6A6A6A" : "#FFB4B4B4");
        nativeLook.insert(QStringLiteral("divider"), dark ? "#14FFFFFF" : "#13FF0000");
        nativeLook.insert(QStringLiteral("text"), dark ? "#FFF2F2F5" : "#FF1B1B20");
        nativeLook.insert(QStringLiteral("subtext"), dark ? "#FFB6B6BF" : "#FF5C5C66");
        nativeLook.insert(QStringLiteral("faint"), dark ? "#FF8C8C96" : "#FF8A8A93");
        nativeLook.insert(QStringLiteral("hover"), dark ? "#0DFFFFFF" : "#0A000000");
        nativeLook.insert(QStringLiteral("pressed"), dark ? "#14FFFFFF" : "#12000000");
        nativeLook.insert(QStringLiteral("tagFill"), dark ? "#12FFFFFF" : "#0B000000");
        nativeLook.insert(QStringLiteral("control"), dark ? "#FF2B2B31" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("controlHover"), dark ? "#FF31313A" : "#FFF7F7F9");
        nativeLook.insert(QStringLiteral("controlBorder"), dark ? "#24FFFFFF" : "#24000000");
        nativeLook.insert(QStringLiteral("inputFocused"), dark ? "#FF232328" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("menuBackground"), dark ? "#FF2F2F35" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("menuHover"), dark ? "#FF31313A" : "#FFF7F7F9");
        nativeLook.insert(QStringLiteral("menuEdge"), dark ? "#14FFFFFF" : "#14000000");
        nativeLook.insert(QStringLiteral("accent"), dark ? "#FFF2F2F5" : "#FF1B1B20");
        nativeLook.insert(QStringLiteral("accentFill"), "#FFA8402C");
        nativeLook.insert(QStringLiteral("accentLine"), dark ? "#FFF2F2F5" : "#FFA8402C");
        nativeLook.insert(QStringLiteral("accentText"), "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("textSelection"), "#4DA8402C");
        nativeLook.insert(QStringLiteral("danger"), dark ? "#FFFF7A80" : "#FFD2363E");
        nativeLook.insert(QStringLiteral("dangerFill"), "#FFC42F38");
    } else if (styleName == QStringLiteral("tactile")) {
        nativeLook.insert(QStringLiteral("cardRadius"), 8);
        nativeLook.insert(QStringLiteral("controlRadius"), 6);
        nativeLook.insert(QStringLiteral("largeControlRadius"), 8);
        nativeLook.insert(QStringLiteral("smallControlRadius"), 4);
        nativeLook.insert(QStringLiteral("menuRadius"), 8);
        nativeLook.insert(QStringLiteral("menuItemRadius"), 4);
        nativeLook.insert(QStringLiteral("menuPadding"), 6);
        nativeLook.insert(QStringLiteral("background"), dark ? "#FF2A2A2D" : "#FFF3EFE9");
        nativeLook.insert(QStringLiteral("layer"), "#00000000");
        nativeLook.insert(QStringLiteral("page"), dark ? "#FF2A2A2D" : "#FFF3EFE9");
        nativeLook.insert(QStringLiteral("windowEdge"), dark ? "#FF6A6A6A" : "#FFB4B4B4");
        nativeLook.insert(QStringLiteral("divider"), dark ? "#42000000" : "#15643C14");
        nativeLook.insert(QStringLiteral("text"), dark ? "#FFF3F1ED" : "#FF1F1B16");
        nativeLook.insert(QStringLiteral("subtext"), dark ? "#FFC9C4BC" : "#FF554F46");
        nativeLook.insert(QStringLiteral("faint"), dark ? "#FFB0ABA3" : "#FF6B645A");
        nativeLook.insert(QStringLiteral("hover"), dark ? "#08000000" : "#08643C14");
        nativeLook.insert(QStringLiteral("pressed"), dark ? "#0DFFFFFF" : "#0F643C14");
        nativeLook.insert(QStringLiteral("tagFill"), dark ? "#12FFFFFF" : "#0F643C14");
        nativeLook.insert(QStringLiteral("control"), dark ? "#FF55555B" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("controlHover"), dark ? "#FF55555B" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("controlBorder"), dark ? "#66000000" : "#24643C14");
        nativeLook.insert(QStringLiteral("inputFocused"), dark ? "#FF202022" : "#FFECE8E1");
        nativeLook.insert(QStringLiteral("menuBackground"), dark ? "#FF3B3B3F" : "#FFFFFEFC");
        nativeLook.insert(QStringLiteral("menuHover"), dark ? "#FF55555B" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("menuEdge"), dark ? "#48000000" : "#12643C14");
        nativeLook.insert(QStringLiteral("accent"), dark ? "#FFFFA36B" : "#FFB13D08");
        nativeLook.insert(QStringLiteral("accentFill"), "#FFC2410C");
        nativeLook.insert(QStringLiteral("accentLine"), dark ? "#FFFF7A30" : "#FFF2600F");
        nativeLook.insert(QStringLiteral("accentText"), "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("textSelection"), "#4DF2600F");
        nativeLook.insert(QStringLiteral("danger"), dark ? "#FFFF8078" : "#FFC2362F");
        nativeLook.insert(QStringLiteral("dangerFill"), "#FFB42D26");
    } else if (styleName == QStringLiteral("liquid")) {
        nativeLook.insert(QStringLiteral("cardRadius"), 10);
        nativeLook.insert(QStringLiteral("controlRadius"), 6);
        nativeLook.insert(QStringLiteral("largeControlRadius"), 8);
        nativeLook.insert(QStringLiteral("smallControlRadius"), 4);
        nativeLook.insert(QStringLiteral("menuRadius"), 8);
        nativeLook.insert(QStringLiteral("menuItemRadius"), 4);
        nativeLook.insert(QStringLiteral("menuPadding"), 4);
        nativeLook.insert(QStringLiteral("background"), dark ? "#FF202020" : "#FFF3F3F3");
        // 透明效果 on: the layer is translucent, which is exactly the case the
        // compositing rule exists for.
        nativeLook.insert(QStringLiteral("layer"), dark ? "#661F1F1F" : "#80FFFFFF");
        nativeLook.insert(QStringLiteral("page"), dark ? "#FF202020" : "#FFF3F3F3");
        nativeLook.insert(QStringLiteral("windowEdge"), dark ? "#FF555555" : "#FFBEBEBE");
        nativeLook.insert(QStringLiteral("divider"), dark ? "#12FFFFFF" : "#12000000");
        nativeLook.insert(QStringLiteral("text"), dark ? "#FFF5F5F5" : "#FF1A1A1A");
        nativeLook.insert(QStringLiteral("subtext"), dark ? "#FFC5C5C5" : "#FF525252");
        nativeLook.insert(QStringLiteral("faint"), dark ? "#FFA3A3A3" : "#FF686868");
        nativeLook.insert(QStringLiteral("hover"), dark ? "#0DFFFFFF" : "#09000000");
        nativeLook.insert(QStringLiteral("pressed"), dark ? "#09000000" : "#0E000000");
        nativeLook.insert(QStringLiteral("tagFill"), dark ? "#FF3A3A3A" : "#FFE9E9E9");
        nativeLook.insert(QStringLiteral("control"), dark ? "#FF383838" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("controlHover"), dark ? "#FF404040" : "#FFFAFAFA");
        nativeLook.insert(QStringLiteral("controlBorder"), dark ? "#1FFFFFFF" : "#24000000");
        nativeLook.insert(QStringLiteral("inputFocused"), dark ? "#FF252525" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("menuBackground"), dark ? "#FF2C2C2C" : "#FFF9F9F9");
        nativeLook.insert(QStringLiteral("menuHover"), dark ? "#FF3B3B3B" : "#FFEBEBEB");
        nativeLook.insert(QStringLiteral("menuEdge"), dark ? "#1FFFFFFF" : "#24000000");
        nativeLook.insert(QStringLiteral("accent"), dark ? "#FFB8B8B8" : "#FFA8402C");
        nativeLook.insert(QStringLiteral("accentFill"), "#FFA8402C");
        nativeLook.insert(QStringLiteral("accentLine"), dark ? "#FFB8B8B8" : "#FFA8402C");
        nativeLook.insert(QStringLiteral("accentText"), "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("textSelection"), "#4DA8402C");
        nativeLook.insert(QStringLiteral("danger"), dark ? "#FFFFABB3" : "#FFB4232F");
        nativeLook.insert(QStringLiteral("dangerFill"), "#FFB4232F");
    } else {
        // fluent
        nativeLook.insert(QStringLiteral("cardRadius"), 6);
        nativeLook.insert(QStringLiteral("controlRadius"), 4);
        nativeLook.insert(QStringLiteral("largeControlRadius"), 6);
        nativeLook.insert(QStringLiteral("smallControlRadius"), 3);
        nativeLook.insert(QStringLiteral("menuRadius"), 8);
        nativeLook.insert(QStringLiteral("menuItemRadius"), 4);
        nativeLook.insert(QStringLiteral("menuPadding"), 4);
        nativeLook.insert(QStringLiteral("background"), dark ? "#FF202020" : "#FFF9F9F9");
        nativeLook.insert(QStringLiteral("layer"), dark ? "#FF202020" : "#FFF9F9F9");
        nativeLook.insert(QStringLiteral("page"), dark ? "#FF202020" : "#FFF3F3F3");
        nativeLook.insert(QStringLiteral("windowEdge"), dark ? "#FF6A6A6A" : "#FFB4B4B4");
        nativeLook.insert(QStringLiteral("divider"), dark ? "#15FFFFFF" : "#0F000000");
        nativeLook.insert(QStringLiteral("text"), dark ? "#FFF3F3F3" : "#FF1B1B1B");
        nativeLook.insert(QStringLiteral("subtext"), dark ? "#FFB8B8B8" : "#FF666666");
        nativeLook.insert(QStringLiteral("faint"), "#FF8A8A8A");
        nativeLook.insert(QStringLiteral("hover"), dark ? "#0DFFFFFF" : "#09000000");
        nativeLook.insert(QStringLiteral("pressed"), dark ? "#17FFFFFF" : "#12000000");
        nativeLook.insert(QStringLiteral("tagFill"), dark ? "#12FFFFFF" : "#0D000000");
        nativeLook.insert(QStringLiteral("control"), dark ? "#FF373737" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("controlHover"), dark ? "#FF3C3C3C" : "#FFF6F6F6");
        nativeLook.insert(QStringLiteral("controlBorder"), dark ? "#FF444444" : "#FFD5D5D5");
        nativeLook.insert(QStringLiteral("inputFocused"), dark ? "#FF1F1F1F" : "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("menuBackground"), dark ? "#FF2C2C2C" : "#FFF9F9F9");
        nativeLook.insert(QStringLiteral("menuHover"), dark ? "#FF3A3A3A" : "#FFECECEC");
        nativeLook.insert(QStringLiteral("menuEdge"), dark ? "#17FFFFFF" : "#1A000000");
        nativeLook.insert(QStringLiteral("accent"), dark ? "#FFB8B8B8" : "#FF0066CC");
        nativeLook.insert(QStringLiteral("accentFill"), "#FF0066CC");
        nativeLook.insert(QStringLiteral("accentLine"), dark ? "#FFB8B8B8" : "#FF0066CC");
        nativeLook.insert(QStringLiteral("accentText"), "#FFFFFFFF");
        nativeLook.insert(QStringLiteral("textSelection"), "#4D0066CC");
        nativeLook.insert(QStringLiteral("danger"), dark ? "#FFFF99A4" : "#FFC42B1C");
        nativeLook.insert(QStringLiteral("dangerFill"), "#FFC42B1C");
    }

    QVariantMap appearance;
    appearance.insert(QStringLiteral("locale"), QStringLiteral("zh"));
    appearance.insert(QStringLiteral("theme"), themeName);
    appearance.insert(QStringLiteral("uiStyle"), styleName);
    appearance.insert(QStringLiteral("uiSize"), QStringLiteral("standard"));
    appearance.insert(QStringLiteral("resolvedUiFont"), QStringLiteral("Microsoft YaHei UI"));
    appearance.insert(QStringLiteral("nativeLook"), nativeLook);

    // A representative inventory, exactly the shapes the mockup showed.
    std::vector<pkg::Package> rows;
    rows.push_back(makeRow(pkg::Manager::Winget, "winget", "BurntSushi.ripgrep.MSVC", pkg::Scope::User, "14.1.0",
                           "14.1.2", pkg::Provenance::BackendRecorded));
    rows.push_back(makeRow(pkg::Manager::Scoop, "main", "ripgrep", pkg::Scope::User, "14.1.0", "14.1.0",
                           pkg::Provenance::BackendRecorded));
    rows.push_back(makeRow(pkg::Manager::Winget, "msstore", "9NBLGGH4NNS1", pkg::Scope::Machine, "1.21.2911",
                           "1.22.10352", pkg::Provenance::Observed));
    pkg::Package pinned = makeRow(pkg::Manager::Scoop, "extras", "vscode", pkg::Scope::User, "1.94.2", "1.95.1",
                                  pkg::Provenance::BackendRecorded);
    pinned.holdPolicy = "pinned @1.94.2";
    rows.push_back(pinned);
    pkg::Package observed = makeRow(pkg::Manager::Winget, "winget", "7zip.7zip", pkg::Scope::Machine, "24.08",
                                    "unknown", pkg::Provenance::Observed);
    observed.limits = "enumerated from the uninstall registry; WinGet does not manage it";
    rows.push_back(observed);
    rows.push_back(makeRow(pkg::Manager::Scoop, "main", "git", pkg::Scope::User, "2.47.0", "2.47.1",
                           pkg::Provenance::BackendRecorded));
    rows.push_back(makeRow(pkg::Manager::Winget, "winget", "Microsoft.PowerToys", pkg::Scope::User, "0.86.0",
                           "0.87.1", pkg::Provenance::BackendRecorded));
    rows.push_back(makeRow(pkg::Manager::Scoop, "main", "nodejs-lts", pkg::Scope::User, "22.9.0", "22.11.0",
                           pkg::Provenance::Matched));
    // Exactly the shape `winget list` produces: the scope is not reported, so it
    // is Unknown, and the row must still be selectable and removable because the
    // exact id resolves the instance.
    pkg::Package unknownScope =
        makeRow(pkg::Manager::Winget, "winget", "isOfficeAI.OfficeCLI", pkg::Scope::Unknown, "1.0.158", "",
                pkg::Provenance::BackendRecorded);
    unknownScope.name = "OfficeCLI";
    unknownScope.limits = "scope is not reported by `winget list`; only install needs one, so upgrade and "
                          "uninstall use the exact id";
    rows.push_back(unknownScope);
    packages.setRows(rows);

    // Tasks: one row per state the design distinguishes, so the capture shows the
    // queue's real wording rather than a single happy path.
    std::vector<pkg::Task> taskRows;
    {
        pkg::Queue queue;
        auto confirmedPlan = [](pkg::Package p, pkg::Action a) {
            pkg::Plan plan = pkg::makePlan(p, a);
            pkg::applyPreflight(plan, true, "verified", "");
            pkg::confirmPlan(plan, "t");
            return plan;
        };
        // A reinstall that removed the package and then failed: partial failure.
        const auto first = queue.enqueue(confirmedPlan(
            makeRow(pkg::Manager::Scoop, "extras", "vscode", pkg::Scope::User, "1.94.2", "1.95.1",
                    pkg::Provenance::BackendRecorded),
            pkg::Action::Reinstall));
        queue.start(first.id);
        queue.appendLog(first.id, "plan", "ok", "plan: reinstall extras/vscode (mgr=scoop scope=user)", -1);
        queue.setPhase(first.id, "uninstall");
        queue.appendLog(first.id, "uninstall", "info",
                        "argv: powershell -NoProfile -File scoop.ps1 uninstall vscode", -1);
        queue.appendLog(first.id, "uninstall", "info", "Removing vscode (1.94.2) …", 0);
        queue.setPhase(first.id, "install");
        queue.appendLog(first.id, "install", "info",
                        "argv: powershell -NoProfile -File scoop.ps1 install vscode", -1);
        queue.appendLog(first.id, "install", "error",
                        "Install failed: 7-Zip could not extract vscode.1.95.1.7z", 1603);
        queue.appendLog(first.id, "verify", "warning",
                        "the package was removed and the install did not complete", -1);
        queue.finish(first.id, false, 1603);
        queue.appendLog(first.id, "finish", "error",
                        "removed successfully but the install failed; the package may be absent", 1603);
        // A queued upgrade waiting for its lane.
        const auto second = queue.enqueue(confirmedPlan(
            makeRow(pkg::Manager::Scoop, "main", "git", pkg::Scope::User, "2.47.0", "2.47.1",
                    pkg::Provenance::BackendRecorded),
            pkg::Action::Upgrade));
        queue.appendLog(second.id, "queue", "info", "queued; waiting for the scoop lane", -1);
        taskRows = queue.tasks();
    }
    tasks.setTasks(taskRows);

    auto* engine = new QQmlEngine(&controller);

    // Theme.qml is instantiated once and shared, so the scene and the controller
    // resolve the same tokens. The resolver is exposed as a context property so
    // Theme.qml can call it.
    auto* resolver = new pkg::ui::ThemeResolver(engine);
    // The resolver must be visible to Theme.qml before Theme is created: QML
    // raises a ReferenceError for a missing context property, which silently
    // kills every binding that depends on it and leaves the int defaults at 0 —
    // the exact bug that collapsed all the bars in the real host. The engine
    // root context is where Theme.qml resolves names, so it goes there first;
    // the per-scene context below re-exposes it for Main.qml.
    engine->rootContext()->setContextProperty(QStringLiteral("themeResolver"), resolver);
    QQmlComponent themeComponent(engine, QUrl::fromLocalFile(QStringLiteral("src/ui/qml/Theme.qml")));
    if (themeComponent.isError()) {
        std::printf("THEME ERROR: %s\n", themeComponent.errorString().toUtf8().constData());
        return 1;
    }
    QObject* theme = themeComponent.create();
    if (!theme) {
        std::printf("THEME CREATE ERROR: %s\n", themeComponent.errorString().toUtf8().constData());
        return 1;
    }
    controller.setThemeObject(theme);
    // The theme now exists, so pushing the appearance actually reaches it.
    controller.applyAppearance(QStringLiteral("zh"), dark, appearance);

    QQmlContext context(engine);
    context.setContextProperty(QStringLiteral("controller"), &controller);
    context.setContextProperty(QStringLiteral("packagesModel"), &packages);
    context.setContextProperty(QStringLiteral("tasksModel"), &tasks);
    context.setContextProperty(QStringLiteral("themeResolver"), resolver);
    // Lower-case name: QML resolves an identifier starting with an upper-case
    // letter as a TYPE, so `Theme.rowHeight` was undefined and every
    // fixed-height property collapsed to 0.
    context.setContextProperty(QStringLiteral("theme"), theme);

    QQmlComponent component(engine, QUrl::fromLocalFile(QStringLiteral("src/ui/qml/Main.qml")));
    if (component.isError()) {
        std::printf("QML ERROR: %s\n", component.errorString().toUtf8().constData());
        return 1;
    }
    QObject* object = component.create(&context);
    if (!object) {
        std::printf("CREATE ERROR: %s\n", component.errorString().toUtf8().constData());
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
        std::printf("root is not a Window\n");
        return 1;
    }
    window->show();

    // Let the scene lay out and load fonts before grabbing: a grab in the same
    // event-loop turn captures an empty list and unshaped text.
    for (int i = 0; i < 60; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }

    // Debug: dump the item tree geometry so layout regressions are visible in
    // the tool output instead of guessed at from pixels.
    if (qEnvironmentVariableIsSet("PACKAGES_GRAB_DEBUG")) {
        std::function<void(QQuickItem*, int)> dump = [&](QQuickItem* item, int depth) {
            if (!item)
                return;
            const QByteArray indent(depth * 2, ' ');
            std::printf("%s%s %dx%d @%d,%d vis=%d\n", indent.constData(),
                        item->metaObject()->className(), qRound(item->width()), qRound(item->height()),
                        qRound(item->x()), qRound(item->y()), item->isVisible() ? 1 : 0);
            for (auto* child : item->childItems())
                dump(child, depth + 1);
        };
        dump(window->contentItem(), 0);

        // Also dump the Theme singleton's resolved values, which every fixed
        // height in the scene derives from.
        const QVariant themeRow = theme->property("rowHeight");
        const QVariant themeTab = theme->property("tabHeight");
        const QVariant themeHeader = theme->property("headerHeight");
        const QVariant themeBody = theme->property("bodyFont");
        std::printf("Theme.rowHeight=%s Theme.tabHeight=%s Theme.headerHeight=%s Theme.bodyFont=%s\n",
                    themeRow.isValid() ? themeRow.toString().toUtf8().constData() : "(invalid)",
                    themeTab.isValid() ? themeTab.toString().toUtf8().constData() : "(invalid)",
                    themeHeader.isValid() ? themeHeader.toString().toUtf8().constData() : "(invalid)",
                    themeBody.isValid() ? themeBody.toString().toUtf8().constData() : "(invalid)");
        const QVariant tVar = theme->property("t");
        std::printf("Theme.t valid=%s isNull=%s type=%s\n", tVar.isValid() ? "yes" : "no",
                    tVar.isNull() ? "yes" : "no", tVar.typeName() ? tVar.typeName() : "(none)");
        // What does Theme's own context see for themeResolver?
        const QQmlContext* tctx = qmlContext(theme);
        std::printf("Theme context=%p engine-root=%p\n", (void*)tctx, (void*)engine->rootContext());
        if (tctx)
            std::printf("Theme ctx themeResolver=%s\n",
                        tctx->contextProperty(QStringLiteral("themeResolver")).isValid() ? "valid" : "invalid");
        const QVariant fbVar = theme->property("_fallback");
        std::printf("Theme._fallback valid=%s isNull=%s\n", fbVar.isValid() ? "yes" : "no",
                    fbVar.isNull() ? "yes" : "no");
        const QVariant rh = theme->property("rowHeight");
        std::printf("Theme.rowHeight type=%s valid=%s\n", rh.typeName() ? rh.typeName() : "(none)",
                    rh.isValid() ? "yes" : "no");
        std::printf("Theme metaObject props:");
        for (int i = theme->metaObject()->propertyOffset(); i < theme->metaObject()->propertyCount(); ++i)
            std::printf(" %s", theme->metaObject()->property(i).name());
        std::printf("\n");
        if (tVar.isValid()) {
            const QVariantMap tMap = tVar.toMap();
            std::printf("Theme.t isNull=%s empty=%s rowHeight-in-map=%s\n",
                        tVar.isNull() ? "yes" : "no", tMap.isEmpty() ? "yes" : "no",
                        tMap.contains(QStringLiteral("rowHeight")) ? "present" : "missing");
            if (tMap.contains(QStringLiteral("rowHeight")))
                std::printf("Theme.t.rowHeight=%d\n", tMap.value(QStringLiteral("rowHeight")).toInt());
        }
    }

    if (mode == QStringLiteral("tasks")) {
        controller.setTab(QStringLiteral("tasks"));
        // Expand every log panel so the capture shows the per-step log without a
        // mouse. The page starts collapsed for a user.
        const auto pages = window->findChildren<QObject*>(QStringLiteral("tasksPage"));
        if (!pages.isEmpty())
            pages.first()->setProperty("expandAllLogs", true);
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    } else if (mode == QStringLiteral("review")) {
        // Select row 0, then open the review page, which is what a user does.
        packages.setSelected(0);
        controller.selectRow(0);
        controller.toggleRow(0);
        controller.openReview();
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    } else if (mode == QStringLiteral("review-unknown")) {
        // The reported case: an installed row whose scope `winget list` does not
        // report. Select it, choose uninstall, and the review must be confirmable.
        int target = -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (rows[static_cast<std::size_t>(i)].key.scope == pkg::Scope::Unknown &&
                !rows[static_cast<std::size_t>(i)].installedVersion.empty())
                target = i;
        }
        if (target < 0)
            target = static_cast<int>(rows.size()) - 1;
        packages.setSelected(target);
        controller.selectRow(target);
        controller.toggleRow(target);
        controller.openReview();
        // Uninstall is the action a user reaches for when there is no update.
        controller.setReviewAction(QStringLiteral("uninstall"));
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    } else if (mode == QStringLiteral("settings")) {
        // Popups are not QQuickItems in the content tree; they are found by
        // objectName through the QML object tree and opened the way the
        // 设置 button does.
        const auto popups = window->findChildren<QObject*>(QStringLiteral("settingsPopup"));
        if (!popups.isEmpty())
            QMetaObject::invokeMethod(popups.first(), "open");
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
    }

    const QImage image = window->grabWindow();
    if (image.isNull()) {
        std::printf("grab returned an empty image\n");
        return 1;
    }
    if (!image.save(outPath)) {
        std::printf("could not write %s\n", outPath.toUtf8().constData());
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", outPath.toUtf8().constData(), image.width(), image.height());
    return 0;
}
