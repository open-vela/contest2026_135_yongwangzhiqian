/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APPS_DOLPHIN_DOLPHIN_ADC_KEY_H
#define __APPS_DOLPHIN_DOLPHIN_ADC_KEY_H

#include <stdint.h>

enum dolphin_adc_key_event_e
{
  DOLPHIN_ADC_KEY_NEXT,
  DOLPHIN_ADC_KEY_CONFIRM,
  DOLPHIN_ADC_KEY_HOME,
  DOLPHIN_ADC_KEY_ERROR
};

int dolphin_adc_key_start(void);
int dolphin_adc_key_poll(enum dolphin_adc_key_event_e *event, int *error);

#endif
