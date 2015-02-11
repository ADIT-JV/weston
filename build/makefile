# -----------------------------------------------------------------------------
#        (c) ADIT - Advanced Driver Information Technology JV
# -----------------------------------------------------------------------------
# Title:       Subcomponent makefile
#
# Description: Makefile for the subcomponent
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Base settings
# -----------------------------------------------------------------------------

COMP_DIR      = ..
SUBCOMP_NAME  = $(notdir $(shell pwd))

ARCHIVE       =
PROGRAM       =
SHAREDLIB     = $(LIBRARY_DIR)/ivi-shell.so

IVI_SHELL_DIR = $(SOURCE_DIR)/ivi-shell
SHARED_DIR    = $(SOURCE_DIR)/shared
PROTOCOL_DIR  = $(SOURCE_DIR)/protocol

# -----------------------------------------------------------------------------
# Component settings
# -----------------------------------------------------------------------------

include ./settings.mk

# -----------------------------------------------------------------------------
# Sub-Component settings (kernel mode)
# -----------------------------------------------------------------------------
ifeq ($(MODE),KERNEL)

all \
clean \
install \
help \
info:

else
# -----------------------------------------------------------------------------
# Sub-Component settings (user mode)
# -----------------------------------------------------------------------------

INCLUDES     =  -Iinclude_component \
                -I$(KP_SYSROOT_FOLDER)/usr/include \
                -I$(KP_SYSROOT_FOLDER)/usr/include/weston \
                -I$(KP_SYSROOT_FOLDER)/usr/include/pixman-1 \
                -I$(IVI_SHELL_DIR) \
                -I$(PROTOCOL_DIR) \
                -I$(SHARED_DIR)

LIBRARIES    +=

PACKAGES     =  $(SOURCE_PATH_UL)/platform/system/common \
                $(SOURCE_PATH_UL)/platform/pfcfg/pfcfg

LINKAGES     =  rt \
                dl \
                wayland-client

C_DEFINES    += -DUSE_106_BC

SOURCES      =  \
                $(SHARED_DIR)/config-parser.c \
                $(PROTOCOL_DIR)/ivi-application-protocol.c \
                $(IVI_SHELL_DIR)/ivi-shell.c \
                $(IVI_SHELL_DIR)/input-panel-ivi.c \
                $(IVI_SHELL_DIR)/ivi-layout.c \
                $(IVI_SHELL_DIR)/ivi-layout-transition.c

# -----------------------------------------------------------------------------
# Rules
# -----------------------------------------------------------------------------

all: GENERATE $(ARCHIVE) $(PROGRAM) $(SHAREDLIB)

GENERATE:
	wayland-scanner server-header < $(PROTOCOL_DIR)/ivi-application.xml > $(PROTOCOL_DIR)/ivi-application-server-protocol.h
	wayland-scanner code < $(PROTOCOL_DIR)/ivi-application.xml > $(PROTOCOL_DIR)/ivi-application-protocol.c
	wayland-scanner server-header < $(PROTOCOL_DIR)/input-method.xml > $(PROTOCOL_DIR)/input-method-server-protocol.h
	wayland-scanner code < $(PROTOCOL_DIR)/input-method.xml > $(PROTOCOL_DIR)/input-method-protocol.c

install:
	install -d $(STAGING_PATH)/usr/lib/weston
	install -m 755 $(SHAREDLIB) $(STAGING_PATH)/usr/lib/weston
	install -d $(STAGING_PATH)/root/.config
	$(CP) ../conf/* $(STAGING_PATH)/root/.config
	install -d $(STAGING_PATH)/home/pulse/.config
	$(CP) ../conf/* $(STAGING_PATH)/home/pulse/.config

clean \
help \
info:
	-@$(call subcomp_$(MAKECMDGOALS))

# -----------------------------------------------------------------------------
# Include implicite rules
# -----------------------------------------------------------------------------

include $(BASE_DIR)/tools/config/mk/default_tail.mk

endif
