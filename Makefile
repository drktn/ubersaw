# ÜBERSAW — JP-8000 Supersaw for Daisy Patch Init
# ================================================

# Project Name
TARGET = uebersaw

# Sources
CPP_SOURCES = \
	src/main.cpp \
	src/supersaw.cpp

# Library Locations
LIBDAISY_DIR = libDaisy
DAISYSP_DIR = DaisySP

# Additional include paths for our own headers
C_INCLUDES += -Isrc

# Optimize for performance (important for 7-oscillator DSP at 96kHz)
OPT = -O2

# Core location, and generic Makefile
# This pulls in the entire Daisy build system: compiler flags, linker scripts,
# flash targets, include paths for libDaisy, DaisySP, STM32 HAL, and CMSIS.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
