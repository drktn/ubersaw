# ÜBERSAW build guide

## Prerequisites

### Toolchain

Install **ARM GCC v10.3-2021.10**: this exact version is required for libDaisy compatibility.

**macOS (Homebrew):**
```bash
brew install armmbed/formulae/arm-none-eabi-gcc
brew install dfu-util
```

**Linux (Ubuntu/Debian):**
```bash
# Download ARM GCC from:
# https://developer.arm.com/downloads/-/gnu-rm/10-3-2021-10
sudo apt install dfu-util make git
```

**Windows:**
Use the [Daisy Toolchain installer](https://daisy.audio/tutorials/cpp-dev-env/) which bundles everything, or install manually in MSYS2/MINGW64.

### Verify installation

```bash
arm-none-eabi-gcc --version
# Should show: arm-none-eabi-gcc (GNU Arm Embedded Toolchain 10.3-2021.10) 10.3.1 ...

dfu-util --version
make --version
git --version
```

## Clone and build

### 1. Clone the repository with submodules

```bash
git clone --recurse-submodules https://github.com/yourname/uebersaw.git
cd uebersaw
```

If you already cloned without `--recurse-submodules`:
```bash
git submodule update --init --recursive
```

### 2. Build the libraries (first time only)

```bash
cd libDaisy && make clean && make && cd ..
cd DaisySP && make clean && make && cd ..
```

This compiles `libdaisy.a` and `libDaisySP.a`. Takes 1-3 minutes depending on your machine.

### 3. Build the firmware

```bash
make clean
make
```

Build output appears in `build/`:
- `build/uebersaw.elf`: Debug binary with symbols
- `build/uebersaw.bin`: Raw binary for flashing
- `build/uebersaw.hex`: Intel HEX format

The build also prints memory usage:
```
Memory region         Used Size  Region Size  %age Used
           FLASH:       xxxxx B       128 KB     xx.xx%
         DTCMRAM:       xxxxx B       128 KB     xx.xx%
            SRAM:       xxxxx B       512 KB     xx.xx%
```

### 4. Flash to hardware

#### USB DFU (most common method):

1. Put Daisy into DFU bootloader mode:
   - **Hold** the BOOT button
   - **Press and release** the RESET button
   - **Release** the BOOT button
   - The LED should turn off: the device is now in DFU mode

2. Flash:
```bash
make program-dfu
```

#### With Daisy Bootloader (faster iteration):

If you've installed the Daisy bootloader, you don't need the button dance:
```bash
# Install bootloader (one-time)
cd libDaisy && make program-boot && cd ..

# Flash your program via bootloader
make program-dfu
```

#### Via JTAG (for debugging):
```bash
make program
# Uses OpenOCD — requires ST-Link or compatible debug probe
```
## Troubleshooting

**"dfu-util: No DFU capable USB device available"**: The Daisy is not in DFU mode. Repeat the BOOT + RESET button sequence.

**Compilation errors about missing headers**: Ensure submodules are initialized and libraries are built: `git submodule update --init && cd libDaisy && make && cd ../DaisySP && make`

**arm-none-eabi-gcc version mismatch**: libDaisy requires v10.3-2021.10. Other versions may produce build errors. Check with `arm-none-eabi-gcc --version`.
