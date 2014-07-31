# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(CUSTOMER_SERIALNO_MAC), true)
 LOCAL_CFLAGS += -DCUSTOMER_SERIALNO_MAC
endif

LOCAL_SRC_FILES:= \
	builtins.c \
	init.c \
	devices.c \
	property_service.c \
	util.c \
	parser.c \
	bootenv.c \
	logo.c \
	keychords.c \
	signal_handler.c \
	init_parser.c \
	ueventd.c \
	ueventd_parser.c \
	watchdogd.c \
	ubi/ubiutils-common.c \
	ubi/libubi.c

LOCAL_C_INCLUDES += \
	external/mtd-utils/include/ \
	external/mtd-utils/ubi-utils/include/ \
	external/zlib


ifeq ($(strip $(INIT_BOOTCHART)),true)
LOCAL_SRC_FILES += bootchart.c
LOCAL_CFLAGS    += -DBOOTCHART=1
endif

ifeq ($(strip $(UBOOTENV_SAVE_IN_NAND)),true)
LOCAL_CFLAGS += -DUBOOTENV_SAVE_IN_NAND
endif

ifeq ($(strip $(INIT_BOOTARGSCHECK)),true)
LOCAL_CFLAGS    += -DBOOT_ARGS_CHECK=1
endif

ifeq ($(BOARD_MATCH_LOGO_SIZE),true)
  LOCAL_CFLAGS += -DMATCH_LOGO_SIZE
endif

ifeq ($(BOARD_TVMODE_ALL_SCALE),true)
  LOCAL_CFLAGS += -DTVMODE_ALL_SCALE
endif
ifeq ($(TARGET_HAS_HDMIONLY_FUNCTION),true)
  LOCAL_CFLAGS += -DHAS_HDMIONLY_FUNCTION
endif

ifeq ($(TARGET_BOARD_PLATFORM), meson8)
LOCAL_CFLAGS += -DMESON8_ENVSIZE
endif

ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
LOCAL_CFLAGS += -DALLOW_LOCAL_PROP_OVERRIDE=1
endif
ifdef DOLBY_UDC_MULTICHANNEL
  LOCAL_CFLAGS += -DDOLBY_UDC_MULTICHANNEL
endif #DOLBY_UDC_MULTICHANNEL
ifdef DOLBY_DAP
LOCAL_CFLAGS += -DDOLBY_DAP
endif #DOLBY_DAP

ifdef EPG_ENABLE
LOCAL_CFLAGS += -DEPG_ENABLE
endif #EPG_ENABLE

LOCAL_MODULE:= init

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libext4_utils_static libsparse_static libfs_mgr libc libz \
	libe2fsck_static \
        libext2fs \
        libext2_blkid \
        libext2_uuid \
        libext2_profile \
        libext2_com_err \
        libext2_e2p \
        liblogwrap \
	libcutils \
	liblog \
	libc \
	libselinux \
	libmincrypt \
	libext4_utils_static

LOCAL_C_INCLUDES += system/extras/ext4_utils

include $(BUILD_EXECUTABLE)

# Make a symlink from /sbin/ueventd and /sbin/watchdogd to /init
SYMLINKS := \
	$(TARGET_ROOT_OUT)/sbin/ueventd \
	$(TARGET_ROOT_OUT)/sbin/watchdogd

$(SYMLINKS): INIT_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> ../$(INIT_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf ../$(INIT_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
