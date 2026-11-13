// look_fixture.cpp — see look_fixture.h.
//
// The per-style tables are transcribed from WinShun's Look.qml files. They are
// data, not logic: if the host changes a token, this file has to change too, and
// the honest way to notice that is the diff against the host's own files rather
// than a fallback that silently looks plausible.
#include "look_fixture.h"

namespace pkg::ui::dev {

QVariantMap appearanceFor(const QString& style, bool dark, const QString& uiSize)
{
    QVariantMap nativeLook;
    nativeLook.insert(QStringLiteral("style"), style);
    nativeLook.insert(QStringLiteral("dark"), dark);
    nativeLook.insert(QStringLiteral("rounded"), true);

    if (style == QStringLiteral("brand")) {
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
    } else if (style == QStringLiteral("tactile")) {
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
    } else if (style == QStringLiteral("liquid")) {
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
        nativeLook.insert(QStringLiteral("accent"), dark ? "#FFB8B8B8" : "#FF0066CC");
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
    appearance.insert(QStringLiteral("theme"), dark ? QStringLiteral("dark") : QStringLiteral("light"));
    appearance.insert(QStringLiteral("uiStyle"), style);
    appearance.insert(QStringLiteral("uiSize"), uiSize);
    appearance.insert(QStringLiteral("resolvedUiFont"), QStringLiteral("Microsoft YaHei UI"));
    appearance.insert(QStringLiteral("nativeLook"), nativeLook);
    return appearance;
}

} // namespace pkg::ui::dev
