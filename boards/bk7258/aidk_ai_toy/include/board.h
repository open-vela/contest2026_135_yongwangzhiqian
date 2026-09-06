/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * Beken BK7258 AIDK AI Toy board interface
 ****************************************************************************/

#ifndef __BOARDS_BK7258_AIDK_AI_TOY_INCLUDE_BOARD_H
#define __BOARDS_BK7258_AIDK_AI_TOY_INCLUDE_BOARD_H

#include <arch/board/bk7258_board_config.h>
#include <bk7258_board.h>

#ifdef CONFIG_BK7258_AIDK_MOTOR
#  include <stdbool.h>
int bk7258_aidk_motor_initialize(void);
int bk7258_aidk_motor_capture_quiet(bool quiet);
#endif

#endif
