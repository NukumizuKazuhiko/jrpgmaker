# 技术债务登记处

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是唯一债务登记处。每条债务必须包含：不处理原因、后续入口、状态。宪法要求："可记录债务……必须说明不处理原因和后续入口"；禁止无主债务。

| 编号 | 发现日 | 描述 | 分级 | 不处理原因 | 后续入口 | 状态 |
|---|---|---|---|---|---|---|
| DEBT-001 | 2026-08-23 | CI 日志出现 Node.js 20 deprecation warning：`actions/checkout@v4` 等以 Node20 为 target 的 action 被 runner 强制运行于 Node24 | 可记录债务 | runner 当前强制兼容、不影响正确性；升级 action 版本属于独立 chore，不应混入 P0 收尾提交 | P1 期间单独 chore commit 升级 `actions/checkout` 至最新主版本，复核 lukka actions（get-cmake/run-vcpkg/run-cmake）是否有新版 | 开放 |
| DEBT-002 | 2026-08-23 | `tools/ci/check_private_headers.ps1` 缺自动化自测 fixture：本轮修复后用手工构造的正反例 probe 验证，回归无保障 | 可记录债务 | P0 收尾时点手工验证证据充分（相对路径跨模块引用、`<mod/src/...>` 可疑模式两反例均正确拦截）；自动化 fixture 属测试基建增量 | P1 内顺手补 `tools/ci/selftest_private_headers.ps1`（内置正反例临时文件）并入 CI 私有头审计 job | 开放 |
| DEBT-003 | 2026-08-23 | `tests/unit/CMakeLists.txt` 以 `list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")` + `include(Catch)` 接入 Catch2 脚本模块，依赖上游安装目录布局 | 可记录债务 | 当前 vcpkg 锁定的 Catch2 版本下工作正常（win-debug/release 双配置 ctest 通过） | 下次升级 vcpkg builtin-baseline 时复核该路径假设是否仍成立 | 开放 |
| NOISE-001 | 2026-08-23 | 本机 `git add` 时出现 "LF will be replaced by CRLF" 提示 | 已接受噪音 | `.gitattributes` 已定义仓库内统一 LF 存储，提示仅为本机 autocrlf 工作区行为说明，仓库内容与 CI 不受影响 | 无需行动；避免后续会话误判为缺陷 | 已接受 |
| DEBT-004 | 2026-08-23 | D3D12 后端 v0 命令列表共享单一 command allocator：两个列表同时 recording 或 Submit 后未等 GPU 即 Begin 均属误用且仅有 HRESULT 级报错（合同已声明串行约束，代码无防护） | 设计风险 | 主渲染列表仍共享 allocator；`MapReadBack` 的 copy 列表已改独立 allocator（三角形轮次缓解 readback 竞态）；per-frame allocator 演进是主循环轮次的必然工作 | P1 主循环接线前演进 allocator 模型；01 §RHI v0 语义补充 "Begin 前须 GPU idle" 条款 | 部分缓解 |
| DEBT-005 | 2026-08-23 | `D3D12CommandList` 与 `D3D12Device` 同住 d3d12_device.{h,cpp}；后续轮次加入资源管理后将膨胀失控 | 可记录债务 | 骨架期两文件共约 270 行尚可控；拆分动作本身零风险但单独成 commit 无收益 | 下一次 D3D12 功能轮次开工时先拆出 d3d12_command_list.{h,cpp} | 开放 |
| DEBT-006 | 2026-08-23 | D3D12 后端开了 debug layer 但未挂 ID3D12InfoQueue 错误回调与退出时 ReportLiveObjects，GPU 侧错误与对象泄漏不可见 | 可记录债务 | 本轮已在 `WaitForGpuIdle` 后轮询 InfoQueue 并把 ERROR/CORRUPTION 提升为 `std::runtime_error`，并在 `Create` 后查 `GetDeviceRemovedReason`；GPU 错误已对测试可见 | ReportLiveObjects 常驻报告 + live-object 断言仍待资源轮次；InfoQueue 错误回调（异步）可后续替换轮询 | 部分关闭 |
| DEBT-007 | 2026-08-23 | D3D12 `kRtvHeapCapacity=64` 硬编码上限，溢出即抛异常、调用方无从感知预算 | 设计风险 | 清屏用例每纹理 1 RTV 远低于上限；动态 RTV 池是资源轮次的自然工作 | P1 资源轮次引入动态 descriptor 池 + 预算上报 | 开放 |
| DEBT-008 | 2026-08-23 | Vulkan `MapReadBack` 行距硬编码 `width*4`（假定每像素 4 字节），D3D12 用 footprint.RowPitch | 可记录债务 | 当前仅 R8G8B8A8 单格式，BPP=4 恒成立；多格式引入前无需泛化 | 多格式支持轮次改为从 format 查 BPP 或经 vkGetImageSubresourceLayout | 开放 |
| DEBT-009 | 2026-08-23 | `ToNativeFormat` 双后端均映射 `kB8G8R8A8Unorm`，但当前无任何用例消费 B8G8R8A8 | 可记录债务 | swapchain 轮次才需要 B8G8R8A8；当前映射无成本且属合同格式集 | swapchain+SDL3 轮次用 B8G8R8A8 真实路径验收后关闭 | 开放 |
| DEBT-010 | 2026-08-23 | D3D12 与 Vulkan 的 EndRendering 后布局/状态语义不同（D3D12 回 COMMON、Vulkan 转 TRANSFER_SRC），合同层未声明"渲染后资源状态" | 设计风险 | 两后端各自内部自洽，合同语义"EndRendering 后资源可读回"成立；但未来统一状态 API 时需对齐 | 资源状态 API（显式 layout/state 合同化）轮次统一 | 开放 |
| DEBT-011 | 2026-08-23 | D3D12 `MapReadBack` 要求目标纹理带 `kRenderTarget`（检查 has_rtv），Vulkan 仅要求存在且 image 有 TRANSFER_SRC；纯 readback 纹理（无 RT）在 D3D12 下不可读回 | 可记录债务 | 当前用例恒为 RT|ReadBack，无纯 readback 消费者 | 引入纯 readback 纹理用例时移除 D3D12 has_rtv 限制并对齐两后端 | 开放 |
| DEBT-012 | 2026-08-24 | shader 字节码提交入库（`shaders/generated/`）：字节码由 dxc 版本决定，跨 CI/开发机 dxc 版本漂移会改变字节码导致 shader-sync 门禁误报 | 设计风险 | vcpkg builtin-baseline 锁定 `directx-dxc` port 版本，CI 与开发机同 baseline 时字节码稳定；但手动安装其他 dxc 会漂移 | CI shader-sync job 已生成→diff 门禁；若出现漂移，记录并统一 dxc 获取方式 | 开放 |
| DEBT-013 | 2026-08-24 | Vulkan 负高度 viewport 依赖 Vulkan 1.1+ 核心特性（翻转 NDC Y 以统一双后端方向） | 可记录债务 | 目标平台 Vulkan 1.3（MoltenVK/lavapipe 均满足 1.1）；Vulkan 1.3 必支持负高度 viewport | 若未来支持更老 Vulkan 平台，需改用 `VK_KHR_maintenance1` 检查或 shader 层面翻转 | 开放 |
| DEBT-015 | 2026-08-24 | Vulkan 后端无 GPU 错误可见性：D3D12 已把 InfoQueue ERROR/CORRUPTION 提升为 `std::runtime_error`，Vulkan 无验证层或等价机制，GPU 侧错误静默吞掉 | 设计风险 | 当前 Vulkan 用例稳定（lavapipe/MoltenVK 下三角形/清屏测试通过），尚未暴露真实 GPU 错误；接入验证层属工具链增量 | P1 后续或 P2 接入 VK_LAYER_KHRONOS_validation（三平台 CI 可选启），错误回调/日志与 D3D12 对齐 | 开放 |
| DEBT-016 | 2026-08-24 | NDC Y 翻转约定（Vulkan 负高度 viewport）零验证：triangle_test 的 9 个采样点关于中线对称，即使去掉 Vulkan 翻转测试仍通过，Y 方向约定无判别力 | 设计风险 | 当前双后端三角形位置已实测一致（PRECISE 网格逐格吻合），但测试不锁定该行为，未来误删翻转不会被发现 | golden 全帧比对轮次用不对称几何（如顶点非对称三角形）或 Y 方向判别采样点锁定约定 | 开放 |
| DEBT-017 | 2026-08-24 | triangle golden 未达 P1 验收字面（docs/02 "golden image 比对通过（截图+测试日志）"）：无 golden 参考图、无全帧比对、无截图产物，仅 9 采样点断言蓝通道 | 可记录债务 | P1 剩余 golden 流水线轮次将闭环全帧比对与截图；采样点断言是当前阶段可复现的最小 golden | P1 剩余 golden 流水线轮次：生成基准图、全帧逐像素比对、截图产物入库 | 开放 |
| DEBT-018 | 2026-08-24 | `compile_shaders.ps1` dxc 查找含 20+ 候选路径（含未验证的 `x64-windows\x64-windows` 双 triplet 猜测），维护负担与误判面大 | 可记录债务 | CI shader-sync 与本地开发实际命中已验证路径（VCPKG_INSTALLED_DIR/BUILD_DIR）；候选列表兜底但臃肿 | golden 流水线轮次收敛 dxc 查找为单一受控路径（CMake 导出 `DIRECTX_DXC_TOOL` 或统一脚本） | 开放 |
| DEBT-019 | 2026-08-24 | shader entry 名（`vs_main`/`ps_main`）与 profile 表在 `triangle.hlsl`、`vulkan_device.cpp`、`compile_shaders.ps1` 三处重复，改名需改三处 | 可记录债务 | 当前单 shader 用例；entry 名是 ADR-003 约定的一部分 | 多 shader 引入时抽公共约定（如固定 `<name>_vs/_ps` 命名规则）至 docs/01 | 开放 |
| DEBT-020 | 2026-08-24 | Vulkan swapchain surface 路径无 CI/本机可复现实机验证：SDL_Vulkan_CreateSurface 需真实窗口（WSL 无 WSLg、CI 无显示均不可建），仅离屏+错误路径（CreateSwapchain(nullptr) 抛异常）被测试覆盖 | 可记录债务 | swapchain 本属 app 主循环专有（docs 已注明不进 CI）；Linux 实机验证需带显示环境 | 接入真实 Linux 桌面（或有显示 CI runner）时补 swapchain 实机验收证据；P1 剩余 golden 流水线维持离屏 | 开放 |
| DEBT-021 | 2026-08-24 | Vulkan swapchain 复用 graphics 队列族进行 present（`vkGetPhysicalDeviceSurfaceSupportKHR` 仅运行时校验 graphics 队列可 present），未独立选择 present 专用队列族 | 可记录债务 | 目标平台（MoltenVK/lavapipe/主流桌面驱动）graphics 队列族恒可 present；独立 present 队列是异质平台（如部分移动 SoC）才需要 | 引入独立 present 队列支持的平台时，device 创建改为按 surface 支持选择队列族并创建第二队列 | 开放 |
| DEBT-022 | 2026-08-24 | swapchain back buffer 图像布局状态语义未合同化：D3D12 EndRendering 回 COMMON、Vulkan EndRendering 转 TRANSFER_SRC；Present 前 swapchain image 应处 PRESENT_SRC/COMMON，当前 Vulkan 无呈现前显式布局转换 | 设计风险 | 当前 app 实机（D3D12）工作正常；Vulkan surface 未实机验证故布局问题未暴露；真机若校验报错会暴露 | Vulkan 实机验收轮次：EndRendering 对 swapchain image 增加 COMMON/PRESENT 转换或合同层约定"present 目标渲染后回可呈现状态" | 开放 |
| DEBT-023 | 2026-08-24 | app 主循环把 SDL3 接线直接放 app（未建 `engine/platform` 模块），与 docs/01 目录结构"platform(adapter) 负责 SDL3 窗口/输入接线"不一致 | 可记录债务 | P1 为最小装配验证，SDL 直接接线可跑通；platform 模块空置 | P2 起把窗口/输入/文件抽象迁移到 `engine/platform`，app 只做装配 | 开放 |
| DEBT-024 | 2026-08-24 | `SystemRegistration.stage` 是死字段：`RegisterSystem(stage, {stage, order}, cb)` 强制调用方重复传 stage，实现只按参数索引、只读 order，不校验 `registration.stage == stage` | 可记录债务 | 冗余字段 + 调用冗余；v0 阶段语义简单未暴露问题 | Stage 合同成熟（before/after 图落地）时删除字段或改为仅 order 参数 | 开放 |
| DEBT-025 | 2026-08-24 | D3D12 `RegisterSwapchainBuffer` 的 width/height/format 参数全部 `(void)` 未使用，调用方硬编码 `kB8G8R8A8Unorm` | 可记录债务 | swapchain 创建路径当前固定 B8G8R8A8；参数预留但未消费 | swapchain 支持多格式时消费参数，或移除参数 | 开放 |
| DEBT-026 | 2026-08-24 | `StageRunner::Tick` 每帧对全表 `std::sort`；注册时即可维护有序性 | 可记录债务 | 系统数小（v0 空占位），每帧排序成本可忽略 | Stage 成熟时改为注册期排序或按序插入 | 开放 |
| DEBT-027 | 2026-08-24 | `EnabledInstanceExtensions` 对必需扩展（`VK_KHR_surface` 等）不支持时静默跳过而非硬失败，问题推迟到 CreateSwapchain 才暴露 | 可记录债务 | 离屏测试环境（lavapipe）可能缺 surface 扩展但无需 swapchain；静默跳过让离屏可用 | CreateSwapchain 已对 swapchain_supported_ 检查；若需更早失败可在 instance 创建时校验 surface 必需扩展 | 开放 |
| DEBT-028 | 2026-08-24 | `vkAcquireNextImageKHR` 用 `VK_NULL_HANDLE` semaphore/fence：单线程+FIFO present 可用但非规范用法，无帧内同步信号量 | 可记录债务 | 当前单命令列表顺序执行、Present 前有 WaitForGpuIdle 间接同步；未暴露竞争 | 多帧 in-flight 或双缓冲流水线落地时引入 acquire semaphore + present wait semaphore | 开放 |

## 已关闭

| 编号 | 描述 | 关闭方式 |
|---|---|---|
| （P0 审计轮）审计脚本正则脆弱/GetFullPath 无保护 | 2026-08-23 commit `98cff8b` 重写加固并以双反例验收 |
| （P0 审计轮）ci.yml format job 空列表挂起风险 | 同上，加 `xargs -r` |
| （P0 审计轮）smoke_test 弱断言（仅比长度） | 同上，改为 semver 格式校验，ctest 2/2 通过 |
| DEBT-014 | 2026-08-24 显式设 `D3D12_COLOR_WRITE_ENABLE_ALL`（D3D12_BLEND_DESC 零初始化使 RenderTargetWriteMask=0 导致三角形颜色不写） | 已设 write mask 修复；未来引入混合时扩展完整 BlendState |
