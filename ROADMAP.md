# ÜBERSAW development roadmap

## Phase 1: Core oscillator engine (current)

- [x] Project scaffold and build system
- [x] 7-oscillator phase accumulator with 24-bit fixed-point arithmetic
- [x] Original detune table implementation
- [x] Basic mixing (center full volume, sides scaled by spread)
- [x] One-pole pitch-tracked high-pass filter
- [x] Random phase initialization on gate trigger
- [ ] Verify V/Oct tracking accuracy across 5+ octaves
- [ ] Tune detune parameter scaling against JP-8000 recordings
- [ ] A/B test 24-bit mode vs. float mode for audible differences

## Phase 2: Filter research (done)

Research confirms the JP-8000 uses a **one-pole HPF** — not a multi-pole SVF.

Sources:

- 39C3 reverse engineering (Giulioz): one-pole HPF on supersaw output
- Adam Szabo thesis: "pitch-tracked HPF removes noise below fundamental"
- JE-8086 emulator (same reverse engineering basis): one-pole

Current implementation is correct. No changes needed.

Optional future work:

- [ ] Implement HPF in fixed-point for full authenticity (low priority)

## Phase 3: Parameter calibration

- [ ] Calibrate detune knob curve to match JP-8000 front panel behavior
- [ ] Calibrate mix knob curve (Szabo documented a parabolic side-voice curve)
- [ ] Verify aliasing characteristics at 96 kHz vs. original 88.2 kHz

## Phase 4: Polish and features

- [ ] Implement toggle switch GPIO reading for mode select
- [ ] Implement button GPIO reading for manual trigger
- [ ] ADC smoothing/filtering for stable knob readings
- [ ] Anti-click on parameter changes
- [ ] CPU load optimization (profile at 96 kHz with block size 4)
- [ ] Consider block size 2 for lower callback noise (1 kHz tone artifact)
- [ ] Portamento/glide option (via toggle or CV threshold)
- [ ] Gate output: pass-through or clock divider for sequencer sync

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
