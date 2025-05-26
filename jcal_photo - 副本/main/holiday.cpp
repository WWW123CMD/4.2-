#include "holiday.h"
#include "Arduino.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool getHolidays(Holiday& result, int year, int month) {
    String req = "https://timor.tech/api/holiday/year/" + String(year) + "-" + String(month);

    HTTPClient http;
    http.setTimeout(10 * 1000);
    http.begin(req);
    Serial.printf("Request: %s\n", req.c_str());
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {  // 使用预定义常量
        http.end();
        Serial.printf("HTTP错误: %d (%s)\n", httpCode, HTTPClient::errorToString(httpCode).c_str());
        return false;
    }
    
    String resp = http.getString();
    http.end();  // 尽早释放资源

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, resp);
    if (error) {
        Serial.printf("JSON解析失败: %s\n", error.c_str());
        return false;
    }

    // 使用显式类型转换和错误检查
    int statusCode = doc["code"].as<int>();  // 修改点1：添加显式类型转换
    if (statusCode != 0) {  // 修改点2：避免直接在表达式里使用doc["code"]
        Serial.printf("API返回错误码: %d\n", statusCode);
        return false;
    }

    result.year = year;
    result.month = month;

    // 安全访问嵌套对象
    JsonObject holidays = doc["holiday"];
    if (holidays.isNull()) {  // 修改点3：添加空对象检查
        Serial.println("未找到假期数据");
        return false;
    }

    int i = 0;
    Serial.print("Holiday: ");
    for (JsonPair kv : holidays) {
        // 安全获取键值
        const char* dateStr = kv.key().c_str();  // 修改点4：直接使用const char*
        JsonObject holidayInfo = kv.value();

        // 验证数据结构
        if (!holidayInfo.containsKey("holiday")) {
            continue;
        }

        // 转换为本地数据类型
        bool isHoliday = holidayInfo["holiday"].as<bool>();  // 修改点5：显式转换
        
        // 解析日期
        int day = atoi(dateStr + 3);  // 直接解析字符串中的日期部分
        result.holidays[i] = day * (isHoliday ? 1 : -1);

        Serial.printf("%d ", result.holidays[i]);
        if (++i >= 50) break;
    }
    Serial.println();
    result.length = i;

    return i > 0;  // 确保至少找到一个假期
}
