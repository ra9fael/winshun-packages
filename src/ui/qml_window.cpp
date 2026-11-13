// qml_window.cpp — creates the QML window on the GUI thread.
//
// A failure here is reported, never silently swallowed: if the scene cannot be
// built, the caller logs the reason and stops the process rather than leaving a
// window that never appears.
#include "window.h"

#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QString>
#include <QUrl>

#include "models.h"
#include "theme_resolver.h"

namespace pkg::ui {

bool initializeUi(Controller* controller, QQuickWindow*& window, std::string& error,
                  const QString& qmlSourceDir)
{
    // The engine outlives this call: it keeps the compiled scene and the theme
    // alive, so it is parented to the controller and torn down when the
    // controller is destroyed, which happens after app.exec() returns.
    auto* engine = new QQmlEngine(controller);

    // Where a QML file is read from. The shipped plugin has an empty
    // `qmlSourceDir` and reads the qrc resource; a development harness passes the
    // repository's src/ui/qml so an edit is visible without a rebuild.
    const auto sceneUrl = [&qmlSourceDir](const char* fileName) {
        if (qmlSourceDir.isEmpty())
            return QUrl(QStringLiteral("qrc:/qt/qml/Packages/src/ui/qml/") + QLatin1String(fileName));
        return QUrl::fromLocalFile(qmlSourceDir + QLatin1Char('/') + QLatin1String(fileName));
    };

    // The theme is created from Theme.qml once and handed to the scene as a
    // context property, so every component reads the same tokens and no QML
    // singleton registration is needed. The resolver does the parsing in C++,
    // where it is unit-tested.
    auto* resolver = new ThemeResolver(engine);
    // The resolver must be visible to Theme.qml before Theme is created: QML
    // raises a ReferenceError for a missing context property, which silently
    // kills every binding that depends on it and leaves the int defaults at 0.
    // That is exactly what happened in the real host — every bar collapsed to
    // zero height and the content piled up on top of everything else. The engine
    // root context is where Theme.qml resolves names, so it goes there first.
    engine->rootContext()->setContextProperty(QStringLiteral("themeResolver"), resolver);
    QQmlComponent themeComponent(engine, sceneUrl("Theme.qml"));
    if (themeComponent.isError()) {
        error = themeComponent.errorString().toStdString();
        delete resolver;
        delete engine;
        return false;
    }
    QObject* theme = themeComponent.create();
    if (!theme) {
        error = themeComponent.errorString().toStdString();
        delete resolver;
        delete engine;
        return false;
    }
    controller->setThemeObject(theme);

    // The models and the controller are exposed as named context properties so
    // Main.qml binds to them directly. They exist before the scene is created, so
    // every binding is valid from the first frame.
    auto* context = new QQmlContext(engine);
    context->setContextProperty(QStringLiteral("controller"), controller);
    context->setContextProperty(QStringLiteral("packagesModel"), controller->packagesModel());
    context->setContextProperty(QStringLiteral("tasksModel"), controller->tasksModel());
    context->setContextProperty(QStringLiteral("themeResolver"), resolver);
    // The scene reads the tokens through a context property named `theme`, not
    // `Theme`: an identifier that starts with an upper-case letter is resolved as
    // a TYPE name by QML, so `Theme.rowHeight` was undefined and every
    // fixed-height property collapsed to 0.
    context->setContextProperty(QStringLiteral("theme"), theme);

    // The generated resource prefix is /qt/qml/Packages/ with the source-relative
    // alias, so the scene URL keeps the src/ui/qml segment.
    QQmlComponent component(engine, sceneUrl("Main.qml"));
    if (component.isError()) {
        error = component.errorString().toStdString();
        delete context;
        delete engine;
        return false;
    }

    QObject* object = component.create(context);
    if (!object) {
        error = component.errorString().toStdString();
        delete context;
        delete engine;
        return false;
    }
    // Take ownership of the scene: the QQmlContext is parented to it, so deleting
    // the window tears down the bindings too.
    context->setParent(object);
    window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
        error = "the root QML object is not a Window";
        delete object;
        delete engine;
        return false;
    }
    return true;
}

} // namespace pkg::ui
