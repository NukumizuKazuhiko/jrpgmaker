# jrpgmaker 用户指南

## 快速构建

项目使用 CMake Presets 和 vcpkg manifest。Windows 构建需要 x64 MSVC 开发者环境，Linux 构建需要 GCC、Ninja、Vulkan headers 和 lavapipe。

Windows Release：

```powershell
cmake --preset win-release
cmake --build --preset win-release --parallel 4
ctest --preset win-release --output-on-failure
```

Linux：

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --output-on-failure
```

WSL 没有 WSLg 时不能创建真实 SDL 窗口，但可以运行 Vulkan/lavapipe 的离屏测试和 golden 测试。

## 创建和校验项目

使用 projecttool 从当前参考项目模板创建工作区：

```text
jrpgmaker_projecttool create <output-root> <template-root>
jrpgmaker_projecttool validate <project-root>
jrpgmaker_projecttool diagnose <project-root>
jrpgmaker_projecttool preview <project-root>
```

项目内容位于版本化 JSON 和资源文件中。修改日期、对话、触发点、地图、输入映射、材质实例或插件选择后，应先使用 eventlint 校验：

```text
jrpgmaker_eventlint --check-project <project.json> <project-root>
```

## 启动与输入

从项目根目录启动宿主应用：

```text
jrpgmaker_app
```

默认输入映射来自 `assets/data/input_actions.json`：W/A/S/D 移动，E 确认交互或对话，F5 保存，F9 读档。角色接近数据声明的交互点后会显示提示；只有确认键按下才会排队启动目标事件。

## 发布运行包

发布包只应从已经完成构建的目录装配：

```powershell
pwsh ./tools/ci/package_release.ps1 `
  -BuildRoot ./build/win-release `
  -ProjectRoot . `
  -OutputRoot ./build/release/win
```

`OutputRoot` 必须不存在。脚本会复制宿主可执行文件、顶层运行库、assets、插件 manifest 和插件私有数据，并生成 `release-manifest.json`。构建中间文件不会进入运行包；发布前应在两个空目录各装配一次并比较 manifest。

插件 SDK 使用 `cmake --install` 生成的 CMake package，不随运行时发布包分发。当前插件模型是源码级、构建期注册，不支持跨编译器 DLL 热加载。

## 内容与插件边界

引擎拥有移动、碰撞、寻路、事件、对话、日期、存档和通用渲染合同。项目通过数据文件定义内容；战斗规则和渲染风格由插件定义。修改插件规则或风格时，不应把项目语义写入 `engine/domain` 或 RHI 后端。
