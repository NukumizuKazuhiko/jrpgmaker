# 引擎架构调研：Godot / Unity / Bevy / Unreal

> 状态：当前有效（登记于 [README.md](README.md)）。本文是对象模型与分层决策的证据基础；结论落地于 [01-architecture.md](01-architecture.md) 的决策记录（ADR）。

## 调研目的

在锁定 EnTT ECS 主线前，重评四种运行时对象模型对本项目（Persona 式 JRPG、可插拔战斗、数据先行、三平台）的适配度，并吸收四个成熟引擎中经过验证的架构模式。

## 四引擎速写（证据核验过的事实）

### Godot —— scene → server → driver 三层间接

官方架构分三层：**Scene 层**（`SceneTree`/`Node`，游戏构建的唯一高层入口）、**Server 层**（`RenderingServer`/`PhysicsServer2D/3D`/`AudioServer`/`NavigationServer`/`DisplayServer`/`TextServer` 等启动期单例，承载全部子系统实现）、**Drivers/Platform 层**（Vulkan/D3D12/Metal/GLES3 驱动与 OS 接口）。`core/` 独立于所有层之上存在。

- 标志性模式：`MeshInstance3D` 节点从不直接接触渲染实现，而是调用 `RenderingServer::mesh_create()` 这类抽象 API——场景代码与具体 RHI 完全解耦。
- 场景系统是**可绕过的**：headless 运行时直接用 server API，RID（不透明句柄）手动管理。
- 主循环：物理固定步长 + `_process` 变步长 + 渲染插值。
- TextServer = HarfBuzz + ICU + FreeType（与本项目的 CJK 选型一致，第三方印证）。
- 编辑器本身是一个由 `Control` 节点构成的项目，跑在同一套 Scene 层上——GUI 工具复用引擎自身能力的前例。

### Unity —— GameObject-Component 主体 + DOTS/ECS 混合

主模型是 `GameObject`（重对象，含 Transform/名字/内部标志）挂载 `MonoBehaviour` 组件；DOTS 是补充栈：authoring GameObject 经 Baker **烘焙**为 Entity 数据，SubScene 可流式加载。

- 官方与社区生产共识是 **hybrid 模式**：《V Rising》团队明确"gameplay 全 ECS，表现层（动画角色/粒子/UI）全 GameObject"，且采用**单向数据流**——ECS 只向 GameObject 推数据，从不反向。
- 教训：GameObject 万物皆重对象导致 GC 与规模瓶颈；ECS 与表现层双向互操作会破坏确定性。

### Bevy —— 纯 ECS + 显式 Schedule

`World` 持有 Entity(纯 ID)/Component(纯数据)/Resource(全局单例状态)；`Schedule` 用强类型标签(`ScheduleLabel`)与 `SystemSet` 显式声明系统顺序（before/after），executor 自动并行并检测歧义冲突；`Added<T>`/`Changed<T>` tick 机制做变更检测；结构变更经 `Commands` 延迟应用；模块以 `Plugin` 组织。

- 对本项目的价值不在 Rust 语言而在三个合同思想：**阶段显式化、顺序确定性、变更检测驱动渲染同步**。

### Unreal —— UObject 反射体系 + Gameplay Framework

继承链 `UObject → AActor → APawn → ACharacter`；Actor-Component 组合（`SceneComponent` 承担层级变换）；Gameplay Framework 规定 GameMode/GameState/PlayerState/Controller/Pawn/HUD 的职责分工。工程侧：`Runtime/Editor/Developer/Programs` 按生命周期分目录，模块内 `Public/Private/Internal` 可见性纪律；自研 RHI 抽象层隔离图形 API。

- 对本项目的价值：目录可见性纪律与 RHI 分层先例；反面教训是 UHT 反射代码生成 + 全局 GC 的重量级基础设施对小团队不可承受。

## 对象模型对比（按本项目需求加权）

| 维度 | Godot SceneTree | Unity GO-CS | Unreal Actor | ECS (Bevy/EnTT) |
|---|---|---|---|---|
| 核心抽象 | 树形节点组合 | GO 容器 + 组件 | 反射对象 + 组件 | 实体=ID，组件=纯数据 |
| 内存布局 | 分散堆对象 | 托管堆 + GC | 反射 GC | 连续存储，cache 友好 |
| 父子层级 | 一等公民 | Transform 层级 | SceneComponent 层级 | 需自建 hierarchy 组件（成熟做法） |
| 大量同质实体 | 差 | 差 | 中 | 优 |
| 编辑器友好度 | 极好（树即场景） | 好 | 好 | 弱，需工具层补足 |
| 模拟/表现分离 | 弱（节点即表现） | 中（hybrid 补救） | 弱 | 天然支持 |
| tick 确定性/回放友好 | 低 | 低（DOTS 有目标但混合破坏之） | 低 | 高（顺序显式化后） |

## JRPG 需求侧判断

1. 本项目同屏实体规模小（几十~几百），**性能不是选型决定因素**。
2. 决定性因素依次是：
   - **数据先行边界**（AGENTS.md 纪律 2）：事件指令集、战斗数值表、演出时间轴全是纯数据文件 → "组件即纯数据、逻辑在系统"的形状与领域模型同构；
   - **domain/presentation 单向分离**（owner 合同）：ECS 让"模拟真相"与"渲染投影"天然分层，V Rising 的 one-way 模式是生产级印证；
   - **QTE/cutscene 需要确定性 tick**：显式 stage 序列是前提；
   - 层级表达需求（角色→骨骼→武器挂点）可用 EnTT hierarchy 组件满足；UI 自有控件树，不依赖世界对象模型。
3. 场景树/GO-CS 的优势集中在**编辑器友好性**，而本项目第一阶段明确无 GUI 编辑器（@@BOUNDARY@@）；远期立项时编辑器可作为"ECS 世界之上的视图层"实现（Godot 证明工具可与运行时同构），不要求运行时倒退为节点树。

## 结论：采纳 / 拒绝清单

### 采纳（写入 01-architecture 合同）

| # | 来源 | 采纳内容 |
|---|---|---|
| A1 | Godot | **server 式子系统**：rhi/render/audio 保持无状态服务形状，上层只持 RID 式句柄；scene→server→driver 三段间接映射为本项目 render(高层渲染语义)→rhi(图形合同)→backend(d3d12/vulkan, driver 角色) |
| A2 | Bevy | **Stage 合同**：固定步长 tick 内建立显式阶段序列（Input → Domain Sim → Animation → Presentation Sync → Render Submit），跨阶段系统必须声明 before/after，禁止隐式顺序 |
| A3 | Bevy | **变更检测驱动投影同步**：presentation 只消费带脏标记的 domain 状态变更，不做每帧全量轮询 |
| A4 | Unity/V Rising | **单向 hybrid 红线**：模拟(ECS/domain)只向表现推数据，禁止表现层写回业务真相（强化既有 owner 合同） |
| A5 | Unreal | **Public/Private 目录可见性纪律**：每个 engine 模块分 `include/`(对外合同) 与 `src/`(私有实现)，外部禁止 include 私有头 |
| A6 | Godot | 远期 GUI 编辑器立项时的默认路线：编辑器复用 `engine/ui` 与引擎自身能力（前瞻记录，非当前范围） |

### 拒绝（含理由，防止回潮）

| 来源 | 拒绝内容 | 理由 |
|---|---|---|
| Godot | Node/SceneTree 作为运行时 owner | 重对象树与数据先行边界冲突；headless 测试、确定性回放均劣化 |
| Unity | GO+MonoBehaviour 全盘照搬 | 同上；GC 压力与规模瓶颈已被 Unity 自己用 DOTS 承认 |
| Unreal | UHT 式反射代码生成、全局 GC | 小团队不可承受的基础设施重量；本项目用 JSON schema + 手写绑定替代反射需求 |
| Bevy | 多线程自动并行 executor | C++ 下收益/复杂度比差；JRPG 规模单线程 tick 足够，Stage 合同为将来留门 |

## 决策影响

- **ADR-001（维持）**：EnTT ECS 为运行时唯一对象模型 owner，理由见上文 §JRPG 需求侧判断；场景树仅允许作为未来编辑器的视图投影，不得进入运行时。
- 修订落点见 [01-architecture.md](01-architecture.md)：新增 Stage 合同、单向 hybrid 红线表述强化、目录可见性纪律。
