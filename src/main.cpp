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
dsy_gpio     gate_out_1;
GateOutput   gate_logic;

// State tracking
bool prev_gate = false;
bool prev_button = false;

// ============================================================================
// Audio callback — runs at interrupt priority, ~96000/4 = 24000 times/sec
// ============================================================================

static void AudioCallback(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
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

    // ---- Read button for manual trigger ----
    button.Debounce();
    bool btn = button.Pressed();
    bool btn_trig = (btn && !prev_button);
    prev_button = btn;

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
    dsy_gpio_write(&gate_out_1, gate_out);

    // ---- Generate audio ----
    for (size_t i = 0; i < size; i++) {
        float sample = supersaw.Process();

        // Output to both channels (mono)
        OUT_L[i] = sample;
        OUT_R[i] = sample;
    }
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
    gate_out_1.pin = DaisyPatchSM::B5;
    gate_out_1.mode = DSY_GPIO_MODE_OUTPUT_PP;
    dsy_gpio_init(&gate_out_1);

    // Initialize gate logic
    gate_logic.Init();

    // Start ADC for reading knobs and CV
    hw.StartAdc();

    // Start audio processing
    hw.StartAudio(AudioCallback);

    // Main loop: handle non-real-time tasks
    while (1) {
        // LED indicates gate activity
        hw.SetLed(hw.gate_in_1.State());

        // Small delay to prevent tight-loop issues
        System::Delay(1);
    }
}
