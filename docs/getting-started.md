# Getting started with ÜBERSAW

A step-by-step guide to go from zero to running ÜBERSAW on your
Daisy Patch Init module.

## What you need

### Hardware

- **Daisy Patch Init** (10HP Eurorack module by Electro-Smith)
- **USB-C cable** (data-capable, not charge-only)
- A computer (macOS, Linux, or Windows)

### Optional but recommended

- Eurorack case with power (to actually hear it)
- A V/Oct CV source (sequencer, keyboard, etc.)
- Headphones or speakers connected to the audio output

## Step 1: Install the toolchain

You need three tools: the ARM cross-compiler, a USB flasher, and Git.

### macOS

```bash
brew install armmbed/formulae/arm-none-eabi-gcc
brew install dfu-util
```

Git and Make come with Xcode Command Line Tools:

```bash
xcode-select --install
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install dfu-util make git
```

Download ARM GCC **v10.3-2021.10** from the
[ARM developer site][arm-gcc].
Extract it and add the `bin/` folder to your `PATH`.

[arm-gcc]: https://developer.arm.com/downloads/-/gnu-rm/10-3-2021-10

### Windows

The easiest path is the
[Daisy Toolchain installer](https://daisy.audio/tutorials/cpp-dev-env/)
which bundles everything. Alternatively, install manually in MSYS2/MINGW64.

### Verify everything works

```bash
arm-none-eabi-gcc --version
# Should show: 10.3.1 20210824 (release)

dfu-util --version
make --version
git --version
```

**Important:** libDaisy requires ARM GCC **v10.3-2021.10** specifically.
Other versions may cause build errors.

## Step 2: Get the source code

```bash
git clone --recurse-submodules https://github.com/drktn/ubersaw.git
cd ubersaw
```

The `--recurse-submodules` flag also downloads the two libraries
(libDaisy and DaisySP) that live inside the repository.

If you already cloned without that flag:

```bash
git submodule update --init --recursive
```

## Step 3: Build the libraries (first time only)

```bash
cd libDaisy && make && cd ..
cd DaisySP && make && cd ..
```

This compiles the Daisy hardware library and DSP library. Takes 1-3
minutes. You only need to do this once (unless you update the submodules).

## Step 4: Build the firmware

```bash
make
```

If successful, you will see memory usage output and the build produces:

- `build/uebersaw.bin` — the firmware file to flash
- `build/uebersaw.elf` — debug binary (for JTAG debugging)

To start fresh:

```bash
make clean && make
```

## Step 5: Flash to your Daisy Patch Init

### Enter DFU mode

The Daisy needs to be in "bootloader mode" to receive new firmware:

1. **Connect** the Daisy to your computer via USB-C
2. **Hold** the BOOT button (small button on the Daisy board)
3. While holding BOOT, **press and release** the RESET button
4. **Release** the BOOT button
5. The LED should turn off — the Daisy is now in DFU mode

### Flash the firmware

```bash
make program-dfu
```

This uploads `build/uebersaw.bin` to the Daisy. It takes a few
seconds. When complete, the module automatically resets and starts
running ÜBERSAW.

### Troubleshooting flash issues

**"No DFU capable USB device available"**
The Daisy is not in DFU mode. Repeat the BOOT + RESET sequence above.
Also try a different USB cable — some cables are charge-only.

**Permission denied (Linux)**
Add a udev rule for the Daisy:

```bash
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0483", MODE="0666"' \
  | sudo tee /etc/udev/rules.d/50-daisy.rules
sudo udevadm control --reload-rules
```

Unplug and replug the USB cable, then try again.

## Step 6: Play

Once flashed, ÜBERSAW is running. Here is what the controls do:

| Control | Function |
|---------|----------|
| **Knob 1** (PITCH) | Base pitch — sets the fundamental frequency |
| **Knob 2** (DETUNE) | Spread between the 7 oscillators |
| **Knob 3** (MIX) | Balance: center osc vs. side oscillators |
| **Knob 4** (TONE) | High-pass filter cutoff offset |
| **CV 1** | 1V/Oct pitch input (from sequencer/keyboard) |
| **CV 2-4** | Modulation for detune, mix, tone |
| **Gate 1** | Note trigger (randomizes oscillator phases) |
| **Toggle switch** | Authentic (24-bit) / Modern (float) mode |
| **Button** | Manual trigger (same as gate, for testing) |
| **LED** | Lights when gate is active |

### Quick test without a Eurorack setup

1. Connect headphones or powered speakers to the **OUT L** jack
2. Turn **PITCH** to noon (middle position)
3. Turn **DETUNE** to about 10 o'clock (subtle spread)
4. Turn **MIX** fully clockwise (all oscillators audible)
5. Turn **TONE** to noon
6. Press the **button** to trigger — you should hear the supersaw

### With a sequencer

1. Patch your sequencer's **V/Oct output** to **CV 1**
2. Patch your sequencer's **gate output** to **Gate 1**
3. Set **PITCH** knob to noon as a starting point
4. Adjust to taste — each gate pulse randomizes the oscillator phases,
   giving each note a slightly different character (just like the
   JP-8000)

### Two modes

The **toggle switch** selects between:

- **Authentic mode** — 24-bit fixed-point arithmetic that faithfully
  recreates the TC170C140 DSP behavior from the original JP-8000.
  Slightly grittier due to integer truncation.
- **Modern mode** — floating-point processing. Cleaner, wider dynamic
  range, same algorithm.

Both are valid — try both and use whichever sounds better to you.

## Updating the firmware

When new versions are released:

```bash
cd ubersaw
git pull
make clean && make
# Enter DFU mode (BOOT + RESET), then:
make program-dfu
```

If the submodules were updated:

```bash
git submodule update --init --recursive
cd libDaisy && make clean && make && cd ..
cd DaisySP && make clean && make && cd ..
make clean && make
```

## Running the tests (optional)

The engine has a desktop test suite that runs without hardware:

```bash
cd tests && make test
```

This compiles and runs the tests using your computer's native compiler
(not the ARM toolchain). Useful if you want to modify the code and
verify correctness before flashing.

## Further reading

- [controls.md](controls.md) — detailed control mapping and CV behavior
- [algorithm.md](algorithm.md) — how the supersaw algorithm works
- [build-guide.md](build-guide.md) — advanced build options and
  troubleshooting
