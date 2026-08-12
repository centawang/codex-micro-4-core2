# M5Stack Codex Micro

[English](README.md)

这是一个独立开发的开源兼容固件，可将 M5Stack Core2、CoreS3 或 StickS3
变成 ChatGPT 桌面端 Codex Micro 功能的蓝牙控制器。

固件将 Core2 模拟为 BLE Vendor HID 设备，并通过触摸屏提供 6 个 Agent Key、
6 个 Command Key、4 个摇杆方向和旋钮操作。任务状态颜色、电池状态、按键映射
和按住说话功能由 ChatGPT 桌面端处理。

> [!IMPORTANT]
> 本项目是非官方兼容实现，与 OpenAI、Work Louder 或 M5Stack 没有隶属、授权、
> 赞助、背书或技术支持关系。项目使用未公开的厂商协议，该协议可能随时发生变化。
> 使用或分发前请阅读 [NOTICE.md](NOTICE.md)。

## 功能

- 6 个触摸式 Agent Key，显示任务状态颜色和呼吸动画
- 6 个默认 Command Key：Fast、Approve、Decline、Fork、Mic、Send
- 4 个触摸方向键，对应摇杆的四个方向
- 旋钮逆时针、顺时针、按下和按住 500 ms 操作
- 支持在 ChatGPT 桌面端重新映射命令键和方向动作
- 通过 BLE HID 上报 Core2 电池状态
- 断开连接后自动重新广播
- 使用 PSRAM 全屏 `M5Canvas` 双缓冲，实现无明显闪烁的 320 x 240 界面

本项目是 Codex 控制器，不是通用蓝牙键盘。

## 兼容性

| 项目 | 支持或测试状态 |
| --- | --- |
| 硬件 | 已在 M5Stack Core2 实机上测试；CoreS3 和仅支持 Tasks 的 StickS3 均有独立构建环境 |
| 主机系统 | 已测试 macOS |
| 主机应用 | 支持 Codex Micro 的 ChatGPT 桌面端 |
| 通信方式 | 仅支持 Bluetooth Low Energy HID |
| 构建系统 | PlatformIO + Arduino Framework |

本项目于 2026 年 7 月 16 日在真实 Core2 硬件和 ChatGPT 桌面端上完成验证。
其他 Core2 修订版、其他操作系统、USB 控制和 Work Louder Input 尚未验证。

## 准备工作

- M5Stack Core2、CoreS3 或 StickS3
- 支持数据传输的 USB-C 线
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html) 或
  PlatformIO IDE 扩展
- 支持 Codex Micro 的 ChatGPT 桌面端
- macOS 上为 ChatGPT 开启“输入监控”权限

## 编译和烧录

工程提供三个 PlatformIO 环境。默认环境为 `m5stack-cores3`；其他目标请使用
`-e m5stack-core2` 或 `-e m5stack-sticks3`。

克隆仓库后执行：

```sh
pio run -e m5stack-cores3
pio run -e m5stack-cores3 --target upload
pio device monitor
```

如需构建仅支持 Tasks 功能的 StickS3 固件，请执行：

```sh
pio run -e m5stack-sticks3
pio run -e m5stack-sticks3 --target upload
```

PlatformIO 会自动安装工程锁定的 ESP32 平台和声明的 Arduino 库。串口监视器
波特率为 `115200`。启动成功后会输出：

```text
CODEX_MICRO_READY
```

运行不依赖实体硬件的 StickS3 按键单元测试：

```sh
pio test -e native
```

### 配置 StickS3 按键

使用 Microsoft Edge 或 Google Chrome 直接打开
[tools/sticks3-button-configurator.html](tools/sticks3-button-configurator.html)，
连接 StickS3 的 USB 串口，即可配置 `BtnA` 的单击/双击和 `BtnB` 的
单击/双击/长按。页面支持 Agent 导航、Codex 命令、常用键盘动作和自定义 HID
快捷键，也可设置屏幕变暗和自动关机时间；填写 `0` 可禁用对应超时。写入的
设置保存在 StickS3 NVS 中，断电后仍然有效；点击“恢复默认”会恢复为 Enter、
Right Alt、下一个 Agent、上一个 Agent、打开选中 Agent、1 分钟变暗和 30 分钟
自动关机。

CoreS3 通过原生 USB CDC 提供串口，因此每次复位串口都会断开并重新枚举。

常规应用固件位于：

```text
.pio/build/m5stack-cores3/firmware.bin
```

## 与 ChatGPT 桌面端配对

1. 烧录固件并重启 Core2。
2. 打开 macOS 蓝牙设置，与名为 **Codex Micro** 的设备配对。
3. 打开 ChatGPT 桌面端，并在 macOS 提示时允许“输入监控”。
4. 设备被识别后，打开 **Settings > Codex Micro**。
5. 设置 Agent Key 任务、Command Key 动作、摇杆方向和旋钮行为。本固件会显示任务
   状态颜色，但不复刻原键盘的环境灯光或逐键灯光。

如果 macOS 缓存过旧版 HID 描述符，请先在蓝牙设置中忽略 **Codex Micro**，
重启 Core2 后重新配对。

OpenAI 官方 Codex Micro 使用说明位于
[learn.chatgpt.com](https://learn.chatgpt.com/docs/features/codex-micro)。原键盘的
USB 模式、物理配对控件、灯光硬件和额外层说明不适用于本 Core2 固件。

## 操作说明

Core2 底部的 A、B、C 触摸按钮可直接切换三个页面。CoreS3 的触摸区域止于屏幕
下边缘，没有对应的硬件按钮，请使用屏幕底部的标签栏切换页面。

### Tasks 页面

| 控件 | 行为 |
| --- | --- |
| Agent 1 至 Agent 6 | 选择对应的 Codex 任务槽位 |
| 彩色边框和圆点 | 显示 ChatGPT 桌面端下发的任务状态颜色 |
| 呼吸边框 | 显示主机下发的动态状态 |

StickS3 会在一个紧凑列表中显示全部 6 个 Agent 状态。单击 `BtnB` 选择下一个
Agent，在 350 ms 内双击选择上一个 Agent，长按 500 ms 切换到当前高亮 Agent。
单击 `BtnA` 发送回车，在 350 ms 内双击 `BtnA` 发送右 Alt。StickS3 构建
在没有按键或 Agent 状态变化 1 分钟后降低屏幕亮度，任一活动会恢复正常亮度并
重置计时；连续 30 分钟无活动则自动关机。StickS3 构建有意省略 Commands 和
Navigate 页面。

### Commands 页面

| 按键 | ChatGPT 桌面端默认动作 |
| --- | --- |
| Fast | 开关 Fast 模式 |
| Approve | 批准当前请求 |
| Reject | 拒绝当前请求 |
| New chat | 在新对话中继续当前任务 |
| Mic | 按住说话；双击行为由主机处理 |
| Send | 发送编辑框中的消息 |

Mic 动作使用电脑麦克风。本固件不会采集或通过蓝牙传输 Core2 麦克风音频。

### Navigate 页面

- `UP`、`RIGHT`、`DOWN`、`LEFT` 模拟摇杆的四个方向，副标签显示主机默认
  动作：Plan 模式、前进、侧边栏、后退。
- `CCW` 和 `CW` 分别模拟一次旋钮逆时针和顺时针步进。
- 点击 `DIAL` 模拟按下旋钮。
- 按住 `DIAL` 至少 500 ms，请求打开 Codex Micro 设置。

动作映射由 ChatGPT 桌面端选择。Core2 上的标签显示原始默认布局，在主机端
重新映射后不会同步改变。

## 架构

```mermaid
flowchart LR
    Touch["Core2 触摸输入"] --> UI["页面和动作映射"]
    UI --> Transport["Vendor JSON-RPC 传输"]
    Transport --> HID["BLE HID Report ID 6"]
    HID <--> Desktop["ChatGPT 桌面端"]
    Desktop --> State["任务灯光和设备请求"]
    State --> Canvas["M5Canvas 帧缓冲"]
    Canvas --> LCD["320 x 240 屏幕"]
    Battery["Core2 电源状态"] --> Transport
```

HID 描述符、报告分帧、RPC 方法、并发模型、渲染流程和协议限制详见
[docs/TECHNICAL.zh-CN.md](docs/TECHNICAL.zh-CN.md)。

## 工程结构

```text
include/CodexMicroBle.h   BLE 传输和共享状态声明
src/CodexMicroBle.cpp     HID 描述符、分帧和 RPC 处理
src/main.cpp              Core2 界面、触摸映射、电池和渲染循环
src/main_sticks3.cpp      StickS3 精简 Tasks 界面和按键控制
docs/TECHNICAL.zh-CN.md   实现和协议说明
platformio.ini            可复现的 PlatformIO 环境
LICENSE                   MIT License
NOTICE.md                 版权、商标和免责声明
```

## 故障排查

### 蓝牙设置中找不到设备

- 确认屏幕右上角显示 `PAIR`。
- 重启 Core2 后重新扫描。
- 如果曾经配对过，先忽略旧蓝牙记录。

### ChatGPT 桌面端没有识别设备

- 确认 macOS 显示设备已连接。
- 授予“输入监控”权限后，完全退出并重新打开 ChatGPT。
- 打开“系统设置 > 隐私与安全性 > 输入监控”，确认 ChatGPT 已开启。
- 如果固件 HID 描述符发生过变化，请忽略设备并重新配对。

### 屏幕显示 `Canvas allocation failed`

固件无法分配全屏 16 位帧缓冲。请先重启设备；如果问题持续出现，请确认目标
硬件是带有可用 PSRAM 的 M5Stack Core2 或 CoreS3，并使用工程原有的
`m5stack-core2` 或 `m5stack-cores3` PlatformIO 开发板定义。

### 串口诊断

运行 `pio device monitor`。日志会显示 BLE 配对结果、主机连接状态、收到的 RPC
方法名称和发送的按键动作。公开日志前，请先检查并移除与本机环境有关的信息。

## 安全与限制

- BLE 采用绑定和无输入/无输出的“Just Works”配对方式，请只在可信环境中配对。
- 固件不包含网络客户端，也不会直接连接 OpenAI。控制事件和电池状态仅通过蓝牙
  发送给已配对主机。
- Vendor HID 标识和协议仅用于兼容性，不属于也未分配给本项目。
- ChatGPT 桌面端更新后，协议兼容性可能失效。
- 同一时间只支持一个 BLE 主机连接。
- 本项目不复刻原键盘的 USB 传输、物理按键、模拟摇杆、LED、额外层或固件更新器。

## 参与贡献

欢迎提交 Issue 和 Pull Request。报告兼容性问题时，请提供 Core2 硬件版本、主机
系统版本、ChatGPT 桌面端版本、已移除隐私信息的串口日志和完整复现步骤。

协议改动应保持范围明确，并注明结论来自实际观察、合理推断还是硬件验证。

## 许可证

Copyright (c) 2026 imliubo.

源代码和项目文档采用 [MIT License](LICENSE) 发布。第三方产品名称、商标、协议
标识和依赖仍归其各自权利人所有，不属于本许可证的授权范围。详见
[NOTICE.md](NOTICE.md)。
