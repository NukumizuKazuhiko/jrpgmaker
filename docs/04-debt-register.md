# 技术债务登记处

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是唯一债务登记处。每条未关闭债务必须包含不处理原因、后续入口、状态，并由下方 owner 表明确归属；禁止无主债务。

## 登记规则与 owner

- 主登记表按编号段保留历史顺序，不因状态变化搬动记录；判断当前债务时以“状态”列为准。
- `开放`、`部分缓解`、`已缓解`、`部分关闭` 属未关闭；`已关闭` 不再进入当前工作队列；`已接受` 仅用于已确认无需行动的环境噪音。
- 历史审计问题与短记录使用独立关闭表，避免与含“不处理原因/后续入口”的主表混用列结构。

| Owner | 未关闭记录 |
|---|---|
| `engine/rhi` | DEBT-004–011、DEBT-013、DEBT-015、DEBT-020–022、DEBT-025、DEBT-027–028、DEBT-030 |
| `tools/ci` | DEBT-003、DEBT-012、DEBT-018–019、DEBT-034 |
| `engine/platform` + `app` | DEBT-023 |
| `engine/core` | DEBT-024、DEBT-026、DEBT-031 |
| `engine/render` | DEBT-035 |
| `engine/plugin` | DEBT-036 |
| `engine/ui` + `engine/render` + `app` | DEBT-037 |
| `engine/ui` + `engine/render` + `tools/editor` | DEBT-038 |
| `tools/ci` + `engine/plugin` | DEBT-040 |
| 根构建/发布元数据 | DEBT-041 |
| 根 CMake + `tools/editor` + tests | DEBT-042 |
| `engine/ui` + tests + `tools/ci` | DEBT-043 |
| `.github/workflows/ci.yml` + 平台文档 | DEBT-044 |
| 文档/本机环境 | NOISE-001 |

## 未解决问题摘要（2026-08-30）

本节只提供当前处理顺序，不替代下方各债务行的证据、边界和关闭条件。状态为“开放”“部分缓解”“部分关闭”或“已缓解但仍有残余条件”的记录均视为未完全关闭。

| 优先级 | 未解决编号 | 当前影响 | 进入下一状态前的停止条件 |
|---|---|---|---|
| P1：产品与工具链治理 | DEBT-041、DEBT-042、DEBT-044 | 包元数据残留已否定画风；editor 不是构建可选项；macOS 非产品支持但 CI 仍硬阻断 | 分别清理中性描述、验证 editor-off 核心构建测试、明确 macOS CI 为非阻断信号或正式工程门禁并同步 ADR |
| P1：图形后端正确性与可见性 | DEBT-004、DEBT-007、DEBT-010、DEBT-015、DEBT-020–022、DEBT-027–028 | allocator 串行前置条件、descriptor 容量、资源状态、Vulkan 验证与真实 swapchain/present 同步仍缺完整合同或实机证据 | 在引入多帧/多窗口/真实 Linux surface 前完成对应合同、验证层、同步与实机测试；不得用离屏结果代替呈现证据 |
| P2：结构与维护性 | DEBT-005、DEBT-018–019、DEBT-023–026、DEBT-038 | 后端大文件、shader 工具约定、空 platform owner、Stage 冗余/排序和 editor z-order/字体预热增加维护风险 | 只在真实消费者或 profiling 触发时按各 owner 入口处理，并保持现有合同测试与文档同步 |
| P2：受限能力与未来扩展 | DEBT-003、DEBT-006、DEBT-008、DEBT-011–013、DEBT-030–031、DEBT-034–036 | 依赖布局、诊断、格式假设、多对象动画/资源策略、CI 外部波动及源码级插件边界仍有明确限制 | 触发对应升级、格式、规模、发布或 ABI 需求时按详细债务行验收；当前不得把限制描述成已支持能力 |

当前登记的阻断问题共 0 项；P12 仍需真实连续 30 分钟试玩和发布闭环证据，P11 仍需完成其余发布门禁证据。

## 早期主登记表（DEBT-001–029）

| 编号 | 发现日 | 描述 | 分级 | 不处理原因 | 后续入口 | 状态 |
|---|---|---|---|---|---|---|
| DEBT-001 | 2026-08-23 | CI 日志出现 Node.js 20 deprecation warning：`actions/checkout@v4` 等以 Node20 为 target 的 action 被 runner 强制运行于 Node24 | 已关闭 | — | 2026-08-24 升级 `actions/checkout` v4→v5（node24）、`actions/upload-artifact` v4→v6（node24）；lukka actions 核对其 major tag 指向最新（get-cmake@latest、run-vcpkg@v11 含 v11.6、run-cmake@v10 含 v10.9）。CI 全绿后确认 warning 消失 | 已关闭 |
| DEBT-002 | 2026-08-23 | `tools/ci/check_private_headers.ps1` 缺自动化自测 fixture：本轮修复后用手工构造的正反例 probe 验证，回归无保障；且存在 `Write-Error`+`$ErrorActionPreference='Stop'` 组合缺陷（ForEach 首条 Write-Error 即抛终止错误，多泄漏只报第一条、`exit 1` 永不执行） | 已关闭 | — | 2026-08-24 落地 `tools/ci/selftest_private_headers.ps1`：临时 fixture 树覆盖五用例（同模块私有头允许、跨模块私有头拒绝、`<suspicious-src-path>` 拒绝、engine 外消费拒绝、公共头允许），子进程调用隔离终止错误并断言 exit 1 + 诊断行内容；check 脚本改为聚合诊断并显式 `exit 1`；CI private-headers job 纳入 selftest | 已关闭 |
| DEBT-003 | 2026-08-23 | `tests/unit/CMakeLists.txt` 以 `list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")` + `include(Catch)` 接入 Catch2 脚本模块，依赖上游安装目录布局 | 可记录债务 | 当前 vcpkg 锁定的 Catch2 版本下工作正常（win-debug/release 双配置 ctest 通过） | 下次升级 vcpkg builtin-baseline 时复核该路径假设是否仍成立 | 开放 |
| NOISE-001 | 2026-08-23 | 本机 `git add` 时出现 "LF will be replaced by CRLF" 提示 | 已接受噪音 | `.gitattributes` 已定义仓库内统一 LF 存储，提示仅为本机 autocrlf 工作区行为说明，仓库内容与 CI 不受影响 | 无需行动；避免后续会话误判为缺陷 | 已接受 |
| DEBT-004 | 2026-08-23 | D3D12 主渲染命令列表仍共享 device 级 command allocator：两个列表同时 recording，或 Submit 后未等 GPU 即 Begin，均属调用误用且没有结构化防护 | 设计风险 | readback/upload copy 列表已使用独立 allocator，主路径也在资源销毁前等待 GPU；但共享 allocator 的串行前置条件仍由调用方维护 | 引入多帧 in-flight、并行录制或多个主列表前，改为 per-frame/per-list allocator 并增加误用测试；当前文档继续明确串行约束 | 部分缓解 |
| DEBT-005 | 2026-08-23 | `D3D12CommandList` 与 `D3D12Device` 仍同住 `d3d12_device.{h,cpp}`；资源、descriptor、pipeline、上传和 readback 增长后，当前 cpp 已约 1180 行 | 设计风险 | swapchain 已拆为独立文件，但 command list/device 仍耦合；机械拆文件不改变行为，却会影响高风险后端的审阅定位 | 下一次实际修改 D3D12 command-list 生命周期或绑定状态时，沿现有 private header 边界拆出 command-list 实现，并以全量 D3D12 合同/golden 阻断行为漂移 | 开放 |
| DEBT-006 | 2026-08-23 | D3D12 后端开了 debug layer 但未挂 ID3D12InfoQueue 错误回调与退出时 ReportLiveObjects，GPU 侧错误与对象泄漏不可见 | 可记录债务 | 本轮已在 `WaitForGpuIdle` 后轮询 InfoQueue 并把 ERROR/CORRUPTION 提升为 `std::runtime_error`，并在 `Create` 后查 `GetDeviceRemovedReason`；GPU 错误已对测试可见 | ReportLiveObjects 常驻报告 + live-object 断言仍待资源轮次；InfoQueue 错误回调（异步）可后续替换轮询 | 部分关闭 |
| DEBT-007 | 2026-08-23 | D3D12 `kRtvHeapCapacity=64` 仍是硬编码上限，溢出只在创建纹理时抛异常，调用方无法查询剩余 descriptor 预算 | 设计风险 | 当前 app/editor 和离屏测试规模未达到上限，固定容量尚未造成实际失败 | 引入多 viewport、大量 render target 或资源预算诊断时，提供可查询容量或动态 descriptor 池，并覆盖耗尽/回收测试 | 开放 |
| DEBT-008 | 2026-08-23 | Vulkan `MapReadBack` 行距硬编码 `width*4`（假定每像素 4 字节），D3D12 用 footprint.RowPitch | 可记录债务 | 当前仅 R8G8B8A8 单格式，BPP=4 恒成立；多格式引入前无需泛化 | 多格式支持轮次改为从 format 查 BPP 或经 vkGetImageSubresourceLayout | 开放 |
| DEBT-009 | 2026-08-23 | `ToNativeFormat` 双后端映射 `kB8G8R8A8Unorm` 曾无真实消费者 | 已关闭 | app 与 editor 的 swapchain、场景/UI/文本 pipeline 现均使用 B8G8R8A8；Windows/D3D12 已有实机窗口证据，Vulkan 后端保留相同格式映射 | 后续 Vulkan 真实桌面 swapchain 风险由 DEBT-020/022 跟踪，不再把格式“无人消费”保持为开放债务 | 已关闭 |
| DEBT-010 | 2026-08-23 | D3D12 与 Vulkan 的 EndRendering 后布局/状态语义不同（D3D12 回 COMMON、Vulkan 转 TRANSFER_SRC），合同层未声明"渲染后资源状态" | 设计风险 | 两后端各自内部自洽，合同语义"EndRendering 后资源可读回"成立；但未来统一状态 API 时需对齐 | 资源状态 API（显式 layout/state 合同化）轮次统一 | 开放 |
| DEBT-011 | 2026-08-23 | D3D12 `MapReadBack` 要求目标纹理带 `kRenderTarget`（检查 has_rtv），Vulkan 仅要求存在且 image 有 TRANSFER_SRC；纯 readback 纹理（无 RT）在 D3D12 下不可读回 | 可记录债务 | 当前用例恒为 `RT\|ReadBack`，无纯 readback 消费者 | 引入纯 readback 纹理用例时移除 D3D12 has_rtv 限制并对齐两后端 | 开放 |
| DEBT-012 | 2026-08-24 | shader 字节码提交入库（`shaders/generated/`）：字节码由 dxc 版本决定，跨 CI/开发机 dxc 版本漂移会改变字节码导致 shader-sync 门禁误报 | 设计风险 | vcpkg builtin-baseline 锁定 `directx-dxc` port 版本，CI 与开发机同 baseline 时字节码稳定；但手动安装其他 dxc 会漂移 | **2026-08-25 实锤并收口**：同一 vcpkg baseline 下 Windows/Linux port 的 dxc 二进制版本不同（win `1.9.2602.24` vs linux `1.9.0.5191`），Windows dxc 生成的 DXIL 在 CI Linux dxc 重编译必漂移（+16 字节版本戳，语义不变）。**修复**：字节码改为 **Linux dxc 权威生成**（shader-sync 是 Linux job），提交后与 CI 自洽；Windows 用新字节码渲染 golden 双端 delta=0 证明语义不变。**残余**：未来升级 vcpkg baseline 改 dxc 版本时须用 Linux dxc 重生成字节码 | 已缓解 |
| DEBT-013 | 2026-08-24 | Vulkan 负高度 viewport 依赖 Vulkan 1.1+ 核心特性（翻转 NDC Y 以统一双后端方向） | 可记录债务 | 当前 Linux/Vulkan 1.3 满足要求；未来 macOS/MoltenVK 适配也必须满足相同合同 | 若未来支持更老 Vulkan 平台，需改用 `VK_KHR_maintenance1` 检查或 shader 层面翻转 | 开放 |
| DEBT-015 | 2026-08-24 | Vulkan 后端无 GPU 错误可见性：D3D12 已把 InfoQueue ERROR/CORRUPTION 提升为 `std::runtime_error`，Vulkan 无验证层或等价机制，GPU 侧错误静默吞掉 | 设计风险 | 当前 Linux Vulkan 用例稳定，尚未暴露真实 GPU 错误；接入验证层属工具链增量 | 在 Windows/Linux 门禁接入 `VK_LAYER_KHRONOS_validation` 可选启用；未来 macOS 适配复用同一结构化错误合同 | 开放 |
| DEBT-018 | 2026-08-24 | `compile_shaders.ps1` dxc 查找含 20+ 候选路径（含未验证的 `x64-windows\x64-windows` 双 triplet 猜测），维护负担与误判面大 | 可记录债务 | CI shader-sync 与本地开发实际命中已验证路径（VCPKG_INSTALLED_DIR/BUILD_DIR）；候选列表兜底但臃肿 | golden 流水线轮次收敛 dxc 查找为单一受控路径（CMake 导出 `DIRECTX_DXC_TOOL` 或统一脚本） | 开放 |
| DEBT-019 | 2026-08-24 | shader entry 名 `vs_main`/`ps_main` 同时固化在全部 HLSL、Vulkan pipeline 创建和 `compile_shaders.ps1` profile 表中，改名必须跨三处合同同步 | 可记录债务 | 当前七个 shader 均遵循统一固定入口，重复尚未导致漂移；入口名也是 ADR-003 单源双目标约定的一部分 | 若需要每文件自定义 entry，先建立结构化 shader manifest 供生成器与 runtime 共同消费；否则维持固定入口并在文档中明确，不做字符串级抽象 | 开放 |
| DEBT-020 | 2026-08-24 | Vulkan swapchain surface 路径无 CI/本机可复现实机验证：SDL_Vulkan_CreateSurface 需真实窗口（WSL 无 WSLg、CI 无显示均不可建），仅离屏+错误路径（CreateSwapchain(nullptr) 抛异常）被测试覆盖 | 可记录债务 | swapchain 本属 app 主循环专有（docs 已注明不进 CI）；Linux 实机验证需带显示环境 | 接入真实 Linux 桌面（或有显示 CI runner）时补 swapchain 实机验收证据；P1 剩余 golden 流水线维持离屏 | 开放 |
| DEBT-021 | 2026-08-24 | Vulkan swapchain 复用 graphics 队列族进行 present（`vkGetPhysicalDeviceSurfaceSupportKHR` 仅运行时校验 graphics 队列可 present），未独立选择 present 专用队列族 | 可记录债务 | 当前 Linux 桌面 Vulkan 路径以 graphics 队列 present；未来 macOS/MoltenVK 适配必须实机复核该假设 | 引入独立 present 队列支持的平台时，device 创建改为按 surface 支持选择队列族并创建第二队列 | 开放 |
| DEBT-022 | 2026-08-24 | swapchain back buffer 图像布局状态语义未合同化：D3D12 EndRendering 回 COMMON、Vulkan EndRendering 转 TRANSFER_SRC；Present 前 swapchain image 应处 PRESENT_SRC/COMMON，当前 Vulkan 无呈现前显式布局转换 | 设计风险 | 当前 app 实机（D3D12）工作正常；Vulkan surface 未实机验证故布局问题未暴露；真机若校验报错会暴露 | Vulkan 实机验收轮次：EndRendering 对 swapchain image 增加 COMMON/PRESENT 转换或合同层约定"present 目标渲染后回可呈现状态" | 开放 |
| DEBT-023 | 2026-08-24 | `engine/platform` 仍为空目录骨架；SDL3 窗口、输入和音频输出直接位于 app/editor executable，Vulkan surface 接线位于 backend | 设计风险 | 当前 owner 已在 docs/01 按实际依赖标明，运行行为可用；但 app/editor 重复平台 adapter，目标目录长期空置会误导维护者 | 出现第三个 SDL host 或需要共享窗口/输入生命周期时，建立窄 platform adapter；否则删除空骨架并继续让 executable 拥有接线，禁止维持虚假 owner | 开放 |
| DEBT-024 | 2026-08-24 | `SystemRegistration.stage` 是死字段：`RegisterSystem(stage, {stage, order}, cb)` 强制调用方重复传 stage，实现只按参数索引、只读 order，不校验 `registration.stage == stage`；当前也没有文档曾承诺的 before/after 依赖图 | 可记录债务 | 固定五阶段枚举与同阶段唯一数字 order 已足以支撑当前单线程主循环，但调用冗余且复杂依赖只能靠人工分配数字表达 | Stage 合同需要表达真实复杂依赖时，先决定删除冗余 stage 字段并保留简单 order，还是引入有环检测的 before/after 图；未实现前文档只承诺枚举顺序和数字 order | 开放 |
| DEBT-025 | 2026-08-24 | D3D12 `RegisterSwapchainBuffer` 的 width/height/format 参数全部 `(void)` 未使用，调用方硬编码 `kB8G8R8A8Unorm` | 可记录债务 | swapchain 创建路径当前固定 B8G8R8A8；参数预留但未消费 | swapchain 支持多格式时消费参数，或移除参数 | 开放 |
| DEBT-026 | 2026-08-24 | `StageRunner::Tick` 每帧对各 Stage 的注册表执行 `std::sort`，即使注册集合未变化 | 可记录债务 | 当前系统数量小且单线程固定步长，尚无性能证据表明排序是瓶颈 | 若 profiling 显示调度开销，或 Stage 注册转为启动期冻结，改为注册期有序插入/冻结排序；不得在无数据时引入复杂 scheduler | 开放 |
| DEBT-027 | 2026-08-24 | `EnabledInstanceExtensions` 对必需扩展（`VK_KHR_surface` 等）不支持时静默跳过而非硬失败，问题推迟到 CreateSwapchain 才暴露 | 可记录债务 | 离屏测试环境（lavapipe）可能缺 surface 扩展但无需 swapchain；静默跳过让离屏可用 | CreateSwapchain 已对 swapchain_supported_ 检查；若需更早失败可在 instance 创建时校验 surface 必需扩展 | 开放 |
| DEBT-028 | 2026-08-24 | `vkAcquireNextImageKHR` 用 `VK_NULL_HANDLE` semaphore/fence：单线程+FIFO present 可用但非规范用法，无帧内同步信号量 | 可记录债务 | 当前单命令列表顺序执行、Present 前有 WaitForGpuIdle 间接同步；未暴露竞争 | 多帧 in-flight 或双缓冲流水线落地时引入 acquire semaphore + present wait semaphore | 开放 |
| DEBT-029 | 2026-08-24 | 材质/纹理兼容输入曾未完整落地：cgltf 材质元数据、纹理解码及向渲染风格插件的交接需要独立 owner，不能由 engine/domain 解释 | 可记录债务 | P2 聚焦网格/变换/相机；RHI 采样能力先独立落地，避免引擎提前绑定固定材质模型 | P8-1 已完成 `stb_image` 解码、`SceneLoad` 保留 glTF material/texture 引用、独立 `TextureResourceService`、插件拥有的材质 schema/validator 及 sampled draw；后续仅保留异步流式加载、压缩格式和多材质批处理 | 已关闭（Windows/D3D12 与 Linux/Vulkan 本地全量 CTest 各 213/213；CI run `33070203706` 的三平台 Debug/Release、golden-sync、shader-sync、data-lint、私有头审计和 format 全部通过） |

## 历史审计关闭记录

| 编号 | 描述 | 关闭方式 |
|---|---|---|
| AUDIT-P0-01 | （P0 审计轮）审计脚本正则脆弱/GetFullPath 无保护 | 2026-08-23 commit `98cff8b` 重写加固并以双反例验收 |
| AUDIT-P0-02 | （P0 审计轮）ci.yml format job 空列表挂起风险 | 同上，加 `xargs -r` |
| AUDIT-P0-03 | （P0 审计轮）smoke_test 弱断言（仅比长度） | 同上，改为 semver 格式校验，ctest 2/2 通过 |
| DEBT-014 | 2026-08-24 显式设 `D3D12_COLOR_WRITE_ENABLE_ALL`（D3D12_BLEND_DESC 零初始化使 RenderTargetWriteMask=0 导致三角形颜色不写） | 已设 write mask 修复；未来引入混合时扩展完整 BlendState |
| DEBT-016 | 2026-08-24 NDC Y 翻转约定零验证：triangle 采样点关于中线对称，去掉 Vulkan 翻转测试仍通过 | 2026-08-24 CI golden 流水线落地：triangle_test 改为全帧比对 `tests/golden/triangle_64x64.ppm`（lavapipe 权威生成），三角形顶点非对称，Y 翻转必然全帧差异 → 约定被锁定 |
| DEBT-017 | 2026-08-24 triangle golden 未达 P1 验收字面：无 golden 参考图、无全帧比对、无截图产物，仅 9 采样点断言蓝通道 | 2026-08-24 CI golden 流水线落地：基准图提交入库、triangle_test 全帧逐像素比对、CI golden-sync job 上传基准图截图 artifact；P1 验收达成 |
| AUDIT-R1 | （审计轮 R1）2026-08-24 Vulkan `SelectPhysicalDevice` 用 `deviceType > best_type` 选最大枚举值，而 `VK_PHYSICAL_DEVICE_TYPE_CPU=4` 是最大，真机同时装 lavapipe 与硬件 GPU 时会优先选择软件光栅 | `vulkan_device.cpp` 引入 `DeviceTypePriority`：离散 > 集成 > 虚拟 > other > CPU，硬件优先、CPU 兜底；WSL linux-debug 构建与 ctest 15/15 通过 |
| AUDIT-D1 | （审计轮 D1）2026-08-24 CI format job 只对 `*.cpp`/`*.hpp` 跑 clang-format，后端私有 `.h` 文件不在门禁内 | `ci.yml` format job 纳入 `*.h`；4 个相关文件本机 clang-format dry-run 通过 |
| AUDIT-P2-01 | （P2 轮）2026-08-24 `compile_shaders.ps1` 输出点号分隔文件名，与构建和已提交生成物期望的下划线命名不一致，导致继续消费旧 shader | 生成脚本统一为下划线命名；清理误生成文件后重编译，D3D12 root signature 同步允许 input assembler；双端构建零警告、ctest 15/15、golden 与 Windows 冒烟通过 |
| AUDIT-R2 | （审计轮 R2）2026-08-24 RHI 合同头声明未实现的 `CreateBuffer/DestroyBuffer` 与 `CopyTexture`，接口与文档矛盾并误导调用方 | 删除未实现接口、相关句柄/描述和把半成品当特性的测试；后端私有 readback 实现保留；WSL 与 Windows 双端构建、ctest 15/15 通过 |
| AUDIT-R3 | （审计轮 R3）2026-08-24 app 退出未等待 GPU idle，窗口早于 Vulkan surface 销毁，且未处理 SDL3 窗口关闭请求 | 修正为 `WaitForGpuIdle → DestroyPipeline → swapchain.reset() → SDL_DestroyWindow`，并处理 `SDL_EVENT_WINDOW_CLOSE_REQUESTED`；Windows 实机点 X 正常退出 |
| AUDIT-R4 | （审计轮 R4）2026-08-24 双后端对 `TextureUsage::kNone` 行为分叉 | D3D12 增加 usage=0 校验并与 Vulkan 对齐，合同注明至少一个 usage bit；双端构建与 ctest 15/15 通过 |
| AUDIT-R5 | （审计轮 R5）2026-08-24 Stage 框架与渲染路径脱节，实际 Acquire/Draw/Submit/Present 硬编码在主循环 | 渲染提交移入 `kRenderSubmit` 阶段，主循环只做输入、固定步进和阶段推进；Windows 实机渲染及退出正常 |
| AUDIT-P4-A1 | （P4 审计轮 A1）2026-08-26 CUBICSPLINE 导入重复计算三元组数量，首尾采样按错误 stride 读取 tangent | 按 glTF accessor 元素语义校验并统一 value offset；加入合法 cubic clip，双平台 `[cubic-spline]` 与全量 ctest 140/140 通过 |
| AUDIT-P4-A2 | （P4 审计轮 A2）2026-08-26 `BoneMatrices` 假定 parent 索引小于 child，合法 glTF joint 顺序可触发错误 | 保留原始 joint 索引，构造期验证 parent 范围与无环，按 parent 依赖记忆化求值；双平台 ctest 142/142 通过 |
| AUDIT-P4-A3 | （P4 审计轮 A3）2026-08-26 双后端 Draw 前未统一校验 pipeline/rendering/resource 绑定，Begin 未完全清理状态 | pipeline 保存资源要求，Draw 前统一拒绝缺失绑定，Begin 清空状态且 Vulkan reset command buffer；双平台 ctest 143/143 通过 |
| AUDIT-P4-A4 | （P4 审计轮 A4）2026-08-26 `BufferEntry` 丢弃 `BufferDesc.usage`，绑定入口可接受用途不匹配的 buffer | 双后端保存 usage 并在 vertex/index/uniform 入口校验；组合 usage 仍可复用；双平台 ctest 144/144 通过 |

## 后续主登记表（DEBT-030–045）

| 编号 | 发现日 | 描述 | 分级 | 不处理原因 | 后续入口 | 状态 |
|---|---|---|---|---|---|---|
| DEBT-030 | 2026-08-25 | RHI per-object uniform 为单 buffer 绑定单对象（无 ring buffer/多对象 dynamic offset）：每对象独立 buffer + 每帧 MapWrite，多角色同帧渲染时 buffer 数量随角色数线性增长 | 可记录债务 | P4 骨骼动画 v0 仅单蒙皮对象 golden 闭环，CharacterController/NPC 多对象渲染在后续轮次才需要 | P4 相机/动态场景子任务引入多对象渲染时评估 ring buffer（帧内偏移对齐 256B）或 per-frame 大 buffer + dynamic offset | 开放 |
| DEBT-031 | 2026-08-25 | 动画状态机未落地：BlendPose 仅支持双 clip 标量权重混合（v0 混合树），clip 图、过渡时间线、动画事件轨缺失 | 可记录债务 | P4 子任务 1 范围是导入/采样/混合最小闭环；状态机属 CharacterController 及演出时间轴（P6）的消费语义，提前落地无消费者 | P4 CharacterController 移动驱动动画时按需扩展（idle-walk-run 一维混合已可表达）；事件轨随 P6 cutscene 时间轴 | 开放 |
| DEBT-032 | 2026-08-27 | 统一项目 lint 曾未调度插件私有 validator | 设计风险 | P9 已提供 `IPlugin::ValidateData`、有界读取、样例插件私有 schema 校验、资源依赖、i18n/CJK、输入合同和确定性资源打包清单；`eventlint --check-project` 与 `--build-resource-package` 均已接入 CI | 二进制压缩归档和远端缓存服务另行迭代，不回退到 app 运行时兜底 | 已关闭 |
| DEBT-033 | 2026-08-27 | P5/P6 的 macOS CI 门禁尚未在本轮复验 | 可记录债务 | Windows/D3D12 与 Linux/Vulkan/lavapipe 当前源码已各 209/209；137 个 C++ 源文件已用 clang-format 22.1.3 dry-run 通过；WSL 无 WSLg 不能做 SDL Vulkan surface 实机验收，但不影响离屏 Vulkan 回归 | 2026-08-27 CI run `33052747956` 的 macOS Debug/Release build+test、format、私有头审计和 data-lint 均通过；golden/shader 专项 job 未进入比对，因 vcpkg `libmount` 依赖下载遭遇 502/SSL/timeout，专项门禁转由该外部阻断记录跟踪 | 已关闭（macOS build/test 证据） |
| DEBT-034 | 2026-08-27 | CI golden-sync、shader-sync 和 Linux Debug 依赖全新 runner 的 vcpkg 安装，易受 GNU/kernel 镜像瞬时 502、SSL 或 timeout 影响，导致未进入项目编译/比对 | 环境风险 | CI run `33052747956` 首轮三个失败 job 均在安装 `libmount` 时失败；随后加入按平台/manifest 的 vcpkg 下载缓存，CI run `33058546267` attempt 2 的 Linux Release、data-lint、golden-sync、shader-sync 均通过 | 保留缓存并在 vcpkg baseline 或 manifest 变化时复核；不得以跳过 golden/shader 比对代替门禁 | 已缓解 |
| DEBT-035 | 2026-08-27 | 纹理资源服务尚未提供取消 token、优先级调度、压缩纹理格式和按访问热度淘汰 | 可记录债务 | P8-3 已补齐有界 CPU 文件解码、错误诊断、RGBA8 upload packet、app 阶段接线及 Acquire/Release/Unload；明确保留上述能力为后续资源系统迭代，避免把插件材质语义或后台线程 RHI 操作混入核心 | 后续增加取消/优先级/格式策略/可观测淘汰；不得绕过 `TextureResourceService` 直接在 app 创建 GPU 纹理 | 部分关闭 |
| DEBT-036 | 2026-08-28 | P11 当前插件是源码级、构建期注册；`eventlint` 和 app 的样例宿主仍由编译期 registrar 提供工厂，不支持从 manifest 动态加载第三方二进制 | 设计风险 | 这是已确认的 P11 边界：避免跨编译器 DLL ABI、热加载和隐式任意代码执行；公开 SDK 已要求第三方宿主显式注册自己的工厂 | 若未来需要无源码插件分发，另立 ABI/签名/沙箱设计，不在 P11 偷渡动态加载 | 开放 |
| DEBT-037 | 2026-08-29 | runtime 曾只能以 i18n 文本生成诊断投影，窗口内没有 FreeType glyph atlas 到 RHI 的可见字形绘制 | 阻断问题 | 功能实现已关闭：`DialogPresentationSnapshot` 经 runtime overlay、FreeType glyph atlas、`UiTextDrawPacket`/有界 `UiTextGpuBatch` 和 sampled texture pipeline 绘制；D3D12/Vulkan 使用统一 alpha blend 合同。CJK GPU golden 的可复现 git/CI 边界已由 DEBT-043 收口 | 2026-08-30 本地证据包含 Windows/D3D12 实机中文对话/提示、Windows/Linux 构建测试及 sampled-text RHI 比对；这些证明功能链存在，不替代 DEBT-043 的干净 checkout 门禁 | 已关闭（功能实现） |
| DEBT-038 | 2026-08-29 | P13 编辑器已落地 glyph atlas、fallback 字体、sampled-text batch 与矩形裁剪；仍缺通用显式 z-order 与字体预热策略 | 设计风险 | 2026-09-04 集成审查确认菜单被 Project 搜索框局部遮挡违反当前导航闭环的冻结 GUI 验收合同；该具体缺陷已通过 `#menubar` 顶层 stacking context 修复，并由 `[editor][rmlui][menu][z-order]` 回归测试锁定，但修复后的 Windows 真窗证据尚待补齐 | 后续编辑器体验迭代仍需在真实消费者出现时设计共享 draw-order 合同，并按实际字库与项目语言设计有界字体预热；不得再把可见遮挡降级为非阻断体验问题 | 部分缓解（菜单层叠已修，真窗待复验） |
| DEBT-039 | 2026-08-30 | `PrepareSave` 通过 `tools/project::Diagnose` 对完整 working copy 执行同源核心 parser、跨文件引用、本地化覆盖、adapter 与插件 validator；插件 sidecar working copy 现在经有界 overlay reader 进入 `plugin::ValidateProjectPluginData`，非法插件数据和 validator 异常均阻断 token | 阻断问题 | — | 已补充 sidecar 非法数据、validator 异常、无 token、原文件不变及现有核心保存阻断测试；CLI/GUI 继续共享 `ProjectWorkspace` 保存 seam，descriptor 声明约束不替代插件 validator | 已关闭 |
| DEBT-040 | 2026-08-30 | 插件运行时 manifest 允许多个任意安全相对 `data_roots`，但 `tools/ci/package_release.ps1` 仅校验这些声明，装配时固定复制 manifest 同目录下的 `data/`，既不逐项复制声明 root，也不拒绝未被装配的合法 root；第三方插件可得到成功但缺少私有数据的发布包 | 阻断问题 | `package_release.ps1` 现已逐项解析并装配所有声明 root，按 root 保留包内相对路径；对缺失、越界、重复、嵌套冲突、输出目录冲突和无法保持包内路径的 root 拒绝；`selftest_package_release.ps1` 覆盖自定义/多 root、拒绝场景及两次确定性清单 | Windows/Linux 发布 job 已接入该自测；需在当前支持平台的干净 runner 上随 P11 发布证据复跑实际样例包 | 已关闭（装配实现与行为自测） |
| DEBT-041 | 2026-08-30 | 根 `vcpkg.json` 的 description 仍称项目为 `Persona-style JRPGs` 引擎，与产品真源已否定固定 Persona 式视觉目标、渲染风格由插件和项目资产拥有的边界冲突 | 设计风险 | 该字段不影响构建和运行，但可能进入包元数据、工具展示或后续文档，造成已否定方向回潮；本轮用户限定只修改文档，因此不改构建元数据 | 独立元数据清理轮将 description 改为与 `docs/00-product.md` 一致的中性 JRPG 运行框架定位，并搜索当前非历史文件确认 `Persona-style` 零残留；不得同时改变依赖或版本 | 开放 |
| DEBT-042 | 2026-08-30 | 文档把 P13 editor 描述为可删除工具，但根 CMake 无条件 `add_subdirectory(tools/editor)`，单元测试目标直接链接 `jrpgmaker::editor_host`；editor 虽不是 app 的运行时依赖，却不是当前构建图中的可选组件 | 设计风险 | 运行包不包含 editor，运行时边界未被破坏；但干净核心构建仍会解析 SDL/editor 目标，测试也无法在排除 editor host 后原样构建 | 增加明确的 editor build option，按目标职责拆分 editor 专属测试或受同一 option 控制；关闭该 option 时配置、核心 build/test、app 和发布装配必须通过，再恢复“可删除”表述 | 开放 |
| DEBT-043 | 2026-08-30 | runtime overlay CJK GPU golden 曾未形成干净 checkout 可复现门禁，golden-sync 未生成或上传该基准 | 阻断问题 | — | `runtime_overlay_test.cpp`、固定的 `NotoSansCJK-Regular.ttc`（OFL-1.1）及 `runtime_overlay_cjk_256x160.ppm` 均已由 git 跟踪；`tools/ci/generate_runtime_overlay_golden.ps1` 通过真实 Catch2 GPU readback 生成基准，CI golden-sync 生成后执行 `git diff --exit-code -- tests/golden` 并上传 artifact；Windows/D3D12 与 Linux/Vulkan 本地输出 SHA-256 均为 `AA4F27D716AFFE81F34E634A26EFBAC9F2F9F4CB2D30A8C1132016D780FED94A` | 已关闭（2026-08-31） |
| DEBT-044 | 2026-08-30 | 产品与 ADR 将 macOS 定义为后续适配、非当前完成门禁，但 `.github/workflows/ci.yml` 的 `build-test` matrix 仍包含 mac-debug/mac-release，且没有 `continue-on-error` 或条件豁免；任何 macOS 失败都会使整个 CI workflow 失败 | 设计风险 | 多平台兼容性检查本身有价值，但“产品不承诺”与“PR 被硬阻断”是两个不同合同；当前文档若只称其历史检查会掩盖实际合并影响 | 独立 CI 治理轮明确二选一：将 macOS job 调整为非阻断兼容性信号，或正式提升为工程合并门禁并同步 ADR/产品边界；在决策前文档必须同时陈述产品非支持与 CI 实际硬失败语义 | 开放 |
| DEBT-045 | 2026-08-30 | 原登记误把 `CONTEXT.md`、当前文档、ADR 与 archive 历史入口判定为未被 Git 跟踪；当前索引链接目标均存在，且由 `git ls-files` 覆盖，未发现干净 checkout 会丢失的索引目标 | 阻断问题 | — | `tools/ci/check_docs_index.ps1` 逐项检查 README 本地链接目标存在且已被 Git 跟踪，并确认当前有效表存在目标；后续新增真源继续通过该门禁登记 | 已关闭（索引/链接门禁） |

## 补充关闭记录

| 编号 | 描述 | 关闭方式 | 关闭日 |
|---|---|---|---|
| A5 | Vulkan descriptor set 在 draw 间复用并更新，可能污染已录制 draw | 已修复：按绑定快照分配独立 set，设备销毁时统一回收 pool | 2026-08-26 |
| P3-9 | `TextBlock` 对未加载字体或零像素高度缺少输入保护，可能产生除零/无效布局 | 已修复：无效字体度量或零高度返回零尺寸，并有 widget 回归测试 | 2026-08-26 |
| P3-10 | `FlagTriggerSystem` 每次 `FlagChanged` 都线性扫描全部 trigger | 已修复：构造期建立不可变 flag→event 索引，查询降为均摊 O(1) | 2026-08-26 |
| P3-11 | Lua `log()` 空操作导致脚本诊断消息被吞掉 | 已修复：接入 domain 的 `std::clog` 诊断出口 | 2026-08-26 |
| AUDIT-P12-01 | Vulkan 设备兜底析构先释放仍绑定于 buffer/image 的内存，再销毁对象，违反 Vulkan 生命周期要求 | 调整为 readback/buffer 的 `destroy buffer → free memory`、texture 的 `destroy view → destroy image → free memory`；新增设备析构接管未显式释放 buffer、texture 和 readback 的 Vulkan 回归测试 | 2026-08-28 |
| AUDIT-P12-02 | app 对 choice 只允许无选项对话确认，任何选项事件进入后永久阻塞；`DialogRequested` 仅保存 text key，未消费项目 i18n | 新增 `ui::DialogPresentation`，以 localization 解析正文与选项、维护高亮索引并循环选择；增加可配置 previous/next 输入，确认时向 `AdvanceDialog(index)` 提交选中索引，缺本地化键在边界阻断 | 2026-08-29 |
