/****************************************************************************
 * app/dolphin/dolphin_ui.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_DOLPHIN_DOLPHIN_UI_H
#define __APP_DOLPHIN_DOLPHIN_UI_H

/* Starts the sole Dolphin LVGL owner task.  The caller must keep the board
 * UIKit binding disabled: LVGL has one process-wide display/input context.
 */

int dolphin_ui_start(void);

#endif /* __APP_DOLPHIN_DOLPHIN_UI_H */
