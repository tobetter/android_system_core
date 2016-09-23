/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _INIT_LOG_H_
#define _INIT_LOG_H_

#include <cutils/klog.h>

#define ERROR(x...)   init_klog_write(KLOG_ERROR_LEVEL, x)
#define NOTICE(x...)  init_klog_write(KLOG_NOTICE_LEVEL, x)
#define INFO(x...)    init_klog_write(KLOG_INFO_LEVEL, x)

void init_klog_write(int level, const char* fmt, ...) __printflike(2, 3);
int selinux_klog_callback(int level, const char* fmt, ...) __printflike(2, 3);

#define D(fmt, args...) \
    { ERROR(" D : [File] : %s; [Line] : %d; [Func] : %s; " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

#define W(fmt, args...) \
    { ERROR(" W : [File] : %s; [Line] : %d; [Func] : %s; " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

#define E(fmt, args...) \
    { ERROR(" E : [File] : %s; [Line] : %d; [Func] : %s; " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

#endif
