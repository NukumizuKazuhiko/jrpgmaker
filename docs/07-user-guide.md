# jrpgmaker 用户指南

> 当前支持平台为 Windows(D3D12) 与 Linux(Vulkan)。macOS 仅保留未来 Vulkan/MoltenVK 适配入口，不属于当前构建、运行或发布承诺。

## 快速构建

项目使用 CMake Presets 和 vcpkg manifest。所有命令都从仓库根目录执行，并要求：

- CMake 3.28 或更高版本、Ninja、Git；
- 已 clone 并 bootstrap 的 vcpkg，环境变量 `VCPKG_ROOT` 指向其实例；preset 会从 `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` 加载 toolchain；
- Windows 使用 x64 MSVC 开发者环境；
- Linux 使用 GCC，并安装 Vulkan/lavapipe、SDL3 所需窗口/输入/音频系统开发包和 vcpkg 构建工具。

Windows Release：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
# 先从“x64 Native Tools Command Prompt for VS”启动 pwsh，或先执行对应 VsDevCmd.bat
cmake --preset win-release
cmake --build --preset win-release --parallel 4
ctest --preset win-release --output-on-failure
```

Linux（Debian/Ubuntu 系）：

```bash
sudo apt-get update
sudo apt-get install -y build-essential ninja-build git libvulkan-dev mesa-vulkan-drivers \
  libx11-dev libxft-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libwayland-dev libxkbcommon-dev libegl1-mesa-dev libdbus-1-dev libudev-dev libasound2-dev \
  libibus-1.0-dev autoconf automake autopoint libtool libltdl-dev pkg-config \
  autoconf-archive python3-venv fonts-noto-cjk
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --output-on-failure
```

上述命令假定 vcpkg 已位于 `$HOME/vcpkg` 且已完成 bootstrap；如果位置不同，应修改 `VCPKG_ROOT`。WSL 没有 WSLg 时不能创建真实 SDL 窗口，因而不能验收 app/editor 的窗口与 swapchain，但可以运行 Vulkan/lavapipe 离屏测试。

## 创建和校验项目

构建不会把工具自动加入 `PATH`。以下示例以 Windows Release 输出为例；Linux Debug 将前缀替换为 `./build/linux-debug`，并移除 `.exe`：

```powershell
./build/win-release/tools/projecttool/jrpgmaker_projecttool.exe create <output-root> <template-root>
./build/win-release/tools/projecttool/jrpgmaker_projecttool.exe validate <project-root>
./build/win-release/tools/projecttool/jrpgmaker_projecttool.exe diagnose <project-root>
./build/win-release/tools/projecttool/jrpgmaker_projecttool.exe preview <project-root>
```

项目内容位于版本化 JSON 和资源文件中。修改日期、对话、触发点、地图、输入映射、材质实例或插件选择后，应先使用 eventlint 校验：

```powershell
./build/win-release/tools/eventlint/jrpgmaker_eventlint.exe --check-project <project.json> <project-root>

# 校验编辑器安装资源
./build/win-release/tools/editorlint/jrpgmaker_editorlint.exe <editor-resource-root>
```

## 启动与输入

不带参数时，宿主把当前仓库作为项目根并读取 `assets/data/project_demo.json`。要运行由 projecttool 创建的项目，应把项目根作为第一个参数传入：

```powershell
./build/win-release/app/jrpgmaker_app.exe
./build/win-release/app/jrpgmaker_app.exe <project-root>
```

编辑器同样接受可选的项目根参数：

```powershell
./build/win-release/tools/editor/jrpgmaker_editor.exe <project-root>
```

默认输入映射来自 `assets/data/input_actions.json`：W/A/S/D 移动，方向键上/下选择对话选项，E 确认交互、普通对话或当前选项，F5 保存，F9 读档。角色接近数据声明的交互点后会显示提示；只有确认键按下才会排队启动目标事件。按键与 action 的映射来自项目数据，可替换而无需修改 app。

## 发布运行包

发布包只应从已经完成构建的目录装配：

```powershell
pwsh ./tools/ci/package_release.ps1 `
  -BuildRoot ./build/win-release `
  -ProjectRoot . `
  -OutputRoot ./build/release/win
```

`OutputRoot` 必须不存在。脚本会复制宿主可执行文件、顶层运行库、assets、插件 manifest，并生成 `release-manifest.json`。构建中间文件不会进入运行包；发布前应在两个空目录各装配一次并比较 manifest。

脚本按每个插件 manifest 声明的全部 `data_roots` 装配私有数据，并保留 root 的包内相对路径。缺失、越界、重复、嵌套冲突、输出目录冲突或无法保持包内相对路径的 root 会使发布失败；不会补装未声明的目录。可用 `pwsh ./tools/ci/selftest_package_release.ps1` 运行发布装配行为自测。

插件 SDK 使用 `cmake --install` 生成的 CMake package，不随运行时发布包分发。当前插件模型是源码级、构建期注册，不支持跨编译器 DLL 热加载。

## 内容与插件边界

引擎拥有移动、碰撞、寻路、事件、对话、日期、存档和通用渲染合同。项目通过数据文件定义内容；战斗规则和渲染风格由插件定义。修改插件规则或风格时，不应把项目语义写入 `engine/domain` 或 RHI 后端。
