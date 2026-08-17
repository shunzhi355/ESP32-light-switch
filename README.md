# ESP8266 双舵机语音 / 网页灯控

本工程使用 NodeMCU v3（ESP8266）控制两个舵机机械按压灯的墙壁开关。SU-03T 完成离线语音识别，并通过 3.3 V TTL 串口把控制帧发给 ESP8266；ESP8266 同时提供手机网页控制。

> 原工程中的 `driver/ledc.h`、新版 I2S 和 GPIO7 都是错误方向：它们属于 ESP32 或会占用 ESP8266 的板载 Flash。本工程已改用 ESP8266 Arduino Core、Servo 和硬件 UART0。

## 功能

- 语音控制灯 1 开/关、灯 2 开/关、两灯同时开/关
- 手机网页分别开关两盏灯，一键全开和一键全关
- 家庭 2.4 GHz Wi-Fi 连接失败时自动建立应急热点
- 串口帧头、异或校验、帧间超时恢复，能过滤启动日志或线路干扰
- 舵机按压后自动回到中位并停止 PWM，降低持续堵转、发热和抖动
- 上电不主动转动舵机；首次操作前网页状态显示“未知”

## 接线

| 设备 | 设备引脚 | NodeMCU / 电源 |
|---|---|---|
| 舵机 1 | PWM 信号 | D1 / GPIO5 |
| 舵机 2 | PWM 信号 | D2 / GPIO4 |
| SU-03T | TX | RX / GPIO3（ESP 接收） |
| SU-03T | RX | TX / GPIO1（ESP 发送） |
| SU-03T | VCC | 5 V |
| SU-03T | GND | 公共 GND |
| 两个舵机 | V+ | 独立稳压 5 V |
| 两个舵机 | GND | 独立电源 GND，并与 NodeMCU GND 共地 |

重要事项：

1. 两个舵机不要由 NodeMCU 的 3V3 引脚供电。建议使用不低于 5 V / 2 A 的独立稳压电源；两舵机同时启动时电源需留足峰值余量。
2. 所有模块必须共地。舵机接口附近保留原理图中的 470 µF 电解电容，NodeMCU 和 SU-03T 电源入口保留 10 µF + 100 nF 去耦。
3. SU-03T 的 UART 是 3.3 V 电平，可与 ESP8266 直接连接；图中的 100 Ω 串联电阻可保留。
4. SU-03T 保持原理图的 RX/TX 接口，TX、RX 按上表交叉连接。

## SU-03T 串口协议

串口参数：`9600, 8N1`。在 SU-03T 的语音平台中，把每条识别词配置为发送下表对应的 5 个十六进制字节。

请求格式：

```text
AA 55 CMD PARAM CHECKSUM
CHECKSUM = AA XOR 55 XOR CMD XOR PARAM
```

| 建议语音词 | CMD | PARAM | 完整发送帧 |
|---|---:|---:|---|
| 打开一号灯 | `01` | `01` | `AA 55 01 01 FF` |
| 关闭一号灯 | `01` | `00` | `AA 55 01 00 FE` |
| 打开二号灯 | `02` | `01` | `AA 55 02 01 FC` |
| 关闭二号灯 | `02` | `00` | `AA 55 02 00 FD` |
| 全部打开 | `03` | `01` | `AA 55 03 01 FD` |
| 全部关闭 | `03` | `00` | `AA 55 03 00 FC` |

ESP8266 完成动作后会返回：

```text
55 AA CMD RESULT CHECKSUM
CHECKSUM = 55 XOR AA XOR CMD XOR RESULT
```

`RESULT=00` 表示成功，`01` 表示命令无效，`02` 表示参数无效。SU-03T 不需要应答时可以只连接其 TX，NodeMCU 的 TX/GPIO1 可不接。

## 配置与舵机校准

编辑 [`include/project_config.h`](include/project_config.h)：

1. 将 `WIFI_SSID` 和 `WIFI_PASSWORD` 攓成家庭 2.4 GHz Wi-Fi。保持占位内容时，ESP8266 会直接建立类似 `ESP8266-Light-ABC123` 的热点，密码为 `light8266`。
2. 舵机先不要安装到开关上，通电观察动作，再修改 `SERVO_1_*_ANGLE` 和 `SERVO_2_*_ANGLE`。默认假设两个舵机镜像安装。
3. 若开关没有压到位，微调 ON/OFF 角；若挤压过大，减小目标角与中位角的差值。必要时调整 `SERVO_PRESS_MS`。

## 编译与烧录

推荐 VS Code + PlatformIO：

```powershell
pio run
pio run -t upload
pio device monitor
```

工程目标板已经固定为 `nodemcuv2`。USB 串口对应 UART0，波特率为 9600，用于 SU-03T 数据帧；程序不会向该串口输出普通调试文字。诊断日志从 UART1 TX（D4/GPIO2）以 115200 输出，如有需要可连接独立 USB-TTL 模块查看。首次构建时 PlatformIO 会自动安装固定版本的 ESP8266 平台依赖。

启动后的访问方式：

- 成功连接路由器：访问 `http://light-control.local/`，也可从路由器 DHCP 列表或 UART1 调试输出查询局域网 IP
- 未配置或连接失败：手机连接 `ESP8266-Light-xxxxxx`，密码 `light8266`，访问 `http://192.168.4.1/`

## HTTP 接口

- `GET /api/state`：读取两路状态和网络状态
- `POST /api/action?target=1&state=on`
- `POST /api/action?target=2&state=off`
- `POST /api/action?target=all&state=on`

网页调用的就是这些接口，也可以接入 Home Assistant 或其他局域网控制器。状态表示本次上电后 ESP8266 已执行的命令，不是灯具电流的真实反馈；如果需要掉电记忆或闭环检测，应再增加 NVS/EEPROM 或光敏/电流传感器。
