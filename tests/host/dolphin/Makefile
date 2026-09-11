############################################################################
# SPDX-License-Identifier: Apache-2.0
############################################################################

CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
BUILD := build
TARGET := $(BUILD)/test_dolphin
RECORDING_TARGET := $(BUILD)/test_dolphin_recording
COPY_TARGET := $(BUILD)/test_dolphin_copy
LVGL_ROOT ?= $(abspath ../../../../apps/graphics/lvgl/lvgl)
UI_BUILD ?= $(BUILD)/ui
UI_CONFIG := $(abspath ui_lv_conf.h)
JOBS ?= 8

all: check

$(TARGET): test_dolphin.c ../../../app/dolphin/dolphin_main.c \
		mocks/nuttx/config.h mocks/nuttx/lib/builtin.h mocks/spawn.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Imocks test_dolphin.c -o $@

$(RECORDING_TARGET): test_dolphin_recording.c \
		../../../app/dolphin/dolphin_recording.c \
		../../../app/dolphin/dolphin_recording.h mocks/media_recorder.h mocks/nuttx/config.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Imocks -pthread test_dolphin_recording.c -o $@

$(COPY_TARGET): test_dolphin_copy.c ../../../app/dolphin/dolphin_copy.c \
		../../../app/dolphin/dolphin_copy.h mocks/nuttx/config.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -pthread \
		-Imocks test_dolphin_copy.c -o $@

check: $(TARGET) $(RECORDING_TARGET) $(COPY_TARGET)
	$(TARGET)
	$(RECORDING_TARGET)
	$(COPY_TARGET)

$(UI_BUILD)/lvgl/CMakeCache.txt: ui_lv_conf.h Makefile
	cmake -S $(LVGL_ROOT) -B $(UI_BUILD)/lvgl -DLV_CONF_PATH=$(UI_CONFIG) \
		-DLV_CONF_BUILD_DISABLE_EXAMPLES=ON -DLV_CONF_BUILD_DISABLE_DEMOS=ON \
		-DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=ON

$(UI_BUILD)/test_dolphin_ui: test_dolphin_ui.c ui_lv_conf.h \
		../../../app/dolphin/dolphin_ui.c ../../../app/dolphin/dolphin_ui.h \
		$(UI_BUILD)/lvgl/lib/liblvgl.a
	$(CC) -std=c11 -D_DEFAULT_SOURCE -DDOLPHIN_HOST_TEST -O2 -Wall -Wextra \
		-DLV_CONF_PATH=$(UI_CONFIG) -Imocks -I$(dir $(LVGL_ROOT)) \
		test_dolphin_ui.c $(UI_BUILD)/lvgl/lib/liblvgl.a -lm -pthread -o $@

ui-check: $(UI_BUILD)/lvgl/CMakeCache.txt
	cmake --build $(UI_BUILD)/lvgl --target lvgl -j$(JOBS)
	$(MAKE) $(UI_BUILD)/test_dolphin_ui
	$(UI_BUILD)/test_dolphin_ui $(UI_BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all check clean ui-check
