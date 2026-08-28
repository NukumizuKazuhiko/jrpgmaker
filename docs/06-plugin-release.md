# P11 插件发布与排障

## 兼容性矩阵

| 项目 | 当前合同 | 失败行为 |
|---|---:|---|
| 插件 manifest schema | `1` | 解析期返回 `manifest.schema` |
| engine contract | `1` | 解析或注册期返回 `manifest.contract` / `registry.contract` |
| 插件注册数量 | `32` | 注册返回 `registry.capacity` |
| validator 文件数 | `32` | 读取返回 `plugin.validator.file_budget` |
| validator 单文件 | `256 KiB` | 读取返回 `plugin.validator.file_size` |
| validator 总数据 | `1 MiB` | 读取返回 `plugin.validator.byte_budget` |
| battle presentation command | `128` | 输出校验失败 |
| battle presentation payload | `64 KiB` | 输出校验失败 |

战斗宿主调用必须经过 `CreateBattleSession` / `AdvanceBattleSession` wrapper；插件异常和无错误失败结果均转换为结构化错误，不得直接跨越宿主边界。

合同升级必须同步公开头文件、manifest 示例、兼容矩阵和回归测试。当前不提供跨编译器 DLL ABI、热加载或从 manifest 动态执行任意二进制；插件由宿主在构建期显式注册。

## 发布包

发布包由 `tools/ci/package_release.ps1` 生成，输入一个已完成的构建目录和项目根目录。它只复制宿主可执行文件及其顶层运行库（`.dll`/`.so`/`.dylib`），不会把 `CMakeFiles`、对象文件或 CMake 元数据装入运行包；同时复制项目 assets、插件 manifest 和插件私有 `data`，并生成 `release-manifest.json`。装配前会严格检查 manifest 的 schema、id、type、version、engine contract、data roots 和 capabilities，且要求 contract 等于当前 SDK contract。清单按相对路径排序并记录文件大小与 SHA-256；文件数和总大小均有上界，已有输出目录会被拒绝以避免覆盖。

开发者 SDK 不混入运行时发布包；使用构建目录执行 `cmake --install` 可得到 `jrpgmakerConfig.cmake`、公共头和 `jrpgmaker::plugin` target，外部插件工程通过 `find_package(jrpgmaker CONFIG REQUIRED)` 消费。

Windows、Linux、macOS 应分别使用对应的 release 构建目录，并对两个空输出目录生成的 `release-manifest.json` 做字节或 SHA-256 比较。CI 的六平台 build-test 矩阵会执行该检查。

## 常见排障

- `registry.contract`：检查插件 manifest 的 `engine_contract` 是否等于公开头文件中的 `kPluginEngineContract`。
- `plugin.validator.path`：检查请求路径是否为安全相对路径，并确认没有通过符号链接越出声明的 `data_roots`。
- `plugin.validator.file_budget` 或 `byte_budget`：拆分 validator 数据读取，不能通过增加宿主上限绕过合同。
- 发布脚本找不到应用：确认 `BuildRoot/app` 下存在当前平台的 `jrpgmaker_app(.exe)`，并使用完成构建的 release 目录。
- 清单两次生成不一致：检查构建目录是否在装配期间被其他进程写入；清理后从同一 commit 重新构建。
