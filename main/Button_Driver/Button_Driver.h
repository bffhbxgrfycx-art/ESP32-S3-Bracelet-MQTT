#ifndef BUTTON_BSP_H
#define BUTTON_BSP_H
#include <stdio.h>
#include <stdbool.h>  
#include "multi_button.h"


#define BOOT_KEY_PIN     0

#define Button_PIN1   BOOT_KEY_PIN

extern struct Button BUTTON1;              /* 按键句柄，供外部重新绑定回调 */
extern PressEvent BOOT_KEY_State;    

void button_Init(void);

#endif