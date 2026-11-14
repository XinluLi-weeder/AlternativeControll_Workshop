# 使用旧版 new_rotate.ino 通过 OSC 控制 Hallyway1

> v0.3.0 起，游戏已内置 OSC 接收：在 Settings 选择 Physical controller 即可，无需运行本页的 Python 桥接。请勿同时运行旧桥接和新版实体模式，它们都使用 UDP 8000。新版操作见 [WindowsPlaytest-v0.3.0](WindowsPlaytest-v0.3.0.md)。以下保留为历史桥接方案。

本方案保留 `Arduino/new_rotate/new_rotate.ino` 的 OSC 输出。Unreal 当前原生接收的是本机 UDP 19872 上的 JSON，因此电脑运行 `Tools/osc_bridge.py`：接收 ESP32 发来的 OSC `/rotary`、`/imu`，转换成游戏已有的角度和视角命令。Arduino 固件无需改成串口协议；ESP32 和电脑需要在同一局域网/手机热点下。

## 1. 核对接线

旧版草图里的 GPIO 分配如下，不要套用 `Tools/CubeControllerESP32/CubeControllerESP32.ino` 的新分配：

| 编码器 | A | B |
|---|---:|---:|
| 1 | GPIO4 | GPIO5 |
| 2 | GPIO6 | GPIO7 |
| 3 | GPIO8 | GPIO9 |
| 4 | GPIO10 | GPIO11 |
| 5 | GPIO14 | GPIO38 |

编码器模块 `C` 脚留空，VCC/GND/A/B 按模块规格接线。确认编码器输出适用于 ESP32-S3 的 3.3 V GPIO；不要把 5 V 输出接入 GPIO。BNO055 接 SDA=GPIO1、SCL=GPIO2、共地；VIN 供电需先确认该 BNO055 模块的规格。

旧版编码器每圈计数固定为 80。在实际旋转面上准确转一整圈，用串口 `STATUS` 查看计数变化；若不是 80，应将草图的 `COUNTS_PER_REV` 改为实测值后重新上传。方向反了可调整对应 `encoderDirection` 的 1/-1。

## 2. 配置并上传旧版 OSC 固件

1. Arduino IDE 打开 `Arduino/new_rotate/new_rotate.ino`，选择 FireBeetle 2 ESP32-S3 对应板卡和 USB COM 端口，安装 ESP32 板卡包、Adafruit BNO055 与其依赖库、CNMAT OSC 库。
2. 在草图 Wi-Fi 区域填入 ESP32 与电脑将共同加入的手机热点名称和密码。
3. 将 `laptopIP` 改成电脑在该热点下的 IPv4 地址，`laptopPort` 保持为 `8000`。在 Windows 终端运行 `ipconfig`，查看连接热点的 Wi-Fi 适配器 IPv4 地址；不要填写 ESP32 自己的地址，也不要填 `127.0.0.1`。
4. 上传后打开串口监视器，115200 波特率，确认显示 Wi-Fi 已连接及 BNO055 状态。输入 `STATUS` 检查五个编码器旋转时角度变化。

## 3. 启动 Unreal 和 OSC 桥接

1. 电脑和 ESP32 接入同一个手机热点/局域网。
2. Unreal 打开 `RoomShiftDemo.uproject`，进入 `Hallyway1` 并按 Play。当前关卡的 JSON 控制接口在本机 `127.0.0.1:19872`。
3. 在项目根目录启动 PowerShell，运行：

   ```powershell
   python Tools/osc_bridge.py
   ```

   桥接程序使用 Python 标准库，不需要 `pyserial` 或 Arduino 串口监视器。它会监听所有电脑网卡的 UDP 8000，并将收到的 OSC 转发给 Unreal。它会合并传感器突发数据，每路最高 30 Hz 发送最新值，不逐帧等待 Unreal 回执；拼合动作仍需 Unreal 确认。若 Windows 防火墙首次询问，允许 Python 在当前网络接收通信；若 8000 端口被占用，先关闭旧桥接进程。

4. 桥接窗口应出现来自 ESP32 Wi-Fi 地址的 `172.x.x.x OSC /rotary ...` 和 `172.x.x.x OSC /imu ...`；来源若为 `127.0.0.1`，那是本机测试数据，不是实体手柄。游戏左上角调试框也应显示该来源与最新消息。按 F3 可开关调试框。缓慢转动一个面，确认对应墙面和家具连续转动；转盒子确认视角变化。
5. 编码器 1–5 分别控制游戏 A–E 面，互不排队，可以同时旋转。初次测试建议逐个将一个编码器转约 90°，确认对应游戏面也转 90°；若实体转角不符，先用草图的 `STATUS` 校准 `COUNTS_PER_REV`。拼合到目标位置后停稳至少 0.2 秒，再沿 BNO055 的 Z 轴方向摇动两次；串口 `/imu` 的最后一个加速度值应出现两次超过约 6 m/s² 的峰值。桥接显示 `OSC shake assembled aligned furniture` 才代表 Unreal 接受了拼合。空格键可单独验证关卡本身的拼合逻辑。
6. 可以保留桥接窗口，反复停止并重新开始 Unreal Play。每局有独立标识，重新开始后会自动重设编码器和 BNO 的中立点；开始时将盒子保持在舒适的正向姿势约一秒。结束整次测试后，在桥接窗口按 Ctrl+C。

## 4. 自检与故障定位

桥接启动时会打印本机所有 IPv4 地址。请把其中 Wi-Fi 热点网卡的地址（通常是 `172.20.10.3`）填到 `new_rotate.ino` 的 `laptopIP`。

不接手柄也能验证整条链路：

1. 先在 Unreal 中进入 Hallyway 关卡并按下 Play。
2. 在项目根目录运行 `python Tools/osc_bridge.py --selftest`（或运行 `Start Controller Bridge.cmd --selftest`）。
3. 桥接会自己发送 5 秒虚拟 OSC，然后输出 `SELFTEST angles_changed=True view_moved=True PASS` 并以退出码 0 结束。PASS 表示 OSC 解析、角度映射、视角映射和 Unreal 命令都正常。
4. 自检只会短暂转动 B 面并移动视角，结束时会恢复原来的角度和视角。

正常运行时，桥接先显示本机地址，收到手柄第一个包后显示 `OSC link online: first packet from 172.20.10.x`。若 5 秒内没有收到任何包，桥接会每 10 秒提示一次，检查 ESP32 网络、`laptopIP` 和防火墙。游戏左上角调试框（F3 切换）显示最近收到的 OSC 原文和 Unreal 回执。

固件 `new_rotate.ino` 现在会自动重连热点；串口每 0.5 秒输出 `WiFi OK | sent N | IMU x/y/z ... | ROTARY ...`。若显示 `WiFi DOWN` 或 `sent` 不增长，问题在 ESP32 网络侧，不在 Unreal。IMU 使用 IMUPLUS（加速度+陀螺仪）融合，避免磁力计漂移。

注意：玄关阶段只有 B（右墙）、C（左墙）、E（天花板）可以转动，A 和 D 进入卧室后才解锁，HUD 对应面会显示 `LOCKED`。这是关卡设计，不是通信故障。

## 消息映射和限制

- `/rotary` + 五个 float：转为游戏五面角度；首次连接时以游戏当前角度建立偏移，避免接入瞬间跳转。
- 游戏以编码器输出的连续角度旋转墙面；90° 的实体运动对应约 90° 游戏运动，拼合判定允许目标角度 ±5°，且需停稳 0.2 秒。这个角度关系取决于编码器实际每圈计数及实体面与编码器轴的传动比；`COUNTS_PER_REV=80` 只是当前草图的初始值。
- `/imu` + 四个 float：旧版固件按 BNO055 Euler X/Y/Z 和线性加速度 Z 发送。每局以初始 yaw/pitch 为视角中立点，默认左右灵敏度 2.5 倍、上下 2 倍，平滑时间约 40 毫秒。手柄绕竖轴转 20°，视角转约 50°；抬/低头 10°，视角约变化 20°。该功能检测转动姿态，平移盒子不会产生持续的视角变化。若方向反了，可运行 `python Tools/osc_bridge.py --yaw-gain -2.5` 或 `--pitch-gain -2`。如 BNO 板侧装、俯仰动作主要改变 Euler Y 而不是 Z，可加 `--pitch-axis roll`。
- 重开 Play 后，桥接在下一次状态查询时识别新局（正常约 0.5 秒），丢弃上一局待处理动作，重新校准角度和视角。短暂停止 Play 不需要重新上传 Arduino，也不需要重启 ESP32。Unreal 会拒绝带旧局标识的输入，避免新局被旧输入干扰。
- 摇动：旧版没有单独发送 `/shake`，桥接程序从 `/imu` 的线性加速度 Z 检测两次峰值；实际灵敏度取决于 BNO055 的安装方向和手柄动作。空格可独立验证拼合逻辑。
- 当前游戏 HUD 显示的 Unreal ACK 是电脑桥接与游戏之间的回执；旧版固件本身发送 OSC，没有接收 Unreal 回传，因此不会收到此回执或驱动实体振动马达。

若桥接没有出现来自 ESP32 Wi-Fi 地址的 OSC，先确认串口启动信息有 `SUCCESS! Connected to hotspot.`，且 `laptopIP` 是电脑该热点网卡地址、`laptopPort=8000`；串口实时打印传感器值本身不能证明 UDP 已送达。Windows 防火墙需允许 Python 接收来自本地子网的 UDP 8000，并确认没有其他程序占用 8000。默认桥接会忽略 `127.0.0.1` 本机测试包；只有运行本地测试时才加 `--allow-loopback`。
