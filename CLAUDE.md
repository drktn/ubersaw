# ÜBERSAW — Claude Code Context

JP-8000 supersaw recreation for Daisy Patch Init (Eurorack). 24-bit fixed-point,
7 naive saw oscillators, pitch-tracked HPF. No test framework.

## Build Commands

```bash
# first-time setup (submodules + libraries)
git submodule update --init --recursive
cd libDaisy && make && cd ..
cd DaisySP && make && cd ..

# build firmware (output: build/uebersaw.bin)
make

# clean rebuild
make clean && make

# flash via DFU (hold BOOT, press RESET, release both, then:)
make program-dfu
```

**Toolchain:** `arm-none-eabi-gcc` v10.3-2021.10 (exact version required), `dfu-util`, GNU `make`

## Project Structure

```text
src/main.cpp        — entry point, audio callback, hardware I/O, V/Oct conversion
src/supersaw.h      — SuperSaw class definition, 24-bit helpers (Wrap24, Mul24)
src/supersaw.cpp    — engine implementation (ProcessAuthentic, ProcessFloat)
Makefile            — TARGET=uebersaw, -O2, includes libDaisy/core/Makefile
libDaisy/           — hardware abstraction (git submodule)
DaisySP/            — DSP library (git submodule)
docs/               — algorithm.md, build-guide.md, controls.md
```

## Architecture

### SuperSaw Engine (`src/supersaw.h`, `src/supersaw.cpp`)

- 7 oscillators (`NUM_OSCS = 7`) as 24-bit signed phase accumulators
- Integer overflow at 24-bit boundary IS the sawtooth discontinuity (no wavetable)
- Asymmetric detune table: `{0, 128, -128, 816, -824, 1408, -1440}`
- Detuning is pitch-proportional: `voice_detune = (kDetuneTable[i] * pitch * detune) >> 7`
- Center osc at full volume, 6 side oscs scaled by mix parameter
- One-pole pitch-tracked HPF on output
- xorshift32 PRNG randomizes phases on gate trigger

### Dual Processing Modes

- `ProcessAuthentic()` — 24-bit fixed-point, matches original TC170C140 hardware
- `ProcessFloat()` — floating-point approximation for A/B comparison
- Selected via `SetAuthentic(bool)`, defaults to `true`

### Audio Config

- Sample rate: 96 kHz (closest to JP-8000's 88.2 kHz)
- Block size: 4 samples (~42 us per callback)
- Mono output duplicated to L+R

## Key Conventions

### Wrap24 After Every Arithmetic Op

Every operation that could exceed 24-bit range MUST be followed by `Wrap24()`.
This is critical for matching original hardware overflow behavior.

```cpp
saw_[i] = Wrap24(saw_[i] + pitch_inc_ + voice_detune);
sum = Wrap24(sum + saw_[i]);
```

### Knob + CV Parameter Pattern

All parameters follow: `final = clamp(knob + (cv_bipolar * 0.5), 0.0, 1.0)`

- CV inputs are bipolar (-5V to +5V), ADC maps to 0.0-1.0
- Reconstruct bipolar: `cv_bipolar = (cv_normalized - 0.5) * 2.0`
- Knob channels: `CV_5`-`CV_8`, CV channels: `CV_1`-`CV_4`

### Naming

- Private members: `trailing_underscore_` (e.g., `pitch_inc_`, `sample_rate_`)
- Static constants: `kCamelCase` (e.g., `kDetuneTable`)
- Public methods: `CamelCase`, helper functions: `camelCase`

### 24-bit Constants

```cpp
INT24_MAX   =  8388607   // (2^23) - 1
INT24_MIN   = -8388608   // -(2^23)
INT24_RANGE = 16777216   // 2^24
```

## Hardware (Daisy Patch Init)

- STM32H750 ARM Cortex-M7 @ 480 MHz, hardware FPU
- Knobs 1-4 → `CV_5`-`CV_8`, CV inputs 1-4 → `CV_1`-`CV_4`
- Gate: `hw.gate_in_1.State()`, LED: `hw.SetLed(bool)`
- Hardware object: `DaisyPatchSM hw` (namespace `daisy::patch_sm`)
- Audio callback constraints: no malloc, no blocking I/O
- Estimated CPU load: ~5-10%, substantial headroom

## Current Phase

Phase 1 (core engine) mostly complete. See `ROADMAP.md` for remaining work:
V/Oct verification, detune calibration, A/B testing. Phase 2 targets multi-pole
filter refinement.
