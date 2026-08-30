# P11 插件 SDK 与发布合同

本文是当前源码级插件 SDK 的公开入口。插件是构建期注册的 C++20 目标，不提供跨编译器 DLL ABI、热加载或沙箱脚本接口。

权威规则按以下顺序读取：

1. [插件系统规范](11-plugin-system.md)：身份、类型、生命周期、数据、错误、能力、安全边界和编辑器扩展。
2. 本文：最小工程、SDK 消费方式和验收命令。
3. [插件发布与排障](06-plugin-release.md)：兼容矩阵、装配内容和运行问题定位。

源码、公开头和本文冲突时以当前源码为事实证据，并同步修正文档；插件不得只依赖参考实现中的私有约定。

## 最小插件

`templates/plugin_minimal/` 是可复制的最小工程。插件只应依赖 `jrpgmaker/plugin/plugin.hpp`（战斗插件再依赖 `battle.hpp`），由宿主工程把工厂注册到 `PluginRegistry`。插件不能 include SDL、D3D12、Vulkan 或 `engine/domain` 的私有实现。

安装后的 SDK 提供 `jrpgmakerConfig.cmake`，外部 CMake 项目可使用
`find_package(jrpgmaker CONFIG REQUIRED)` 和 `jrpgmaker::plugin`；
`tests/fixtures/sdk_consumer` 是该消费路径的最小编译烟测。

插件 manifest 必须包含：`schema`、`id`、`type`、`version`、`engine_contract`、`data_roots`、`capabilities`。当前值为 `schema=1`、`engine_contract=1`；后者对应公开常量 `jrpgmaker::plugin::kPluginEngineContract`。注册前和解析期都会拒绝不兼容合同。

## 数据与错误边界

插件私有数据由 `data_roots` 声明，validator 只能通过 `PluginValidationContext::read_file` 读取安全相对路径。宿主对每次校验限制为最多 32 个文件、单文件 256 KiB、总计 1 MiB；读取失败、validator 异常和结构化 issue 都会转换为 `PluginError`，不会让宿主静默继续。

插件输出仍受公开合同约束：战斗输入最多 32 个 action、presentation command 最多 128 个、presentation payload 总计最多 64 KiB。资源和渲染风格插件应使用对应 service 的容量预算，不能直接操作后端句柄。

宿主通过 `CreateBattleSession` 与 `AdvanceBattleSession` 调用战斗插件；这两个 owner wrapper 会统一执行输入/输出合同校验，并把插件异常或缺少错误的失败结果转换为 `PluginError`。渲染计划执行器同样隔离 adapter/resolver 异常，并在失败时结束当前 rendering pass。

## 兼容矩阵

| SDK 合同 | `schema` | `engine_contract` | 兼容状态 |
|---|---:|---:|---|
| P11 当前源码 SDK | 1 | 1 | 支持 |
| 未知 schema | 非 1 | 任意 | 拒绝 |
| 未知 engine contract | 1 | 非 1 | 拒绝并报告 `manifest.contract` / `registry.contract` |

升级 engine contract 时应先增加兼容性测试和迁移说明，再更新插件 manifest；不保留未经验证的双合同运行路径。

## 注册与替换

项目只保存插件 id。渲染风格和战斗规则分别通过 `CreateProjectRenderStyle` 与 `CreateProjectBattlePlugin` 创建，因此替换实现不需要改 domain 业务分支或 RHI 后端。样例注册器仅用于测试和演示；生产宿主应生成或维护自己的注册函数。

插件 id、capability、错误 code、presentation command id 和 result key 的命名与兼容规则见 [插件系统规范](11-plugin-system.md)。编辑器扩展不是运行时插件类型：它通过相邻的 `plugin.editor.json` 和可选 editor SDK Adapter 声明，删除后不得影响项目 lint、app 或发布包。

## 验收

```text
cmake --build --preset win-debug
ctest --preset win-debug
pwsh ./tools/ci/check_private_headers.ps1
cmake --install build/win-release --prefix <sdk-prefix>
cmake -S tests/fixtures/sdk_consumer -B <consumer-build> -DCMAKE_PREFIX_PATH=<sdk-prefix> -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=<triplet>
cmake --build <consumer-build>
```

最小插件的 manifest 解析、注册、创建、合同拒绝和 validator 边界必须在宿主测试中覆盖。发布包必须来自干净构建目录，并保留宿主二进制、插件 manifest、manifest 声明的全部插件私有数据和构建合同版本。

发布装配命令按平台选择构建目录（当前插件是源码级静态链接，包内交付宿主二进制、插件 manifest 和插件私有数据）：

```powershell
pwsh ./tools/ci/package_release.ps1 -BuildRoot ./build/win-release -ProjectRoot . -OutputRoot ./build/release/win
# 当前支持的 Linux 使用 ./build/linux-release；macOS preset 仅供未来适配环境使用
```

当前脚本尚未完整实现上述 `data_roots` 合同：它会校验声明是安全相对路径，却只复制每个 `plugin.json` 同目录下固定名称的 `data/`。因此在 DEBT-040 关闭前，只有全部运行时私有数据都放在该目录的插件可使用此命令生成完整发布包；其他合法 root 即使通过 manifest 校验也可能被静默漏装，不能据此发布。

脚本拒绝覆盖已有输出目录，并对文件数（4096）和总大小（512 MiB）设上界；`release-manifest.json` 按相对路径排序记录每个文件的大小与 SHA-256。发布前应在两个空输出目录运行两次并比较 manifest，确保装配确定性。
