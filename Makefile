# Dome Heartbeat Monitor v2
# Based on LUFA Library makefile template

# Run "make help" for target help.

# Maximum number of seconds to run the motors
MAX_OPEN_SECONDS = 23
MAX_CLOSE_SECONDS = 55
MAX_AUX_CLOSE_SECONDS = 65
SIREN_ACTIVE_SECONDS = 5

MCU                = atmega32u4
ARCH               = AVR8
BOARD              = MICRO
F_CPU              = 16000000
F_USB              = $(F_CPU)
AVRDUDE_PROGRAMMER = avr109
AVRDUDE_PORT       = /dev/tty.usbmodem*

OPTIMIZATION = s
TARGET       = main
SRC          = main.c usb.c usb_descriptors.c $(LUFA_SRC_USB) $(LUFA_SRC_USBCLASS)
LUFA_PATH    = LUFA
CC_FLAGS     = -DUSE_LUFA_CONFIG_HEADER -DMAX_OPEN_SECONDS=$(MAX_OPEN_SECONDS) -DMAX_CLOSE_SECONDS=$(MAX_CLOSE_SECONDS) -DMAX_AUX_CLOSE_SECONDS=$(MAX_AUX_CLOSE_SECONDS) -DSIREN_ACTIVE_SECONDS=$(SIREN_ACTIVE_SECONDS)
LD_FLAGS     = -Wl,-u,vfprintf -lprintf_flt -lm

# Default target
all:

# Alias the avrdude target to install for convenience
install: avrdude

disasm:	main.elf
	avr-objdump -d main.elf

# Include LUFA-specific DMBS extension modules
DMBS_LUFA_PATH ?= $(LUFA_PATH)/Build/LUFA
include $(DMBS_LUFA_PATH)/lufa-sources.mk
include $(DMBS_LUFA_PATH)/lufa-gcc.mk

# Include common DMBS build system modules
DMBS_PATH      ?= $(LUFA_PATH)/Build/DMBS/DMBS
include $(DMBS_PATH)/core.mk
include $(DMBS_PATH)/cppcheck.mk
include $(DMBS_PATH)/doxygen.mk
include $(DMBS_PATH)/dfu.mk
include $(DMBS_PATH)/gcc.mk
include $(DMBS_PATH)/hid.mk
include $(DMBS_PATH)/avrdude.mk
include $(DMBS_PATH)/atprogram.mk
