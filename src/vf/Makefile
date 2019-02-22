# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2017-2018 Nippon Telegraph and Telephone Corporation

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = spp_vf

# all source are stored in SRCS-y
SRCS-y := spp_vf.c classifier_mac.c spp_forward.c
SRCS-y += common/string_buffer.c common/ringlatencystats.c common/spp_port.c
SRCS-y += common/command_conn.c common/command_parse.c common/command_proc.c
SRCS-y += common/spp_proc.c
SRCS-y += ../shared/common.c
SRCS-y += ../shared/secondary/utils.c ../shared/secondary/add_port.c

CFLAGS += -DALLOW_EXPERIMENTAL_API
CFLAGS += $(WERROR_FLAGS) -O3 -MMD
CFLAGS += -I$(SRCDIR)/../
CFLAGS += -I$(SRCDIR)/common
CFLAGS += -DSPP_VF_MODULE
#CFLAGS += -DSPP_DEMONIZE
#CFLAGS += -DSPP_RINGLATENCYSTATS_ENABLE

ifeq ($(CONFIG_RTE_BUILD_SHARED_LIB),y)
LDLIBS += -lrte_pmd_ring
LDLIBS += -lrte_pmd_vhost
endif

include $(RTE_SDK)/mk/rte.extapp.mk
