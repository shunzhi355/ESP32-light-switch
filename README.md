# ESP8266 双舵机语音与网页灯控

基于 NodeMCU v3（ESP8266）的双路机械灯控项目。ESP8266 驱动两个舵机按压墙壁开关，并同时支持 SU-03T 离线语音控制和手机网页控制。

## 功能特性

- 1 号灯、2 号灯独立打开与关闭
- 两盏灯一键全部打开或全部关闭
- SU-03T 离线语音模块通过 UART 控制
- 手机浏览器局域网控制
- 上电立即创建管理热点，同时尝试连接家庭 2.4 GHz Wi-Fi
- 舵机动作完成后自动回到中位并停止 PWM
- 板载 LED 上电常亮，执行控制命令期间闪烁
- UART 帧头、异或校验和帧间超时恢复
- 家庭 Wi-Fi 断开后自动重连

## 系统结构

```text
SU-03T ── UART ──┐
                  ├── ESP8266 ── PWM ── 舵机 1 ── 灯开关 1
手机网页 ─ Wi-Fi ─┘          └── PWM ── 舵机 2 ── 灯开关 2
```

## 所需硬件

- NodeMCU v3 / ESP8266 开发板
- SU-03T 离线语音模块
- 两个 5 V 舵机
- 独立 5 V 稳压电源，建议输出能力不低于 2 A
- 8 Ω 扬声器与驻极体麦克风（按 SU-03T 模块要求）
- 470 µF、10 µF、100 nF 等电源去耦电容

## 接线

| 设备 | 设备引脚 | NodeMCU / 电源 |
|---|---|---|
| 舵机 1 | PWM 信号 | D1 / GPIO5 |
| 舵机 2 | PWM 信号 | D2 / GPIO4 |
| SU-03T | TX | RX / GPIO3 |
| SU-03T | RX | TX / GPIO1 |
| SU-03T | VCC | 5 V |
| SU-03T | GND | 公共 GND |
| 两个舵机 | V+ | 独立稳压 5 V |
| 两个舵机 | GND | 独立电源 GND，并与 NodeMCU 共地 |

> [!WARNING]
> 不要使用 NodeMCU 的 3V3 引脚为舵机供电。两个舵机同时启动时可能产生较大的瞬时电流，供电不足会导致 ESP8266 复位、Wi-Fi 消失或舵机抖动。所有模块必须共地。

SU-03T 的 UART 使用 3.3 V 逻辑电平，TX 与 RX 需要交叉连接。当前固件使用 UART0，串口参数为 `9600 8N1`。

## 项目结构

```text
.
├── include/
│   ├── project_config.h    # Wi-Fi、引脚、角度和动作时间配置
│   └── voice_protocol.h    # SU-03T 串口协议与解析器
├── src/
│   └── main.cpp            # 舵机、网页、Wi-Fi、LED 和主循环
├── platformio.ini          # PlatformIO / NodeMCU 构建配置
└── README.md
```

## 快速开始

### 1. 配置参数

编辑 `include/project_config.h`：

```cpp
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
```

ESP8266 只支持 2.4 GHz Wi-Fi。

发布仓库前不要提交真实 Wi-Fi 名称和密码。建议将本地配置文件加入 `.gitignore`，并在仓库中只保留不含凭据的示例配置。

舵机默认使用以下安装角度：

| 参数 | 舵机 1 | 舵机 2 |
|---|---:|---:|
| 中位 | 90° | 90° |
| 开灯方向 | 45° | 135° |
| 关灯方向 | 135° | 45° |

两个舵机按镜像安装设计。首次测试时不要先固定在墙壁开关上，应确认转动方向和角度不会造成堵转，再逐步调整 `SERVO_*_ANGLE`。

### 2. 编译

安装 VS Code 与 PlatformIO IDE 扩展，然后执行：

```powershell
pio run
```

目标板已在 `platformio.ini` 中配置为：

```ini
platform = espressif8266@4.2.1
board = nodemcuv2
framework = arduino
```

### 3. 烧录

```powershell
pio run -t upload
```

也可以显式指定串口，例如：

```powershell
pio run -t upload --upload-port COM10
```

编译后的固件位于：

```text
.pio/build/nodemcuv2/firmware.bin
```

## 联网与网页控制

设备上电后会立即创建管理热点，同时在后台连接配置的家庭 Wi-Fi。

### 管理热点

```text
热点名称：ESP8266-Light-xxxxxx
默认密码：light8266
控制地址：http://192.168.4.1/
```

`xxxxxx` 来自 ESP8266 芯片 ID，因此每块开发板的热点名称不同。

### 家庭 Wi-Fi

连接家庭 Wi-Fi 成功后，同一局域网中的设备可以访问：

```text
http://light-control.local/
```

如果 mDNS 在当前系统中不可用，也可以从路由器 DHCP 客户端列表查看 ESP8266 的 IP 地址。管理热点在家庭 Wi-Fi 成功后仍然保留。

网页提供以下按钮：

- 打开一号灯
- 关闭一号灯
- 打开二号灯
- 关闭二号灯
- 全部打开
- 全部关闭

## SU-03T 串口协议

在 SU-03T 配置平台中选择：

- 控制方式：端口输出
- 控制类型：`UART1_TX`
- 动作：发送
- 串口参数：`9600 8N1`
- 参数格式：两个十六进制字符为一组，使用空格分隔

不要添加 `0x`、逗号或换行。

### 请求帧

```text
AA 55 CMD PARAM CHECKSUM
```

```text
CHECKSUM = AA XOR 55 XOR CMD XOR PARAM
```

| 语音功能 | SU-03T 发送帧 |
|---|---|
| 打开一号灯 | `AA 55 01 01 FF` |
| 关闭一号灯 | `AA 55 01 00 FE` |
| 打开二号灯 | `AA 55 02 01 FC` |
| 关闭二号灯 | `AA 55 02 00 FD` |
| 打开全部灯 | `AA 55 03 01 FD` |
| 关闭全部灯 | `AA 55 03 00 FC` |

字段定义：

| 字段 | 数值 | 含义 |
|---|---|---|
| `CMD` | `01` | 一号灯 |
| `CMD` | `02` | 二号灯 |
| `CMD` | `03` | 全部灯 |
| `PARAM` | `00` | 关闭 |
| `PARAM` | `01` | 打开 |

### 应答帧

```text
55 AA CMD RESULT CHECKSUM
```

```text
CHECKSUM = 55 XOR AA XOR CMD XOR RESULT
```

| 执行结果 | ESP8266 返回帧 |
|---|---|
| 一号灯命令成功 | `55 AA 01 00 FE` |
| 二号灯命令成功 | `55 AA 02 00 FD` |
| 全部灯命令成功 | `55 AA 03 00 FC` |

`RESULT` 定义：

- `00`：执行成功
- `01`：命令码无效
- `02`：参数无效

校验错误、帧不完整或相邻字节间隔超过 100 ms 时，ESP8266 会丢弃该帧且不返回应答。

## HTTP API

### 查询状态

```http
GET /api/state
```

示例响应：

```json
{
  "light1": true,
  "light2": false,
  "busy": false,
  "network": "Wi-Fi + 应急热点",
  "ip": "192.168.1.100"
}
```

首次上电且尚未执行过控制命令时，`light1` 和 `light2` 为 `null`。

### 控制灯光

```http
POST /api/action?target=1&state=on
POST /api/action?target=1&state=off
POST /api/action?target=2&state=on
POST /api/action?target=2&state=off
POST /api/action?target=all&state=on
POST /api/action?target=all&state=off
```

例如：

```bash
curl -X POST "http://192.168.4.1/api/action?target=all&state=off"
```

## 板载 LED 状态

| LED 状态 | 含义 |
|---|---|
| 常亮 | 系统处于空闲状态 |
| 闪烁 | 正在执行舵机控制命令 |

当前 NodeMCU 使用 D4/GPIO2 上的低电平有效蓝色 LED。GPIO2 同时是 UART1 TX，因此固件不使用 UART1 输出调试日志。

## 工作方式与限制

- 舵机收到命令后先到达中位，再按压开灯或关灯方向，随后回到中位并断开 PWM。
- 两个舵机同时动作时，两个目标角连续写入，时间差仅为微秒量级。
- 一次完整舵机动作约需要 920 ms，此期间新的网页请求会等待。
- 网页状态表示 ESP8266 本次上电后最后执行的命令，不代表灯具的真实电气状态。
- 手动拨动墙壁开关后，网页状态不会自动更新。
- 如需真实状态反馈，需要增加光敏、电流或开关位置传感器。
- 网页和 HTTP API 默认没有身份认证，仅建议在可信局域网中使用。

## 常见问题

### 搜索不到管理热点

1. 确认开发板供电正常，CH340 串口能够被电脑识别。
2. 搜索名称类似 `ESP8266-Light-xxxxxx` 的 2.4 GHz 热点。
3. 检查 5 V 电源是否因舵机启动而压降。
4. 暂时断开两个舵机电源，仅保留 NodeMCU 测试热点。
5. 重新编译并烧录最新固件。

### 舵机动作时 ESP8266 复位

- 不要从 3V3 引脚为舵机供电。
- 使用独立 5 V / 2 A 或更高规格电源。
- 舵机电源与 NodeMCU 必须共地。
- 在舵机电源入口增加大容量电解电容。

### 网页显示状态不准确

这是当前设计的正常限制。系统没有灯光状态传感器，只记录最近一次软件命令。

## 安全提示

本项目通过舵机机械操作墙壁开关，不应直接连接或改造市电线路。安装时必须保证舵机结构不会损坏开关，并避免持续堵转。涉及市电部分应由具备资质的人员处理。
