#pragma once

class Smooth {
public:
    void Init(float coeff) {
        coeff_ = coeff;
        value_ = 0.0f;
    }

    void SetCoeff(float coeff) {
        coeff_ = coeff;
    }

    float Process(float input) {
        value_ = coeff_ * value_ + (1.0f - coeff_) * input;
        return value_;
    }

    void SetValue(float val) {
        value_ = val;
    }

private:
    float value_ = 0.0f;
    float coeff_ = 0.9f;
};
