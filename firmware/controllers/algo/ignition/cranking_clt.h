#pragma once
#include <cmath>
#include <cstddef>

// Source: user's tested uaEFI Family custom-v25 (27 Oct 2026).
// This helper preserves the legacy RPM curve unless enabled, axes are
// strictly increasing and all interpolated cells have sane values.
template <typename Config>
void initializeCrankingClt(Config& cfg) {
    static const int temperatures[] = {-40, -20, 0, 20, 60, 100};
    static_assert(sizeof(cfg.crankingAdvanceCltBins) / sizeof(cfg.crankingAdvanceCltBins[0]) == 6);
    cfg.crankingAdvanceCltEnabled = 0;
    for (size_t row = 0; row < 6; row++) {
        cfg.crankingAdvanceCltBins[row] = temperatures[row];
        for (size_t col = 0; col < sizeof(cfg.crankingAdvance) / sizeof(cfg.crankingAdvance[0]); col++) {
            cfg.crankingAdvanceCltTable[row][col] = static_cast<float>(cfg.crankingAdvance[col]);
        }
    }
}

template <typename Config>
bool crankingCltTableUsable(const Config& cfg, bool cltValid, float clt) {
    if (cfg.crankingAdvanceCltEnabled != 1 || !cltValid || !std::isfinite(clt)) {
        return false;
    }
    constexpr size_t rows = sizeof(cfg.crankingAdvanceCltBins) / sizeof(cfg.crankingAdvanceCltBins[0]);
    constexpr size_t cols = sizeof(cfg.crankingAdvanceBins) / sizeof(cfg.crankingAdvanceBins[0]);
    for (size_t col = 0; col < cols; col++) {
        if (col && cfg.crankingAdvanceBins[col] <= cfg.crankingAdvanceBins[col - 1]) return false;
    }
    for (size_t row = 0; row < rows; row++) {
        if (row && cfg.crankingAdvanceCltBins[row] <= cfg.crankingAdvanceCltBins[row - 1]) return false;
        for (size_t col = 0; col < cols; col++) {
            const float angle = cfg.crankingAdvanceCltTable[row][col];
            if (!std::isfinite(angle) || angle < -20 || angle > 90) return false;
        }
    }
    return true;
}
