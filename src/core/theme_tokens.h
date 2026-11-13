// theme_tokens.h — resolve Win顺's appearance into concrete, opaque values.
//
// The host sends `appearance.nativeLook`: the current 界面风格's colours as
// "#AARRGGBB" strings and radii in logical pixels, already resolved by the
// 圆角/直角 setting. Four styles exist (fluent / brand / tactile / liquid) and
// they all use the same token names, so this code has no style branches: it reads
// whatever the host sent and falls back to fluent's values for anything missing.
//
// Two rules from docs/plugins.md「外观」that this file exists to enforce:
//   * "开着透明效果时 layer、hover 等可能是半透明的，画在不透明的底上要先叠到
//     background 上" — a plugin window has no DWM backdrop of its own, so every
//     possibly-translucent token is composited onto `background` here;
//   * unknown fields are ignored, so a future host adding tokens cannot break us.
//
// Pure logic: no OS headers, no Qt, so the rules are unit-testable (L1).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pkg::theme {

// A resolved colour, always opaque after compositing.
struct Argb {
    std::uint8_t a = 255, r = 0, g = 0, b = 0;
};

inline bool operator==(const Argb& x, const Argb& y)
{
    return x.a == y.a && x.r == y.r && x.g == y.g && x.b == y.b;
}

// Parses "#AARRGGBB", "#RRGGBB" and the named forms the host may send. Returns
// false for anything else (an empty string, "transparent", garbage).
bool parseColor(const std::string& text, Argb& out);

// Composites `src` over `dst` (both may have alpha) and returns an opaque result.
Argb composite(const Argb& src, const Argb& dst);

// The 界面大小 tiers the host uses (UiScale.qml): dense | compact | standard | roomy.
struct UiScale {
    int titleFont = 18;
    int bodyFont = 16;
    int captionFont = 14;
    int smallFont = 12;
    int rowHeight = 60;
    int headerHeight = 64;
    int tabHeight = 32;
};

UiScale uiScaleFor(const std::string& size);

// Every token the plugin reads. Anything the host did not send keeps the fluent
// value, which is what the host itself falls back to.
struct Tokens {
    // meta
    std::string style = "fluent";
    bool dark = false;
    bool rounded = true;

    // radii (logical px, already squared by the host when 直角)
    int cardRadius = 6;
    int controlRadius = 4;
    int largeControlRadius = 6;
    int smallControlRadius = 3;
    int menuRadius = 8;
    int menuItemRadius = 4;
    int menuPadding = 4;

    // colours, composited onto `background` where the host may send alpha
    Argb background{255, 0xF9, 0xF9, 0xF9};
    Argb layer{255, 0xF9, 0xF9, 0xF9};
    Argb page{255, 0xF3, 0xF3, 0xF3};
    Argb windowEdge{255, 0xB4, 0xB4, 0xB4};
    Argb divider{255, 0xE6, 0xE6, 0xE6};
    Argb text{255, 0x1B, 0x1B, 0x1B};
    Argb subtext{255, 0x66, 0x66, 0x66};
    Argb faint{255, 0x8A, 0x8A, 0x8A};
    Argb hover{255, 0xF0, 0xF0, 0xF0};
    Argb pressed{255, 0xE4, 0xE4, 0xE4};
    Argb tagFill{255, 0xF0, 0xF0, 0xF0};
    Argb control{255, 0xFF, 0xFF, 0xFF};
    Argb controlHover{255, 0xF6, 0xF6, 0xF6};
    Argb controlBorder{255, 0xD5, 0xD5, 0xD5};
    Argb inputFocused{255, 0xFF, 0xFF, 0xFF};
    Argb menuBackground{255, 0xF9, 0xF9, 0xF9};
    Argb menuHover{255, 0xEC, 0xEC, 0xEC};
    Argb menuEdge{255, 0xE0, 0xE0, 0xE0};
    Argb accent{255, 0x00, 0x66, 0xCC};
    Argb accentFill{255, 0x00, 0x66, 0xCC};
    Argb accentLine{255, 0x00, 0x66, 0xCC};
    Argb accentText{255, 0xFF, 0xFF, 0xFF};
    Argb textSelection{255, 0xCC, 0xE0, 0xFF};
    Argb danger{255, 0xC4, 0x2B, 0x1C};
    Argb dangerFill{255, 0xC4, 0x2B, 0x1C};
    // Not in nativeLook: the host requires plugins to derive these themselves
    // (docs/plugins.md lists nativeLook only up to dangerFill). Values follow
    // fluent's, keyed on `dark`.
    Argb success{255, 0x0F, 0x7B, 0x0F};
    Argb caution{255, 0x9D, 0x5D, 0x00};
};

// The raw appearance object as the SDK hands it over: a flat map of
// "nativeLook/xxx" and top-level keys. Keeping it as strings keeps this header
// free of the JSON type, so the L1 tests need no host.
using RawAppearance = std::unordered_map<std::string, std::string>;

// Reads `appearance["uiSize"]` (top level) and `appearance["nativeLook/<key>"]`.
// Missing nativeLook (an older host, or the host's UI not built yet) leaves the
// fluent defaults in place; that is the documented fallback.
Tokens resolve(const RawAppearance& appearance);

} // namespace pkg::theme
