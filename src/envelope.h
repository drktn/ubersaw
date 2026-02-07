#pragma once
#include <cmath>

class Envelope {
public:
    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        value_ = 0.0f;
        gate_ = false;
        SetAttack(0.01f);
        SetRelease(0.1f);
    }

    void SetAttack(float seconds) {
        if (seconds < 0.001f) seconds = 0.001f;
        attack_coeff_ = 1.0f - expf(-1.0f / (seconds * sample_rate_));
    }

    void SetRelease(float seconds) {
        if (seconds < 0.001f) seconds = 0.001f;
        release_coeff_ = 1.0f - expf(-1.0f / (seconds * sample_rate_));
    }

    void Gate(bool on) { gate_ = on; }

    float Process() {
        float target = gate_ ? 1.0f : 0.0f;
        float coeff = gate_ ? attack_coeff_ : release_coeff_;
        value_ += (target - value_) * coeff;
        return value_;
    }

    float GetValue() const { return value_; }

private:
    float sample_rate_ = 96000.0f;
    float value_ = 0.0f;
    bool gate_ = false;
    float attack_coeff_ = 0.0f;
    float release_coeff_ = 0.0f;
};
