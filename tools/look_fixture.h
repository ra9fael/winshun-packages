// look_fixture.h — the host's appearance values for each UI style, for tools.
//
// Not shipped and not used by the plugin: the plugin receives these from the host
// at runtime. The development harness and the offscreen capture tool both need the
// same table, so it lives in one place instead of being copied into each tool —
// a copy would drift and the two tools would then disagree about what "fluent" is.
//
// The values are the ones WinShun's own Look.qml files produce, so what a tool
// renders is what the host would really send for that style.
#pragma once

#include <QString>
#include <QVariantMap>

namespace pkg::ui::dev {

// The four styles the host supports, in the order its settings present them.
inline const char* const kStyles[] = {"fluent", "brand", "tactile", "liquid"};

// The four 界面大小 tiers.
inline const char* const kSizes[] = {"dense", "compact", "standard", "roomy"};

// nativeLook + the top-level appearance fields the plugin reads, for one style at
// one theme. `style` is one of kStyles (anything else falls back to fluent).
QVariantMap appearanceFor(const QString& style, bool dark, const QString& uiSize = QStringLiteral("standard"));

} // namespace pkg::ui::dev
