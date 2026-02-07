// ============================================================================
// ÜBERSAW — Main Entry Point
// ============================================================================
// JP-8000 Supersaw recreation for Electro-Smith Daisy Patch Init
//
// Hardware: Daisy Patch Init (10HP Eurorack)
//   - STM32H750 ARM Cortex-M7 @ 480MHz
//   - 24-bit audio codec, stereo I/O
//   - 4 knobs, 4 CV inputs, 2 gate inputs
//
// Control mapping:
//   Knob 1 (CV_5) — Pitch (coarse frequency select)
//   Knob 2 (CV_6) — Detune (oscillator spread)
//   Knob 3 (CV_7) — Mix (center vs. side oscillators)
//   Knob 4 (CV_8) — Tone (HPF cutoff offset)
//   CV 1           — 1V/Oct pitch input
//   CV 2           — Detune CV modulation
//   CV 3           — Mix CV modulation
//   CV 4           — Tone CV modulation
//   Gate 1         — Note trigger (randomize phases)
//   Toggle         — Authentic (24-bit) / Modern (float) mode
//   Button         — Manual trigger
//   LED            — Gate activity indicator
// ============================================================================

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "supersaw.h"
#include "voct.h"
#include "gate_output.h"

using namespace daisy;
using namespace patch_sm;

// Hardware and DSP objects
DaisyPatchSM hw;
SuperSaw     supersaw;
Switch       toggle;
Switch       button;
GateOutput   gate_logic;

// State tracking
bool prev_gate = false;
bool prev_button = false;

#ifdef SCOPE_DEBUG
// Test signal modes: button cycles through these
// 0 = Normal supersaw (real knobs/CV)
// 1 = Single saw @ 440 Hz (verify waveform + freq counter)
// 2 = Single saw @ knob pitch (verify V/Oct tracking)
// 3 = Full supersaw @ 440 Hz, detune=0.5, mix=1.0 (verify full algo)
int scope_mode = 0;
constexpr int kScopeModeCount = 4;
uint32_t led_counter = 0;
#endif

// ============================================================================
// Audio callback — runs at interrupt priority, ~96000/4 = 24000 times/sec
// ============================================================================

static void AudioCallback(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
#ifdef SCOPE_DEBUG
    // CPU profiling: gate out HIGH during callback
    hw.gate_out_1.Write(true);
#endif

    // Read all hardware controls (knobs, CVs, gates, buttons)
    hw.ProcessAllControls();

    // ---- Read knobs (0.0 to 1.0) ----
    float knob_pitch  = hw.GetAdcValue(CV_5);  // Knob 1: Coarse pitch
    float knob_detune = hw.GetAdcValue(CV_6);  // Knob 2: Detune amount
    float knob_mix    = hw.GetAdcValue(CV_7);  // Knob 3: Mix / spread
    float knob_tone   = hw.GetAdcValue(CV_8);  // Knob 4: HPF tone offset

    // ---- Read CV inputs (0.0 to 1.0, maps from -5V to +5V) ----
    float cv_pitch  = hw.GetAdcValue(CV_1);    // CV 1: 1V/Oct
    float cv_detune = hw.GetAdcValue(CV_2);    // CV 2: Detune mod
    float cv_mix    = hw.GetAdcValue(CV_3);    // CV 3: Mix mod
    float cv_tone   = hw.GetAdcValue(CV_4);    // CV 4: Tone mod

    // ---- Read gate input ----
    bool gate = hw.gate_in_1.State();
    bool gate_trig = (gate && !prev_gate);     // Rising edge detection
    prev_gate = gate;

    // ---- Read toggle switch for mode ----
    toggle.Debounce();
    supersaw.SetAuthentic(toggle.Pressed());

    // ---- Read button ----
    button.Debounce();
    bool btn = button.Pressed();
    bool btn_trig = (btn && !prev_button);
    prev_button = btn;

#ifdef SCOPE_DEBUG
    // Button cycles test signal modes
    if (btn_trig) {
        scope_mode = (scope_mode + 1) % kScopeModeCount;
    }

    switch (scope_mode) {
    case 0:  // Normal supersaw
    {
        float freq = VoctToFreq(cv_pitch, knob_pitch);
        supersaw.SetFreq(freq);

        float detune_cv = (cv_detune - 0.5f) * 2.0f;
        float detune = fclamp(knob_detune + detune_cv * 0.5f, 0.0f, 1.0f);
        supersaw.SetDetune(detune);

        float mix_cv = (cv_mix - 0.5f) * 2.0f;
        float mix = fclamp(knob_mix + mix_cv * 0.5f, 0.0f, 1.0f);
        supersaw.SetMix(mix);

        float tone_cv = (cv_tone - 0.5f) * 2.0f;
        float tone = fclamp(knob_tone + tone_cv * 0.5f, 0.0f, 1.0f);
        supersaw.SetFilterOffset(0.25f + tone * 1.5f);

        if (gate_trig) {
            supersaw.Trigger();
        }
        break;
    }
    case 1:  // Single saw @ 440 Hz
        supersaw.SetFreq(440.0f);
        supersaw.SetDetune(0.0f);
        supersaw.SetMix(0.0f);
        supersaw.SetFilterOffset(0.25f);
        break;
    case 2:  // Single saw @ knob pitch
    {
        float freq = VoctToFreq(cv_pitch, knob_pitch);
        supersaw.SetFreq(freq);
        supersaw.SetDetune(0.0f);
        supersaw.SetMix(0.0f);
        supersaw.SetFilterOffset(0.25f);
        break;
    }
    case 3:  // Full supersaw @ 440 Hz, fixed params
        supersaw.SetFreq(440.0f);
        supersaw.SetDetune(0.5f);
        supersaw.SetMix(1.0f);
        supersaw.SetFilterOffset(1.0f);
        break;
    }
#else
    // ---- Combine knob + CV for each parameter ----
    // Pitch: knob sets base, CV adds V/Oct
    float freq = VoctToFreq(cv_pitch, knob_pitch);
    supersaw.SetFreq(freq);

    // Detune: knob + bipolar CV offset
    float detune_cv = (cv_detune - 0.5f) * 2.0f;  // Map to -1..+1
    float detune = fclamp(knob_detune + detune_cv * 0.5f, 0.0f, 1.0f);
    supersaw.SetDetune(detune);

    // Mix: knob + bipolar CV offset
    float mix_cv = (cv_mix - 0.5f) * 2.0f;
    float mix = fclamp(knob_mix + mix_cv * 0.5f, 0.0f, 1.0f);
    supersaw.SetMix(mix);

    // Tone: knob controls HPF offset from pitch-tracking
    // 0.0 = HPF well below fundamental (less filtering)
    // 1.0 = HPF at fundamental (maximum sub-fundamental removal)
    float tone_cv = (cv_tone - 0.5f) * 2.0f;
    float tone = fclamp(knob_tone + tone_cv * 0.5f, 0.0f, 1.0f);
    supersaw.SetFilterOffset(0.25f + tone * 1.5f);  // Range: 0.25x to 1.75x fundamental

    // ---- Trigger on gate rising edge or button press ----
    if (gate_trig || btn_trig) {
        supersaw.Trigger();
    }

    // ---- Gate output (pass-through or clock divider) ----
    bool gate_out = gate_logic.Process(gate);
    hw.gate_out_1.Write(gate_out);
#endif

    // ---- Generate audio ----
    for (size_t i = 0; i < size; i++) {
        float sample = supersaw.Process();

        // Output to both channels (mono)
        OUT_L[i] = sample;
        OUT_R[i] = sample;
    }

#ifdef SCOPE_DEBUG
    // CPU profiling: gate out LOW after callback completes
    hw.gate_out_1.Write(false);
#endif
}

// ============================================================================
// Main — hardware initialization and start
// ============================================================================

int main(void) {
    // Initialize Daisy Patch SM hardware
    hw.Init();

    // Configure audio: 96kHz to match JP-8000's 88.2kHz as closely as
    // possible. The high sample rate is critical — the original relies on
    // it to push aliasing artifacts above the audible range.
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_96KHZ);

    float sample_rate = hw.AudioSampleRate();

    // Initialize supersaw engine
    supersaw.Init(sample_rate);

    // Initialize toggle switch (pin B8) and button (pin B7)
    toggle.Init(DaisyPatchSM::B8, hw.AudioCallbackRate());
    button.Init(DaisyPatchSM::B7, hw.AudioCallbackRate());

    // Initialize gate output 1 (pin B5)
    hw.gate_out_1.Init(DaisyPatchSM::B5, GPIO::Mode::OUTPUT);

    // Initialize gate logic
    gate_logic.Init();

    // Start ADC for reading knobs and CV
    hw.StartAdc();

    // Start audio processing
    hw.StartAudio(AudioCallback);

    // Main loop: handle non-real-time tasks
    while (1) {
#ifdef SCOPE_DEBUG
        // LED indicates test signal mode
        switch (scope_mode) {
        case 0: hw.SetLed(false); break;
        case 1: hw.SetLed(true); break;
        case 2: hw.SetLed((led_counter / 500) % 2); break;  // slow blink
        case 3: hw.SetLed((led_counter / 125) % 2); break;  // fast blink
        }
        led_counter++;
#else
        // LED indicates gate activity
        hw.SetLed(hw.gate_in_1.State());
#endif

        // Small delay to prevent tight-loop issues
        System::Delay(1);
    }
}
