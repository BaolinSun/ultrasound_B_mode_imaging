#ifndef __KEY_H
#define __KEY_H

#include "../GPIO/gpio.h"
#include "../main.h"
#include "../TX7332/tx7332.h"
#define KEY_0_EMIO_PIN 56
#define KEY_1_EMIO_PIN 57
#define KEY_2_EMIO_PIN 58

void key_init(void);
u32 key_intr_setup(void);
void key_intr_handler(void *CallBackRef, u32 Bank, u32 Status);
#endif
