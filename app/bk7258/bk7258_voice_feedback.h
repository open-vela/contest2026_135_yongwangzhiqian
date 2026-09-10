/****************************************************************************
 * app/bk7258/bk7258_voice_feedback.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Best-effort bridge from the voice-turn state machine to product eyes.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_FEEDBACK_H
#define __APP_BK7258_BK7258_VOICE_FEEDBACK_H

#include "bk7258_voice_turn.h"

int bk7258_voice_feedback_start(void);
void bk7258_voice_feedback_report(void *context,
                                  enum bkvoice_turn_state_e state);

#endif /* __APP_BK7258_BK7258_VOICE_FEEDBACK_H */
