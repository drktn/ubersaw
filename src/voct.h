#pragma once

#include <cmath>

/// Clamp float to [min, max].
static inline float fclamp(float val, float min, float max) {
    return val < min ? min : (val > max ? max : val);
}

/// Convert 1V/Oct CV + knob to frequency in Hz.
/// cv_normalized: ADC value 0.0 (-5V) to 1.0 (+5V)
/// knob_normalized: 0.0 to 1.0
/// Output clamped to [10 Hz, 20 kHz].
static inline float VoctToFreq(float cv_normalized, float knob_normalized) {
    float cv_voltage = (cv_normalized - 0.5f) * 10.0f;
    float base_note = 24.0f + knob_normalized * 72.0f;
    float total_note = base_note + cv_voltage * 12.0f;
    float freq = 440.0f * powf(2.0f, (total_note - 69.0f) / 12.0f);
    return fclamp(freq, 10.0f, 20000.0f);
}
