# 分享 Room Shift Demo

## 给只想试玩的人：Windows 打包版

从 GitHub Releases 下载 Windows 压缩包，完整解压到普通文件夹，再双击 `RoomShiftDemo.exe`。请勿只从压缩包里单独拖出 exe：旁边的 `Engine/` 和 `RoomShiftDemo/Content/` 等文件也是游戏的一部分。试玩者无需安装 Unreal Engine 或 Visual Studio。当前只制作 Windows 版；鼠标和键盘即可玩，Arduino 不是必需品。压缩包内附有中文操作说明。

项目作者可在已安装 Unreal Engine 5.7 的 Windows 电脑上重新打包：在编辑器的 Platforms → Windows 中选择 Package Project，或运行 Unreal Automation Tool 的 BuildCookRun（Win64 Development，build/cook/stage/package/archive）。打包结果放在 `Releases/Windows/`，它被 Git 忽略。运行 `python Tools/make_release_zip.py` 会生成 `Releases/RoomShiftDemo-Windows-UE5.7.zip`，排除不影响游玩的调试符号，作为 GitHub Release 附件上传。向试玩者发送 Release 下载链接。

## 给想看代码或改项目的人：GitHub 仓库

上传 `Config/`、`Content/`、`Plugins/RoomShift/Source/`、`Source/`、`Tools/`、`.uproject` 和说明文件。`.gitignore` 已排除 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`、打包输出及日志。`Content/RoomShift` 内的 `.uasset` 和 `.umap` 必须保留，只有 C++ 代码无法复原现有场景资源。

开发者克隆仓库后，用 Unreal Engine 5.7 打开 `RoomShiftDemo.uproject`，允许生成/编译 C++ 模块。初次编译需要 Visual Studio 2022 C++ 构建工具与 Windows SDK，时间可能较长。关卡中的房间、家具部件和灯光可在编辑器里直接选择、修改和保存；按 Play 则运行解谜。若视口停在房间外，在 Outliner 选中 `RoomShiftPuzzle` 并按 `F` 聚焦。也可以设置 `UE_ROOT` 指向自己的引擎安装目录，再双击 `Launch Demo.cmd`。

GitHub 仓库页面的“Download ZIP”下载的是**项目源码**，不能直接当成免安装游戏。给非开发者分享 GitHub Release 中的打包版。

## 给 Mac 用户

现有 `RoomShiftDemo-Windows-UE5.7.zip` 只能在 Windows 上运行。Mac 用户如果装有 Unreal Engine 5.7 和兼容的 Xcode，可以克隆源码，在 macOS 上打开 `RoomShiftDemo.uproject`，允许编译项目与 `RoomShift` 插件，然后在编辑器里点击 Play。项目的核心玩法使用 Unreal 的跨平台模块；`DefaultEngine.ini` 中 DX11 配置仅位于 Windows 专用设置节。这个 Mac 源码运行路径尚未经过实机验证。

如果要让普通 Mac 用户免装 Unreal 试玩，需要在 Mac 上打开项目并选择 Platforms → macOS → Package Project，测试生成的 `.app`，再将**完整打包输出**单独压缩并作为 GitHub Release 的 macOS 附件发布。Windows `.exe` 不能改名为 `.app`，也不能直接替代 Mac 打包过程。若希望兼容 Apple Silicon 与 Intel Mac，需分别验证目标架构，或按 Unreal 的通用二进制方式构建；优先在实际目标设备上测试画面和鼠标操作。向更多用户分发时，还应在 Mac 上处理 Apple 开发者签名与公证，减少系统阻止启动的情况。当前尚未制作或测试 macOS 包。

## 当前公开状态

源码已上传到 [AlternativeControll_Workshop](https://github.com/XinluLi-weeder/AlternativeControll_Workshop) 的 `main` 分支。Windows 试玩包已作为 [v0.1.0 Release 附件](https://github.com/XinluLi-weeder/AlternativeControll_Workshop/releases/tag/v0.1.0) 上传；本地打包目录不会随源码提交。真实 Arduino/编码器尚未经过实物测试；鼠标玩法可独立运行。Mac 版尚未构建。
