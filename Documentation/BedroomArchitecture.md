# 独立卧室：窗户、场景隔离与视觉归属

2026-11-07。本文件取代 BedroomV2AndOSC.md 中“完成窗户继续随 D 墙旋转”的行为说明。

## 修复的根因

旧版在窗户归位时额外乘入 D 墙当前旋转（目标恰为180°），因此出现倒置；随后 FollowTransforms 每帧继续写窗户变换。这与“已完成物体固定归位”冲突。现已同时删除运行代码和关卡中的 RH_FOLLOW_FACE_* 标签，不再为 item 8 单独添加旋转例外。

所有物件使用统一生命周期：未完成碎片随面旋转 → 0.2秒内消除对齐容差 → 1.15秒按一个完整组合移动到原始位置 → 隐藏碎片并显示原完整模型/附属件 → 固定不动。玻璃、窗框、窗台使用同一个组合变换，大小不变。Joined 和 completion_serial 在归位结束时才提交，避免“逻辑已经完成但动画还没结束”的中间态。

SourceTransforms 与 PropTransforms 是进入游戏时记录的固定归位姿态；普通墙面操作不改变它们。每块碎片的 Home 指向所属完整模型，或 RH_HOME_<ActorName> 指定的附属件。初始化检查所有 Home 引用是否存在、组合是否能刚性归位。错配会记录错误，阻止错误拼装，而不是悄悄挤到一起。

## 代码职责

路径统一位于 Plugins/RoomShift/Source/RoomShift/Private/：

| 文件 | 职责 |
|---|---|
| HallwayGame.cpp | 收集关卡数据、状态、旋转/确认/撤销/重置规则、固定视点、Tick 调度 |
| HallwayAnimation.cpp | 转面、刚性归位、成功效果、兼容旧双房间的过渡动画 |
| HallwayPresentation.cpp | 已完成模型显示/固定姿态、卧室隔离、五面颜色、细线创建和更新 |
| HallwayDevice.cpp | 本机 UDP JSON 解析、校验、状态回复，调用统一玩法入口 |
| HallwayUI.cpp | 鼠标键盘输入与 HUD，调用同一玩法入口 |

公共声明仍在 Public/HallwayGame.h，保留原 Unreal 类名与地图引用，避免关卡资产丢失绑定。未改写旧 RoomShiftGame.cpp 的第一版场景。当前仍保留旧双房间兼容字段、Actor 标签和固定物件ID；这是兼容原型的约束，不代表已把整个项目重构为通用关卡编辑器。下一步扩展大量房间时应将物件定义与面定义迁到数据资产，而非继续增加 ItemName 分支。

## 独立卧室

Hallyway1 继续 bStartInBedroom=true。新增 Bedroom/FixedEnclosure 文件夹：固定外壳六面与门洞封板。外壳不带 RH_PANEL，不进入旋转数组，因此内墙旋转时仍不会暴露门厅。门厅带标签对象与帘子在卧室模式下隐藏，不发生跨房间过渡；旧房间资产保留以便后续复用。

## 颜色与连接提示

A 陶土、B 鼠尾草绿、C 灰蓝、D 灰紫、E 沙色。新材质是 /Game/RoomShift/Bedroom/MI_Face_A..E，基于原 M_Plaster，保留细微墙面质感而非高饱和荧光色。HUD 色条、选中高亮和细线都使用对应色系。

细线采用无碰撞、无投影的细圆柱网格（不是只在开发版有效的调试线），从碎片实际包围盒中心连到所属墙面。锚点与碎片使用同一旋转参考系，动画中也跟随插值。非选中线直径约0.7cm，选中约1.4cm；开始归位后隐藏。它表达归属关系，不参与物理约束，也不改变已有模型尺寸。连接线指向在墙面法向上的投影点，不代表真实绳索动力学。

## 验证

- Win64 Development Editor 普通编译与 -DisableUnity 逐文件编译成功。
- Tools/verify_window_lifecycle.py 在真实 PIE 中验证：归位刚性结构、尺寸不变、最终位置/旋转等于原模型、完成后连续输入及D墙动画均不移动窗户、重置恢复碎片、封闭层持续可见。结果为 Documentation/WindowLifecycleTest.json。
- Tools/test_bedroom_start_runtime.py 完成窗户→柜子→床→桌子，全部通过。
- 独立游戏再次验证窗口生命周期，采样169帧通过；窗户完成后仅剩8条未完成物件连接线，重置后恢复15条。真实离屏游戏截图验证窗户正向固定、D墙转动后窗户不变，以及原门洞被封闭。截图：Saved/Screenshots/WindowsEditor/HighresScreenshot00004.png（开局）、00005.png（窗户完成并转D墙）、00006.png（门洞侧）。
- 不将本机测试视为实体手柄已通过验证；OSC端口和桥接命令沿用此前说明。

备份：Documentation/BedroomIdentityBackup-20261107-223351，含修改前地图与插件源码。改色若需微调，可以直接编辑五个 MI_Face 材质实例；不要删除 RH_ITEM/RH_FACE/RH_GOAL/RH_HOME 等玩法标签。
