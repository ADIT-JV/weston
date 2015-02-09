# -----------------------------------------------------------------------------
#        (c) ADIT - Advanced Driver Information Technology JV
# -----------------------------------------------------------------------------
# Title:       Component Globals default makefile 'settings.mk'
#
# Description: This is the default makefile which contains
#              all basic settings needed for the component.
# -----------------------------------------------------------------------------
# Requirement: COMP_DIR, SUBCOMP_NAME
# -----------------------------------------------------------------------------

COMPONENT     = WESTON_IVI_SHELL
COMP_NAME     = ivi-shell

# -----------------------------------------------------------------------------
# Base Directory
# -----------------------------------------------------------------------------

BASE_DIR      = $(COMP_DIR)/..

# -----------------------------------------------------------------------------
# Include all global settings
# -----------------------------------------------------------------------------

include $(BASE_DIR)/tools/config/mk/default.mk

# -----------------------------------------------------------------------------
# Component Settings
# -----------------------------------------------------------------------------

C_FLAGS       =
CPP_FLAGS     =
LD_FLAGS      = -L$(LIBRARY_DIR) -L$(KP_SYSROOT_FOLDER)/usr/lib/ias
SO_FLAGS      =

C_DEFINES     =
CPP_DEFINES   = $(C_DEFINES)

## example of dynamic evaluiation of settings and options:
# ifeq ($(PRODUCT_OS),dual)
#    C_DEFINES     +=
# else
#    C_DEFINES     += -DIPOD_FAKE_AUTHENTICATE
# endif
