# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	data_integrity_guard.c

LOCAL_MODULE:= dig

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_STATIC_LIBRARIES := libcutils libc\
	libe2fsck_static \
        libext2fs \
        libext2_blkid \
        libext2_uuid \
        libext2_profile \
        libext2_com_err \
	libext2_e2p \
	libcrypto_static

include $(BUILD_EXECUTABLE)

