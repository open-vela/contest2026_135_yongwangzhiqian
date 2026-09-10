############################################################################
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_DOLPHIN),y)
PROGNAME  = dolphin
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = $(CONFIG_DOLPHIN_STACKSIZE)
MODULE    = $(CONFIG_DOLPHIN)
MAINSRC   = dolphin_main.c
endif

ifeq ($(CONFIG_DOLPHIN_UI),y)
CSRCS += dolphin_ui.c
endif

include $(APPDIR)/Application.mk
