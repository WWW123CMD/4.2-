#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <OneButton.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "GxEPD2_display_selection_new_style.h"
#include "led.h"
#include "_sntp.h"
#include "weather.h"
#include "screen_ink.h"
#include "_preference.h"
#include "version.h"
#include "bitmap1.h"
#include "bitmap2.h"
#include "bitmap3.h"
// 电子纸显示定义
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(GxEPD2_DRIVER_CLASS(5, 17, 16, 4));
#define PIN_BUTTON GPIO_NUM_14
OneButton button(PIN_BUTTON, true);
// 全局状态变量
volatile bool isImageMode = false;       // 当前显示模式（false: 日历，true: 图片）
volatile int currentImageIndex = 0;      // 当前图片索引
volatile bool displayCompleted = false;  // 显示完成标记
unsigned long _idle_millis;              // 空闲计时
unsigned long TIME_TO_SLEEP = 180 * 1000;// 休眠时间
bool _wifi_flag = false;
unsigned long _wifi_failed_millis;
// WiFi管理器及参数定义
WiFiManager wm;
WiFiManagerParameter para_qweather_key("qweather_key", "和风天气Token", "", 32);
WiFiManagerParameter para_qweather_type("qweather_type", "天气类型（0:每日，1:实时）", "0", 2);
WiFiManagerParameter para_qweather_location("qweather_loc", "位置ID", "", 9);
WiFiManagerParameter para_cd_day_label("cd_day_label", "倒数日标签（4字）", "", 10);
WiFiManagerParameter para_cd_day_date("cd_day_date", "日期（yyyyMMdd）", "", 8);
WiFiManagerParameter para_tag_days("tag_days", "日期Tag（yyyyMMddx）", "", 30);
WiFiManagerParameter para_si_week_1st("si_week_1st", "每周起始（0:周日，1:周一）", "0", 2);
// 中断处理函数
void IRAM_ATTR checkTicks() {
    button.tick();
}
// 函数声明
void buttonClick(void* oneButton);
void buttonDoubleClick(void* oneButton);
void buttonLongPressStop(void* oneButton);
void go_sleep();
void displayImage();

// 显示图片函数
void displayImage() {
    const uint8_t* bitmap;
    int16_t width, height;
    switch(currentImageIndex % 3) {  // 确保索引在0-2范围内
        case 0:
            bitmap = bitmap1_bmp;
            width = bitmap1_width;
            height = bitmap1_height;
            break;
        case 1:
            bitmap = bitmap2_bmp;
            width = bitmap2_width;
            height = bitmap2_height;
            break;
        case 2:
            bitmap = bitmap3_bmp;
            width = bitmap3_width;
            height = bitmap3_height;
            break;
        default:
            return;
    }
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawBitmap(
            (display.width() - width)/2,
            (display.height() - height)/2,
            bitmap, width, height, GxEPD_RED
        );
    } while (display.nextPage());
    display.hibernate();
    displayCompleted = true;
}
// 按钮单击处理（模式切换）
void buttonClick(void* oneButton) {
    if (wm.getConfigPortalActive()) return;
    isImageMode = !isImageMode;  // 切换显示模式
    _idle_millis = millis();      // 重置休眠计时
    
    if (isImageMode) {
        currentImageIndex = (currentImageIndex + 1) % 3;
        displayImage();
    } else {
        si_screen();
    }
}
void saveParamsCallback() {
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.putString(PREF_QWEATHER_KEY, para_qweather_key.getValue());
    pref.putString(PREF_QWEATHER_TYPE, strcmp(para_qweather_type.getValue(), "1") == 0 ? "1" : "0");
    pref.putString(PREF_QWEATHER_LOC, para_qweather_location.getValue());
    pref.putString(PREF_CD_DAY_LABLE, para_cd_day_label.getValue());
    pref.putString(PREF_CD_DAY_DATE, para_cd_day_date.getValue());
    pref.putString(PREF_TAG_DAYS, para_tag_days.getValue());
    pref.putString(PREF_SI_WEEK_1ST, strcmp(para_si_week_1st.getValue(), "1") == 0 ? "1" : "0");
    pref.end();

    Serial.println("Params saved.");

    _idle_millis = millis(); // 刷新无操作时间点

    ESP.restart();
}

void preSaveParamsCallback() {
}

// 双击打开配置页面
void buttonDoubleClick(void* oneButton) {
    Serial.println("Button double click.");
    if (wm.getConfigPortalActive()) {
        ESP.restart();
        return;
    }

    if (weather_status == 0) {
        weather_stop();
    }

    // 设置配置页面
    // 根据配置信息设置默认值
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    String qToken = pref.getString(PREF_QWEATHER_KEY);
    String qType = pref.getString(PREF_QWEATHER_TYPE, "0");
    String qLoc = pref.getString(PREF_QWEATHER_LOC);
    String cddLabel = pref.getString(PREF_CD_DAY_LABLE);
    String cddDate = pref.getString(PREF_CD_DAY_DATE);
    String tagDays = pref.getString(PREF_TAG_DAYS);
    String week1st = pref.getString(PREF_SI_WEEK_1ST, "0");
    pref.end();

    para_qweather_key.setValue(qToken.c_str(), 32);
    para_qweather_location.setValue(qLoc.c_str(), 9);
    para_qweather_type.setValue(qType.c_str(), 1);
    para_cd_day_label.setValue(cddLabel.c_str(), 16);
    para_cd_day_date.setValue(cddDate.c_str(), 8);
    para_tag_days.setValue(tagDays.c_str(), 30);
    para_si_week_1st.setValue(week1st.c_str(), 1);

    wm.setTitle("J-Calendar");
    wm.addParameter(&para_si_week_1st);
    wm.addParameter(&para_qweather_key);
    wm.addParameter(&para_qweather_type);
    wm.addParameter(&para_qweather_location);
    wm.addParameter(&para_cd_day_label);
    wm.addParameter(&para_cd_day_date);
    wm.addParameter(&para_tag_days);
    // std::vector<const char *> menu = {"wifi","wifinoscan","info","param","custom","close","sep","erase","update","restart","exit"};
    std::vector<const char*> menu = { "wifi","param","update","sep","info","restart","exit" };
    wm.setMenu(menu); // custom menu, pass vector
    wm.setConfigPortalBlocking(false);
    wm.setBreakAfterConfig(true);
    wm.setPreSaveParamsCallback(preSaveParamsCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setSaveConnect(false); // 保存完wifi信息后是否自动连接，设置为否，以便于用户继续配置param。
    wm.startConfigPortal("J-Calendar", "password");

    led_config(); // LED 进入三快闪状态

    // 控制配置超时180秒后休眠
    _idle_millis = millis();
}


// 重置系统，并重启
void buttonLongPressStop(void* oneButton) {
    Serial.println("Button long press.");

    // 删除Preferences，namespace下所有健值对。
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.clear();
    pref.end();

    ESP.restart();
}
void setup() {
    // 硬件初始化
    Serial.begin(115200);
    delay(10);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    display.init(115200);
    display.setRotation(0);
    // 按钮设置
    button.setClickMs(300);
    button.setPressMs(3000); // 设置长按的时长
    button.attachClick(buttonClick, &button);
    button.attachDoubleClick(buttonDoubleClick, &button);
    button.attachLongPressStop(buttonLongPressStop, &button);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), checkTicks, CHANGE);
    // WiFi连接和初始化
    led_init();
    WiFiManager wifiManager;
    if (wifiManager.autoConnect("J-Calendar")) {
        _wifi_flag = true;
        led_on();
    } else {
        _wifi_flag = false;
        _sntp_exec(2);
        weather_exec(2);
        WiFi.mode(WIFI_OFF);
    }
    // 显示初始日历
    si_screen();
}
// 新增全局变量
bool _hourly_trigger = false;          // 整点触发标记
unsigned long _hourly_start_millis;    // 整点切换开始时间
#define HOURLY_DISPLAY_DURATION 10000  // 整点图片显示时长（10秒）

void loop() {
    // 新增整点检测（在函数开头）
    if (_sntp_status() > 0 && !wm.getConfigPortalActive()) { // 仅在时间同步后检测
        checkHourlyTrigger();
    }
    // 新增整点回切处理
    if (_hourly_trigger && (millis() - _hourly_start_millis > HOURLY_DISPLAY_DURATION)) {
        Serial.println(">> Hourly trigger reset");
        _hourly_trigger = false;
        isImageMode = false;
        si_screen();          // 切回日历界面
        _idle_millis = millis();  // 重置休眠计时器
    }
    button.tick();
    wm.process();
    
    // 自动刷新日历数据（增加hourly_trigger判断）
    if (!isImageMode && !_hourly_trigger) {
        if (_sntp_status() == -1) _sntp_exec();
        if (weather_status() == -1) weather_exec();
        if (_sntp_status() > 0 && weather_status() > 0 && si_screen_status() == -1) {
            si_screen();
            displayCompleted = true;
        }
    }
    // 修改后的休眠判断
    if (!wm.getConfigPortalActive() && displayCompleted && !_hourly_trigger) {
        if ((millis() - _idle_millis > TIME_TO_SLEEP) ||
            (!_wifi_flag && millis() - _wifi_failed_millis > 10 * 1000)) {
            go_sleep();
        }
    }
    delay(10);
}

void checkHourlyTrigger() {
    static uint8_t last_min = 255; // 存储上分钟数
    static uint8_t check_counter = 0;
    
    // 每分钟检测一次以降低功耗
    if (++check_counter < 60) return; // 改为秒级检测
    check_counter = 0;
    time_t now = time(nullptr);
    if (now < 86400) return; // 过滤无效时间
    
    struct tm* t = localtime(&now);
    
    // 整点检测（精确到±5秒容错）
    if (t->tm_min == 0 && t->tm_sec < 5 && last_min != 0) {
        Serial.println("\n>> Hourly trigger activated");
        _hourly_trigger = true;
        _hourly_start_millis = millis();
        
        // 强制切换到图片模式
        isImageMode = true;
        currentImageIndex = (currentImageIndex + 1) % 3;
        displayImage();
    }
    last_min = t->tm_min;
}
#define uS_TO_S_FACTOR 1000000
#define TIMEOUT_TO_SLEEP  10 // seconds
time_t blankTime = 0;
void go_sleep() {
    // 设置唤醒时间为下个偶数整点。
    time_t now = time(NULL);
    struct tm tmNow = { 0 };
    // Serial.printf("Now: %ld -- %s\n", now, ctime(&now));
    localtime_r(&now, &tmNow); // 时间戳转化为本地时间结构

    uint64_t p;
    // 根据配置情况来刷新，如果未配置qweather信息，则24小时刷新，否则每2小时刷新
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    String _qweather_key = pref.getString(PREF_QWEATHER_KEY, "");
    pref.end();
    if (_qweather_key.length() == 0 || weather_type() == 0) { // 没有配置天气或者使用按日天气，则第二天刷新。
        Serial.println("Sleep to next day.");
        now += 3600 * 24;
        localtime_r(&now, &tmNow); // 将新时间转成tm
        // Serial.printf("Set1: %ld -- %s\n", now, ctime(&now));

        struct tm tmNew = { 0 };
        tmNew.tm_year = tmNow.tm_year;
        tmNew.tm_mon = tmNow.tm_mon;        // 月份从0开始
        tmNew.tm_mday = tmNow.tm_mday;           // 日期
        tmNew.tm_hour = 0;           // 小时
        tmNew.tm_min = 0;            // 分钟
        tmNew.tm_sec = 10;            // 秒, 防止离线时出现时间误差，所以，延后10s
        time_t set = mktime(&tmNew);

        p = (uint64_t)(set - time(NULL));
        Serial.printf("Sleep time: %ld seconds\n", p);
    } else {
        if (tmNow.tm_hour % 2 == 0) { // 将时间推后两个小时，偶整点刷新。
            now += 7200;
        } else {
            now += 3600;
        }
        localtime_r(&now, &tmNow); // 将新时间转成tm
        // Serial.printf("Set1: %ld -- %s\n", now, ctime(&now));

        struct tm tmNew = { 0 };
        tmNew.tm_year = tmNow.tm_year;
        tmNew.tm_mon = tmNow.tm_mon;        // 月份从0开始
        tmNew.tm_mday = tmNow.tm_mday;           // 日期
        tmNew.tm_hour = tmNow.tm_hour;           // 小时
        tmNew.tm_min = 0;            // 分钟
        tmNew.tm_sec = 10;            // 秒, 防止离线时出现时间误差，所以，延后10s
        time_t set = mktime(&tmNew);

        p = (uint64_t)(set - time(NULL));
        Serial.printf("Sleep time: %ld seconds\n", p);
    }

    esp_sleep_enable_timer_wakeup(p * (uint64_t)uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(PIN_BUTTON, 0);

    // 省电考虑，关闭RTC外设和存储器
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF); // RTC IO, sensors and ULP, 注意：由于需要按键唤醒，所以不能关闭，否则会导致RTC_IO唤醒失败
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF); // 
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

    // 省电考虑，重置gpio，平均每针脚能省8ua。
    gpio_reset_pin(PIN_LED); // 减小deep-sleep电流
    gpio_reset_pin(GPIO_NUM_5); // 减小deep-sleep电流
    gpio_reset_pin(GPIO_NUM_17); // 减小deep-sleep电流
    gpio_reset_pin(GPIO_NUM_16); // 减小deep-sleep电流
    gpio_reset_pin(GPIO_NUM_4); // 减小deep-sleep电流

    delay(10);
    Serial.println("Deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}
