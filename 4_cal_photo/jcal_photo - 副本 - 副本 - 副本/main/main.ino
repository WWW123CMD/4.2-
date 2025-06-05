/*
 * J-Calendar 智能日历系统 - ESP32墨水屏日历
 * 
 * 主要功能：
 * 1. 日历显示（支持农历、节气、倒计时）
 * 2. 自动时间同步（SNTP）
 * 3. 和风天气信息获取
 * 4. 图片轮播功能
 * 5. 低功耗深度睡眠模式
 * 6. Wi-Fi配置管理器
 * 
 * 硬件要求：
 * - ESP32开发板
 * - 三色墨水屏（黑白红）
 * - 物理按钮（接GPIO14）
 * - LED指示灯
 * 
 * 按键功能：
 * - 单击：日历/图片模式切换
 * - 双击：配置模式
 * - 长按：恢复出厂设置
 * 
 * 开发环境：
 * - Arduino IDE 2.0+
 * - ESP32开发板支持包
 */

#include <Arduino.h>
#include <ArduinoJson.h>      // JSON解析库（用于天气数据）
#include <WiFiManager.h>      // Wi-Fi配置管理
#include <OneButton.h>        // 按键事件处理库
#include <U8g2_for_Adafruit_GFX.h> // 显示渲染库
#include "GxEPD2_display_selection_new_style.h" // 墨水屏驱动
#include "led.h"              // LED控制模块
#include "_sntp.h"            // SNTP时间同步模块
#include "weather.h"          // 天气数据获取模块
#include "screen_ink.h"       // 墨水屏渲染逻辑
#include "_preference.h"      // 存储管理（Preferences）
#include "version.h"          // 版本信息
#include "bitmap1.h"          // 图片资源1
#include "bitmap2.h"          // 图片资源2
#include "bitmap3.h"          // 图片资源3

// 调试模式开关（发布时设为false禁用调试输出）
#define DEBUG_MODE true

// 墨水屏显示定义（指定SPI引脚）
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> 
    display(GxEPD2_DRIVER_CLASS(5, 17, 16, 4)); // CS, DC, RST, BUSY

#define PIN_BUTTON GPIO_NUM_14 // 按钮引脚
OneButton button(PIN_BUTTON, true); // 初始化按钮（内部上拉）

//---------------------------------
// 全局状态变量
//---------------------------------
volatile bool isImageMode = false;       // 当前显示模式 (false=日历, true=图片)
volatile int currentImageIndex = 0;      // 当前显示的图片索引
volatile bool displayCompleted = false;  // 屏幕刷新完成标志
unsigned long _idle_millis;              // 空闲开始时间（用于休眠计时）
unsigned long TIME_TO_SLEEP = 100 * 1000;// 触发休眠的空闲时间（100秒≈1分40秒）
bool _wifi_flag = false;                 // Wi-Fi连接状态标志
unsigned long _wifi_failed_millis;       // Wi-Fi连接失败时间点
bool _wake_from_sleep = false;           // 是否从睡眠状态唤醒

//---------------------------------
// 日期缓存系统 - 解决网络中断时的日期显示
// RTC_DATA_ATTR 将数据存储在RTC内存，深度睡眠后保留
//---------------------------------
RTC_DATA_ATTR tm last_valid_time = {0};  // 完整的日期时间结构
bool date_restored = false;              // 日期恢复状态

//---------------------------------
// 整点检测系统
//---------------------------------
volatile bool _hourly_trigger = false;   // 整点触发标志
unsigned long _imageModeStartMillis = 0; // 进入图片模式的时间点
#define IMAGE_MODE_DURATION 20000        // 图片显示持续时间（20秒）
volatile uint8_t last_checked_minute = 255; // 上次检测的分钟值（初始255表示未设置）

//---------------------------------
// Wi-Fi管理器参数配置
// 用于Web配置页面的输入字段
//---------------------------------
WiFiManager wm; // Wi-Fi配置管理器实例
WiFiManagerParameter para_qweather_key("qweather_key", "和风天气Token", "", 32);
WiFiManagerParameter para_qweather_type("qweather_type", "天气类型（0:每日，1:实时）", "0", 2);
WiFiManagerParameter para_qweather_location("qweather_loc", "位置ID", "", 9);
WiFiManagerParameter para_cd_day_label("cd_day_label", "倒数日标签（4字）", "", 10);
WiFiManagerParameter para_cd_day_date("cd_day_date", "日期（yyyyMMdd）", "", 8);
WiFiManagerParameter para_tag_days("tag_days", "日期Tag（yyyyMMddx）", "", 30);
WiFiManagerParameter para_si_week_1st("si_week_1st", "每周起始（0:周日，1:周一）", "0", 2);

//---------------------------------
// 中断服务函数（IRAM_ATTR确保代码在IRAM中运行）
//---------------------------------
void IRAM_ATTR checkTicks() {
    button.tick(); // 处理按钮事件（必须在中断中调用）
}

// 函数前置声明
void buttonClick(void* oneButton);
void buttonDoubleClick(void* oneButton);
void buttonLongPressStop(void* oneButton);
void go_sleep();
void displayImage();
void checkMinuteChange(bool force = false);
void checkImageModeTimeout();
void processSerialCommands();
bool cacheValidDate();
bool restoreCachedDate();

/**
 * 显示当前图片
 * 根据currentImageIndex选择图片资源
 * 居中显示在墨水屏上
 */
void displayImage() {
    const uint8_t* bitmap = NULL;  // 图像数据指针
    int16_t width = 0, height = 0; // 图像尺寸
    
    // 从三个图片资源中选择（循环）
    switch(currentImageIndex % 3) {
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

    if(bitmap == NULL) {
        Serial.println("错误: 未找到图片数据!");
        return;
    }

    // 设置全屏更新模式
    display.setFullWindow();
    display.firstPage();
    
    // 墨水屏分页渲染
    do {
        display.fillScreen(GxEPD_WHITE);
        // 居中显示图片（使用红色）
        display.drawBitmap(
            (display.width() - width)/2,
            (display.height() - height)/2,
            bitmap, width, height, GxEPD_RED
        );
    } while (display.nextPage());

    // 休眠显示屏以节省功耗
    display.hibernate();
    
    displayCompleted = true; // 设置显示完成标志
    #if DEBUG_MODE
    Serial.print("显示图片 #");
    Serial.println(currentImageIndex);
    #endif
}

/**
 * 缓存有效日期到RTC存储器
 * @return 缓存是否成功
 */
bool cacheValidDate() {
    time_t now = time(nullptr);
    if (now < 100000) return false;  // 无效时间（UNIX时间戳过小）
  
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) return false;
  
    // 复制时间结构到RTC存储区
    memcpy(&last_valid_time, &timeinfo, sizeof(tm));
  
    #if DEBUG_MODE
    Serial.printf("缓存有效日期: %04d-%02d-%02d %02d:%02d\n", 
                  last_valid_time.tm_year + 1900, last_valid_time.tm_mon + 1, 
                  last_valid_time.tm_mday, last_valid_time.tm_hour, last_valid_time.tm_min);
    #endif
  
    return true;
}

/**
 * 从RTC存储器恢复缓存日期
 * @return 恢复是否成功
 */
bool restoreCachedDate() {
    if (last_valid_time.tm_year < 0) {
        return false; // 无效的缓存时间
    }
  
    #if DEBUG_MODE
    Serial.printf("使用缓存的日期: %04d-%02d-%02d %02d:%02d\n", 
                  last_valid_time.tm_year + 1900, last_valid_time.tm_mon + 1, 
                  last_valid_time.tm_mday, last_valid_time.tm_hour, last_valid_time.tm_min);
    #endif
  
    // 将缓存时间转换为time_t并设置系统时间
    time_t cached_time = mktime(&last_valid_time);
    struct timeval tv = { .tv_sec = cached_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);
  
    return true;
}

/**
 * 触发整点切换（串口调试用）
 */
void triggerHourlySwitch() {
    #if DEBUG_MODE
    Serial.println(">> 手动触发整点切换");
    #endif
    _hourly_trigger = true;
    _imageModeStartMillis = millis();
    isImageMode = true;
    currentImageIndex = (currentImageIndex + 1) % 3;
    displayImage();
}

/**
 * 立即切换回日历（串口调试用）
 */
void triggerCalendarSwitch() {
    #if DEBUG_MODE
    Serial.println(">> 手动切换回日历");
    #endif
    _hourly_trigger = false;
    isImageMode = false;
    
    // 确保使用有效日期
    if (time(nullptr) < 100000) {
        restoreCachedDate();
    }
    
    si_screen(); // 调用日历渲染函数
    displayCompleted = true;  // 设置显示完成标志
}

/**
 * 按钮单击事件处理
 * 切换日历/图片模式
 */
void buttonClick(void* oneButton) {
    // 如果配置门户激活，忽略按钮事件
    if (wm.getConfigPortalActive()) return;

    // 重置整点触发标志
    _hourly_trigger = false;

    // 切换显示模式
    isImageMode = !isImageMode;
    
    // 重置计时器
    _idle_millis = millis();
    _imageModeStartMillis = millis();

    if (isImageMode) {
        // 切换到图片模式
        currentImageIndex = (currentImageIndex + 1) % 3;
        displayImage();
        #if DEBUG_MODE
        Serial.println("按钮切换: 图片模式");
        #endif
    } else {
        // 切换到日历模式
        if (time(nullptr) < 100000) {
            restoreCachedDate();
        }
        si_screen(); // 渲染日历
        displayCompleted = true;
        #if DEBUG_MODE
        Serial.println("按钮切换: 日历模式");
        #endif
    }
}

/**
 * 配置保存回调函数
 * 保存参数到Preferences
 */
void saveParamsCallback() {
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    
    // 保存所有配置参数
    pref.putString(PREF_QWEATHER_KEY, para_qweather_key.getValue());
    pref.putString(PREF_QWEATHER_TYPE, strcmp(para_qweather_type.getValue(), "1") == 0 ? "1" : "0");
    pref.putString(PREF_QWEATHER_LOC, para_qweather_location.getValue());
    pref.putString(PREF_CD_DAY_LABLE, para_cd_day_label.getValue());
    pref.putString(PREF_CD_DAY_DATE, para_cd_day_date.getValue());
    pref.putString(PREF_TAG_DAYS, para_tag_days.getValue());
    pref.putString(PREF_SI_WEEK_1ST, strcmp(para_si_week_1st.getValue(), "1") == 0 ? "1" : "0");
    
    pref.end();

    #if DEBUG_MODE
    Serial.println("参数已保存 - 系统将重启");
    #endif
    _idle_millis = millis();
    ESP.restart(); // 重启系统
}

/**
 * 配置前回调（空实现）
 */
void preSaveParamsCallback() {
    // 预留扩展点
}

/**
 * 按钮双击事件处理
 * 打开Wi-Fi配置页面
 */
void buttonDoubleClick(void* oneButton) {
    #if DEBUG_MODE
    Serial.println("按钮双击: 打开配置页面");
    #endif

    // 重置整点触发标志
    _hourly_trigger = false;

    // 如果配置页面已激活，则重启系统
    if (wm.getConfigPortalActive()) {
        ESP.restart();
        return;
    }

    // 停止天气更新任务（如果正在运行）
    if (weather_status == 0) {
        weather_stop();
    }

    // 从Preferences加载当前配置值
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

    // 设置配置表单的预填值
    para_qweather_key.setValue(qToken.c_str(), 32);
    para_qweather_location.setValue(qLoc.c_str(), 9);
    para_qweather_type.setValue(qType.c_str(), 1);
    para_cd_day_label.setValue(cddLabel.c_str(), 16);
    para_cd_day_date.setValue(cddDate.c_str(), 8);
    para_tag_days.setValue(tagDays.c_str(), 30);
    para_si_week_1st.setValue(week1st.c_str(), 1);

    // 配置WiFiManager
    wm.setTitle("J-Calendar");
    wm.addParameter(&para_si_week_1st);
    wm.addParameter(&para_qweather_key);
    wm.addParameter(&para_qweather_type);
    wm.addParameter(&para_qweather_location);
    wm.addParameter(&para_cd_day_label);
    wm.addParameter(&para_cd_day_date);
    wm.addParameter(&para_tag_days);

    // 设置配置菜单
    std::vector<const char*> menu = {"wifi","param","update","sep","info","restart","exit"};
    wm.setMenu(menu);
    wm.setConfigPortalBlocking(false); // 非阻塞模式
    wm.setBreakAfterConfig(true);     // 配置后重置连接
    wm.setPreSaveParamsCallback(preSaveParamsCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setSaveConnect(false);         // 不自动保存Wi-Fi连接
    wm.startConfigPortal("J-Calendar", "password"); // 启动配置门户

    led_config(); // LED进入提示模式
    _idle_millis = millis();
}

/**
 * 按钮长按事件处理
 * 恢复出厂设置
 */
void buttonLongPressStop(void* oneButton) {
    #if DEBUG_MODE
    Serial.println("按钮长按: 重置系统");
    #endif

    // 重置整点触发标志
    _hourly_trigger = false;

    // 清除所有存储的配置
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.clear();
    pref.end();
  
    // 清除缓存的日期
    memset(&last_valid_time, 0, sizeof(tm));
    last_valid_time.tm_year = -1; // 设置为无效值

    #if DEBUG_MODE
    Serial.println("系统将重启...");
    #endif
    ESP.restart(); // 执行系统重启
}

/**
 * 整点检测函数
 * @param force 强制检查（忽略时间间隔）
 */
void checkMinuteChange(bool force) {
    // 配置页面激活时不检测
    if (wm.getConfigPortalActive()) return;

    // 获取当前时间，无效时尝试恢复缓存
    time_t now = time(nullptr);
    if (now < 100000 && !restoreCachedDate()) return;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
  
    // 缓存当前有效日期
    cacheValidDate();

    uint8_t current_minute = timeinfo.tm_min;

    // 初始化记录
    if (last_checked_minute == 255) {
        last_checked_minute = current_minute;

        // 唤醒后处理整点逻辑
        if (_wake_from_sleep && current_minute == 0) {
            #if DEBUG_MODE
            Serial.println(">> 唤醒后检测到整点，触发整点切换");
            #endif
            _hourly_trigger = true;
            _imageModeStartMillis = millis();
            isImageMode = true;
            currentImageIndex = (currentImageIndex + 1) % 3;
            displayImage();
        }
        return;
    }

    // 分钟变化检测
    if (force || (current_minute != last_checked_minute)) {
        // 整点触发逻辑（00分钟）
        if (current_minute == 0) {
            #if DEBUG_MODE
            Serial.println(">> 整点触发");
            #endif
            _hourly_trigger = true;
            _imageModeStartMillis = millis();
            isImageMode = true;
            currentImageIndex = (currentImageIndex + 1) % 3;
            displayImage();
        } 

        // 更新最后检测的分钟值
        last_checked_minute = current_minute;
    }
}

/**
 * 图片模式超时检测
 * 20秒后自动切回日历
 */
void checkImageModeTimeout() {
    if (isImageMode) {
        unsigned long elapsed = millis() - _imageModeStartMillis;
        // 超过20秒自动切换回日历
        if (elapsed > IMAGE_MODE_DURATION) {
            #if DEBUG_MODE
            Serial.println(">> 图片模式超时，切换回日历");
            #endif
            isImageMode = false;
            _hourly_trigger = false; // 清除整点标志

            // 确保有有效的系统时间
            if (time(nullptr) < 100000) {
                restoreCachedDate();
            }

            // 显示日历并重置状态
            si_screen();
            displayCompleted = true;
            _idle_millis = millis(); // 重置休眠计时器
        }
    }
}

/**
 * 处理串口命令（调试功能）
 */
void processSerialCommands() {
    // 仅在调试模式启用串口命令
    #if DEBUG_MODE
    if (!Serial.available()) return;

    String command = Serial.readStringUntil('\n');
    command.trim();  // 清理输入

    Serial.print("> 收到指令: ");
    Serial.println(command);

    if (command == "TRIGGER_HOURLY") {
        triggerHourlySwitch();
    } 
    else if (command == "SWITCH_CALENDAR") {
        triggerCalendarSwitch();
    }
    else if (command == "SHOW_INFO") {
        // 获取当前系统时间
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timeStr[30];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

        // 获取缓存时间
        char cachedTimeStr[30] = "无效";
        if (last_valid_time.tm_year >= 0) {
            strftime(cachedTimeStr, sizeof(cachedTimeStr), "%Y-%m-%d %H:%M:%S", &last_valid_time);
        }

        // 计算空闲时间
        unsigned long elapsedMillis = millis() - _idle_millis;
        unsigned long elapsedSeconds = elapsedMillis / 1000;
        
        // 打印系统信息
        Serial.printf("系统信息:\n");
        Serial.printf("  当前时间: %s\n", timeStr);
        Serial.printf("  缓存时间: %s\n", cachedTimeStr);
        Serial.printf("  模式: %s\n", isImageMode ? "图片" : "日历");
        Serial.printf("  图片索引: %d\n", currentImageIndex);
        Serial.printf("  整点触发: %s\n", _hourly_trigger ? "是" : "否");
        Serial.printf("  空闲时间: %lu秒\n", elapsedSeconds);
        Serial.printf("  唤醒原因: %s\n", _wake_from_sleep ? "深度睡眠" : "正常启动");

        // 图片模式剩余时间显示
        if (isImageMode) {
            unsigned long remaining = IMAGE_MODE_DURATION - (millis() - _imageModeStartMillis);
            Serial.printf("  图片模式剩余: %lu秒\n", remaining / 1000);
        }

        // 休眠状态分析
        Serial.println("  休眠状态检查:");
        Serial.printf("    配置页面激活: %s\n", wm.getConfigPortalActive() ? "是" : "否");
        Serial.printf("    显示完成: %s\n", displayCompleted ? "是" : "否");
        Serial.printf("    整点触发: %s\n", _hourly_trigger ? "是" : "否");
        Serial.printf("    图片模式: %s\n", isImageMode ? "是" : "否");
        Serial.printf("    WiFi状态: %s\n", _wifi_flag ? "已连接" : "未连接");
        Serial.printf("    最后检测分钟: %d\n", last_checked_minute);

        // 休眠条件检查
        if (!wm.getConfigPortalActive() && displayCompleted && !_hourly_trigger && !isImageMode) {
            if (elapsedMillis > TIME_TO_SLEEP) {
                Serial.println("    休眠条件满足 - 即将进入休眠");
            } else {
                unsigned long remaining = TIME_TO_SLEEP - elapsedMillis;
                Serial.printf("    距离休眠还有: %lu秒\n", remaining / 1000);
            }
        } else {
            Serial.println("    当前不满足休眠条件");
        }
    }
    else if (command == "SLEEP") {
        Serial.println(">> 手动触发休眠");
        go_sleep();
    }
    else if (command == "RESTART") {
        Serial.println(">> 手动重启系统");
        ESP.restart();
    }
    else if (command == "NEXT_IMAGE") {
        if (isImageMode) {
            currentImageIndex = (currentImageIndex + 1) % 3;
            displayImage();
            Serial.printf("显示下一张图片 #%d\n", currentImageIndex);
        } else {
            Serial.println("当前未在图片模式");
        }
    }
    else if (command == "HELP") {
        Serial.println("可用的串口调试指令:");
        Serial.println("  TRIGGER_HOURLY - 模拟整点事件");
        Serial.println("  SWITCH_CALENDAR - 立即切换回日历");
        Serial.println("  NEXT_IMAGE - 显示下一张图片");
        Serial.println("  SHOW_INFO - 显示系统信息");
        Serial.println("  SLEEP - 进入休眠模式");
        Serial.println("  RESTART - 重启系统");
        Serial.println("  HELP - 显示帮助信息");
    }
    else {
        Serial.println("未知指令，输入 HELP 查看可用命令");
    }
    #else
        // 非调试模式下忽略串口输入
        while(Serial.available()) Serial.read();
        Serial.println("调试模式已禁用，无法执行指令");
    #endif
}

/**
 * 系统初始化函数
 */
void setup() {
    // 初始化串口（115200波特率）
    Serial.begin(115200);
    delay(1000); // 等待串口稳定

    // 初始化时间缓存结构
    memset(&last_valid_time, 0, sizeof(tm));
    last_valid_time.tm_year = -1; // 无效标志

    // 检测唤醒原因（用于特殊逻辑处理）
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || 
        wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        _wake_from_sleep = true;
    }

    // 打印系统启动信息
    Serial.println("\n\n-------------------------------");
    Serial.println("    J-Calendar 智能日历系统");
    Serial.println("    构建日期: " __DATE__ " " __TIME__);
    Serial.print("    唤醒原因: ");
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0: Serial.println("按钮唤醒"); break;
        case ESP_SLEEP_WAKEUP_TIMER: Serial.println("定时器唤醒"); break;
        default: Serial.println("正常启动"); break;
    }
    Serial.println("-------------------------------");

    #if DEBUG_MODE
    Serial.println(">> 调试模式已启用");
    Serial.println(">> 输入 'HELP' 查看串口指令列表");
    #endif

    // 初始化墨水屏
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    display.init(115200); // SPI速度优化
    display.setRotation(0); // 设置默认方向

    // 配置按钮回调函数
    button.setClickMs(300);    // 单击识别时间
    button.setPressMs(3000);   // 长按识别时间
    button.attachClick(buttonClick, &button);
    button.attachDoubleClick(buttonDoubleClick, &button);
    button.attachLongPressStop(buttonLongPressStop, &button);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), checkTicks, CHANGE);

    // 初始化LED
    led_init();

    /*
     * Wi-Fi连接策略：
     * 1. 非唤醒启动：关闭WiFi减少干扰
     * 2. 唤醒启动：保留先前连接状态
     */
    if (!_wake_from_sleep) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    // 尝试自动连接WiFi
    if (wm.autoConnect("J-Calendar")) {
        _wifi_flag = true;
        led_on();
        Serial.println("WiFi连接成功");
    } else {
        _wifi_flag = false;
        Serial.println("WiFi连接失败");
        restoreCachedDate(); // 尝试使用缓存时间
        _wifi_failed_millis = millis();
    }

    // 系统核心服务初始化
    _sntp_exec();    // 启动时间同步
    weather_exec();  // 获取天气数据
  
    // 日期缓存系统初始化
    cacheValidDate();
    if (time(nullptr) < 100000) {
        restoreCachedDate();
    }

    // 首次显示日历
    si_screen();
    displayCompleted = true;
    Serial.println("初始日历已显示");
    _idle_millis = millis(); // 初始化空闲计时

    // 从睡眠唤醒后的特殊处理
    if (_wake_from_sleep) {
        checkMinuteChange(true); // 强制检查分钟变化
    }

    // 初始化图片模式计时器
    _imageModeStartMillis = millis();
}

/**
 * 计算唤醒延时（精确到下一个整点前1分钟）
 * @param now 当前时间戳
 * @return 定时器值（微秒）
 */
int64_t calculateWakeDelay(time_t now) {
    // 无效时间或缓存恢复失败时使用默认值
    if (now < 100000 && !restoreCachedDate()) {
        return 60 * 60 * 1000000ULL; // 默认1小时
    }
  
    struct tm tmNow;
    if (!localtime_r(&now, &tmNow)) {
        Serial.println("错误: 无法转换本地时间");
        return 30 * 60 * 1000000ULL; // 默认30分钟
    }

    // 计算到下一个整点的时间（秒）
    int current_minute = tmNow.tm_min;
    int current_second = tmNow.tm_sec;
    int seconds_to_next_hour = (60 - current_minute - 1) * 60 + (60 - current_second);

    // 设置在整点前1分钟唤醒（59分00秒）
    return (seconds_to_next_hour - 60) * 1000000ULL;
}

/**
 * 进入深度睡眠函数
 */
void go_sleep() {
    // 尝试缓存当前日期
    if (!cacheValidDate()) {
        Serial.println("警告: 无法缓存有效日期");
    }

    // 输出休眠信息
    Serial.println("进入休眠准备阶段...");
    Serial.printf("空闲时间: %lu秒\n", (millis() - _idle_millis) / 1000);
    
    #if DEBUG_MODE
    if (wm.getConfigPortalActive() || !displayCompleted || _hourly_trigger || isImageMode) {
        Serial.println("警告: 休眠前有未完成操作");
    }
    #endif

    display.powerOff(); // 关闭屏幕电源

    // 计算休眠时间（默认1小时）
    uint64_t sleep_us = 60 * 60 * 1000000ULL;
    time_t now = time(nullptr);

    // 仅在有效时间下计算精确唤醒时间
    if (now > 100000 || restoreCachedDate()) {
        int64_t wake_delay = calculateWakeDelay(now);
        if (wake_delay > 0) {
            sleep_us = wake_delay;
        }

        #if DEBUG_MODE
        int wake_seconds = sleep_us / 1000000;
        int minutes = wake_seconds / 60;
        int seconds = wake_seconds % 60;
        Serial.printf("休眠时间: %d 分 %d 秒\n", minutes, seconds);
        #endif
    } 
    #if DEBUG_MODE
    else {
        Serial.println("使用默认休眠时间: 1小时");
    }
    #endif

    // 设置定时器唤醒
    esp_sleep_enable_timer_wakeup(sleep_us);
    // 设置按键唤醒（GPIO14，低电平触发）
    esp_sleep_enable_ext0_wakeup(PIN_BUTTON, 0);

    // 配置省电模式（关闭RTC内存）
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

    // 重置GPIO减少功耗
    gpio_reset_pin(PIN_LED);
    gpio_reset_pin(GPIO_NUM_5);
    gpio_reset_pin(GPIO_NUM_17);
    gpio_reset_pin(GPIO_NUM_16);
    gpio_reset_pin(GPIO_NUM_4);

    #if DEBUG_MODE
    Serial.println("进入深度睡眠...");
    Serial.flush();
    delay(100); // 确保信息输出完成
    #endif

    esp_deep_sleep_start(); // 开始深度睡眠
}

/**
 * 主循环函数
 */
void loop() {
    // 变量初始化
    static unsigned long lastMinuteCheck = 0; // 上次分钟检查时间
    const unsigned long minuteCheckInterval = 10000; // 10秒检查间隔

    //---------------------------------
    // 主要任务调度
    //---------------------------------
    
    // 1. 串口指令处理（仅在调试模式）
    processSerialCommands();

    // 2. 每分钟检查（整点检测）
    if (millis() - lastMinuteCheck > minuteCheckInterval) {
        lastMinuteCheck = millis();
        checkMinuteChange();
    }

    // 3. 图片模式超时检查
    checkImageModeTimeout();

    // 4. 按钮事件轮询
    button.tick();

    // 5. Wi-Fi配置处理
    wm.process();

    //---------------------------------
    // 日历模式下的任务
    //---------------------------------
    if (!isImageMode && !_hourly_trigger) {
        // Wi-Fi重连策略（每分钟尝试一次）
        if (!_wifi_flag && (millis() - _wifi_failed_millis > 60 * 1000)) {
            // 确保有有效日期
            if (time(nullptr) < 100000) {
                restoreCachedDate();
            }
            #if DEBUG_MODE
            Serial.println("尝试重新连接WiFi...");
            #endif
            
            WiFi.mode(WIFI_STA);
            WiFi.begin();
    
            if (WiFiManager().autoConnect("J-Calendar")) {
                _wifi_flag = true;
                led_on();
                #if DEBUG_MODE
                Serial.println("重新连接到WiFi");
                #endif
                
                // 重新初始化核心服务
                _sntp_exec();
                weather_exec();
                cacheValidDate(); // 更新日期缓存
                
                // 刷新日历显示
                si_screen();
            } else {
                #if DEBUG_MODE
                Serial.println("WiFi重连失败");
                #endif
                if (time(nullptr) < 100000) {
                    restoreCachedDate();
                }
                _wifi_failed_millis = millis();
                WiFi.mode(WIFI_OFF);
            }
        }
      
        // 刷新服务：时间 > 天气 > 屏幕
        if (_sntp_status() == -1) _sntp_exec();
        if (weather_status() == -1) weather_exec();
        if (time(nullptr) > 100000) cacheValidDate();

        // 在以下条件下刷新日历：
        // 1. 时间有效（已同步或恢复缓存）
        // 2. 天气更新成功或没有WiFi
        // 3. 屏幕需要刷新
        if ((_sntp_status() > 0 || (time(nullptr) > 100000)) && 
            (weather_status() > 0 || !_wifi_flag) && 
            si_screen_status() == -1) {
            si_screen();
            displayCompleted = true;
            #if DEBUG_MODE
            Serial.println("日历数据已刷新");
            #endif
        }
    }

    //---------------------------------
    // 休眠条件检测
    // 仅在以下状态满足时：
    // 1. 不处于配置模式
    // 2. 屏幕刷新完成
    // 3. 无整点事件处理中
    // 4. 非图片模式
    //---------------------------------
    if (!wm.getConfigPortalActive() && 
        displayCompleted && 
        !_hourly_trigger && 
        !isImageMode) {
        
        unsigned long elapsedMillis = millis() - _idle_millis;

        // 满足休眠时间条件
        if (elapsedMillis > TIME_TO_SLEEP) {
            #if DEBUG_MODE
            Serial.println(">> 开始进入休眠流程");
            Serial.printf("  空闲时间: %lu秒\n", elapsedMillis / 1000);
            #endif
            go_sleep();
        } 
        #if DEBUG_MODE
        // 调试模式下显示休眠倒计时
        static unsigned long lastStatusPrint = 0;
        if (millis() - lastStatusPrint > 30000) {
            lastStatusPrint = millis();
            unsigned long remaining = TIME_TO_SLEEP - elapsedMillis;
            Serial.printf("距离休眠还有: %lu秒\n", remaining / 1000);
        }
        #endif
    }

    // 主循环延时（100ms周期，10Hz）
    delay(100);
}
