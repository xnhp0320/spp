# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2019 Nippon Telegraph and Telephone Corporation

ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = pipe_test

# all source are stored in SRCS-y
SRCS-y := pipe_test.c

CFLAGS += -O3
CFLAGS += $(WERROR_FLAGS)

EXTRA_LDLIBS = -L$(SRCDIR)/../build/lib --whole-archive -lrte_pmd_pipe --no-whole-archive

include $(RTE_SDK)/mk/rte.extapp.mk
