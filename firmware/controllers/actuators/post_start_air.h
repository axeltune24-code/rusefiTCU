#pragma once
#include <cmath>
#include <cstdint>
// V25 source: time-based post-start idle air, no change of fuel or timing phases.
class PostStartAir {
public:
    static bool configured(float hold, float decay) {
        return std::isfinite(hold) && std::isfinite(decay)
            && hold >= 0 && hold <= 3 && decay > 0 && decay <= 10;
    }
    void reset() { *this = PostStartAir{}; }
    void update(int64_t nowUs, bool stopped, bool cranking, bool running,
                bool validInput, bool driverTakingOver, float hold, float decay) {
        if (stopped) { reset(); return; }
        if (!configured(hold, decay) || !validInput) {
            m_armed = false; m_finished = true; m_fraction = 1;
            return;
        }
        if (cranking && !m_active && !m_finished) m_armed = true;
        if (!m_active && !m_finished && running && m_armed) {
            m_active = true; m_startUs = nowUs; m_fraction = 0;
        }
        if (!m_active || m_finished) return;
        const float elapsed = float(nowUs - m_startUs) * 1e-6f;
        float normal = smooth((elapsed - hold) / decay);
        if (driverTakingOver && !m_cancelled) {
            m_cancelled = true; m_cancelUs = nowUs; m_cancelFraction = m_fraction;
        }
        if (m_cancelled) {
            const float handover = m_cancelFraction + (1 - m_cancelFraction)
                * smooth(float(nowUs - m_cancelUs) * 1e-6f / 0.3f);
            if (handover > normal) normal = handover;
        }
        if (normal > m_fraction) m_fraction = normal;
        if (m_fraction >= 1) { m_fraction = 1; m_finished = true; }
    }
    float fraction() const { return m_fraction; }
    bool allowIdleCatch(float rpm, float target) {
        if (!m_active || m_finished || m_cancelled || m_fraction <= 0
            || !std::isfinite(rpm) || !std::isfinite(target) || target <= 0) return false;
        if (rpm <= target + 150) m_idleCatch = true;
        return m_idleCatch;
    }
private:
    static float smooth(float x) {
        if (x <= 0) return 0;
        if (x >= 1) return 1;
        return x * x * (3 - 2 * x);
    }
    int64_t m_startUs = 0, m_cancelUs = 0;
    float m_fraction = 1, m_cancelFraction = 1;
    bool m_armed = false, m_active = false, m_finished = false;
    bool m_cancelled = false, m_idleCatch = false;
};
