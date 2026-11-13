// Theme.qml — design tokens resolved from Win顺's appearance.
//
// The host sends `appearance.nativeLook`: the current 界面风格's colours as
// "#AARRGGBB" strings and radii in logical pixels already resolved by the
// 圆角/直角 setting. Four styles exist (fluent / brand / tactile / liquid) and
// they share token names, so this file has no style branches: it reads what the
// host sent and falls back to fluent's values for anything missing.
//
// The heavy lifting (parsing, alpha compositing, fallbacks, the uiSize table)
// lives in src/core/theme_tokens.cpp and is unit-tested; this file only turns the
// resolved values into QML properties. The controller hands the raw appearance
// map over as `look` (a QVariantMap of "nativeLook/xxx" and top-level keys).
import QtQuick

QtObject {
    // The id must not collide with the context property name that carries this
    // object into the scene, so it stays `themeTokens`.
    id: themeTokens

    // Set by the controller on appearanceChanged; unknown keys are ignored, which
    // the host contract explicitly allows for future additions.
    property var look: ({})

    // ---- resolved values (opaque colours, clamped radii) ----------------------
    // themeResolver is a context property exposed by qml_window.cpp; it wraps
    // pkg::theme::resolve, so every rule here is unit-tested in C++.
    //
    // `t` is always a non-null object: when the resolver is missing (host
    // startup, or the grab tool creating Theme before the context is set) we
    // fall back to a fluent-shaped object carrying every default. That way the
    // `t.xxx !== undefined` guards below always take the fallback branch and
    // never leave a property at its int default of 0, which is what collapsed
    // every bar in the real host.
    readonly property var _fallback: ({
        cardRadius: 6, controlRadius: 4, largeControlRadius: 6, smallControlRadius: 3,
        menuRadius: 8, menuItemRadius: 4, menuPadding: 4,
        background: "#F9F9F9", layer: "#F9F9F9", page: "#F3F3F3", windowEdge: "#B4B4B4",
        divider: "#E6E6E6", text: "#1B1B1B", subtext: "#666666", faint: "#8A8A8A",
        hover: "#F0F0F0", pressed: "#E4E4E4", tagFill: "#F0F0F0", control: "#FFFFFF",
        controlHover: "#F6F6F6", controlBorder: "#D5D5D5", inputFocused: "#FFFFFF",
        menuBackground: "#F9F9F9", menuHover: "#ECECEC", menuEdge: "#E0E0E0",
        accent: "#0066CC", accentFill: "#0066CC", accentLine: "#0066CC", accentText: "#FFFFFF",
        textSelection: "#CCE0FF", danger: "#C42B1C", dangerFill: "#C42B1C",
        success: "#0F7B0F", caution: "#9D5D00",
        titleFont: 18, bodyFont: 16, captionFont: 14, smallFont: 12,
        rowHeight: 60, headerHeight: 64, tabHeight: 32,
        dark: false, rounded: true, style: "fluent"
    })
    readonly property var t: themeResolver ? themeResolver.resolve(look) : _fallback

    readonly property bool dark: t !== null ? t.dark : false
    readonly property bool rounded: t !== null ? t.rounded : true
    readonly property string style: t !== null ? t.style : "fluent"

    // ---- radii ---------------------------------------------------------------
    readonly property int cardRadius: t !== null && t.cardRadius !== undefined ? t.cardRadius : 6
    readonly property int controlRadius: t !== null && t.controlRadius !== undefined ? t.controlRadius : 4
    readonly property int largeControlRadius: t !== null && t.largeControlRadius !== undefined ? t.largeControlRadius : 6
    readonly property int smallControlRadius: t !== null && t.smallControlRadius !== undefined ? t.smallControlRadius : 3
    readonly property int menuRadius: t !== null && t.menuRadius !== undefined ? t.menuRadius : 8
    readonly property int menuItemRadius: t !== null && t.menuItemRadius !== undefined ? t.menuItemRadius : 4
    readonly property int menuPadding: t !== null && t.menuPadding !== undefined ? t.menuPadding : 4

    // ---- colours (all opaque; translucent host tokens were composited) -------
    readonly property color background: t !== null && t.background !== undefined ? t.background : "#F9F9F9"
    readonly property color layer: t !== null && t.layer !== undefined ? t.layer : "#F9F9F9"
    readonly property color page: t !== null && t.page !== undefined ? t.page : "#F3F3F3"
    readonly property color windowEdge: t !== null && t.windowEdge !== undefined ? t.windowEdge : "#B4B4B4"
    readonly property color divider: t !== null && t.divider !== undefined ? t.divider : "#E6E6E6"
    readonly property color text: t !== null && t.text !== undefined ? t.text : "#1B1B1B"
    readonly property color subtext: t !== null && t.subtext !== undefined ? t.subtext : "#666666"
    readonly property color faint: t !== null && t.faint !== undefined ? t.faint : "#8A8A8A"
    readonly property color hover: t !== null && t.hover !== undefined ? t.hover : "#F0F0F0"
    readonly property color pressed: t !== null && t.pressed !== undefined ? t.pressed : "#E4E4E4"
    readonly property color tagFill: t !== null && t.tagFill !== undefined ? t.tagFill : "#F0F0F0"
    readonly property color control: t !== null && t.control !== undefined ? t.control : "#FFFFFF"
    readonly property color controlHover: t !== null && t.controlHover !== undefined ? t.controlHover : "#F6F6F6"
    readonly property color controlBorder: t !== null && t.controlBorder !== undefined ? t.controlBorder : "#D5D5D5"
    readonly property color inputFocused: t !== null && t.inputFocused !== undefined ? t.inputFocused : "#FFFFFF"
    readonly property color menuBackground: t !== null && t.menuBackground !== undefined ? t.menuBackground : "#F9F9F9"
    readonly property color menuHover: t !== null && t.menuHover !== undefined ? t.menuHover : "#ECECEC"
    readonly property color menuEdge: t !== null && t.menuEdge !== undefined ? t.menuEdge : "#E0E0E0"
    readonly property color accent: t !== null && t.accent !== undefined ? t.accent : "#0066CC"
    readonly property color accentFill: t !== null && t.accentFill !== undefined ? t.accentFill : "#0066CC"
    readonly property color accentLine: t !== null && t.accentLine !== undefined ? t.accentLine : "#0066CC"
    readonly property color accentText: t !== null && t.accentText !== undefined ? t.accentText : "#FFFFFF"
    readonly property color textSelection: t !== null && t.textSelection !== undefined ? t.textSelection : "#CCE0FF"
    readonly property color danger: t !== null && t.danger !== undefined ? t.danger : "#C42B1C"
    readonly property color dangerFill: t !== null && t.dangerFill !== undefined ? t.dangerFill : "#C42B1C"
    // Not in nativeLook: derived from `dark` by the resolver, the way fluent does.
    readonly property color success: t !== null && t.success !== undefined ? t.success : "#0F7B0F"
    readonly property color caution: t !== null && t.caution !== undefined ? t.caution : "#9D5D00"
    readonly property color ok: success
    readonly property color warn: caution
    readonly property color selection: textSelection

    // ---- 界面大小 (appearance.uiSize): dense | compact | standard | roomy ------
    // The table is the host's UiScale.qml, verbatim; an unknown size is standard.
    readonly property int titleFont: t !== null && t.titleFont !== undefined ? t.titleFont : 18
    readonly property int bodyFont: t !== null && t.bodyFont !== undefined ? t.bodyFont : 16
    readonly property int captionFont: t !== null && t.captionFont !== undefined ? t.captionFont : 14
    readonly property int smallFont: t !== null && t.smallFont !== undefined ? t.smallFont : 12
    readonly property int rowHeight: t !== null && t.rowHeight !== undefined ? t.rowHeight : 60
    readonly property int headerHeight: t !== null && t.headerHeight !== undefined ? t.headerHeight : 64
    readonly property int tabHeight: t !== null && t.tabHeight !== undefined ? t.tabHeight : 32

    // The plugin's own type scale, derived from the host's tiers so a dense
    // launcher means a dense plugin window too.
    readonly property int baseFont: bodyFont
    readonly property int metaFont: captionFont
    readonly property int tinyFont: smallFont

    readonly property string fontFamily: look.resolvedUiFont !== undefined && look.resolvedUiFont !== ""
                                         ? look.resolvedUiFont : "Segoe UI Variable Text"
    readonly property string monoFamily: "Cascadia Mono"
}
