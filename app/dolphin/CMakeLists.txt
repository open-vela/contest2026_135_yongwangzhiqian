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
endif()
