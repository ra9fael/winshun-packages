// theme_resolver.cpp — see theme_resolver.h.
#include "theme_resolver.h"

#include <QColor>

#include "../core/theme_tokens.h"

namespace pkg::ui {
namespace {

// Turns a resolved Argb into "#AARRGGBB", the form QML's color type reads.
QString toColorText(const pkg::theme::Argb& c)
{
    return QString::fromStdString([](const pkg::theme::Argb& v) {
        char buffer[10];
        std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X%02X", v.a, v.r, v.g, v.b);
        return std::string(buffer);
    }(c));
}

} // namespace

ThemeResolver::ThemeResolver(QObject* parent) : QObject(parent) {}

QVariantMap ThemeResolver::resolve(const QVariantMap& appearance) const
{
    // Flatten the QVariantMap into the string form the pure core expects.
    pkg::theme::RawAppearance raw;
    for (auto it = appearance.cbegin(); it != appearance.cend(); ++it) {
        const QVariant& value = it.value();
        if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
            // A nested map (nativeLook as the host sends it): prefix its keys.
            const QVariantMap nested = value.toMap();
            const std::string prefix = it.key().toStdString() + "/";
            for (auto nit = nested.cbegin(); nit != nested.cend(); ++nit)
                raw[prefix + nit.key().toStdString()] = nit.value().toString().toStdString();
        } else if (value.canConvert<QString>()) {
            raw[it.key().toStdString()] = value.toString().toStdString();
        }
    }

    const pkg::theme::Tokens t = pkg::theme::resolve(raw);
    const pkg::theme::UiScale scale = pkg::theme::uiScaleFor(
        raw.count("uiSize") ? raw.at("uiSize") : std::string());

    QVariantMap out;
    out.insert(QStringLiteral("style"), QString::fromStdString(t.style));
    out.insert(QStringLiteral("dark"), t.dark);
    out.insert(QStringLiteral("rounded"), t.rounded);

    out.insert(QStringLiteral("cardRadius"), t.cardRadius);
    out.insert(QStringLiteral("controlRadius"), t.controlRadius);
    out.insert(QStringLiteral("largeControlRadius"), t.largeControlRadius);
    out.insert(QStringLiteral("smallControlRadius"), t.smallControlRadius);
    out.insert(QStringLiteral("menuRadius"), t.menuRadius);
    out.insert(QStringLiteral("menuItemRadius"), t.menuItemRadius);
    out.insert(QStringLiteral("menuPadding"), t.menuPadding);

    out.insert(QStringLiteral("background"), toColorText(t.background));
    out.insert(QStringLiteral("layer"), toColorText(t.layer));
    out.insert(QStringLiteral("page"), toColorText(t.page));
    out.insert(QStringLiteral("windowEdge"), toColorText(t.windowEdge));
    out.insert(QStringLiteral("divider"), toColorText(t.divider));
    out.insert(QStringLiteral("text"), toColorText(t.text));
    out.insert(QStringLiteral("subtext"), toColorText(t.subtext));
    out.insert(QStringLiteral("faint"), toColorText(t.faint));
    out.insert(QStringLiteral("hover"), toColorText(t.hover));
    out.insert(QStringLiteral("pressed"), toColorText(t.pressed));
    out.insert(QStringLiteral("tagFill"), toColorText(t.tagFill));
    out.insert(QStringLiteral("control"), toColorText(t.control));
    out.insert(QStringLiteral("controlHover"), toColorText(t.controlHover));
    out.insert(QStringLiteral("controlBorder"), toColorText(t.controlBorder));
    out.insert(QStringLiteral("inputFocused"), toColorText(t.inputFocused));
    out.insert(QStringLiteral("menuBackground"), toColorText(t.menuBackground));
    out.insert(QStringLiteral("menuHover"), toColorText(t.menuHover));
    out.insert(QStringLiteral("menuEdge"), toColorText(t.menuEdge));
    out.insert(QStringLiteral("accent"), toColorText(t.accent));
    out.insert(QStringLiteral("accentFill"), toColorText(t.accentFill));
    out.insert(QStringLiteral("accentLine"), toColorText(t.accentLine));
    out.insert(QStringLiteral("accentText"), toColorText(t.accentText));
    out.insert(QStringLiteral("textSelection"), toColorText(t.textSelection));
    out.insert(QStringLiteral("danger"), toColorText(t.danger));
    out.insert(QStringLiteral("dangerFill"), toColorText(t.dangerFill));
    out.insert(QStringLiteral("success"), toColorText(t.success));
    out.insert(QStringLiteral("caution"), toColorText(t.caution));

    out.insert(QStringLiteral("titleFont"), scale.titleFont);
    out.insert(QStringLiteral("bodyFont"), scale.bodyFont);
    out.insert(QStringLiteral("captionFont"), scale.captionFont);
    out.insert(QStringLiteral("smallFont"), scale.smallFont);
    out.insert(QStringLiteral("rowHeight"), scale.rowHeight);
    out.insert(QStringLiteral("headerHeight"), scale.headerHeight);
    out.insert(QStringLiteral("tabHeight"), scale.tabHeight);

    return out;
}

} // namespace pkg::ui
