#pragma once
// Continuous heat gradient for the big "common correction" percentage.
// Pure data: the UI thread only paints this; no live game state is read here.
#include <array>

namespace whm {

struct Rgb {
    unsigned char r = 0, g = 0, b = 0;
};
struct NumberPalette {
    Rgb core, outline, glow;
};

// Exactly the three colours used by the existing number() glow passes.
// 100% must keep this warm-gold look, including the bright core, soft halo
// and dark outline. It is reused, not replaced by a flat colour.
inline constexpr NumberPalette kBaselineNumberPalette{
    {255, 238, 191}, {225, 124, 57}, {113, 58, 39}};

struct HeatNode {
    double percent;
    NumberPalette palette;
};

// Anchors from the approved design sketch. Colours between anchors are
// linearly interpolated, so there is no sudden jump at any node.
inline constexpr std::array<HeatNode, 9> kHeatNodes{{
    {50.,  {{198, 216, 234}, {92, 122, 158},   {42, 60, 86}}},   // soft blue-grey
    {75.,  {{220, 231, 240}, {126, 150, 178},  {58, 76, 104}}},  // pale blue silver
    {90.,  {{240, 233, 208}, {180, 162, 128},  {88, 78, 68}}},   // cool pale gold
    {100., kBaselineNumberPalette},                               // existing warm gold
    {125., {{255, 231, 146}, {238, 160, 48},   {128, 74, 34}}},  // richer gold
    {150., {{255, 222, 126}, {244, 136, 38},   {140, 68, 30}}},  // orange gold
    {175., {{255, 206, 132}, {238, 100, 44},   {142, 54, 32}}},  // orange red
    {200., {{255, 240, 190}, {232, 72, 50},    {128, 36, 36}}},  // blazing red, pale gold core
    {250., {{255, 248, 226}, {176, 34, 50},    {96, 24, 40}}},   // deep crimson rim, capped
}};

constexpr unsigned char lerpChannel(unsigned char a, unsigned char b, double t) {
    const double value = static_cast<double>(a) +
                         (static_cast<double>(b) - static_cast<double>(a)) * t;
    if (value <= 0.) return 0;
    if (value >= 255.) return 255;
    return static_cast<unsigned char>(value + 0.5);
}
constexpr Rgb lerpRgb(Rgb a, Rgb b, double t) {
    return {lerpChannel(a.r, b.r, t), lerpChannel(a.g, b.g, t),
            lerpChannel(a.b, b.b, t)};
}
constexpr NumberPalette lerpPalette(const NumberPalette &a,
                                    const NumberPalette &b, double t) {
    return {lerpRgb(a.core, b.core, t), lerpRgb(a.outline, b.outline, t),
            lerpRgb(a.glow, b.glow, t)};
}

// Values <=50% and >=250% are clamped: the top end never keeps darkening.
constexpr NumberPalette paletteForPercent(double percent) {
    if (!(percent == percent)) return kBaselineNumberPalette; // NaN guard
    if (percent <= kHeatNodes.front().percent) return kHeatNodes.front().palette;
    if (percent >= kHeatNodes.back().percent) return kHeatNodes.back().palette;
    for (std::size_t i = 1; i < kHeatNodes.size(); ++i) {
        const auto &lo = kHeatNodes[i - 1], &hi = kHeatNodes[i];
        if (percent <= hi.percent) {
            const double t = (percent - lo.percent) / (hi.percent - lo.percent);
            return lerpPalette(lo.palette, hi.palette, t);
        }
    }
    return kHeatNodes.back().palette;
}

} // namespace whm
