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
| core | `engine/core` | 数学类型(GLM 封装)、EnTT registry 封装（`Scene`：实体/变换层级/世界矩阵，ADR-001 执行）、句柄式资产管理（`MeshData` 等 CPU 侧资产数据结构）、事件总线、时钟/时间步、日志、诊断 | 仅标准库 + GLM/EnTT | 任何图形 API、平台 API、JRPG 语义 |
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
- v0 裁剪：三角形用例经顶点缓冲 + 单 float3 位置属性（location 0）绘制（P2 起）；几何与 P1 的 `SV_VertexID` 生成三角形完全一致，golden 基准图无需重新标定。顶点输入支持单 interleaved buffer + 可选索引缓冲（uint16/uint32），覆盖 glTF 网格导入需要。

### 场景合同（P2 落地版，ADR-001 执行）

- 运行时对象模型 = EnTT ECS；`core::Scene` 封装 `entt::registry`（owner 依据 core"EnTT registry 封装"，禁止场景树进入运行时）。
- 组件：`Transform`（GLM TRS，与 glTF node TRS 语义对齐，缺省单位阵）+ `Parent`（单亲层级）。世界矩阵 `Scene::WorldMatrix(entity)` 沿 parent 链组合（缺失 Transform 视为单位阵，缺失 Parent 终止链，循环引用有深度守卫）。
- 资产引用：场景实体挂 `assetimport::MeshRef{mesh_index}`（索引入 `SceneLoad::meshes` 池）。v0 场景导入仅覆盖静态网格 + node 层级/TRS/matrix；动画、skin、Draco 压缩在 P2 范围外（cgltf 可解析但导入器拒绝并报错）。
- glTF 坐标约定：glTF node matrix 为列主序（与 GLM 一致，`glm::make_mat4` 直读）；matrix 形式经 `glm::decompose`（GTX 实验扩展，`GLM_ENABLE_EXPERIMENTAL` 限定于 assetimport.cpp 单 TU）分解为 TRS；rotation 为 glTF (x,y,z,w) 顺序转 `glm::quat(w,x,y,z)`。
- 场景导入不缓存（v0 每次调用独立 parse+load）；句柄式异步资产系统是 P2 后续子任务，届时 MeshRef 升级为资产句柄。

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
| 脚本 | sol2 + Lua 5.4 | 逃生舱定位，见事件指令集合同 |
| 序列化 | nlohmann/json | 数据先行工作流基础设施 |
| glTF 解析 | cgltf（vcpkg port 1.15） | 零依赖单文件 C99 库；glTF 2.0 全特性覆盖；仅作资产导入的兼容输入（ADR-004）。vcpkg port 无 CMake config，tools/assetimport 用 `find_path(CGLTF_INCLUDE_DIRS NAMES cgltf.h)` 解析 include；`CGLTF_IMPLEMENTATION` 仅在 asset_import.cpp 单 TU 实例化；MSVC 下该 TU 局部抑制 `_CRT_SECURE_NO_WARNINGS`（C 头合法使用 fopen/strcpy 会被仓库级 `/WX` 提升为错误，抑制范围限定在该 target） |
| 纹理解码 | stb（stb_image，vcpkg port） | 与 cgltf 同属零依赖工具族；PBR 纹理/立绘导入兼容输入（ADR-004） |
| 字体 | FreeType + HarfBuzz | CJK 整形与禁则处理必需 |
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
