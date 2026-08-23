# 技术债务登记处

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是唯一债务登记处。每条债务必须包含：不处理原因、后续入口、状态。宪法要求："可记录债务……必须说明不处理原因和后续入口"；禁止无主债务。

| 编号 | 发现日 | 描述 | 分级 | 不处理原因 | 后续入口 | 状态 |
|---|---|---|---|---|---|---|
| DEBT-001 | 2026-08-23 | CI 日志出现 Node.js 20 deprecation warning：`actions/checkout@v4` 等以 Node20 为 target 的 action 被 runner 强制运行于 Node24 | 可记录债务 | runner 当前强制兼容、不影响正确性；升级 action 版本属于独立 chore，不应混入 P0 收尾提交 | P1 期间单独 chore commit 升级 `actions/checkout` 至最新主版本，复核 lukka actions（get-cmake/run-vcpkg/run-cmake）是否有新版 | 开放 |
| DEBT-002 | 2026-08-23 | `tools/ci/check_private_headers.ps1` 缺自动化自测 fixture：本轮修复后用手工构造的正反例 probe 验证，回归无保障 | 可记录债务 | P0 收尾时点手工验证证据充分（相对路径跨模块引用、`<mod/src/...>` 可疑模式两反例均正确拦截）；自动化 fixture 属测试基建增量 | P1 内顺手补 `tools/ci/selftest_private_headers.ps1`（内置正反例临时文件）并入 CI 私有头审计 job | 开放 |
| DEBT-003 | 2026-08-23 | `tests/unit/CMakeLists.txt` 以 `list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")` + `include(Catch)` 接入 Catch2 脚本模块，依赖上游安装目录布局 | 可记录债务 | 当前 vcpkg 锁定的 Catch2 版本下工作正常（win-debug/release 双配置 ctest 通过） | 下次升级 vcpkg builtin-baseline 时复核该路径假设是否仍成立 | 开放 |
| NOISE-001 | 2026-08-23 | 本机 `git add` 时出现 "LF will be replaced by CRLF" 提示 | 已接受噪音 | `.gitattributes` 已定义仓库内统一 LF 存储，提示仅为本机 autocrlf 工作区行为说明，仓库内容与 CI 不受影响 | 无需行动；避免后续会话误判为缺陷 | 已接受 |
| DEBT-004 | 2026-08-23 | D3D12 后端 v0 所有命令列表共享单一 command allocator：两个列表同时 recording 或 Submit 后未等 GPU 即 Begin 均属误用且仅有 HRESULT 级报错（合同已声明串行约束，代码无防护） | 设计风险 | 当前测试与用法严格串行；per-list/per-frame allocator 演进是主循环轮次的必然工作，届时一并消除该简化 | P1 主循环接线前演进 allocator 模型；01 §RHI v0 语义补充 "Begin 前须 GPU idle" 条款 | 开放 |
| DEBT-005 | 2026-08-23 | `D3D12CommandList` 与 `D3D12Device` 同住 d3d12_device.{h,cpp}；后续轮次加入资源管理后将膨胀失控 | 可记录债务 | 骨架期两文件共约 270 行尚可控；拆分动作本身零风险但单独成 commit 无收益 | 下一次 D3D12 功能轮次开工时先拆出 d3d12_command_list.{h,cpp} | 开放 |
| DEBT-006 | 2026-08-23 | D3D12 后端开了 debug layer 但未挂 ID3D12InfoQueue 错误回调与退出时 ReportLiveObjects，GPU 侧错误与对象泄漏不可见 | 可记录债务 | 骨架期无资源创建，泄漏面为零；挂钩代码在清屏/资源轮次才有真实价值 | 本轮清屏排查已用探针内 InfoQueue 取证，但后端仍未常驻挂钩；下轮（pipeline/资源）接入 InfoQueue + live-object 报告并入测试断言 | 开放 |
| DEBT-007 | 2026-08-23 | D3D12 `kRtvHeapCapacity=64` 硬编码上限，溢出即抛异常、调用方无从感知预算 | 设计风险 | 清屏用例每纹理 1 RTV 远低于上限；动态 RTV 池是资源轮次的自然工作 | P1 资源轮次引入动态 descriptor 池 + 预算上报 | 开放 |
| DEBT-008 | 2026-08-23 | Vulkan `MapReadBack` 行距硬编码 `width*4`（假定每像素 4 字节），D3D12 用 footprint.RowPitch | 可记录债务 | 当前仅 R8G8B8A8 单格式，BPP=4 恒成立；多格式引入前无需泛化 | 多格式支持轮次改为从 format 查 BPP 或经 vkGetImageSubresourceLayout | 开放 |
| DEBT-009 | 2026-08-23 | `ToNativeFormat` 双后端均映射 `kB8G8R8A8Unorm`，但当前无任何用例消费 B8G8R8A8 | 可记录债务 | swapchain 轮次才需要 B8G8R8A8；当前映射无成本且属合同格式集 | swapchain+SDL3 轮次用 B8G8R8A8 真实路径验收后关闭 | 开放 |
| DEBT-010 | 2026-08-23 | D3D12 与 Vulkan 的 EndRendering 后布局/状态语义不同（D3D12 回 COMMON、Vulkan 转 TRANSFER_SRC），合同层未声明"渲染后资源状态" | 设计风险 | 两后端各自内部自洽，合同语义"EndRendering 后资源可读回"成立；但未来统一状态 API 时需对齐 | 资源状态 API（显式 layout/state 合同化）轮次统一 | 开放 |
| DEBT-011 | 2026-08-23 | D3D12 `MapReadBack` 要求目标纹理带 `kRenderTarget`（检查 has_rtv），Vulkan 仅要求存在且 image 有 TRANSFER_SRC；纯 readback 纹理（无 RT）在 D3D12 下不可读回 | 可记录债务 | 当前用例恒为 RT|ReadBack，无纯 readback 消费者 | 引入纯 readback 纹理用例时移除 D3D12 has_rtv 限制并对齐两后端 | 开放 |

## 已关闭

| 编号 | 描述 | 关闭方式 |
|---|---|---|
| （P0 审计轮）审计脚本正则脆弱/GetFullPath 无保护 | 2026-08-23 commit `98cff8b` 重写加固并以双反例验收 |
| （P0 审计轮）ci.yml format job 空列表挂起风险 | 同上，加 `xargs -r` |
| （P0 审计轮）smoke_test 弱断言（仅比长度） | 同上，改为 semver 格式校验，ctest 2/2 通过 |
