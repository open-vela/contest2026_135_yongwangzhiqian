# ##############################################################################
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

if(CONFIG_DOLPHIN)
  nuttx_add_application(
    NAME dolphin
    STACKSIZE ${CONFIG_DOLPHIN_STACKSIZE}
    SRCS dolphin_main.c)
endif()

if(CONFIG_DOLPHIN_UI)
  target_sources(apps PRIVATE dolphin_ui.c)
  if(CONFIG_DOLPHIN_ADC_KEY)
    target_sources(apps PRIVATE dolphin_adc_key.c)
  endif()
  if(CONFIG_DOLPHIN_RECORDER)
    target_include_directories(apps PRIVATE
        ${NUTTX_APPS_DIR}/frameworks/multimedia/media/include)
    target_sources(apps PRIVATE dolphin_recording.c)
  endif()
endif()

if(CONFIG_DOLPHIN_FILE_COPY)
  target_sources(apps PRIVATE dolphin_copy.c)
endif()
