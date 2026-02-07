# ÜBERSAW development roadmap

## Phase 1: Core oscillator engine (done)

- [x] Project scaffold and build system
- [x] 7-oscillator phase accumulator with 24-bit fixed-point arithmetic
- [x] Original detune table implementation
- [x] Basic mixing (center full volume, sides scaled by spread)
- [x] One-pole pitch-tracked high-pass filter
- [x] Random phase initialization on gate trigger
- [x] Verify V/Oct tracking accuracy across 5+ octaves
- [x] A/B test 24-bit mode vs. float mode for audible differences

## Phase 2: Filter research (done)

Research confirms the JP-8000 uses a **one-pole HPF** — not a multi-pole SVF.

Sources:

- 39C3 reverse engineering (Giulioz): one-pole HPF on supersaw output
- Adam Szabo thesis: "pitch-tracked HPF removes noise below fundamental"
- JE-8086 emulator (same reverse engineering basis): one-pole

Current implementation is correct. No changes needed.

Optional future work:

- [ ] Implement HPF in fixed-point for full authenticity (low priority)

## Phase 3: Parameter calibration (done)

- [x] Calibrate detune knob curve (Szabo 11th-order polynomial)
- [x] Calibrate mix knob curve (Szabo parabolic side-voice curve)
- [x] Verify aliasing characteristics at 96 kHz vs. original 88.2 kHz

## Phase 4: Polish and features

- [x] ADC smoothing/filtering for stable knob readings
- [x] Anti-click on parameter changes
- [x] Portamento/glide option
- [x] Implement toggle switch GPIO reading for mode select
- [x] Implement button GPIO reading for manual trigger
- [x] Gate output: pass-through or clock divider for sequencer sync
- [ ] CPU load optimization (profile at 96 kHz with block size 4, needs hardware)
- [ ] Consider block size 2 for lower callback noise (needs hardware)

## Phase 5: Extended features (post-1.0)

- [ ] Stereo spread: route odd/even oscillators to L/R outputs
- [ ] Sub-oscillator option (one octave below, common JP-8000 technique)
- [ ] Waveform options beyond saw (square superstack, triangle)
- [ ] MIDI input via USB for standalone use
- [ ] Preset storage on SD card
- [ ] Daisy Web Programmer (.bin) release for non-developer users
- [ ] Panel design files (SVG/DXF for laser cutting or PCB panel)

## Performance budget

At 96 kHz sample rate, block size 4:

- **Audio callback rate:** 24,000 callbacks/sec
- **Time budget per callback:** ~42 us
- **CPU:** 480 MHz Cortex-M7 with hardware FPU
- **Estimated load:** 7 oscillators x (2 adds + 1 multiply + 1 wrap) + 1 filter = minimal (~5-10% CPU)
- **Headroom:** Substantial: room for additional features
