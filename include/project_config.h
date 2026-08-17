#pragma once

#include <stdint.h>

namespace ProjectConfig {

// 填写家中 2.4 GHz Wi-Fi。保持占位值时，设备会直接创建自己的热点。
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// 配网失败时的应急热点。密码至少需要 8 个字符。
constexpr char AP_NAME_PREFIX[] = "ESP8266-Light";
constexpr char AP_PASSWORD[] = "light8266";
constexpr char MDNS_HOSTNAME[] = "light-control";

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_AP_FALLBACK_MS = 15000;

// NodeMCU 引脚号（括号内为 ESP8266 GPIO）：
// 舵机 1 = D1(GPIO5)，舵机 2 = D2(GPIO4)
constexpr uint8_t SERVO_1_PIN = 5;
constexpr uint8_t SERVO_2_PIN = 4;

// 使用 NodeMCU 原理图上的硬件 UART0：
// SU-03T TX -> RX(GPIO3)，SU-03T RX <- TX(GPIO1)
constexpr uint8_t VOICE_RX_PIN = 3;
constexpr uint8_t VOICE_TX_PIN = 1;
constexpr uint32_t VOICE_BAUD_RATE = 9600;

// 先在舵机未安装到墙壁开关时校准角度。
// 2 号舵机默认镜像安装，所以开/关角与 1 号相反。
constexpr uint8_t SERVO_1_NEUTRAL_ANGLE = 90;
constexpr uint8_t SERVO_1_ON_ANGLE = 45;
constexpr uint8_t SERVO_1_OFF_ANGLE = 135;
constexpr uint8_t SERVO_2_NEUTRAL_ANGLE = 90;
constexpr uint8_t SERVO_2_ON_ANGLE = 135;
constexpr uint8_t SERVO_2_OFF_ANGLE = 45;

constexpr uint16_t SERVO_MIN_PULSE_US = 500;
constexpr uint16_t SERVO_MAX_PULSE_US = 2500;
constexpr uint16_t SERVO_SETTLE_MS = 120;
constexpr uint16_t SERVO_PRESS_MS = 450;
constexpr uint16_t SERVO_RETURN_MS = 350;

}  // namespace ProjectConfig
