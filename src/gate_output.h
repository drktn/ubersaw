#pragma once

class GateOutput {
public:
    enum Mode { PASSTHROUGH, CLOCK_DIV };

    void Init() {
        mode_ = PASSTHROUGH;
        divisor_ = 2;
        count_ = 0;
        prev_gate_ = false;
        output_ = false;
    }

    void SetMode(Mode mode) {
        mode_ = mode;
        count_ = 0;
        output_ = false;
    }

    void SetDivisor(int divisor) {
        divisor_ = divisor;
    }

    bool Process(bool gate_in) {
        if (mode_ == PASSTHROUGH) {
            prev_gate_ = gate_in;
            return gate_in;
        }

        // Clock divider: detect rising edge
        bool rising = gate_in && !prev_gate_;
        prev_gate_ = gate_in;

        if (rising) {
            count_++;
            if (count_ >= divisor_) {
                count_ = 0;
                output_ = !output_;
            }
        }

        return output_;
    }

private:
    Mode mode_ = PASSTHROUGH;
    int divisor_ = 2;
    int count_ = 0;
    bool prev_gate_ = false;
    bool output_ = false;
};
