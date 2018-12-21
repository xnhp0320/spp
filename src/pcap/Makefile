# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Nippon Telegraph and Telephone Corporation

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = spp_pcap

# all source are stored in SRCS-y
SRCS-y := spp_pcap.c
SRCS-y += spp_proc.c
SRCS-y += command_proc.c command_dec.c
SRCS-y += ../shared/common.c
SRCS-y += ../vf/common/command_conn.c ../vf/common/spp_port.c
SRCS-y += ../vf/common/ringlatencystats.c ../vf/common/string_buffer.c
SRCS-y += /usr/lib/x86_64-linux-gnu/liblz4.a

CFLAGS += -DALLOW_EXPERIMENTAL_API
CFLAGS += $(WERROR_FLAGS) -O3 -MMD
CFLAGS += -I$(SRCDIR)/../
CFLAGS += -I$(SRCDIR)/../vf/common
CFLAGS += -I$(SRCDIR)/../shared
#CFLAGS += -DSPP_DEMONIZE
#CFLAGS += -DSPP_RINGLATENCYSTATS_ENABLE

ifeq ($(CONFIG_RTE_BUILD_SHARED_LIB),y)
LDLIBS += -lrte_pmd_ring
LDLIBS += -lrte_pmd_vhost
endif

include $(RTE_SDK)/mk/rte.extapp.mk
