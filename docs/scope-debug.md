# Scope Debug Mode

Compile-time mode for CPU profiling and waveform verification.
Zero overhead when disabled. Requires oscilloscope + audio probe.

## Build

```bash
make clean && make scope-debug
make program-dfu
```

Normal build (`make`) is completely unaffected.

## Controls in Scope Debug Mode

| Control | Function |
|---------|----------|
| Button | Cycle test signal mode (0→1→2→3→0) |
| Toggle | Authentic/float (still active) |
| LED | Mode indicator (see below) |
| Gate out | CPU profiling pulse |
| Knobs/CV | Active in modes 0 and 2 only |

### LED Modes

| Mode | LED | Signal |
|------|-----|--------|
| 0 | Off | Normal supersaw (real knobs/CV) |
| 1 | Solid | Single saw @ 440 Hz |
| 2 | Slow blink | Single saw @ knob pitch |
| 3 | Fast blink | Full supersaw @ 440 Hz, detune=0.5, mix=1.0 |

## Measurement Procedures

### CPU Load

1. Probe gate output jack, set trigger on rising edge
2. Callback period = 41.67 us (96 kHz / block 4)
3. Measure pulse width: `load% = pulse_width / 41.67us * 100`
4. Flip toggle to compare authentic vs float CPU cost
5. Try all 4 modes — mode 0 (normal) is the real-world load

### Block Size 2 Feasibility

1. In `src/main.cpp`, change `SetAudioBlockSize(4)` to `(2)`
2. Rebuild: `make clean && make scope-debug && make program-dfu`
3. New callback period = 20.83 us
4. Feasible if pulse width < ~18 us (leaves headroom)
5. Test both authentic and float modes

### Waveform Verification

**Mode 1 — single saw @ 440 Hz:**

- Scope: sawtooth waveform, no glitches
- Freq counter: should read 440 Hz
- Flip toggle: compare authentic vs float shape

**Mode 2 — single saw @ knob pitch:**

- Sweep knob 1, verify freq counter tracks smoothly
- Patch V/Oct CV: verify 1V/Oct tracking
- Check frequency bounds at knob extremes

**Mode 3 — full supersaw @ 440 Hz:**

- FFT: harmonic spread visible around 440 Hz
- Compare authentic vs float spectral shape
- RMS level should be higher than mode 1

## Scopes

- Keysight EDUX1052A (2ch, FFT, freq counter)
- R&S RTC1002 (2ch, FFT, freq counter)

Probe audio output jacks directly — no extra wiring needed.
