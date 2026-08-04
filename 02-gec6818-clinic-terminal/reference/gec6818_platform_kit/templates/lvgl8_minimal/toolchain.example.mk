# Safe Make include example for a GEC6818 LVGL 8 project.
# Populate only from scope=toolchain confirmed-runtime fields belonging to the
# same selected compiler, or explicit user input. Board and host-backend scope
# values cannot select a compiler.
# It does not select a compiler, invoke a build, link old FreeType, or use -static.

GEC6818_PROFILE_VERIFIED ?= 0
GEC6818_UNVERIFIED_DEFAULTS ?= 0

CC ?=
SYSROOT ?=
TARGET_ARCH_FLAGS ?=
TARGET_ABI_FLAGS ?=
TARGET_FLOAT_ABI ?=

ifeq ($(strip $(CC)),)
$(error CC is unset; select a compiler only after probe_toolchain.sh confirmation)
endif

ifneq ($(strip $(SYSROOT)),)
GEC6818_SYSROOT_FLAG := --sysroot=$(SYSROOT)
else
GEC6818_SYSROOT_FLAG :=
endif

ifneq ($(strip $(TARGET_FLOAT_ABI)),)
GEC6818_FLOAT_ABI_FLAG := -mfloat-abi=$(TARGET_FLOAT_ABI)
else
GEC6818_FLOAT_ABI_FLAG :=
endif

GEC6818_PLATFORM_CPPFLAGS := $(GEC6818_SYSROOT_FLAG)
GEC6818_PLATFORM_CFLAGS := $(TARGET_ARCH_FLAGS) $(TARGET_ABI_FLAGS) $(GEC6818_FLOAT_ABI_FLAG)

# A confirmed empty `-print-sysroot` observation means no explicit --sysroot is
# added here; it does not prove that the compiler's target libc is complete.
# Resolve FreeType through the confirmed sysroot or rebuild it with this exact
# toolchain. Never add archived libfreetype.a/.so paths to this file.
