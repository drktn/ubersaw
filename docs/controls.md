# ÜBERSAW control mapping

## Panel layout

```
┌─────────────────────────────────┐
│          Ü B E R S A W          │
│                                 │
│   (K1)    (K2)    (K3)    (K4)  │
│  PITCH   DETUNE   MIX    TONE  │
│                                 │
│  [CV1]   [CV2]   [CV3]   [CV4] │
│  V/OCT    DET     MIX    TONE  │
│                                 │
│  [GT1]   [GT2]   [AL]    [AR]  │
│  GATE     ---    OUT L   OUT R  │
│                                 │
│   (B)    [T]     ◉ LED          │
│  TRIG    MODE                   │
└─────────────────────────────────┘
```

## Knobs

| Knob | Label  | ADC Channel | Range | Function |
|------|--------|-------------|-------|----------|
| K1   | PITCH  | `CV_5`      | 0.0-1.0 | Coarse pitch select. Spans C1 (~32 Hz) to C7 (~2093 Hz). Sets the base note before V/Oct CV is applied. |
| K2   | DETUNE | `CV_6`      | 0.0-1.0 | Detune spread amount. 0 = all oscillators in unison (single saw). Max = full JP-8000 detune spread. |
| K3   | MIX    | `CV_7`      | 0.0-1.0 | Balance between center and side oscillators. 0 = center only. 1.0 = side oscillators at full volume. This is the JP-8000's "Mix" knob. |
| K4   | TONE   | `CV_8`      | 0.0-1.0 | High-pass filter cutoff offset from the pitch-tracked default. Low = less filtering (more bass). High = HPF at/above fundamental (tighter, brighter). |

## CV inputs

| Jack | Label | ADC Channel | Voltage | Function |
|------|-------|-------------|---------|----------|
| CV1  | V/OCT | `CV_1`      | -5V to +5V | 1V/Oct pitch control. 0V = reference pitch set by PITCH knob. Standard Eurorack V/Oct tracking. |
| CV2  | DET   | `CV_2`      | -5V to +5V | Detune modulation. Bipolar: adds to DETUNE knob setting. |
| CV3  | MIX   | `CV_3`      | -5V to +5V | Mix modulation. Bipolar: adds to MIX knob setting. |
| CV4  | TONE  | `CV_4`      | -5V to +5V | Tone/HPF modulation. Bipolar: adds to TONE knob setting. |

## Gate inputs

| Jack | Label | Pin | Function |
|------|-------|-----|----------|
| GT1  | GATE  | `gate_in_1` | Note trigger. Rising edge randomizes all oscillator phases (matching JP-8000 note-on behavior). |
| GT2  | ---   | `gate_in_2` | Reserved for future use. Potential uses: hard sync, freeze, accent. |

## Audio outputs

| Jack | Label | Channel | Function |
|------|-------|---------|----------|
| AL   | OUT L | `out[0]` | Main supersaw output (mono). |
| AR   | OUT R | `out[1]` | Duplicate of main output (mono). Future: could carry unfiltered or differently processed signal. |

## Switches

| Control | Type | Function |
|---------|------|----------|
| Toggle  | ON-ON | **Mode select.** Position 1: Authentic 24-bit fixed-point mode (true to original hardware). Position 2: Modern floating-point mode (cleaner, less aliasing character). |
| Button  | Momentary | **Manual trigger.** Press to randomize oscillator phases, same as receiving a gate. Useful for testing without CV connected. |

## LED

| Indicator | Function |
|-----------|----------|
| Red LED   | Gate activity. Lights when Gate 1 is high or button is pressed. |

## Parameter interaction

The Knob + CV paradigm follows standard Eurorack convention: **the knob sets a base value, and CV adds or subtracts from it.** CV inputs are bipolar (-5V to +5V), providing both positive and negative modulation. The combined value is clamped to the valid range (0.0 to 1.0 for most parameters).

For V/Oct pitch: the PITCH knob selects the base note, and CV1 adds precise pitch control on top. Together they determine the fundamental frequency of all seven oscillators.
