Windows 试玩：下载 **RoomShiftDemo-Windows-UE5.7-v0.3.0.zip**，完整解压，双击 **RoomShiftDemo.exe**。无需安装 Unreal Engine、Python 或 Arduino IDE。

本版包含：
- Hallyway1 卧室拼合：床、柜子、桌子、窗户及窗框，五个独立旋转面与家具牵引线。
- 开始/继续、设置和退出菜单，Esc 返回菜单。
- 鼠标与实体控制器两种模式；实体模式隐藏鼠标按钮栏和光标，显示实时连接状态。
- 游戏内直接接收现有 ESP32 OSC `/rotary` 与 `/imu`，不再需要 Python 桥接。编码器控制五面，BNO 控制视角，摇晃使已对齐的家具归位。

实体模式要求 ESP32 和电脑在同一网络，固件目标 IP 指向当前电脑，UDP 端口 8000；请关闭旧桥接，避免端口冲突。视角左右灵敏度为 2.5 倍，上下为 2 倍。编码器角度精度仍取决于实物每圈计数标定。

这是 Windows x64 试玩包，不能在 macOS 上直接运行。GitHub 下方自动生成的 Source code ZIP 是公开源码，不包含第三方授权模型原件与派生网格；直接试玩请选择上述 Windows ZIP。

旧 Release 保留。本版在制作电脑上进行打包与独立运行验证，其他电脑的性能与首次运行库环境可能不同。详细操作见压缩包内“玩法说明.txt”。
