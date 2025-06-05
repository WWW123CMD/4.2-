#pragma once
#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include "GxEPD2_display_selection_new_style.h" // 提前包含，确保宏定义优先

// 现在可以正确识别 GxEPD2_DISPLAY_CLASS
extern GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display;

int si_calendar_status();
void si_calendar();

int si_wifi_status();
void si_wifi();

int si_weather_status();
void si_weather();

int si_screen_status();
void si_screen();

void print_status();
