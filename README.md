# Room Shift — Unreal 鼠标解谜 Demo

引擎：Unreal Engine 5.7。独立 C++ 项目，玩法位于可迁移的 `Plugins/RoomShift` 插件中。

v0.2.0 是五面房间原型：四面墙与天花板按 90° 档位旋转，恢复六组家具。Windows Release 包含完整已烹制场景，运行无需 Unreal；公开源码仓库保存玩法、关卡布局和构建脚本，不收录授权的第三方原始模型与由其切出的家具网格。完成时有短促画面微震、兼容手柄震动和 Arduino 反馈接口。它仍是解谜原型，不包含开门动画或物理碰撞解谜。新版说明与验证见 `Documentation/StudioGuide.md`。

## 打开与运行

Windows 可以双击 `Launch Demo.cmd` 游玩：它读取环境变量 `UE_ROOT`（应指向 Unreal Engine 5.7 安装目录）；在制作电脑上也能自动找到已有安装。其他电脑可以打开 `RoomShiftDemo.uproject`，等待模块编译和加载后点击 Play。Windows 源码运行方式需要 Unreal Engine 5.7、Visual Studio 2022 C++ 构建工具和 Windows SDK。没有编译环境的 Windows 试玩者应下载打包版，解压后运行其中的 `RoomShiftDemo.exe`；无需安装 Unreal。

macOS 不能运行 Windows 的 `.exe`。Mac 开发者可以安装 Unreal Engine 5.7 与该版本所需的 Xcode，再用 `RoomShiftDemo.uproject` 打开源码并编译；如要给无需安装 Unreal 的 Mac 玩家试玩，需在 Mac 上另行打包 macOS 应用。当前没有 Mac 实机验证或 macOS 安装包，具体步骤见 `SHARING.md`。

Windows 试玩版下载：[GitHub Release v0.2.0](https://github.com/XinluLi-weeder/AlternativeControll_Workshop/releases/tag/v0.2.0)。请下载 Release 附件，完整解压后启动 `RoomShiftDemo.exe`；GitHub 页面上的“Code → Download ZIP”是源码，不包含第三方原始素材。
默认关卡为 `/Game/RoomShift/RoomShiftStudio`。原先的 `/Game/RoomShift/RoomShift` 保留，包含升级前用户保存的场景。新版已保存房间、家具部件和灯光，运行时沿用这些场景对象。若首次打开时镜头停在房间外，可以在 Outliner 中找到 `PreviewCamera` 并选择 Pilot，或选中场景对象后按 `F` 聚焦。公开源码克隆需要自行取得授权素材、补齐 `/Game/ArchVis` 和 `/Game/OldFurniturePack_01`，并重建 `Content/RoomShift/Studio` 下的素材，才能完整编辑新版场景；直接试玩请使用 Release 包。

## 在编辑器里修改场景

World Outliner 中的 `RoomShift` 文件夹按房间、旋转面、家具和灯光分组。选中网格对象即可在 Details 中调整位置、大小和材质；`Content/RoomShift/EditableMaterials` 存放场景使用的材质实例。编辑完按 `Ctrl+S` 保存关卡，按 Play 会使用这些已保存对象。旋转面的轴心坐标在 `RoomShiftPuzzle` 的 `RoomShift|Layout → Face Centers` 中调整；移动轴心时应一并检查所属墙面与家具部件的位置。家具部件的初始位置会从关卡读取，但拼接目标角度和完成后的落地位置仍由 C++ 规则定义。改变解谜目标时需同步修改代码中的目标变换。

## 操作

- 左键点击墙板、碎片或底部 A–E 卡片，选择对应面。A 前墙、B 右墙、C 左墙、D 后墙、E 天花板；选中面的彩色边框发光，碎片显示淡色轮廓。
- 点击 TURN -90 / TURN +90，每次转 90°，四次一整圈，可无限循环。
- 视点固定在房间中央略靠后、离地 150 cm，视野 120°，初始仰角 12°。按住鼠标右键拖动环视，Home 回正。
- 游戏中不能移动位置；WASD、中键和滚轮不再移动或缩放镜头。预留 BNO055 视角接口 `VIEW,yaw,pitch`（角度），详见 Documentation/StudioGuide.md。
- ASSEMBLE 在部件对齐后可用；完成的家具在约1.2秒内平稳落地，并固定在地板上，后续墙面旋转不再影响它。
- UNDO 恢复角度、拼接状态与父子关系；RESET 重置关卡；HINT 显示下一件家具的目标角度。
- 键盘备用：1–5 选面、Q/E 旋转、空格组装、Z 撤销、R 重置、H 提示；N 重新打乱起始角度，V 关闭或开启画面微震。底部工具栏也提供 NEW MIX、撤销、重置和 HELP。

五个旋转面均绕各自面法线旋转。四面墙和天花板上的旋转内板可以操作，外框和地板固定。新增家具引入背墙以及天花板的第三个控制轴。
家具碎片在解谜阶段悬挂于装配高度，由各自墙面带动；拼接后从墙面解除父子关系，并作为完整家具下降到地面。落地为确定性的动画，不使用重力或刚体碰撞。

## 关卡规则

| 家具 | 对齐角度 | 完成后位置 |
|---|---|---|
| 沙发 | A=90°，B=270° | 地板，房间前部 |
| 桌子 | A=180°，C=180° | 地板，沙发前方 |
| 门 | B=90°，C=0° | 地板，靠右墙竖立 |
| 扶手椅 | C=90°，D=270° | 地板，左侧阅读区 |
| 书架 | A=270°，D=90°，E=180° | 地板，前侧右部 |
| 书桌 | B=180°，C=270°，E=90° | 地板，后侧右部 |

每一块的初始局部变换由装配目标的世界变换反算，因此目标档位的各部件会精确拼合。
组装时使用 KeepWorldTransform 将碎片转移到房间根节点下的 Assembly 节点，保留拼接瞬间的位置，再统一向下移动180cm。三件家具底部最终均为地板高度 Z=0。
六组家具全部落地后完成关卡，仍可移动视角检查，也可继续转墙；已完成的家具不会移动。撤销同时恢复角度、父节点和装配状态。前四组涉及两个面，书架和书桌需要三个面配合。

## 材质资源

`Tools/generate_visual_sources.py` 用 NumPy/Pillow 生成原创纹理、法线贴图和圆角盒模型源文件，`Tools/create_visual_assets.py` 在 Unreal 内导入并创建材质。生成的 `.uasset` 已保存，正常游玩无需安装 Python 依赖或重新运行生成脚本。

## 外设接口

游戏只监听本机 `127.0.0.1:19872` UDP。Arduino 串口通过 `Tools/device_bridge.py` 转成同一组玩法命令。

无需硬件测试：

```powershell
python Tools/device_bridge.py --stdin
python Tools/device_bridge.py --solve-demo
```

接 Arduino（串口模式需安装 pyserial）：

```powershell
python Tools/device_bridge.py --serial COM4 --baud 115200
```

Arduino 每行发送一个事件：

```text
ROTATE,A,1
ROTATE,B,-1
ROTATE,C,1
ROTATE,D,-1
ROTATE,E,1
ASSEMBLE
UNDO
RESET
```

一条 ROTATE 表示一个虚拟90°操作，不能把原始编码器每个电平边沿都直接当成一次90°旋转。
五路编码器应先正交解码、去抖，再按需要的档位/累计计数调用 `emitTurn`。`Tools/EncoderOutput` 是通信适配模板，尚未分配硬件引脚。串口桥接会轮询拼合完成计数，并向 Arduino 发送一次 `HAPTIC,180,140`（180 毫秒，PWM 强度 140）。模板默认关闭电机引脚，需按实际开发板配置 PWM 引脚与电机驱动电路。

UDP 示例：

```json
{"session":"unique-device-session","seq":1,"cmd":"rotate","face":0,"dir":1}
```

face 0–4 对应 A–E；dir 为 1 或 -1。返回 `ok`、`duplicate`、`busy` 或 `invalid`。`status` 命令可在动画期间读取五个角度、六个完成状态、busy 和 completion_serial；后者用于震动去重。
桥接程序串行发送；忙或丢包时用同一序号重试，已接受指令去重。设备重启需换 session；桥接超时会退出，防止重新连接后回放积压动作。
第一次测试请只运行一个游戏实例，避免19872端口冲突。Unreal 原生鼠标输入和设备均调用 `RotateFace` / `Assemble` / `Undo` / `ResetPuzzle`。

## 编译与资源生成

```powershell
& 'D:/DKU_Academic/UE_5.7/Engine/Build/BatchFiles/Build.bat' RoomShiftDemoEditor Win64 Development "-Project=$PWD/RoomShiftDemo.uproject" -WaitMutex
& 'D:/DKU_Academic/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$PWD/RoomShiftDemo.uproject" -run=pythonscript "-script=$PWD/Tools/create_assets.py" -unattended -nullrhi
```

内置无渲染逻辑校验使用 `-game -nullrhi -unattended -RoomShiftSelfTest`，日志标记 `ROOMSHIFT_SELF_TEST PASS/FAIL`。
