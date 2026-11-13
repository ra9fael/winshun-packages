// theme_tokens_tests.cpp — L1: appearance resolution and compositing.
//
// Each case names the rule it enforces; a failure points at the docs/plugins.md
// sentence it broke rather than at a function name.
#include <cstdio>
#include <string>

#include "../src/core/theme_tokens.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

void section(const char* name) { std::printf("-- %s\n", name); }

using namespace pkg::theme;

RawAppearance look(std::initializer_list<std::pair<const std::string, std::string>> pairs)
{
    return RawAppearance(pairs);
}

void colorParsing()
{
    section("colour parsing");
    Argb c;
    CHECK(parseColor("#FFA8402C", c) && c.a == 0xFF && c.r == 0xA8 && c.g == 0x40 && c.b == 0x2C);
    CHECK(parseColor("#A8402C", c) && c.a == 255 && c.r == 0xA8 && c.g == 0x40 && c.b == 0x2C);
    CHECK(parseColor("#ffa8402c", c) && c.r == 0xA8);   // case-insensitive
    CHECK(!parseColor("", c));
    CHECK(!parseColor("transparent", c));
    CHECK(!parseColor("#12345", c));
    CHECK(!parseColor("#1234567", c));
    CHECK(!parseColor("#GGGGGG", c));
    CHECK(!parseColor("A8402C", c));   // the host always sends the '#'
}

void compositing()
{
    section("alpha compositing (docs/plugins.md 外观)");
    // An opaque source wins outright.
    const Argb opaque{255, 0x10, 0x20, 0x30};
    const Argb base{255, 0xFF, 0xFF, 0xFF};
    CHECK(composite(opaque, base) == opaque);

    // A translucent layer over white: 50 % black over white is mid grey, opaque.
    const Argb halfBlack{128, 0, 0, 0};
    const Argb overWhite = composite(halfBlack, base);
    CHECK(overWhite.a == 255);
    CHECK(overWhite.r > 0x70 && overWhite.r < 0x90);   // ~127

    // A translucent layer over black stays dark, not washed out.
    const Argb overBlack = composite(halfBlack, Argb{255, 0, 0, 0});
    CHECK(overBlack.a == 255 && overBlack.r < 0x10);

    // Fully transparent falls back to the base.
    CHECK(composite(Argb{0, 1, 2, 3}, base) == base);

    // The host's real Mica case: white 78 % over the dark window fill.
    const Argb layer{199, 255, 255, 255};   // 0xC7 ≈ 78 %
    const Argb window{255, 0x20, 0x20, 0x24};
    const Argb composited = composite(layer, window);
    CHECK(composited.a == 255);
    // Mostly white, but not pure: the dark base shows through a little.
    CHECK(composited.r > 0xC0 && composited.r < 0xFF);
}

void fluentFallback()
{
    section("fluent fallback when nativeLook is absent");
    // An older host, or the host's UI not built yet: the documented fallback is
    // the plugin's own fluent values, not a blank window.
    const Tokens t = resolve({});
    CHECK(t.style == "fluent");
    CHECK(!t.dark);
    CHECK(t.rounded);
    CHECK(t.cardRadius == 6);
    CHECK(t.controlRadius == 4);
    CHECK(t.background == Argb{255, 0xF9, 0xF9, 0xF9});
    CHECK(t.text == Argb{255, 0x1B, 0x1B, 0x1B});
    CHECK(t.danger == Argb{255, 0xC4, 0x2B, 0x1C});
}

void fullTokenSet()
{
    section("full token set from nativeLook");
    // Brand's real values (from the host's brand/Look.qml), sent as the host sends
    // them: #AARRGGBB strings.
    const RawAppearance brand = look({
        {"nativeLook/style", "brand"},
        {"nativeLook/dark", "false"},
        {"nativeLook/rounded", "true"},
        {"nativeLook/cardRadius", "10"},
        {"nativeLook/controlRadius", "6"},
        {"nativeLook/largeControlRadius", "8"},
        {"nativeLook/smallControlRadius", "4"},
        {"nativeLook/menuRadius", "8"},
        {"nativeLook/menuItemRadius", "4"},
        {"nativeLook/menuPadding", "4"},
        {"nativeLook/background", "#FFFDFCFD"},
        {"nativeLook/layer", "#FFFAF9FA"},
        {"nativeLook/text", "#FF1B1B20"},
        {"nativeLook/subtext", "#FF5C5C66"},
        {"nativeLook/faint", "#FF8A8A93"},
        {"nativeLook/accent", "#FF1B1B20"},
        {"nativeLook/accentFill", "#FFA8402C"},
        {"nativeLook/accentLine", "#FFA8402C"},
        {"nativeLook/accentText", "#FFFFFFFF"},
        {"nativeLook/danger", "#FFD2363E"},
        {"nativeLook/dangerFill", "#FFC42F38"},
        {"uiSize", "standard"},
    });
    const Tokens t = resolve(brand);
    CHECK(t.style == "brand");
    CHECK(!t.dark);
    CHECK(t.cardRadius == 10);
    CHECK(t.controlRadius == 6);
    CHECK(t.background == Argb{255, 0xFD, 0xFC, 0xFD});
    CHECK(t.text == Argb{255, 0x1B, 0x1B, 0x20});
    CHECK(t.accentFill == Argb{255, 0xA8, 0x40, 0x2C});
    CHECK(t.danger == Argb{255, 0xD2, 0x36, 0x3E});
    // Tokens the style did not send keep fluent's values rather than going blank.
    CHECK(t.control == Argb{255, 0xFF, 0xFF, 0xFF});
    CHECK(t.divider.a == 255);
}

void translucentComposited()
{
    section("translucent tokens composited onto background");
    // fluent with 透明效果 on: layer is white 78 %, hover white 5 %, over the dark
    // window fill. These are the exact shapes the host sends.
    const RawAppearance fluentDark = look({
        {"nativeLook/style", "fluent"},
        {"nativeLook/dark", "true"},
        {"nativeLook/rounded", "true"},
        {"nativeLook/background", "#FF202020"},
        {"nativeLook/layer", "#C7FFFFFF"},
        {"nativeLook/hover", "#0DFFFFFF"},
        {"nativeLook/pressed", "#17FFFFFF"},
        {"nativeLook/tagFill", "#12FFFFFF"},
        {"nativeLook/text", "#FFF3F3F3"},
    });
    const Tokens t = resolve(fluentDark);
    CHECK(t.dark);
    // Everything a plugin paints with is opaque: no see-through to the desktop.
    CHECK(t.layer.a == 255);
    CHECK(t.hover.a == 255);
    CHECK(t.pressed.a == 255);
    CHECK(t.tagFill.a == 255);
    // The composited layer is light (mostly white) but not pure white.
    CHECK(t.layer.r > 0xC0);
    // Hover is a subtle lift over the dark base, not a white flash.
    CHECK(t.hover.r > 0x20 && t.hover.r < 0x40);
    CHECK(t.text == Argb{255, 0xF3, 0xF3, 0xF3});
}

void roundedSquare()
{
    section("rounded=false squares every radius");
    const RawAppearance square = look({
        {"nativeLook/rounded", "false"},
        {"nativeLook/cardRadius", "10"},
        {"nativeLook/controlRadius", "6"},
        {"nativeLook/menuRadius", "8"},
    });
    const Tokens t = resolve(square);
    CHECK(!t.rounded);
    CHECK(t.cardRadius == 0);
    CHECK(t.controlRadius == 0);
    CHECK(t.largeControlRadius == 0);
    CHECK(t.smallControlRadius == 0);
    CHECK(t.menuRadius == 0);
    CHECK(t.menuItemRadius == 0);
}

void unknownStyleIgnored()
{
    section("unknown style name does not branch");
    // A future style the plugin has never heard of must resolve like any other:
    // read the tokens, fall back for the missing ones, never crash.
    const RawAppearance future = look({
        {"nativeLook/style", "aurora"},
        {"nativeLook/dark", "true"},
        {"nativeLook/background", "#FF101014"},
        {"nativeLook/text", "#FFEFEFEF"},
    });
    const Tokens t = resolve(future);
    CHECK(t.style == "aurora");
    CHECK(t.dark);
    CHECK(t.background == Argb{255, 0x10, 0x10, 0x14});
    CHECK(t.text == Argb{255, 0xEF, 0xEF, 0xEF});
    CHECK(t.control.a == 255);   // fluent fallback, still opaque
}

void successCautionDerived()
{
    section("success/caution derived from dark (not in nativeLook)");
    RawAppearance light = look({{"nativeLook/dark", "false"}});
    const Tokens tl = resolve(light);
    CHECK(tl.success == Argb{255, 0x0F, 0x7B, 0x0F});
    CHECK(tl.caution == Argb{255, 0x9D, 0x5D, 0x00});

    RawAppearance dark = look({{"nativeLook/dark", "true"}});
    const Tokens td = resolve(dark);
    CHECK(td.success == Argb{255, 0x6C, 0xCB, 0x5F});
    CHECK(td.caution == Argb{255, 0xFC, 0xE1, 0x00});
}

void uiSizeTiers()
{
    section("uiSize tiers follow the host's UiScale table");
    const auto dense = uiScaleFor("dense");
    CHECK(dense.rowHeight == 32 && dense.headerHeight == 44 && dense.bodyFont == 14);
    const auto compact = uiScaleFor("compact");
    CHECK(compact.rowHeight == 52 && compact.headerHeight == 56 && compact.bodyFont == 14);
    const auto standard = uiScaleFor("standard");
    CHECK(standard.rowHeight == 60 && standard.headerHeight == 64 && standard.bodyFont == 16);
    const auto roomy = uiScaleFor("roomy");
    CHECK(roomy.rowHeight == 68 && roomy.headerHeight == 72 && roomy.bodyFont == 18);
    // An unknown size is standard, which is what the host does too.
    const auto unknown = uiScaleFor("gigantic");
    CHECK(unknown.rowHeight == 60);
    const auto empty = uiScaleFor("");
    CHECK(empty.rowHeight == 60);
}

void malformedInput()
{
    section("malformed input keeps the fallback");
    const RawAppearance bad = look({
        {"nativeLook/cardRadius", "not-a-number"},
        {"nativeLook/background", "#XYZ"},
        {"nativeLook/text", ""},
        {"nativeLook/dark", "maybe"},
    });
    const Tokens t = resolve(bad);
    CHECK(t.cardRadius == 6);   // fluent fallback
    CHECK(t.background == Argb{255, 0xF9, 0xF9, 0xF9});
    CHECK(t.text == Argb{255, 0x1B, 0x1B, 0x1B});
    CHECK(!t.dark);
    // An absurd radius is clamped, not honoured.
    const RawAppearance huge = look({{"nativeLook/cardRadius", "9999"}});
    CHECK(resolve(huge).cardRadius == 64);
    const RawAppearance negative = look({{"nativeLook/controlRadius", "-3"}});
    CHECK(resolve(negative).controlRadius == 0);
}

} // namespace

int main()
{
    std::printf("ra9fael.packages L1 theme token tests\n");
    colorParsing();
    compositing();
    fluentFallback();
    fullTokenSet();
    translucentComposited();
    roundedSquare();
    unknownStyleIgnored();
    successCautionDerived();
    uiSizeTiers();
    malformedInput();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
