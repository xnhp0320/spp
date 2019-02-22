# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2018 Nippon Telegraph and Telephone Corporation

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = spp_mirror

# all source are stored in SRCS-y
SRCS-y := spp_mirror.c
SRCS-y += ../shared/common.c
SRCS-y += ../shared/secondary/utils.c ../shared/secondary/add_port.c
SRCS-y += ../vf/common/spp_proc.c ../vf/common/spp_port.c
SRCS-y += ../vf/common/command_conn.c ../vf/common/command_proc.c
SRCS-y += ../vf/common/command_parse.c
SRCS-y += ../vf/common/ringlatencystats.c ../vf/common/string_buffer.c

CFLAGS += -DALLOW_EXPERIMENTAL_API
CFLAGS += $(WERROR_FLAGS) -O3 -MMD
CFLAGS += -I$(SRCDIR)/../
CFLAGS += -I$(SRCDIR)/../vf/common
CFLAGS += -DSPP_MIRROR_MODULE
CFLAGS += -DSPP_MIRROR_SHALLOWCOPY
#CFLAGS += -DSPP_DEMONIZE
#CFLAGS += -DSPP_RINGLATENCYSTATS_ENABLE

ifeq ($(CONFIG_RTE_BUILD_SHARED_LIB),y)
LDLIBS += -lrte_pmd_ring
LDLIBS += -lrte_pmd_vhost
endif

include $(RTE_SDK)/mk/rte.extapp.mk
