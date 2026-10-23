# Room Shift — Unreal 鼠标解谜 Demo

引擎：Unreal Engine 5.7。独立 C++ 项目，玩法位于可迁移的 `Plugins/RoomShift` 插件中。

这是第二版可玩的房间原型：鼠标点选 + 90° 档位旋转、布料/木材/墙面纹理与法线、圆角沙发、选中高亮、自由观察，以及家具拼接后的平滑落地。房间模型已保存到关卡，在 Unreal 编辑器中可直接查看和修改；仍属于原型模型，不包含开门动画或物理碰撞解谜。验证记录见 `Documentation/Validation.md`。

## 打开与运行

Windows 可以双击 `Launch Demo.cmd` 游玩：它读取环境变量 `UE_ROOT`（应指向 Unreal Engine 5.7 安装目录）；在制作电脑上也能自动找到已有安装。其他电脑可以打开 `RoomShiftDemo.uproject`，等待模块编译和加载后点击 Play。Windows 源码运行方式需要 Unreal Engine 5.7、Visual Studio 2022 C++ 构建工具和 Windows SDK。没有编译环境的 Windows 试玩者应下载打包版，解压后运行其中的 `RoomShiftDemo.exe`；无需安装 Unreal。

macOS 不能运行 Windows 的 `.exe`。Mac 开发者可以安装 Unreal Engine 5.7 与该版本所需的 Xcode，再用 `RoomShiftDemo.uproject` 打开源码并编译；如要给无需安装 Unreal 的 Mac 玩家试玩，需在 Mac 上另行打包 macOS 应用。当前没有 Mac 实机验证或 macOS 安装包，具体步骤见 `SHARING.md`。

Windows 试玩版下载：[GitHub Release v0.1.0](https://github.com/XinluLi-weeder/AlternativeControll_Workshop/releases/tag/v0.1.0)。请下载 Release 附件，完整解压后启动 `RoomShiftDemo.exe`；GitHub 页面上的“Code → Download ZIP”是源码。
关卡 `/Game/RoomShift/RoomShift` 已保存房间、家具部件和灯光。运行时沿用这些场景对象，另行生成鼠标操作界面。若首次打开时镜头停在房间外，可在 Outliner 选中 `RoomShiftPuzzle`，按 `F` 聚焦，然后用右键加 WASD 移动编辑器镜头。

## 在编辑器里修改场景

World Outliner 中的 `RoomShift` 文件夹按房间、旋转面、家具和灯光分组。选中网格对象即可在 Details 中调整位置、大小和材质；`Content/RoomShift/EditableMaterials` 存放场景使用的材质实例。编辑完按 `Ctrl+S` 保存关卡，按 Play 会使用这些已保存对象。旋转面的轴心坐标在 `RoomShiftPuzzle` 的 `RoomShift|Layout → Face Centers` 中调整；移动轴心时应一并检查所属墙面与家具部件的位置。家具部件的初始位置会从关卡读取，但拼接目标角度和完成后的落地位置仍由 C++ 规则定义。改变解谜目标时需同步修改代码中的目标变换。

## 操作

- 左键点击墙板、碎片或左侧 A/B/C 卡片，选择对应墙面。选中墙面的彩色边框发光，所控制的碎片同步高亮；A/B/C 字母固定在各墙面上方。
- 点击 TURN -90 / TURN +90，每次转 90°，四次一整圈，可无限循环。
- 按住鼠标右键拖动自由环视；按住右键时 WASD 移动、空格上升、左 Ctrl 下降、Shift 加速。
- 中键拖动平移视角，滚轮缩放，Home 恢复初始位置和视角。镜头移动限制在房间内部，用于观察，不模拟角色碰撞。
- ASSEMBLE 在部件对齐后可用；完成的家具在约1.2秒内平稳落地，并固定在地板上，后续墙面旋转不再影响它。
- UNDO 恢复角度、拼接状态与父子关系；RESET 重置关卡；HINT 显示下一件家具的目标角度。
- 键盘备用：1/2/3 选面、Q/E 旋转、空格组装、Z 撤销、R 重置、H 提示。

三面均是竖直墙上的圆形旋转内板，绕各自面法线旋转。地板、天花板和背墙固定。房间内第四面可见的平面通常是固定地板，不是第四个控制面。
家具碎片在解谜阶段悬挂于装配高度，由各自墙面带动；拼接后从墙面解除父子关系，并作为完整家具下降到地面。落地为确定性的动画，不使用重力或刚体碰撞。

## 关卡规则

| 家具 | 对齐角度 | 完成后位置 |
|---|---|---|
| 沙发 | A=90°，B=270° | 地板，房间前部 |
| 桌子 | A=180°，C=180° | 地板，沙发前方 |
| 门 | B=90°，C=0° | 地板，靠右墙竖立 |

每一块的初始局部变换由装配目标的世界变换反算，因此目标档位的各部件会精确拼合。
组装时使用 KeepWorldTransform 将碎片转移到房间根节点下的 Assembly 节点，保留拼接瞬间的位置，再统一向下移动180cm。三件家具底部最终均为地板高度 Z=0。
三件家具全部落地后完成关卡，仍可移动视角检查，也可继续转墙；已完成的家具不会移动。撤销同时恢复角度、父节点和装配状态。

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
ASSEMBLE
UNDO
RESET
```

一条 ROTATE 表示一个虚拟90°操作，不能把原始编码器每个电平边沿都直接当成一次90°旋转。
三路编码器应先正交解码、去抖，再按需要的档位/累计计数调用 `emitTurn`。`Tools/EncoderOutput` 是通信适配模板，尚未分配硬件引脚。

UDP 示例：

```json
{"session":"unique-device-session","seq":1,"cmd":"rotate","face":0,"dir":1}
```

face 0/1/2 对应 A/B/C；dir 为 1 或 -1。返回 `ok`、`duplicate`、`busy` 或 `invalid`。
桥接程序串行发送；忙或丢包时用同一序号重试，已接受指令去重。设备重启需换 session；桥接超时会退出，防止重新连接后回放积压动作。
第一次测试请只运行一个游戏实例，避免19872端口冲突。Unreal 原生鼠标输入和设备均调用 `RotateFace` / `Assemble` / `Undo` / `ResetPuzzle`。

## 编译与资源生成

```powershell
& 'D:/DKU_Academic/UE_5.7/Engine/Build/BatchFiles/Build.bat' RoomShiftDemoEditor Win64 Development "-Project=$PWD/RoomShiftDemo.uproject" -WaitMutex
& 'D:/DKU_Academic/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$PWD/RoomShiftDemo.uproject" -run=pythonscript "-script=$PWD/Tools/create_assets.py" -unattended -nullrhi
```

内置无渲染逻辑校验使用 `-game -nullrhi -unattended -RoomShiftSelfTest`，日志标记 `ROOMSHIFT_SELF_TEST PASS/FAIL`。
