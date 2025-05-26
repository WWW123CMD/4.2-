# J-Calendar 智能电子日历


基于ESP32的智能电子墨水屏日历，集成天气、倒数日和个性化图片展示功能，具备低功耗特性。

## 🌟 功能亮点
- **双模显示切换**：日历/图片模式一键转换
- **智能信息聚合**：
  - 📡 实时天气数据（支持和风天气API）
  - 📌 自定义倒数日事件
  - 🏷️ 日期标签系统
- **省电设计**：
  - 自动休眠（3分钟无操作）
  - 深度睡眠电流<20μA
- **易配置**：
  - Web配置界面（支持手机访问）
- **扩展性强**：
  - 支持最多3张自定义位图
  - 可扩展GPIO设备

## 📋 目录
- [硬件需求](#-硬件需求)
- [环境搭建](#-环境搭建)
- [代码结构](#-代码结构)


---

## 🛠️ 硬件需求
| 组件                | 规格要求                          |
|---------------------|---------------------------------- |
| ESP32开发板         | 推荐ESP32-WROOM-32D               |
| 电子墨水屏          | 4.2英寸 GxEPD2兼容屏              |
| 按键                | 微动开关                          |
| 电源                | 5V/1A USB电源 或 3.7V锂电池       |

**接线示意图**：
```
 墨水屏         ESP32
┌───────┐     ┌───────┐
| BUSY  ├─────┤ GPIO4 │
| RST   ├─────┤ GPIO16│
| DC    ├─────┤ GPIO17│
| CS    ├─────┤ GPIO5 │
| CLK   ├─────┤ SCK(18) 
| DIN   ├─────┤ MOSI(23)
| GND   ├─────┤ GND   │
| 3.3V  ├─────┤ 3.3V  │
└───────┘     └───────┘
（按钮接GPIO14-GND）
```

## 🔧 环境搭建
### 1. 开发环境
1. 安装 [Arduino IDE 1.8.x+](https://www.arduino.cc/en/software)
2. 添加ESP32支持：
   ```bash
   # Boards Manager URL添加：
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. 安装依赖库：
   ```ini
   WiFiManager @ ^0.16.0
   ArduinoJson @ ^6.21.0
   GxEPD2 @ ^1.5.2
   OneButton @ ^2.0.4
   U8g2_for_Adafruit_GFX @ ^1.8.0
   ```

### 2. 获取天气API
1. 注册[和风开发者账号](https://dev.qweather.com/)
2. 创建项目获取API Key
3. 查询[位置ID](https://github.com/qwd/LocationList)

---

## 📂 代码结构
```
J-Calendar/
├── main.ino            # 主程序入口
├── weather.h           # 天气数据处理模块
├── screen_ink.h        # 墨水屏显示驱动
├── _preference.h       # 参数存储管理
├── _sntp.h             # 时间同步模块
├── bitmaps/            # 图片资源
│   ├── bitmap1.h
│   ├── bitmap2.h
│   └── bitmap3.h
