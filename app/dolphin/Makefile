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
ifeq ($(CONFIG_DOLPHIN_ADC_KEY),y)
CSRCS += dolphin_adc_key.c
endif
ifeq ($(CONFIG_DOLPHIN_RECORDER),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)$(DELIM)frameworks$(DELIM)multimedia$(DELIM)media$(DELIM)include
CSRCS += dolphin_recording.c
endif
endif

ifeq ($(CONFIG_DOLPHIN_FILE_COPY),y)
CSRCS += dolphin_copy.c
endif

include $(APPDIR)/Application.mk
