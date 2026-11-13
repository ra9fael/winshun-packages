// main.cpp — the Win顺 resident plugin entry point.
//
// Threading, following the shared-host Qt contract used by the other Qt plugins
// in this workspace:
//   * Qt Quick needs the GUI thread, so the Qt event loop owns the main thread;
//   * the JSON-RPC loop moves to a worker thread and keeps ownership of stdio;
//   * nothing is written to stdout except protocol messages.
//
// What a query does: it returns one row that opens the window. It never installs,
// never probes a backend and never accepts an agreement, because all of that is a
// consequence the user must ask for explicitly inside the window.
#include "winshun.h"

#include <QGuiApplication>
#include <QIcon>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <thread>

#include "adapters/scoop_adapter.h"
#include "adapters/winget_adapter.h"
#include "core/redact.h"
#include "ui/models.h"
#include "ui/window.h"
namespace {

using pkg::ui::Controller;
using pkg::ui::PackagesModel;
using pkg::ui::TasksModel;

// Reads the manifest settings object the host sends on initialize/settingsChanged.
pkg::ui::UiSettings readSettings(const winshun::Json& settings)
{
    pkg::ui::UiSettings out;
    const auto text = [&](const char* key) -> std::string {
        const winshun::Json& v = settings[key];
        return v.isString() ? v.toString() : std::string();
    };
    const std::string language = text("uiLanguage");
    if (language == "zh-CN" || language == "en-US")
        out.language = language;
    if (settings.has("showObserved"))
        out.showObserved = settings["showObserved"].toBool(true);
    if (settings.has("refreshMinutes"))
        out.refreshMinutes = static_cast<int>(settings["refreshMinutes"].toInt(30));
    if (settings.has("debugLogging"))
        out.debugLogging = settings["debugLogging"].toBool(false);
    return out;
}

class PackagesPlugin : public winshun::Plugin {
public:
    PackagesPlugin()
    {
        // The window is created lazily on the first open, so a host that loads the
        // plugin but never opens it pays for one thread and nothing else.
    }

    std::string name() const override { return "ra9fael.packages"; }

    // A query returns one row that opens the window. It carries no action that
    // could change the machine.
    winshun::Items query(const winshun::Query& q) override
    {
        winshun::Items items;
        const bool zh = chinese();
        const std::string title = zh ? "打开软件包窗口" : "Open Packages";
        const std::string subtitle =
            zh ? "已安装 / 发现 / 更新 / 任务；查询不会安装任何东西"
               : "Installed / Discover / Updates / Tasks; querying installs nothing";
        auto item = winshun::Item("packages-open", title, subtitle);
        item.icon = "glyph:U+E390";
        item.data = winshun::Json::Object{{"action", "open"}};
        items.push_back(std::move(item));
        (void)q;
        return items;
    }

    winshun::Effects action(const winshun::ActionRequest& request) override
    {
        (void)request;
        // The effect only hides the search box. The window itself is created on
        // the GUI thread, so the request is marshalled rather than built here.
        if (auto* controller = ui()) {
            QMetaObject::invokeMethod(controller, [controller] { controller->openWindow(); }, Qt::QueuedConnection);
        }
        return {winshun::effect::hide()};
    }

    void initialized() override
    {
        settingsChanged(winshun::Json::Object{});
        if (auto* controller = ui()) {
            const QVariantMap look = appearanceLook();
            QMetaObject::invokeMethod(
                controller,
                [controller, locale = locale(), dark = theme() == "dark", look] {
                    controller->applyAppearance(QString::fromStdString(locale), dark, look);
                },
                Qt::QueuedConnection);
        }
    }

    void settingsChanged(const winshun::Json& settings) override
    {
        const auto parsed = readSettings(settings);
        if (auto* controller = ui()) {
            QMetaObject::invokeMethod(
                controller, [controller, parsed] { controller->applySettings(parsed); }, Qt::QueuedConnection);
        }
    }

    void appearanceChanged() override
    {
        if (auto* controller = ui()) {
            const QVariantMap look = appearanceLook();
            QMetaObject::invokeMethod(
                controller,
                [controller, locale = locale(), dark = theme() == "dark", look] {
                    controller->applyAppearance(QString::fromStdString(locale), dark, look);
                },
                Qt::QueuedConnection);
        }
    }

    void shuttingDown() override
    {
        if (auto* controller = ui()) {
            QMetaObject::invokeMethod(controller, [controller] { controller->shutdown(); }, Qt::QueuedConnection);
        }
    }

    // The GUI thread calls this after the controller asked for the window.
    void setController(Controller* controller) { m_controller = controller; }

private:
    Controller* ui() { return m_controller; }

    bool chinese() const { return locale().rfind("zh", 0) == 0; }

    // The host's resolved look, handed to QML so the window matches Win顺:
    // nativeLook's colours and radii, plus the top-level fields the plugin needs
    // (uiSize for the type scale, resolvedUiFont for the family). Unknown fields
    // are ignored, which the host contract allows.
    QVariantMap appearanceLook() const
    {
        QVariantMap map;
        const auto put = [&map](const char* key, const winshun::Json& value) {
            if (value.isString())
                map.insert(QString::fromUtf8(key), QString::fromStdString(value.toString()));
            else if (value.isBool())
                map.insert(QString::fromUtf8(key), value.toBool());
            else if (value.isNumber())
                map.insert(QString::fromUtf8(key), value.toNumber());
        };
        for (const char* key : {"uiStyle", "uiSize", "themeSetting", "corners", "transparency", "renderer",
                                "resolvedUiFont", "uiFont", "locale", "theme"})
            put(key, appearance()[key]);
        const winshun::Json& native = appearance()["nativeLook"];
        if (native.isObject()) {
            QVariantMap nativeMap;
            for (const auto& [token, value] : native.toObject()) {
                if (value.isString())
                    nativeMap.insert(QString::fromStdString(token), QString::fromStdString(value.toString()));
                else if (value.isBool())
                    nativeMap.insert(QString::fromStdString(token), value.toBool());
                else if (value.isNumber())
                    nativeMap.insert(QString::fromStdString(token), value.toNumber());
            }
            map.insert(QStringLiteral("nativeLook"), nativeMap);
        }
        return map;
    }

    Controller* m_controller = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/plugin-icons/app.ico")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    app.setQuitOnLastWindowClosed(false);

    PackagesModel packagesModel;
    TasksModel tasksModel;
    Controller controller;
    controller.attachModels(&packagesModel, &tasksModel);

    // The protocol loop owns stdio and never touches a QObject directly.
    PackagesPlugin plugin;
    plugin.setController(&controller);

    std::thread protocol([&plugin] {
        plugin.serve();
        // serve() returned: the host asked for shutdown or closed stdin. Quit the
        // GUI loop; it is the only thing keeping this process alive.
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    });

    // The window is created on the GUI thread when the controller asks for it.
    // The pointer lives in a shared slot because the close path has to null it as
    // well, and the scene object must outlive the last access to it.
    auto windowSlot = std::make_shared<QQuickWindow*>(nullptr);
    QObject::connect(&controller, &Controller::windowShouldShow, &app, [&] {
        if (*windowSlot) {
            (*windowSlot)->show();
            (*windowSlot)->raise();
            (*windowSlot)->requestActivate();
            return;
        }
        std::string error;
        QQuickWindow* created = nullptr;
        if (!pkg::ui::initializeUi(&controller, created, error)) {
            // A scene failure must not leave a headless process: report it and go.
            plugin.log("error", "window failed to load: " + error);
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return;
        }
        *windowSlot = created;
        created->show();
        created->raise();
        created->requestActivate();
    });
    QObject::connect(&controller, &Controller::windowShouldClose, &app, [windowSlot] {
        // Hiding only: the resident plugin keeps its cache, and the process stays
        // until the host shuts it down.
        if (*windowSlot)
            (*windowSlot)->hide();
    });

    const int rc = app.exec();
    // The scene, its context and the engine are parented to objects destroyed
    // here, so the GUI thread's dispatcher shuts down cleanly and the process can
    // exit instead of a window thread waiting on a dead dispatcher.
    if (*windowSlot) {
        delete *windowSlot;
        *windowSlot = nullptr;
    }
    protocol.join();
    return rc;
}
