# 架构与 owner 合同

> 状态：当前有效（登记于 [README.md](README.md)）。本文是模块边界的唯一真源；新增第三方库必须先在此登记选型理由并获用户确认。

## 分层图与依赖规则

依赖方向严格单向向下，禁止反向依赖与跨层跳跃：

```
adapter   tools/(CLI) · platform/(SDL3) · (远期)editor
             ↓ 只做接线
shell     app/ 主循环装配 · demo 宿主
             ↓
domain    engine/domain  ← JRPG 业务真相唯一 owner
          (event · dialogue · battle · cutscene · save)
             ↓ 经事件总线发结构化命令/状态 projection
ui        engine/ui        render     engine/render     audio    engine/audio
(控件树·动效·CJK排版)      (场景·材质·后处理)      (BGM/SFX·混音总线)
             ↓                      ↓                        ↓
rhi       engine/rhi 图形 API 合同层（d3d12 / vulkan 为 adapter 后端）
             ↓
core      engine/core 数学 · ECS · 资产句柄 · 事件总线 · 时间 · 日志
```

## Owner 边界合同表

| 层 | 目录 | 唯一 owner 职责 | 允许依赖 | 禁止事项 |
|---|---|---|---|---|
| core | `engine/core` | 数学类型(GLM 封装)、EnTT registry 封装（`Scene`：实体/变换层级/世界矩阵，ADR-001 执行）、句柄式资产管理（`AssetRegistry`：注册/查询/卸载/泄漏计数 + `MeshData` 等 CPU 侧资产数据结构）、事件总线、时钟/时间步、日志、诊断 | 仅标准库 + GLM/EnTT | 任何图形 API、平台 API、JRPG 语义 |
| rhi 合同 | `engine/rhi` | 图形 API 语义：设备、swapchain、pipeline、buffer/texture/sampler、command list、同步原语 | core | 出现任何游戏概念；暴露后端类型 |
| rhi 后端 | `engine/rhi/backends/{d3d12,vulkan}` | 实现 RHI 合同 | rhi 合同 + core | 相互引用；后端头文件泄漏出 `backends/` |
| render | `engine/render` | 场景渲染器、材质系统、光照、相机投影、后处理栈(bloom/LUT)、toon shading 与描边 | rhi, core | 私造游戏状态；读取战斗/存档数据 |
| ui | `engine/ui` | 保留式控件树、布局、九宫格、缓动动效、UI shader 效果、FreeType+HarfBuzz CJK 文本排版 | render, core | 决定游戏流程；私造文案 |
| audio | `engine/audio` | BGM/SFX 播放、混音总线、音频解码(miniaudio) | core | 判断游戏状态 |
| domain | `engine/domain` | **JRPG 业务真相**：事件指令集+解释器、flag 存储、对话模型、BattleRules 插件合同、QTE 时间轴合同、cutscene 时间轴语义、存档 schema、Lua(sol2) 绑定 | core（对 ui/render/audio 仅经事件总线发结构化命令） | 直接调用 presentation 内部 API 绘制；硬编码剧情/数值 |
| platform(adapter) | `engine/platform` | SDL3 窗口/输入接线、文件系统抽象、剪贴板 | core | 游戏语义 |
| shell(app) | `app/` | 主循环装配、各层初始化顺序、demo/游戏宿主 | 全部下层 | 成为第二业务逻辑 owner |
| tools(adapter) | `tools/` | CLI：schema lint、资产导入、golden image 对比 | core, domain(schema) | 运行时行为 |

## 数据流合同（单向）

```
输入(SDL3) → shell → domain 解释为游戏状态变化
domain → 结构化命令/状态 projection（事件总线）→ ui / render / audio 执行呈现
presentation 层禁止反推业务结论（如"谁赢了""权限够不够"）
```

## 决策记录（ADR）

| 编号 | 决策 | 状态 | 依据 |
|---|---|---|---|
| ADR-001 | 运行时唯一对象模型 owner = EnTT ECS；场景树只允许作为远期编辑器的视图投影，禁止进入运行时 | 已决 | [03-engine-survey.md](03-engine-survey.md) §JRPG 需求侧判断 |
| ADR-002 | 主线 P0–P7 不实现 ACT 物理战斗；ACT 战斗定位为远期 BattleRules 插件植入，P5 建立的插件合同必须保证零引擎核心改动接入 | 已决（用户确认） | [00-product.md](00-product.md) §边界 |
| ADR-003 | 着色语言与编译器 = HLSL 源码 + DXC 单源双目标：DXIL 供 D3D12 后端、SPIR-V（`-spirv`，Khronos 官方维护）供 Vulkan 后端；禁止双语言双编译链 | 已决（用户确认） | golden image 双后端一致性要求 shader 语义同源；GLSL 无 DXIL 原生路径，Slang 工具链成熟度不足（P1-A1 论证） |
| ADR-004 | glTF 导入库 = cgltf 1.15（纹理解码配 vcpkg `stb`/stb_image）；不选 tinygltf/fastgltf/assimp | 已决（用户确认 2026-08-24） | 零依赖单文件契合"第三方库最小化"纪律（vcpkg manifest 无传递依赖）；glTF 定位是兼容导入输入（docs/00 边界），非美术目标，无需 assimp 格式广度或 fastgltf 性能极致；cgltf 为 Godot/Filament/bgfx 采用、glTF 2.0 全特性覆盖（节点层级/网格/PBR 材质/accessor/缓冲视图，skin/animation 解析留待 P4 使用） |

从外部架构模式采纳的合同增强（A1–A6 清单及拒绝项理由见调研文档）：rhi/render/audio 保持 server 式无状态服务形状，三段间接映射为 render(高层渲染语义)→rhi(图形合同)→backend(d3d12/vulkan, driver 角色)（Godot）；显式 Stage 序列与变更检测投影同步（Bevy）；单向 hybrid 红线（Unity/V Rising）；Public/Private 可见性纪律（Unreal）。

## 关键合同设计

### 主循环与 Stage 合同（Bevy 式显式阶段）

固定步长 tick 内的阶段序列是稳定合同，系统注册必须声明所属 Stage，跨 Stage 依赖必须显式声明 before/after，禁止隐式顺序：

```
Input → Domain Sim → Animation → Presentation Sync → Render Submit
```

- Presentation Sync 只消费带脏标记（变更检测）的 domain 状态变更做投影同步，禁止每帧全量轮询。
- 变步长渲染与固定步长模拟之间以插值衔接（Godot 同款策略）。

### 单向 hybrid 红线

模拟侧（ECS/domain）只向表现侧推数据；表现层（ui/render/audio）禁止写回业务真相、禁止反向驱动模拟状态。违反即阻断缺陷。

### RHI 合同（双后端一致性）

- 合同测试套件：同一组渲染行为用例参数化跑 D3D12 与 Vulkan 两后端，输出 golden image 比对（容差阈值）。
- 改动合同必须同一提交内同步两个后端并通过合同测试，否则不得声称完成（AGENTS.md 纪律 1）。
- macOS 经 MoltenVK 走 Vulkan 后端，不单独维护 Metal 后端。
- Shader 编译（ADR-003）：HLSL 单一源，用 DXC 编出 DXIL 与 SPIR-V 两种字节码（开发/CI 经 `tools/ci/compile_shaders.ps1`）；RHI pipeline 接口只消费预编译字节码，后端各自选择消费格式；运行时无 shader 编译器依赖。**字节码提交入库**（`shaders/generated/`，只读生成物，变更走 `compile_shaders.ps1` 重新生成后提交）；CI 的 shader-sync job（Linux）重新生成后 `git diff` 为空作为门禁；macOS 无 dxc 分发（官方 release 无 mac 二进制、无 brew formula、vcpkg `directx-dxc` port 仅 win/linux-x64），故**三平台构建均不调用 dxc**，直接消费已提交字节码。

### RHI v0 合同语义（P1 落地版）

- 对象模型：接口类（`IDevice`/`ICommandList`/`ISwapchain`）+ 强类型句柄（`TextureHandle`/`PipelineHandle`/`BufferHandle`，`kInvalid` 表示失败）；描述符全部为 POD 聚合。v0 无 buffer/顶点输入概念（`CreateBuffer`/`CopyTexture` 未进合同，随 P2 glTF 导入按真实需求引入）。**P2 顶点输入已进合同**：`BufferHandle` + `BufferDesc`（usage: kVertex/kIndex）+ `CreateBuffer`/`DestroyBuffer`/`MapWrite`（v0 host-visible 上传，无 staging）+ `VertexInputLayout`（attribute location/format/offset + 单 binding stride）+ `ICommandList::SetVertexBuffer/SetIndexBuffer/DrawIndexed`；`GraphicsPipelineDesc.vertex_input` 默认空指针保留 P1 纯 shader 几何路径。triangle golden 改由顶点缓冲驱动同一三角形（几何不变 → 基准图不变），双后端一致。
- 渲染模型：dynamic rendering 风格（`BeginRendering`/`EndRendering` 直接绑定 target），无显式 render-pass 对象——这是 D3D12 与 Vulkan 1.3 的公共面；viewport/scissor 默认全 target，由后端内部维护。
- **NDC 方向约定**：顶点着色器输出 clip space 坐标，NDC Y 轴方向在两后端由后端各自保证一致（D3D12 原生 NDC 映射；Vulkan 后端在 `BeginRendering` 设置**负高度 viewport**（`y = height, height = -height`，Vulkan 1.1+ 核心特性）翻转 Y，使同一 shader 在两后端产生相同的 framebuffer 位置）。golden 采样点以该统一约定设计。
- 后端选择：合同层仅暴露 `CreateDevice(Backend)` 工厂声明；各后端静态库提供该符号定义，app 按平台链接对应后端目标。合同头禁止出现任何后端类型或 SDL 类型（窗口以 `void* native_window_handle` 传入，由 platform/app 层负责提取）。
- golden image 路径：CI 无窗口环境走离屏渲染——`CreateTexture(RenderTarget|ReadBack)` → `BeginRendering`+clear → `SetPipeline` + `Draw`（v0 三角形用例）或仅 clear（清屏基线）→ `Submit` + `WaitForGpuIdle` → `MapReadBack`（返回 `MappedTexture{data, row_pitch_bytes}`，行距由后端各自报告：D3D12 为 footprint.RowPitch，Vulkan 为紧密 `width*4`）→ 按行距逐像素比对；readback copy 由后端在 `MapReadBack` 内部完成（每次调用重建 command list 执行 copy 并等待，独立 command allocator），合同层不暴露 copy 命令。swapchain 仅 app 主循环使用，不进 CI。
- **golden 流水线（P1 落地版）**：基准图（`tests/golden/*.ppm`，二进制 PPM P6，RGB 每像素 3 字节）由 **lavapipe（Linux CI 权威环境）生成并提交入库，生成物只读**——变更必须经 `tools/goldenimage` CLI 重新生成后提交，禁止手改。`jrpgmaker_goldenimage` CLI 支持 `generate <out.ppm>`（渲染三角形→写基准图）与 `compare <ref.ppm> [tolerance]`（渲染→全帧逐像素比对，输出 diff 统计与退出码）；纯算法（PPM 读写、RGBA8 全帧比对）在 `jrpgmaker_golden` 静态库（`tools/goldenimage/golden_image.{hpp,cpp}`，无 RHI 依赖）。CI 的 golden-sync job（Linux lavapipe）重新生成基准图→`git diff --exit-code` 作为门禁（与 shader-sync 同模式），并把基准图上传为 artifact（截图产物）。**跨驱动容差**：v0 三角形在 WARP 与 lavapipe 下实测 max channel delta=0（纯色、无抗锯齿、64×64 光栅化规则一致），triangle_test 用 tolerance=2 保留余量；未来引入插值/抗锯齿场景时须重新标定（见 docs/02 §P1 预检"头号技术风险"）。三角形顶点非对称（上方顶点、下方底边），全帧比对天然锁定 Vulkan 负高度 viewport 的 NDC-Y 约定（DEBT-016 闭环）。
- **swapchain（P1 落地版）**：`CreateSwapchain(void* native_window_handle, width, height, format)` 创建呈现链。**native_window_handle 语义**：D3D12 后端接收 HWND（app 经 SDL 属性提取）；Vulkan 后端接收 `SDL_Window*`（backend 私有用 `SDL_Vulkan_CreateSurface` 建 surface，SDL3 是 Vulkan backend 的链接依赖）。`AcquireTexture()` 返回当前 back buffer 的 `TextureHandle`，该 handle **注册进 device 的 texture 表**（D3D12 分配 RTV、Vulkan 建 image view），可直接用于 `BeginRendering`；back buffer 生命周期由 swapchain 持有，`DestroyTexture` 对 swapchain 纹理抛错（`is_swapchain` 标记）。`Present()` 提交呈现；`Resize(w,h)` 内部 `WaitForGpuIdle` 后重建。Vulkan 用 FIFO present mode + sRGB 色彩空间，D3D12 用 FLIP_DISCARD。
- 生命周期约束：`DestroyXxx` 要求调用方保证 GPU 已空闲（即 `Submit` + `WaitForGpuIdle` 之后）；延迟删除是后端后续增强，不属于 v0 合同语义。swapchain back buffer 由 `DestroySwapchain` 统一释放（先 `UnregisterSwapchain*` 反注册再销毁呈现资源）。
- **纹理采样管线（P3 DEBT-029 落地）**：`SamplerHandle` + `CreateSampler(SamplerDesc{filter(kNearest/kLinear), address(kClamp/kRepeat)})`；`IDevice::UploadTexture(handle, data, row_pitch_bytes)`（host→GPU，后端内部建 staging + barrier + copy + WaitForGpuIdle，与 `MapReadBack` 对称——合同层不暴露 copy 命令）；`ICommandList::SetSampledTexture(texture, sampler)` 绑定 v0 单一采样槽位。`GraphicsPipelineDesc.sample_slot`（1=像素着色器采样 slot 0，0=默认无采样，现有 pipeline 不受影响）。**sample_slot 是强制合同（2026-08-25 审计生效）**：双后端在 pipeline 创建时记录，`SetSampledTexture` 对未声明采样的 pipeline 抛 `std::runtime_error`——绑定采样前必须先创建带 `sample_slot > 0` 的 pipeline。**双后端绑定模型**：D3D12 root signature 增两个 descriptor table（SRV t0 + sampler s0，pixel visibility）+ 独立 shader-visible CBV_SRV_UAV/SAMPLER heap；Vulkan descriptor set（binding 0 = SAMPLED_IMAGE、binding 1 = SAMPLER，fragment stage），与 HLSL `register(t0)/register(s0)` 对齐（textured.hlsl 用 `[[vk::binding]]` 显式钉死，`compile_shaders.ps1` 对 -spirv 注入 `VULKAN_TARGET` 宏）。上传后纹理进入 shader-visible 状态并保持（D3D12 PIXEL_SHADER_RESOURCE / Vulkan SHADER_READ_ONLY_OPTIMAL）。**上传行距语义（2026-08-25 审计修复）**：调用方 `row_pitch_bytes` 声明源行距，D3D12 staging 逐行仅拷 `min(footprint.RowPitch, row_pitch_bytes)`（footprint 对齐行距可大于 tight 源，整拷会越界读），Vulkan 拷 `row_pitch_bytes*height`（tight 布局）。**跨驱动一致性已验证**：textured quad（2×2 四色纹理，nearest/clamp，全屏采样）WARP 与 lavapipe delta=0，`tests/golden/texture_quad_64x64.ppm`（lavapipe 权威生成）。**VertexAttribute 扩展**：新增 `kFloat2` 与 `semantic_name`（D3D12 input-element 语义名，Vulkan 按 location 匹配；多属性时 HLSL 输入声明必须按 location 升序以保持双端对齐）。UI 九宫格/文字位图纹理（P3 UI 控件最小集）消费此管线；**glTF 材质纹理（stb 解码 + material 入 `SceneLoad`）仍不在本轮**，随 P4/P6 PBR 渲染单独做。
- v0 裁剪：三角形用例经顶点缓冲 + 单 float3 位置属性（location 0）绘制（P2 起）；几何与 P1 的 `SV_VertexID` 生成三角形完全一致，golden 基准图无需重新标定。顶点输入支持单 interleaved buffer + 可选索引缓冲（uint16/uint32），覆盖 glTF 网格导入需要。
- **常量传递（P2 子任务 6）**：`ICommandList::SetPushConstants(data, size_bytes)`（需 `SetPipeline` 之后、Draw 之前），`GraphicsPipelineDesc.push_constants_size` 声明 v0 上限 64 字节（一个 float4x4 view-proj）。D3D12 = root constants（`D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`，16 DWORD，VS 可见）；Vulkan = push constants（`VkPushConstantRange` 64 字节 VS stage）。**shader 双目标**：SPIR-V 用 `[[vk::push_constant]]` 全局 struct（`compile_shaders.ps1` 对 -spirv 目标注入 `-D VULKAN_PUSH_CONSTANT`），DXIL 用 cbuffer `register(b0)`——同一 HLSL 源按目标条件编译，布局列主序与 GLM 字节对齐。

### 场景合同（P2 落地版，ADR-001 执行）

- 运行时对象模型 = EnTT ECS；`core::Scene` 封装 `entt::registry`（owner 依据 core"EnTT registry 封装"，禁止场景树进入运行时）。
- 组件：`Transform`（GLM TRS，与 glTF node TRS 语义对齐，缺省单位阵）+ `Parent`（单亲层级）。世界矩阵 `Scene::WorldMatrix(entity)` 沿 parent 链组合（缺失 Transform 视为单位阵，缺失 Parent 终止链，循环引用有深度守卫）。
- 资产引用：场景实体挂 `assetimport::MeshRef{handle}`，handle 是 `core::AssetHandle`（强类型，`kInvalid=0`），索引 `SceneLoad::assets`（`core::AssetRegistry`）——`RegisterMesh`/`FindMesh`/`Unregister`/`live_count`（泄漏检测探针，P2 验收"资产句柄泄漏计数为零"）；句柄单调递增不复用。v0 场景导入仅覆盖静态网格 + node 层级/TRS/matrix；动画、skin、Draco 压缩在 P2 范围外（cgltf 可解析但导入器拒绝并报错）。
- glTF 坐标约定：glTF node matrix 为列主序（与 GLM 一致，`glm::make_mat4` 直读）；matrix 形式经 `glm::decompose`（GTX 实验扩展，`GLM_ENABLE_EXPERIMENTAL` 限定于 assetimport.cpp 单 TU）分解为 TRS；rotation 为 glTF (x,y,z,w) 顺序转 `glm::quat(w,x,y,z)`。
- 场景导入不缓存（v0 每次调用独立 parse+load）；**异步加载（P2 子任务 7）**：assetimport `AsyncLoader` 后台线程只做解析（`LoadGltfMesh` 纯函数），`Poll()` 在调用方线程派发完成回调并按需注册进 `AssetRegistry`——注册表保持单线程访问、无需加锁。
- **v0 渲染路径（P2 子任务 5/6 落地）**：goldenimage/测试把 `Scene::WorldMatrix(entity)` 在 CPU 上烘焙进顶点位置（`glm::vec4 local → world * local`），再上传 vertex/index buffer 用 `DrawIndexed` 绘制——静态场景无需每物体 GPU uniform，双后端 golden 比对锁定变换组合正确性（`scene_64x64.ppm`，lavapipe 权威生成）。**飞行动态观察相机（子任务 6）**：`core::Camera`（eye/target/up + 透视参数）的 view-projection 经 `SetPushConstants` 上传（`camera.hlsl`，子任务 6），世界变换仍 CPU 烘焙——相机视角由 GPU 完成，`camera_64x64.ppm` 双后端 delta=0。**P4 引入每物体 uniform（模型矩阵）时切 GPU 矩阵传递**，`WorldMatrix`/`Camera` 合同不变。

### Stage 运行框架合同（P1 落地版）

- 显式阶段序列：`kInput → kDomainSim → kAnimation → kPresentationSync → kRenderSubmit`（`engine/core` 的 `stage.hpp`）。主循环每个 tick 依序推进全部阶段。
- 系统注册声明：任何系统通过 `StageRunner::RegisterSystem(stage, {stage, order}, callback)` 声明所属 Stage 与 within-stage 顺序（升序）。跨 Stage 顺序即枚举顺序，禁止越级依赖。
- v0 语义：阶段为**空占位**（空回调合法）；before/after 依赖图、系统对象模型随 P3 领域核心落地。delta 秒由主循环传入，固定时间步在 app 主循环实现（60Hz，accumulator 上限 0.25s 防螺旋死亡）。**P1 实机接线**：`kRenderSubmit` 阶段已挂真实渲染提交（app 的 swapchain Acquire→Draw→Submit→Present 回调），其余阶段空占位；渲染路径不再硬编码在主循环。

### BattleRules 插件合同

- 接口要素：阶段推进(tick)、玩家意图提交、输入钩子注册(QTE 时间轴/输入缓冲)、结算投影(发给 UI 的结构化结果)。
- 回合制状态机是首个插件实现；引擎发行版不给予任何内置规则特权地位。
- 完整 ACT 物理战斗不在主线（ADR-002），定位为远期插件植入；因此插件合同的验收标准包含"新规则接入零引擎核心改动"。

### 事件指令集（数据先行）

- JSON schema 版本化；指令集（移动/等待/对话/分支/flag 操作/演出触发…）是稳定合同。
- Lua(sol2) 只是复杂逻辑逃生舱：Lua 入口同样过 schema/lint，禁止绕过指令集私造流程。

#### 事件脚本 schema v1（P3 落地，2026-08-24）

- **载体**：`assets/data/*.json`（示例 `events_demo.json`），解析入口 `jrpgmaker::domain::ParseEventScript`（nlohmann/json，技术栈已锁定；`nlohmann_json::nlohmann_json` target，vcpkg 3.12.0）。**demo 数据自洽约束（2026-08-25 审计强化）**：触发器表（`triggers_demo.json`）的 `target_event_id` 必须在事件表（`events_demo.json`）中存在，否则触发后 `Start` 静默返回 false——由 flag_trigger 数据文件测试端到端断言引用有效。
- **文档结构**：顶层 `schema`（必须 =1，否则抛错）+ `events` 数组；每个事件有 `id` + `instructions` 数组。
- **指令 op 集（v1，封闭）**：
  - `set_flag {flag, value}` / `clear_flag {flag}`（clear 是 set_flag=false 简写）
  - `branch {flag, if_set[], if_not_set[]}` — 按 flag 值选子序列；**分支内禁止阻塞指令（dialog/choice/wait）**（线性 runner 无法表达嵌套阻塞，解释器抛 `std::logic_error` 拒绝，禁止静默吞语义）
  - `dialog {speaker, text_key}` — 发 `DialogRequested{event_id, speaker, text_key, options=[]}` 到事件总线，**解释器阻塞直到宿主 `AdvanceDialog()`**（P3 对话模型子任务落地后的握手语义）
  - `choice {prompt_text_key, options:[{text_key, instructions[]}]}` — 分支提示；宿主 `AdvanceDialog(index)` 选中某选项后执行其内联指令序列，再继续 choice 后的指令；**选项内同样禁止阻塞指令**
  - `wait {seconds}` — 顶层阻塞（`EventRunner::Tick(delta)` 累积扣减）
  - 非法 op / **缺字段（含 set_flag 缺 `value`、wait 缺 `seconds`）** / 负 wait / 空 choice options / 错误 schema 版本 → 解析期 `std::invalid_argument`（含上下文信息；2026-08-25 审计收紧：不再对缺字段静默默认）
- **解释器**：`jrpgmaker::domain::EventRunner`（持有 `EventScript&` + `FlagStore&` + `core::EventBus&`）。`Start(event_id)`（事件进行中再 Start 抛错）、`Tick(delta)`（固定时间步驱动，wait 阻塞时扣 delta 不推进，到期后继续后续指令；dialog/choice 阻塞时 Tick 为 no-op）、`AdvanceDialog()`（确认纯文本对话）/`AdvanceDialog(index)`（选中 choice 选项，越界抛 `std::out_of_range`，无待确认对话时调用抛 `std::logic_error`）、`IsDialogPending()`、`IsActive()`/`IsFinished()`（完成后保留事件供查询，`IsActive()` 归 false）。`Start` 对未知事件 id 返回 false 无副作用。
- **对话投影**：`DialogRequested` 即结构化投影（含 event_id/speaker/text_key/options，choice 有、dialog 空），供 UI 消费——2026-08-25 审计移除冗余的 `DialogState` 死结构体（声明从未发布），合同统一为单一投影事件。打字机进度 / 立绘槽位 / i18n 文本表属 presentation 消费层，随 P3 UI/HarfBuzz 子任务落地。
- **变更检测投影同步（A3，P3 子任务 4 落地）**：Presentation Sync 只消费带脏标记的 domain 状态变更。`EventRunner` 在执行时向事件总线广播四个投影事件：`FlagChanged{flag, value}`（每次 set_flag/clear_flag 写入，含 choice 选支内写入）、`EventStarted{event_id}` / `EventFinished{event_id}`（生命周期）、`DialogRequested`（对话状态，含 options）。`FlagStore` 保持 domain 独占——ui/render 不直读，只订阅投影事件。flag 由宿主直接注入时宿主自行负责对应投影（脚本驱动路径由 runner 统一广播）。
- **flag 存储**：`jrpgmaker::domain::FlagStore`（`Set`/`Get`，命名 bool，空名抛错；`live_count()` 为诊断探针）。单线程、domain 独占；ui/render 不直读（走事件总线 projection）。flag 在事件间持久（会话语义）。
- **事件总线**：`jrpgmaker::core::EventBus`（类型擦除订阅/发布，docs/01 owner map 规定 domain 只发布、presentation 只消费）。订阅无退订（v0 消费者存活于进程期）。
- **事件触发器（P3 子任务 5 落地）**：`jrpgmaker::domain::FlagTriggerSystem` + `ParseFlagTriggers`（`flag_trigger.hpp`）。触发器表为纯数据（`assets/data/triggers_demo.json`，schema v1：`{schema, triggers:[{flag, target_event_id}]}`，重复 flag 绑定/空字段解析期抛错）。`FlagTriggerSystem` 订阅 `FlagChanged`，**边沿触发**（false→true 只触发一次；重复 set true 不重触发，flag 回 false 后重新 true 再触发；false 不触发；未绑定 flag 忽略）。**接线约束**：回调在 `EventRunner::Tick` 内同步触发（`FlagChanged` 广播时源事件仍在完成中），宿主**禁止在回调内直接 `Start`**（会抛 "Start while an event is already active"），应排队到下一事件边界启动（标准游戏循环模式，见 flag_trigger_test 端到端用例）。区域/交互触发器依赖 P4 世界交互（玩家位置/碰撞），不在 P3 范围。
- **lint CLI v1（P3 子任务 3）**：`tools/eventlint`（可执行 `jrpgmaker_eventlint`）+ domain `jrpgmaker::domain::LintEventScript`（`event_lint.hpp`）。职责为**单文件解析器无法表达的跨事件一致性检查**：重复事件 id（error）、空 event id/flag 名/speaker/text_key（error）、branch/choice 子序列内静态检出阻塞指令 dialog/choice/wait（error，与解释器运行时 `std::logic_error` 同源合同，作者期提前暴露）、branch 读取的 flag 在脚本内从未被写入（warning——flag 可由宿主注入，但未写入常是拼写/死分支症状）。**跨文件触发器引用检查（2026-08-25 审计补，`--check-triggers <events.json> <triggers.json>`）**：每个触发器 `target_event_id` 必须在事件脚本存在，否则触发后 `Start` 静默返回 false（error）。退出码 0=干净/1=有 error/2=用法错误；CI `data-lint` job 对 `assets/data/events_demo.json`（单文件 lint）与 `events_demo.json + triggers_demo.json`（交叉检查）要求干净。**text_key→i18n 文本表的跨文件引用检查属 v1 范围外**，随 i18n 子任务（文本表 schema 落地）扩展。
- **文本排版库层（P3 子任务 6，`engine/ui` 首次落地）**：`jrpgmaker::ui`（`text.hpp`）三件套：`Font`（FreeType 加载 ttf/ttc，face_index 选 TTC 内面，`units_per_em`/`ascender`/`descender`/`line_gap` 为字体单位，`LoadGlyph`+位图度量供后续栅格化）；`TextShaper`（HarfBuzz shape UTF-8 → `TextRun{ShapedGlyph{glyph_id, cluster, advance_x, offset_x/y}}`，advance/offset 为像素）；`LineBreaker`（**CJK 禁则换行**：行首禁则——行不以闭括号/悬挂标点（`。」』〉）］｝‰％`、小写假名等）开头，行尾禁则——行不以开括号（`（「『《〈【` 等）结尾；超宽单字强制成行防死循环）。**纯 CPU，无渲染**；栅格化/纹理化属 UI 渲染子任务（依赖 DEBT-029 纹理管线）。单位约定：glyph advance 为像素，font 度量（ascender 等）为字体单位（相对 units_per_em，调用方缩放）。测试字体策略：Windows 用 `msgothic.ttc`、macOS 用 STHeiti、Linux 用 fonts-noto-cjk（CI 已装），找不到时 font/shaper 测试 SKIP（明确原因），禁则换行测试**自包含**（手造 glyph，零字体依赖，双端必跑）。
- **Lua 绑定逃生舱（P3 子任务 7，`engine/domain`）**：`jrpgmaker::domain::LuaScriptEngine`（`lua_binding.hpp`，vcpkg `lua 5.5` + `sol2 3.5.0`）。**受限 API 表面**（docs/01 line 120 合同"Lua 只是复杂逻辑逃生舱，禁止绕过指令集私造流程"）：Lua 仅可 `flags.get/set`（FlagStore 读写；`flags.set` **发布 `FlagChanged` 到事件总线，与 EventRunner 同源投影**，2026-08-25 审计接线——否则 Lua 置位的 flag 触发器/表现层静默不触发）、`events.run(event_id)`（经宿主回调请求启动 EventRunner 事件，宿主决定合法性）、`log(message)`。**不暴露**指令解析/执行/schema/EventRunner 内部——业务流程仍走数据文件 + EventRunner。`Run(source)`/`Call(name)` 错误经 try/catch 捕获 `sol::error` 存入 `last_error()` 返回 false（sol2 的 `script`/`safe_script` 对语法/运行错误均抛异常，不捕获会崩溃）。
- **UI 控件框架最小集（P3 子任务 9，`engine/ui` 纯 CPU 层）**：`jrpgmaker::ui`（`widget.hpp`）。**保留式控件树**：`Widget` 基类（id/visible/parent/AddChild/children，owns children，`Layout(available)` 虚方法存储 widget-local rect 并递归布局子控件）；`Panel`（九宫格背景 + padding 内容区，子控件布局进 padding 后区域）、`TextBlock`（Font* 借用非拥有 + TextShaper/LineBreaker 排版，`SetText` 后 `Layout` 用可用宽度排版，尺寸=排版结果）、`List`（垂直堆叠，可见子项按自身自然高度 + spacing，占满可用宽度）。`SliceNine(outer, slice)` 纯几何函数：把矩形切为 3×3 九宫格（四角保留、边单向拉伸、中心填充），切片内缩超过目标尺寸时钳制中心归零且不重叠。**纯 CPU，无渲染**——render 层后续消费已布局 rect 绘制（ui→render 依赖合同，render 层落地时接线）。测试策略：九宫格几何/控件树/面板/列表为自包含单测（双端必跑）；TextBlock 用真字体（SKIP 策略同 font_shaper）。

### 存档合同

- schema 版本号 + 迁移函数表；读档必须经过校验，禁止裸反序列化。

## 技术栈锁定

| 领域 | 选型 | 选型理由 |
|---|---|---|
| 语言 | C++20 | 用户决策；图形行业生态最全 |
| 构建 | CMake ≥ 3.24 + CMakePresets.json | 三平台统一入口；preset 固化配置矩阵 |
| 依赖 | vcpkg manifest (`vcpkg.json`) | 三平台一致版本锁定 |
| ECS | EnTT | header-only、成熟、JRPG 规模下性能充裕（vcpkg 3.16.0，P2 已引入，core::Scene 落地） |
| 数学 | GLM | 与 GLSL 语义对齐，减少 shader 侧转换错误（vcpkg 1.0.3，P2 已引入；`glm::glm` target） |
| 窗口/输入 | SDL3 | 三平台窗口、输入、剪贴板一站式 |
| 图形 | 自研 RHI + D3D12(Win) / Vulkan(Linux/macOS via MoltenVK) | 平台边界决定的双后端结构 |
| Vulkan 加载 | volk（header-only 运行时加载，vcpkg `volk` port） | 免链接平台 loader 库；macOS CI 用 lavapipe 软件驱动时 loader 由预编译包提供 |
| 着色语言/编译器 | HLSL 2021 + DXC（单源双目标，见 ADR-003） | 双后端 shader 语义同源是 golden image 一致性的前提 |
| 脚本 | sol2 + Lua 5.4 | 逃生舱定位，见事件指令集合同（vcpkg sol2 3.5.0 + lua 5.5，P3 引入；`LuaScriptEngine` 受限 API，禁止绕过指令集） |
| 序列化 | nlohmann/json | 数据先行工作流基础设施（vcpkg 3.12.0，P3 引入；`nlohmann_json::nlohmann_json` target，事件脚本 schema v1 解析用） |
| glTF 解析 | cgltf（vcpkg port 1.15） | 零依赖单文件 C99 库；glTF 2.0 全特性覆盖；仅作资产导入的兼容输入（ADR-004）。vcpkg port 无 CMake config，tools/assetimport 用 `find_path(CGLTF_INCLUDE_DIRS NAMES cgltf.h)` 解析 include；`CGLTF_IMPLEMENTATION` 仅在 asset_import.cpp 单 TU 实例化；MSVC 下该 TU 局部抑制 `_CRT_SECURE_NO_WARNINGS`（C 头合法使用 fopen/strcpy 会被仓库级 `/WX` 提升为错误，抑制范围限定在该 target） |
| 纹理解码 | stb（stb_image，vcpkg port） | 与 cgltf 同属零依赖工具族；PBR 纹理/立绘导入兼容输入（ADR-004） |
| 字体 | FreeType + HarfBuzz | CJK 整形与禁则处理必需（vcpkg freetype 2.14.3（`default-features:false` 裁剪 brotli/bzip2/png，仅 core）+ harfbuzz 14.3.1，P3 引入；`engine/ui` 排版库层落地） |
| 音频 | miniaudio | 单文件起步够用；总线混音自研 |
| 测试 | Catch2 + golden image | 单测 + 渲染双后端一致性门禁 |

## 目录结构（目标形态，随里程碑生长）

可见性纪律（Unreal 式，全模块统一）：每个 `engine/*` 模块分 `include/`（对外合同头）与 `src/`（私有实现）；外部代码禁止 include 他人 `src/` 私有头。

```
<repo-root>/
├── AGENTS.md                  # 项目宪法
├── docs/                      # 真源文档（见 README.md 索引）
├── .github/workflows/ci.yml   # CI 三平台门禁
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── engine/
│   ├── core/
│   ├── rhi/
│   │   ├── include/jrpgmaker/rhi/  # 合同头文件（唯一对外面）
│   │   └── backends/d3d12/ · backends/vulkan/
│   ├── render/
│   ├── ui/
│   ├── audio/
│   ├── domain/
│   │   └── event/ dialogue/ battle/ cutscene/ save/ script/
│   └── platform/
├── app/                       # 主循环装配 + demo 宿主
├── tools/                     # lint / assetimport / goldenimage CLI（tools/ci 现有私有头审计）
│   ├── assetimport/           # cgltf 适配器静态库（glTF → core::MeshData，ADR-004 落地）
│   ├── goldenimage/           # golden image 生成/比对 CLI + 纯算法库
│   └── ci/                    # 私有头审计、shader 编译等脚本
├── assets/
│   ├── data/                  # 事件/对话/数值表 JSON
│   ├── schemas/               # JSON schema（生成物与校验的真源）
│   ├── art/                   # 美术资产（meshes/ 等，P2 起 glTF 兼容输入）
│   └── fonts/ audio/
└── tests/
    ├── unit/                  # Catch2 单测
    └── golden/                # 渲染基准图与比对脚本（生成物只读）
```
