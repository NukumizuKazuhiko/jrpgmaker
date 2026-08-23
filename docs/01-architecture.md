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
| core | `engine/core` | 数学类型(GLM 封装)、EnTT registry 封装、句柄式资产管理、事件总线、时钟/时间步、日志、诊断 | 仅标准库 + GLM/EnTT | 任何图形 API、平台 API、JRPG 语义 |
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
- Shader 编译（ADR-003）：HLSL 单一源，构建期经 DXC 编出 DXIL 与 SPIR-V 两种字节码；RHI pipeline 接口只消费预编译字节码，后端各自选择消费格式；运行时无 shader 编译器依赖。CI 三平台经 Vulkan SDK 提供 dxc。

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
| ECS | EnTT | header-only、成熟、JRPG 规模下性能充裕 |
| 数学 | GLM | 与 GLSL 语义对齐，减少 shader 侧转换错误 |
| 窗口/输入 | SDL3 | 三平台窗口、输入、剪贴板一站式 |
| 图形 | 自研 RHI + D3D12(Win) / Vulkan(Linux/macOS via MoltenVK) | 平台边界决定的双后端结构 |
| 着色语言/编译器 | HLSL 2021 + DXC（单源双目标，见 ADR-003） | 双后端 shader 语义同源是 golden image 一致性的前提 |
| 脚本 | sol2 + Lua 5.4 | 逃生舱定位，见事件指令集合同 |
| 序列化 | nlohmann/json | 数据先行工作流基础设施 |
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
├── assets/
│   ├── data/                  # 事件/对话/数值表 JSON
│   ├── schemas/               # JSON schema（生成物与校验的真源）
│   └── art/ fonts/ audio/
└── tests/
    ├── unit/                  # Catch2 单测
    └── golden/                # 渲染基准图与比对脚本（生成物只读）
```
