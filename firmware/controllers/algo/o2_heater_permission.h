#pragma once
#include <cmath>
#include <cstdint>

// Derived from user's tested V25: ECU *permission* to heat only. The WBO
// controller still enforces its own preheat, dew point, voltage and temperature.
inline bool o2HeaterPermission(bool forceHeating, bool engineRunning,
                               bool ignitionPowered, float batteryVoltage) {
    const bool vehiclePowered = std::isfinite(batteryVoltage)
        && batteryVoltage > 6.0f && batteryVoltage < 23.0f;
    return vehiclePowered && ignitionPowered && (forceHeating || engineRunning);
}
class O2HeaterPermission {
public:
    bool update(int64_t nowUs, bool forceRequested, bool engineRunning,
                bool ignitionPowered, bool ignitionKnown,
                bool batteryValid, float batteryVoltage,
                bool independentIgnition = false) {
        const bool trustworthyVoltage = batteryValid && std::isfinite(batteryVoltage)
            && batteryVoltage >= 0 && batteryVoltage < 23.0f;
        // Battery-only sense requires nearly zero voltage, not starter sag.
        const bool offEvidence = independentIgnition || batteryVoltage < 1.0f;
        const bool knownKeyOff = ignitionKnown && !ignitionPowered
            && trustworthyVoltage && offEvidence;
        bool offConfirmed = false;
        if (knownKeyOff) {
            if (!m_offTracking || nowUs < m_offStartUs) {
                m_offTracking = true;
                m_offStartUs = nowUs;
            }
            if (nowUs - m_offStartUs >= 500000) {
                offConfirmed = true;
                m_forceConsumed = false;
            }
        } else {
            m_offTracking = false;
        }
        if (engineRunning && !offConfirmed) m_forceConsumed = true;
        m_forceActive = false;
        if (!ignitionKnown || !batteryValid
            || !o2HeaterPermission(true, false, ignitionPowered, batteryVoltage)) {
            return false;
        }
        m_forceActive = forceRequested && !m_forceConsumed;
        return engineRunning || m_forceActive;
    }
    bool forceActive() const { return m_forceActive; }
private:
    bool m_forceConsumed = false;
    bool m_forceActive = false;
    bool m_offTracking = false;
    int64_t m_offStartUs = 0;
};
