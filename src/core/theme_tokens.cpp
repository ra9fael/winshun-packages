// theme_tokens.cpp — see theme_tokens.h.
#include "theme_tokens.h"

#include <algorithm>
#include <cctype>

namespace pkg::theme {
namespace {

int clampRadius(int value)
{
    // A negative or absurd radius is a host bug, not something to honour.
    return value < 0 ? 0 : (value > 64 ? 64 : value);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string value(const RawAppearance& appearance, const std::string& key)
{
    const auto it = appearance.find(key);
    return it == appearance.end() ? std::string() : it->second;
}

} // namespace

bool parseColor(const std::string& text, Argb& out)
{
    // "#AARRGGBB" (8 hex digits) or "#RRGGBB" (6). Anything else — "transparent",
    // an empty string, a name — is not something this plugin can honour, and the
    // caller keeps its fallback.
    if (text.empty() || text.front() != '#')
        return false;
    const std::string hex = text.substr(1);
    if (hex.size() != 6 && hex.size() != 8)
        return false;
    for (const char c : hex)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;

    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        return std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
    };
    const auto byte = [&nibble](const char* pair) -> std::uint8_t {
        return static_cast<std::uint8_t>(nibble(pair[0]) * 16 + nibble(pair[1]));
    };

    if (hex.size() == 8) {
        out.a = byte(hex.c_str());
        out.r = byte(hex.c_str() + 2);
        out.g = byte(hex.c_str() + 4);
        out.b = byte(hex.c_str() + 6);
    } else {
        out.a = 255;
        out.r = byte(hex.c_str());
        out.g = byte(hex.c_str() + 2);
        out.b = byte(hex.c_str() + 4);
    }
    return true;
}

Argb composite(const Argb& src, const Argb& dst)
{
    // Straight alpha over. The result is opaque by construction, which is the
    // whole point: a plugin window has no backdrop to blend against later.
    const unsigned sa = src.a, da = dst.a;
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    const unsigned outA = sa + da * (255 - sa) / 255;
    if (outA == 0)
        return Argb{255, 0, 0, 0};
    const auto mix = [sa, da, outA](std::uint8_t s, std::uint8_t d) -> std::uint8_t {
        // src-over: (s*sa + d*da*(255-sa)/255) / outA, rounded to nearest.
        const unsigned numerator = s * sa + d * da * (255 - sa) / 255;
        return static_cast<std::uint8_t>((numerator + outA / 2) / outA);
    };
    return Argb{255, mix(src.r, dst.r), mix(src.g, dst.g), mix(src.b, dst.b)};
}

UiScale uiScaleFor(const std::string& size)
{
    // The host's UiScale.qml table, verbatim. An unknown size is "standard",
    // which is also what the host does (indexOf < 0 → 2).
    const std::string s = lower(size);
    if (s == "dense")
        return {14, 14, 12, 12, 32, 44, 24};
    if (s == "compact")
        return {16, 14, 12, 12, 52, 56, 28};
    if (s == "roomy")
        return {20, 18, 16, 14, 68, 72, 36};
    return {18, 16, 14, 12, 60, 64, 32};   // standard, and anything unrecognised
}

Tokens resolve(const RawAppearance& appearance)
{
    Tokens t;

    // Top-level fields the host sends outside nativeLook.
    const std::string uiSize = value(appearance, "uiSize");
    const UiScale scale = uiScaleFor(uiSize);
    (void)scale;   // consumed by the QML side through uiScaleFor directly

    const std::string nativePrefix = "nativeLook/";
    const auto native = [&appearance, &nativePrefix](const std::string& key) {
        const auto it = appearance.find(nativePrefix + key);
        return it == appearance.end() ? std::string() : it->second;
    };

    // Meta.
    const std::string style = lower(native("style"));
    if (!style.empty())
        t.style = style;   // kept for diagnostics only; no branch on it here
    const std::string dark = lower(native("dark"));
    if (dark == "true" || dark == "1")
        t.dark = true;
    else if (dark == "false" || dark == "0")
        t.dark = false;
    const std::string rounded = lower(native("rounded"));
    if (rounded == "false" || rounded == "0")
        t.rounded = false;
    else if (rounded == "true" || rounded == "1")
        t.rounded = true;

    // Radii. The host already squared them for 直角, so a 0 is honoured as 0.
    const auto radius = [&native](const std::string& key, int fallback) {
        const std::string text = native(key);
        if (text.empty())
            return fallback;
        try {
            return clampRadius(std::stoi(text));
        } catch (...) {
            return fallback;
        }
    };
    t.cardRadius = radius("cardRadius", t.cardRadius);
    t.controlRadius = radius("controlRadius", t.controlRadius);
    t.largeControlRadius = radius("largeControlRadius", t.largeControlRadius);
    t.smallControlRadius = radius("smallControlRadius", t.smallControlRadius);
    t.menuRadius = radius("menuRadius", t.menuRadius);
    t.menuItemRadius = radius("menuItemRadius", t.menuItemRadius);
    t.menuPadding = radius("menuPadding", t.menuPadding);
    if (!t.rounded) {
        // Belt and braces: the host squares them, but a plugin window that draws
        // a pill itself must square it too.
        t.cardRadius = t.controlRadius = t.largeControlRadius = t.smallControlRadius = 0;
        t.menuRadius = t.menuItemRadius = 0;
    }

    // Colours. `background` is the compositing base and must be opaque; the rest
    // are composited onto it, which is what docs/plugins.md asks for when the
    // host's 透明效果 makes a token translucent.
    Argb background;
    if (parseColor(native("background"), background) && background.a == 255)
        t.background = background;

    const auto color = [&native, &t](const std::string& key, Argb& slot, bool compositeOverBackground) {
        Argb parsed;
        if (!parseColor(native(key), parsed))
            return;
        slot = compositeOverBackground ? composite(parsed, t.background) : parsed;
    };

    color("layer", t.layer, true);
    color("page", t.page, true);
    color("windowEdge", t.windowEdge, false);
    color("divider", t.divider, true);
    color("text", t.text, false);
    color("subtext", t.subtext, false);
    color("faint", t.faint, false);
    color("hover", t.hover, true);
    color("pressed", t.pressed, true);
    color("tagFill", t.tagFill, true);
    color("control", t.control, true);
    color("controlHover", t.controlHover, true);
    color("controlBorder", t.controlBorder, true);
    color("inputFocused", t.inputFocused, true);
    color("menuBackground", t.menuBackground, true);
    color("menuHover", t.menuHover, true);
    color("menuEdge", t.menuEdge, true);
    color("accent", t.accent, false);
    color("accentFill", t.accentFill, false);
    color("accentLine", t.accentLine, false);
    color("accentText", t.accentText, false);
    color("textSelection", t.textSelection, true);
    color("danger", t.danger, false);
    color("dangerFill", t.dangerFill, false);

    // success / caution are not in nativeLook; derive them from `dark` the way
    // fluent does, and never pretend the host sent them.
    if (t.dark) {
        t.success = Argb{255, 0x6C, 0xCB, 0x5F};
        t.caution = Argb{255, 0xFC, 0xE1, 0x00};
    } else {
        t.success = Argb{255, 0x0F, 0x7B, 0x0F};
        t.caution = Argb{255, 0x9D, 0x5D, 0x00};
    }

    return t;
}

} // namespace pkg::theme
