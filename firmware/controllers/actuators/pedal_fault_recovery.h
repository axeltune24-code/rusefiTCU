#pragma once
#include <cmath>
#include <cstdint>
// V25: only qualify clearing accumulated PPS faults, never TPS/jam faults.
class ReleasedPedalRecovery {
public:
    void reset() { m_tracking = false; }
    bool update(int64_t nowUs, bool eligible, float primary, float secondary) {
        if (!eligible || nowUs < 0 || !std::isfinite(primary) || !std::isfinite(secondary)
            || primary < -1.0f || primary > 1.0f
            || secondary < -1.0f || secondary > 1.0f) {
            reset();
            return false;
        }
        if (!m_tracking || nowUs < m_lastUs) {
            m_sinceUs = nowUs;
            m_lastUs = nowUs;
            m_tracking = true;
            return false;
        }
        m_lastUs = nowUs;
        return nowUs - m_sinceUs >= 1000000;
    }
private:
    int64_t m_sinceUs = 0;
    int64_t m_lastUs = 0;
    bool m_tracking = false;
};
