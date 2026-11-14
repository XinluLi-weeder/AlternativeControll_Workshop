# Room Shift v0.3.0 — Bedroom Windows playtest

完整解压 Release 中的 Windows ZIP，双击 `RoomShiftDemo.exe`。不需要安装 Unreal Engine、Arduino IDE 或 Python。不要单独移动 exe，保留旁边的 Engine 和 RoomShiftDemo 文件夹。

初次打开显示 Start playing / Settings / Quit。游戏中按 Esc 返回菜单，选择 Resume 继续或 Quit 退出。

## 鼠标模式（默认）

Settings 中选择 Mouse + keyboard。鼠标右键拖动环视；左键选择墙面或底部 A–E 卡片；Q/E 或画面按钮转动；对齐后按空格或 ASSEMBLE 归位。H 提示、Z 撤销、R 重置、Home 视角回正。

本版游玩入口是 Hallyway1 的卧室。四面墙与天花板可旋转，拼合床、柜子、桌子和窗户/窗框。家具可以逐件完成，不必同时对齐。

## 实体控制器模式

1. ESP32 与电脑接入同一热点/局域网。现有 `new_rotate.ino` 的目标地址须为这台电脑的 Wi-Fi IPv4，目标 UDP 端口为 8000。换电脑后需要更新该地址。
2. 关闭旧的 `osc_bridge.py`。新版游戏直接接收 OSC，不需要桥接程序；同时运行会占用相同端口。
3. Settings 选择 Physical controller，等待 Controller connected。连接判定要求最近两秒内同时收到 `/rotary` 与 `/imu`，不会仅凭启动接收器就显示成功。
4. 返回并点击 Start playing / Resume，保持盒子在舒适的正向姿态约一秒。编码器 1–5 对应 A–E，旋转面连续跟随，BNO 控制环视。此模式隐藏鼠标按钮与光标，Esc 仍可返回设置。
5. 对齐后停稳至少 0.2 秒，再沿 BNO 的 Z 轴方向连续摇动两次，将家具归位。左右视角灵敏度为 2.5 倍，上下为 2 倍。

物理旋转 90° 与游戏转角一致的前提，是草图中的每圈计数正确：当前 `COUNTS_PER_REV=80`。用串口 STATUS 核对实际一整圈计数后再标定。无需为了本版重新上传已工作且 IP 正确的固件。

首次使用实体模式，Windows 若询问防火墙权限，允许游戏在当前本地网络接收数据。若提示 Port 8000 busy，请关闭旧桥接或其他 OSC 接收器，游戏会重试。

## 文件与平台

- 本 ZIP 是 Windows x64 试玩版，不能用于 macOS，也不是 UE 编辑工程。
- 如缺少 Microsoft Visual C++ 运行库，可运行附带的 `Engine/Extras/Redist/en-us/vc_redist.x64.exe` 后重试。
- GitHub 的 Source code ZIP 是公开源码；与此前版本相同，不含第三方授权原始模型和派生家具网格。完整试玩请下载 Windows 附件。
- 编码器和 BNO 协议保持 `/rotary` 五个 float、`/imu` 四个 float。固件源码的热点凭据改为本地 `wifi_secrets.h`；公开仓库提供 `.example` 模板。
