/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _GSP_IPC_TRUSTY_H_
#define _GSP_IPC_TRUSTY_H_

struct gsp_message {
	uint32_t cmd;
	uint8_t payload[4];
};

enum gsp_command {
	TA_SET_SECURE = 8,
	TA_SET_UNSECURE  = 9
};

/*
 * 设备没有 "不支持 tipc" 那套条件编译：gsp_ipc_trusty.c 是无条件编译的，
 * 设备 .config 里也根本没有 CONFIG_DRM_SPRD_GSP_IPC_TRUSTY 这个符号
 * （recon/raw/config.txt 里只有 CONFIG_DRM_SPRD_GSP_DVFS=y）。原先的
 * #else 分支那三条 GSP_DEBUG 串在 .ko 里一次都不出现，所以跟着删。
 */
int gsp_tipc_init(void);
ssize_t gsp_tipc_write(void *data_ptr, size_t len);
ssize_t gsp_tipc_read(void *data_ptr, size_t max_len);
void gsp_tipc_exit(void);

#endif
