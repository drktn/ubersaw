# The JP-8000 supersaw algorithm

## How Roland kept a secret for 28 years

The Roland JP-8000, released in 1997, introduced the "Supersaw" oscillator: a sound so distinctive it defined an entire genre of electronic music. For nearly three decades, nobody knew exactly how it worked. Roland's implementation ran on four custom Toshiba TC170C140 "ESP2" DSP chips with a completely undocumented instruction set. No datasheet was ever published.

In December 2025, a researcher known as Giulioz presented **"From Silicon to Darude Sand-storm: breaking famous synthesizer DSPs"** at the 39th Chaos Communication Congress (39C3). Through silicon-level reverse engineering: decapping the chip, microscopy, automated standard cell classification, opcode fuzzing, and JIT compilation, the team extracted the actual firmware and decoded the supersaw algorithm.

The revelation: **the supersaw is far simpler than anyone imagined.**

## The complete algorithm

```c
int24_t saw[7] = {0};  // Phase accumulators (randomized on note-on)

const int24_t detune_table[7] = { 0, 128, -128, 816, -824, 1408, -1440 };

int24_t next(int24_t pitch, int24_t spread, int24_t detune) {
    int24_t sum = 0;
    for (int i = 0; i < 7; i++) {
        int24_t voice_detune = (detune_table[i] * (pitch * detune)) >> 7;
        saw[i] += pitch + voice_detune;
        if (i == 0)
            sum += saw[i];
        else
            sum += saw[i] * spread;
    }
    return high_pass(sum);
}
```

This function is called once per sample at **88,200 Hz** (derived from the system clock: 67.7376 MHz / (16 x 3 x 16) = 88,200).

## Why it works: component breakdown

### Phase accumulator sawtooths

Each oscillator is a 24-bit integer that increments by `pitch + voice_detune` every sample. There is no wavetable. There is no waveform shaping. The raw integer value *is* the sawtooth waveform. When the 24-bit integer overflows (exceeds +/-8,388,607), it wraps around: this natural wrap-around creates the sawtooth discontinuity.

This is the simplest possible sawtooth generator: a counter that overflows.

### The detune table

The seven offsets are **asymmetric by design**:

| Oscillator | Detune offset | Role          |
|-----------|---------------|---------------|
| 0         | 0             | Center (undetuned) |
| 1         | +128          | Nearest pair, slightly sharp |
| 2         | -128          | Nearest pair, slightly flat |
| 3         | +816          | Middle pair, sharp |
| 4         | -824          | Middle pair, flat (8 wider than +816) |
| 5         | +1408         | Widest pair, sharp |
| 6         | -1440         | Widest pair, flat (32 wider than +1408) |

The asymmetry means the detuned oscillators are not perfectly mirrored around the fundamental. This subtle imperfection prevents the sterile sound of perfectly symmetric detuning: it adds organic width and movement.

### Pitch-proportional detuning

The formula `(detune_table[i] * (pitch * detune)) >> 7` makes detuning proportional to pitch. A note one octave higher gets twice the absolute detuning, maintaining consistent musical-interval spread across the keyboard. The `>> 7` right-shift (divide by 128) scales the result to a usable range.

### The mixing formula

The center oscillator always runs at full volume. The six side oscillators are each multiplied by the `spread` (Mix) parameter before being summed. At Mix = 0, you hear a single sawtooth. As Mix increases, the detuned oscillators fade in, building the classic supersaw wall of sound.

### The high-pass filter

After summing all seven oscillators, the result passes through a pitch-tracked high-pass filter. This is critical: the naive (non-bandlimited) sawtooth waves produce aliased harmonics that fold back below the fundamental. These sub-fundamental artifacts sound ugly and muddy. The HPF removes them while preserving the aliased harmonics *above* the fundamental, which contribute desirable brightness and "air."

The 88.2 kHz sample rate works in concert with the HPF: at this rate, the Nyquist frequency (44.1 kHz) pushes most aliasing well above audibility for musical pitches.

### Random phase initialization

On every note-on event, all seven oscillators receive random starting phases. This means every note press sounds slightly different: the phase relationships between oscillators vary, creating subtle timbral variation that gives the sound life.

## Prior art: Adam Szabo's analysis

In 2010, Adam Szabo (KTH Stockholm) published "How to Emulate the Super Saw," a black-box analysis based on FFT and oscilloscope measurements of JP-8000 output. His findings were remarkably close:

- **Correctly identified:** 7 sawtooths, pitch-tracked HPF, asymmetric detuning, random phases, aliasing contribution
- **Approximated with an 11th-degree polynomial:** The perceived non-linear detune curve. The actual implementation uses simple fixed-point integer multiplication: the perceived non-linearity emerges from the parameter mapping
- **Complex mix equations:** Szabo measured center volume decreasing and side volume increasing parabolically. The actual code is simpler: fixed center gain, linear side scaling by a single parameter

Szabo's paper was the best available reference for 15 years. The reverse engineering confirms his fundamental insights while revealing just how minimal the actual code is.

## Implementation notes for ÜBERSAW

### Sample rate

The Daisy runs at **96 kHz** (closest available to 88.2 kHz). This produces very similar aliasing characteristics. For a perfectionist match, internal 2x oversampling at 48 kHz could better approximate 88.2 kHz, but 96 kHz is simpler and close enough.

### 24-bit emulation on 32-bit ARM

The STM32H750 uses 32-bit integers. We emulate 24-bit overflow with:

```cpp
int32_t Wrap24(int32_t val) {
    return (val << 8) >> 8;  // Sign-extend from bit 23
}
```

This must be applied after every arithmetic operation that could exceed 24-bit range: additions, multiplications, and the phase accumulator advance.

### Filter implementation

The exact JP-8000 HPF type is not fully documented in the public reverse engineering data. Community analysis suggests a multi-pole SVF high-pass. The initial implementation uses a one-pole HPF for simplicity, with the roadmap prioritizing filter refinement as Phase 2. The one-pole captures the essential behavior (removing sub-fundamental content) while being stable and computationally trivial.
