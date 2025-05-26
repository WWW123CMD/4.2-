#ifndef ___LED_H__
#define ___LED_H__

#include <Arduino.h>

#define PIN_LED GPIO_NUM_22

void led_init();
void led_fast();
void led_slow();
void led_on();
void led_off();
void led_config();

#endif
