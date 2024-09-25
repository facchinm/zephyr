/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/llext/symbol.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FORCE_EXPORT_SYM(name) \
	extern void name(void); \
	EXPORT_SYMBOL(name);


EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strchr);

EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);

EXPORT_SYMBOL(malloc);
EXPORT_SYMBOL(realloc);
EXPORT_SYMBOL(calloc);
EXPORT_SYMBOL(free);
EXPORT_SYMBOL(rand);
EXPORT_SYMBOL(srand);

EXPORT_SYMBOL(atof);
EXPORT_SYMBOL(atol);
EXPORT_SYMBOL(isspace);
EXPORT_SYMBOL(isalnum);
EXPORT_SYMBOL(tolower);
EXPORT_SYMBOL(toupper);

#if defined(CONFIG_USB_DEVICE_STACK)
FORCE_EXPORT_SYM(usb_enable);
FORCE_EXPORT_SYM(usb_disable);
#endif

FORCE_EXPORT_SYM(z_log_msg_runtime_vcreate);

#if defined(CONFIG_NETWORKING)
FORCE_EXPORT_SYM(net_if_foreach);
FORCE_EXPORT_SYM(net_if_get_by_iface);
#endif

#if defined(CONFIG_NET_DHCPV4)
FORCE_EXPORT_SYM(net_dhcpv4_start);
#if defined(CONFIG_NET_DHCPV4_OPTION_CALLBACKS)
FORCE_EXPORT_SYM(net_dhcpv4_add_option_callback);
#endif
#endif

#if defined(CONFIG_NET_MGMT_EVENT)
FORCE_EXPORT_SYM(net_mgmt_add_event_callback);
#endif


#if defined(CONFIG_NET_SOCKETS)
FORCE_EXPORT_SYM(getaddrinfo);
FORCE_EXPORT_SYM(socket);
FORCE_EXPORT_SYM(connect);
FORCE_EXPORT_SYM(send);
FORCE_EXPORT_SYM(recv);
FORCE_EXPORT_SYM(open);
FORCE_EXPORT_SYM(close);
EXPORT_SYMBOL(exit);
#endif

#if defined(CONFIG_CDC_ACM_DTE_RATE_CALLBACK_SUPPORT)
FORCE_EXPORT_SYM(cdc_acm_dte_rate_callback_set);
#endif

FORCE_EXPORT_SYM(k_timer_init);
//FORCE_EXPORT_SYM(k_timer_user_data_set);
//FORCE_EXPORT_SYM(k_timer_start);

EXPORT_SYMBOL(sin);
EXPORT_SYMBOL(cos);
EXPORT_SYMBOL(tan);
EXPORT_SYMBOL(atan);

EXPORT_SYMBOL(puts);
EXPORT_SYMBOL(putchar);
EXPORT_SYMBOL(printf);
EXPORT_SYMBOL(sprintf);
EXPORT_SYMBOL(snprintf);
FORCE_EXPORT_SYM(cbvprintf);

FORCE_EXPORT_SYM(abort);
#if defined(CONFIG_RING_BUFFER)
FORCE_EXPORT_SYM(ring_buf_get);
FORCE_EXPORT_SYM(ring_buf_peek);
FORCE_EXPORT_SYM(ring_buf_put);
#endif
FORCE_EXPORT_SYM(sys_clock_cycle_get_32);
FORCE_EXPORT_SYM(__aeabi_dcmpun);
FORCE_EXPORT_SYM(__aeabi_dcmple);
FORCE_EXPORT_SYM(__aeabi_d2lz);
FORCE_EXPORT_SYM(__aeabi_uldivmod);
FORCE_EXPORT_SYM(__aeabi_ui2d);
FORCE_EXPORT_SYM(__aeabi_dcmplt);
FORCE_EXPORT_SYM(__aeabi_ddiv);
FORCE_EXPORT_SYM(__aeabi_dmul);
FORCE_EXPORT_SYM(__aeabi_d2f);
FORCE_EXPORT_SYM(__aeabi_fcmpun);
FORCE_EXPORT_SYM(__aeabi_dadd);
FORCE_EXPORT_SYM(__aeabi_fcmple);
FORCE_EXPORT_SYM(__aeabi_idiv);
FORCE_EXPORT_SYM(__aeabi_dcmpgt);
FORCE_EXPORT_SYM(__aeabi_dsub);
FORCE_EXPORT_SYM(__aeabi_i2d);
FORCE_EXPORT_SYM(__aeabi_uidiv);
FORCE_EXPORT_SYM(__aeabi_l2d);
FORCE_EXPORT_SYM(__aeabi_d2uiz);
FORCE_EXPORT_SYM(__aeabi_uidivmod);
FORCE_EXPORT_SYM(__aeabi_dcmpeq);
FORCE_EXPORT_SYM(__aeabi_d2iz);
FORCE_EXPORT_SYM(__aeabi_f2d);
FORCE_EXPORT_SYM(__aeabi_idivmod);

#include <zephyr/syscall_export_llext.c>
