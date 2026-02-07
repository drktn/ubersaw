# ÜBERSAW Roadmap

JP-8000 supersaw for Daisy Patch Init. Phases 1-4 done (software),
hardware verification pending, then Phase 5 extended features.

## Phase 1 — Core Engine (DONE)

- [x] 7-oscillator 24-bit phase accumulator supersaw
- [x] Asymmetric detune table from 39C3 reverse engineering
- [x] Pitch-proportional detuning
- [x] Center/side mix with Szabo parabolic curve (#3)
- [x] One-pole pitch-tracked HPF (confirmed correct, #1)
- [x] xorshift32 phase randomization on trigger
- [x] Dual mode: authentic (24-bit fixed-point) + float

## Phase 2 — Accuracy & Characterization (DONE)

- [x] V/Oct math pipeline verified (#2)
- [x] Multi-octave tracking tests, C1-C8 (#2)
- [x] A/B authentic vs float characterization (#4)
- [x] Szabo 11th-order polynomial detune curve (#5)
- [x] Aliasing analysis: 96 kHz vs 88.2 kHz (#6)
- [x] Frequency bounds clamping (10 Hz - 20 kHz)
- [x] 60 tests passing

## Phase 3 — Controls & Smoothing (DONE)

- [x] Knob + bipolar CV parameter pattern
- [x] Anti-click parameter smoothing (detune, mix, filter offset)
- [x] Portamento/glide for pitch (#9)
- [x] ADC one-pole smoothing class (#7)

## Phase 4 — Hardware I/O (DONE, verification pending)

- [x] Toggle switch: authentic/float mode select (#11)
- [x] Button: manual trigger (#11)
- [x] Gate input: rising edge phase randomization (#11)
- [x] Gate output: pass-through + clock divider (#10)
- [x] LED: gate activity indicator
- [x] Scope debug mode for hardware verification (#12)
- [ ] **CPU load profiling** — needs hardware + scope
- [ ] **Block size 2 feasibility** — needs hardware + scope

See [scope-debug.md](scope-debug.md) for measurement procedures.

## Phase 5 — Extended Features (next)

Ideas for after hardware verification. Each needs its own issue.

### Audio enhancements

- [ ] Unison voice count (1/3/5/7 selectable)
- [ ] Sub-oscillator (-1 oct square or saw)
- [ ] Soft clipping / saturation on output
- [ ] Stereo spread (L/R detune panning)

### Modulation

- [ ] Internal LFO → detune/mix/tone
- [ ] Envelope follower on gate for mix/tone
- [ ] CV2-4 attenuverter scaling

### Hardware features

- [ ] Gate out: trigger on zero-crossing for sync
- [ ] Gate in 2: hard sync or freeze
- [ ] MIDI input (via USB or TRS)
- [ ] Preset save/recall (flash storage)

### Performance

- [ ] SIMD/DSP intrinsics for phase accumulation
- [ ] Block-level parameter interpolation
- [ ] Oversampling experiment (2x48k vs 96k)

## Priorities

1. Hardware verification (Phase 4 blockers) — first session with module
2. Pick 2-3 Phase 5 features based on musical usefulness
3. Create issues before implementing anything
