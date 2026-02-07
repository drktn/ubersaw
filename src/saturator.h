#pragma once
#include <cmath>

class Saturator {
public:
    void Init() { drive_ = 0.0f; }

    // Drive: 0.0 = bypass (unity), 1.0 = heavy saturation
    void SetDrive(float drive) { drive_ = drive; }

    float Process(float input) {
        if (drive_ <= 0.0f) return input;
        // Scale drive to useful tanh range (1x-5x gain)
        float scaled = 1.0f + drive_ * 4.0f;
        float out = tanhf(scaled * input);
        // Gain compensation: normalize so that tanh(scaled*1) maps to 1
        out /= tanhf(scaled);
        return out;
    }

private:
    float drive_ = 0.0f;
};
