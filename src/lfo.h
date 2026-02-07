#pragma once
#include <cmath>
#include <cstdint>

class Lfo {
public:
    enum Waveform { SINE, TRIANGLE, SQUARE, SAMPLE_AND_HOLD };

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        phase_ = 0.0f;
        phase_inc_ = 0.0f;
        freq_ = 1.0f;
        waveform_ = SINE;
        sh_value_ = 0.0f;
        rng_state_ = 0x12345678;
        SetFreq(1.0f);
    }

    void SetFreq(float hz) {
        freq_ = hz;
        phase_inc_ = freq_ / sample_rate_;
    }

    void SetWaveform(Waveform w) { waveform_ = w; }

    float Process() {
        float out = 0.0f;
        switch (waveform_) {
        case SINE:
            out = sinf(phase_ * 2.0f * 3.14159265f);
            break;
        case TRIANGLE:
            if (phase_ < 0.25f)
                out = phase_ * 4.0f;
            else if (phase_ < 0.75f)
                out = 1.0f - (phase_ - 0.25f) * 4.0f;
            else
                out = -1.0f + (phase_ - 0.75f) * 4.0f;
            break;
        case SQUARE:
            out = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;
        case SAMPLE_AND_HOLD:
            out = sh_value_;
            break;
        }

        phase_ += phase_inc_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            if (waveform_ == SAMPLE_AND_HOLD) {
                rng_state_ ^= rng_state_ << 13;
                rng_state_ ^= rng_state_ >> 17;
                rng_state_ ^= rng_state_ << 5;
                sh_value_ = static_cast<float>(rng_state_) /
                            static_cast<float>(0xFFFFFFFF) * 2.0f - 1.0f;
            }
        }

        return out;
    }

    void Reset() {
        phase_ = 0.0f;
        sh_value_ = 0.0f;
    }

private:
    float sample_rate_ = 48000.0f;
    float phase_ = 0.0f;
    float phase_inc_ = 0.0f;
    float freq_ = 1.0f;
    Waveform waveform_ = SINE;
    float sh_value_ = 0.0f;
    uint32_t rng_state_ = 0x12345678;
};
