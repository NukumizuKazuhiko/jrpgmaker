# 技术债务登记处

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是唯一债务登记处。每条债务必须包含：不处理原因、后续入口、状态。宪法要求："可记录债务……必须说明不处理原因和后续入口"；禁止无主债务。

| 编号 | 发现日 | 描述 | 分级 | 不处理原因 | 后续入口 | 状态 |
|---|---|---|---|---|---|---|
| DEBT-001 | 2026-08-23 | CI 日志出现 Node.js 20 deprecation warning：`actions/checkout@v4` 等以 Node20 为 target 的 action 被 runner 强制运行于 Node24 | 可记录债务 | runner 当前强制兼容、不影响正确性；升级 action 版本属于独立 chore，不应混入 P0 收尾提交 | P1 期间单独 chore commit 升级 `actions/checkout` 至最新主版本，复核 lukka actions（get-cmake/run-vcpkg/run-cmake）是否有新版 | 开放 |
| DEBT-002 | 2026-08-23 | `tools/ci/check_private_headers.ps1` 缺自动化自测 fixture：本轮修复后用手工构造的正反例 probe 验证，回归无保障 | 可记录债务 | P0 收尾时点手工验证证据充分（相对路径跨模块引用、`<mod/src/...>` 可疑模式两反例均正确拦截）；自动化 fixture 属测试基建增量 | P1 内顺手补 `tools/ci/selftest_private_headers.ps1`（内置正反例临时文件）并入 CI 私有头审计 job | 开放 |
| DEBT-003 | 2026-08-23 | `tests/unit/CMakeLists.txt` 以 `list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")` + `include(Catch)` 接入 Catch2 脚本模块，依赖上游安装目录布局 | 可记录债务 | 当前 vcpkg 锁定的 Catch2 版本下工作正常（win-debug/release 双配置 ctest 通过） | 下次升级 vcpkg builtin-baseline 时复核该路径假设是否仍成立 | 开放 |
| NOISE-001 | 2026-08-23 | 本机 `git add` 时出现 "LF will be replaced by CRLF" 提示 | 已接受噪音 | `.gitattributes` 已定义仓库内统一 LF 存储，提示仅为本机 autocrlf 工作区行为说明，仓库内容与 CI 不受影响 | 无需行动；避免后续会话误判为缺陷 | 已接受 |

## 已关闭

| 编号 | 描述 | 关闭方式 |
|---|---|---|
| （P0 审计轮）审计脚本正则脆弱/GetFullPath 无保护 | 2026-08-23 commit `98cff8b` 重写加固并以双反例验收 |
| （P0 审计轮）ci.yml format job 空列表挂起风险 | 同上，加 `xargs -r` |
| （P0 审计轮）smoke_test 弱断言（仅比长度） | 同上，改为 semver 格式校验，ctest 2/2 通过 |
