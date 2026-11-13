// theme_resolver.h — exposes pkg::theme::resolve to QML.
//
// Theme.qml calls resolve(look) with the raw appearance map the controller
// received. Keeping the parsing in C++ means every rule (compositing, fallbacks,
// the uiSize table) is unit-tested; QML only reads the resolved values.
#pragma once

#include <QObject>
#include <QVariantMap>

namespace pkg::ui {

class ThemeResolver : public QObject {
    Q_OBJECT
public:
    explicit ThemeResolver(QObject* parent = nullptr);

    // Takes the appearance map as the SDK handed it over: top-level keys plus
    // "nativeLook/<token>" entries. Returns a QVariantMap of resolved values.
    Q_INVOKABLE QVariantMap resolve(const QVariantMap& appearance) const;
};

} // namespace pkg::ui
