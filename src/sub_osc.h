#pragma once

class SubOsc {
public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        phase_ = 0.0f;
        phase_inc_ = 0.0f;
        level_ = 0.0f;
    }

    // Set frequency in Hz. Sub-osc plays one octave below,
    // so caller passes the supersaw freq directly --
    // the class halves it internally.
    void SetFreq(float freq_hz) {
        float sub_freq = freq_hz * 0.5f;
        phase_inc_ = sub_freq / sample_rate_;
    }

    // Level: 0.0 = silent, 1.0 = full volume
    void SetLevel(float level) { level_ = level; }

    float Process() {
        // Square wave: +1 for first half of period, -1 for second half
        float out = (phase_ < 0.5f) ? 1.0f : -1.0f;
        out *= level_;

        // Advance phase
        phase_ += phase_inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        return out;
    }

private:
    float sample_rate_ = 96000.0f;
    float phase_ = 0.0f;
    float phase_inc_ = 0.0f;
    float level_ = 0.0f;
};
