# 插件系统规范

> 状态：当前有效（登记于 [README.md](README.md)）。本文是运行时插件与 P13 编辑器扩展的权威规范。当前实现是 C++20 源码级、构建期注册，不承诺跨编译器 DLL ABI、热加载、沙箱或插件市场。

## 目标与边界

插件系统只解决两类可替换语义：战斗规则和渲染风格。核心引擎拥有宿主合同、预算、错误隔离和项目选择；插件拥有私有规则、数据 schema、validator 和表现命令。项目通过稳定 id 选择插件，不通过类名、文件名或宿主分支选择实现。

编辑器扩展是独立 Adapter，不是第三种运行时插件类型。它只描述插件私有数据如何在 P13 GUI 中投影和编辑，不能获得额外运行时权限，不能绕过插件 validator，也不能让 `engine/domain` 理解战斗或材质字段。

## 模块与 seam

```text
project.json
   │ selects stable plugin ids
   ▼
PluginRegistry ── factory ──► IPlugin
   ├─ IBattlePlugin ──► IBattleSession ──► BattleFrameOutput
   └─ IRenderStyleAdapter ──► RenderPlan

plugin.json ──► manifest parser/validator ──► registration
private data ──► bounded read_file ──► plugin ValidateData

plugin.editor.json ──► editor extension registry ──► field/layout/i18n projection
                                            └──────► same runtime validator
```

- `engine/plugin`：manifest、registry、生命周期、通用错误和 validator 调度的唯一 owner。
- `engine/plugin/battle.hpp`：战斗会话 seam 的唯一 owner。
- `engine/render/style.hpp`：渲染风格、材质 validator、RenderPlan 与资源预算的唯一 owner。
- `plugins/*`：插件实现与私有 schema owner。
- `tools/project`：作者期统一调度现有 parser/validator，并把错误映射为编辑器诊断。
- `tools/editor`：GUI Adapter，只消费描述和结构化结果。

禁止在 app、domain、GUI 面板或输入回调中为具体插件 id 增加业务分支。

## 插件身份与命名

插件 id 一经发布即是持久身份，推荐反向域名格式，例如 `org.example.battle.timeline`。仓库样例的 `sample.*` 只用于演示，不是第三方命名模板。

| 标识 | 格式 | 兼容要求 |
|---|---|---|
| plugin id | 小写 ASCII，`.` 分段 | 发布后不可复用给不同语义 |
| capability | `<owner>.<feature>.v<major>` | 仅声明能力，不自动授予权限 |
| error code | `<plugin-id>.<area>.<reason>` | 稳定、可供 i18n 映射，不包含自然语言 |
| result key | `<plugin-id>.result.<name>` | 项目事件显式映射，宿主不解释含义 |
| presentation command id | `<plugin-id>.presentation.<name>.v<major>` | payload schema 由插件拥有并版本化 |
| 私有文档 type id | `<plugin-id>.document.<name>.v<major>` | editor sidecar 与 validator 共用 |
| i18n key | `plugin.<plugin-id>.<area>.<name>` | 只能写入插件自己的 namespace |

当前 schema 1 只验证 capability 非空且唯一，宿主不会解释任意 capability。规范化 capability registry 属后续合同升级；在实现前，插件不得把 capability 当作权限、依赖解析或宿主必然支持的证明。

## `plugin.json` schema 1

```json
{
  "schema": 1,
  "id": "org.example.battle.timeline",
  "type": "battle",
  "version": 1,
  "engine_contract": 1,
  "data_roots": ["plugins/example_battle/data"],
  "capabilities": ["org.example.battle.timeline.action-sequence.v1"]
}
```

| 字段 | 语义 |
|---|---|
| `schema` | manifest 文档 schema，当前只能为 1 |
| `id` | 稳定插件身份，项目清单和 registry 使用它匹配 |
| `type` | 只能是 `battle` 或 `render_style` |
| `version` | 插件包/私有数据合同的正整数版本；宿主当前只记录，不做范围求解 |
| `engine_contract` | 必须等于 `kPluginEngineContract`，变化意味着重新构建和重新验证 |
| `data_roots` | 插件 validator 唯一可读的安全相对目录集合，必须唯一 |
| `capabilities` | 唯一的声明式能力 id；不扩大文件、线程、渲染或 domain 权限 |

未知字段目前不会被 parser 拒绝，但插件不得依赖宿主保留或解释未知字段。新增权威字段必须提升 manifest schema、同步 parser/validator/模板/文档/迁移和测试，不能靠“先塞一个字段”形成隐式合同。

## 注册与装配

注册发生在宿主初始化阶段，顺序固定：

1. 读取并 `ParseManifest`。
2. `ValidatePluginManifest` 检查 schema、contract、重复 root/capability。
3. 宿主以同一 manifest 和非空 factory 调用 `PluginRegistry::Register`。
4. registry 拒绝重复 id、超过 32 个插件、未知类型、不兼容 contract 或空 factory。
5. 解析 `project.json`，检查 `plugins`、`render_style` 和可选 `battle_plugin` 的引用与类型。
6. 在运行前调用 `ValidateProjectPluginData`；有 issue 时项目不可启动或保存为 clean。
7. 通过 `CreateProjectRenderStyle`/`CreateProjectBattlePlugin` 创建实例。

生产宿主必须维护或生成一个显式注册函数。扫描目录后动态执行未知二进制不属于当前合同；参考 `RegisterSamplePlugins` 只用于仓库样例和测试。

`PluginRegistry` 不是线程安全容器。注册和查找在宿主装配线程完成；启动后 registry 视为只读。插件 factory 每次返回一个新的 `std::unique_ptr<IPlugin>`，所有权立即交给宿主，不得返回共享单例、栈对象或由插件另行销毁的实例。

## 生命周期、线程与异常

```text
manifest parsed → registered → data validated → instance created
                → active calls → session/instance destroyed → host shutdown
```

- 插件实例不得保存 `PluginValidationContext` 或其 `read_file`；context 只在当前 `ValidateData` 调用内有效。
- 战斗 session 由创建结果中的 `unique_ptr` 独占，完成、取消或宿主退出后销毁；销毁前宿主不再调用 `Advance`。
- 插件不得保存调用方传入的引用、JSON 引用或 RHI handle，除非对应 interface 明确赋予跨调用生命周期；当前合同没有这种授权。
- host wrapper 会隔离插件创建、validator、战斗 create/advance、渲染计划构建/执行异常。插件仍应把可恢复失败转换为结构化 result；异常不能承担业务分支。
- 插件若自行创建线程，必须在实例析构前请求停止并 join，队列必须有上界；不得在后台线程调用 host、RHI、UI、registry 或保存借用对象。第一版推荐插件保持单线程。
- 不允许全局可变状态承载项目/session 数据；多项目或多 session 测试必须互不污染。

当前源码级插件与宿主使用同一编译器、运行库、C++ 标准库和依赖版本，因此可通过 STL 值对象与 `unique_ptr` 交互。这不是稳定二进制 ABI 承诺。

## 数据访问与 validator

插件私有内容只位于 manifest 声明的 `data_roots`。validator 通过 `PluginValidationContext::read_file(relative_path)` 读取，禁止自行打开项目路径、读取环境变量、跟随符号链接越界或访问其他插件目录。

| 项目 | 当前上界 |
|---|---:|
| 插件注册数量 | 32 |
| 每个 validator 读取文件数 | 32 |
| 单文件大小 | 256 KiB |
| 每个 validator 总读取量 | 1 MiB |
| 聚合插件 issue | 128 |

达到上界必须返回 error；不得截断后报告 clean，也不得通过重复校验、分片路径或压缩炸弹绕开总预算。读取路径必须同时通过词法安全、canonical project containment 和 canonical declared-root containment。

每份私有数据声明独立 `schema`。插件 `version` 与数据 `schema` 是不同概念：升级实现而不改数据格式只提升 version；不兼容数据变化提升文档 schema 并提供作者期迁移器。validator 应聚合可行动 issue，但保持稳定顺序：规范化相对路径、字段路径、error code。

## 错误合同

运行时 `PluginError` 包含 `code`、`message`、`path`：

- `code` 是程序判断和 i18n 映射的稳定事实。
- `path` 是插件 id、相对文件路径或字段路径，不得包含绝对用户路径、密钥或 payload 内容。
- `message` 是开发日志摘要，不是 GUI 的权威自然语言；P13 通过 code → namespaced i18n key 映射展示，并把参数单独传递。

插件返回 issue 时必须填写 namespaced code。空 code 会被宿主降级为 `plugin.validator.issue`，这只用于防御，不是允许省略 code。错误不得静默吞掉；factory/session 返回失败却不给 error 时，host wrapper 会生成 `*.failed` 防御错误。

作者期 `tools/project` 负责映射严重度：解析、引用完整性、contract、预算、安全路径和 validator exception 均为 error；可继续但值得注意的内容质量问题可为 warning。运行时不能因为 GUI 隐藏 error 而继续启动。

## 战斗插件 interface

战斗插件实现 `IBattlePlugin::CreateSession`，每个遭遇创建一个 `IBattleSession`。宿主只理解生命周期和 opaque payload，不理解 actor、skill、buff、QTE、回合、伤害或奖励。

- `BattleLaunchContext`：稳定 `encounter_id` 和最多 64 KiB opaque payload。
- `BattleFrameInput`：有限 `delta_seconds`、每帧最多 32 个 action id、最多 64 KiB payload 和 cancel 请求。
- `BattleFrameOutput`：完成标记、完成时必需的 result key、最多 128 个 presentation command、presentation payload 合计最多 64 KiB、输出 payload 最多 64 KiB。
- `BattleSnapshot`：active/finished 状态必须一致，payload 最多 64 KiB。

宿主调用只能经过 `CreateBattleSession` 和 `AdvanceBattleSession`，以统一执行输入/输出校验和异常隔离。插件的 presentation command 由插件自己的 presentation Adapter 消费；domain 只根据稳定 result key 排队项目事件，不解释 payload。

同一 session 在一次 `Advance` 完成前不得重入；EventBus 回调内不得创建或推进 session。完成结果只能结算一次，未知 result key 必须由项目 lint 和运行时共同阻断。

## 渲染风格插件 interface

渲染插件实现 `IRenderStyleAdapter`：

- `Descriptor` 返回稳定 id、version 和 `RenderResourceBudget`。
- `ValidateMaterial` 解释项目拥有的材质 JSON；材质 schema 完全属于插件。
- `BuildPlan` 把只读 `SceneSnapshot` 转换为后端无关 `RenderPlan`。

插件不得访问 D3D12/Vulkan 类型、创建 RHI device、持有后端 handle、修改 ECS/domain 或把固定 PBR/toon 字段提升为 engine schema。`RenderPlan` 只引用宿主 catalog 中的 pipeline/mesh/texture id，并受 pass、draw 和材质总字节预算约束；真正录制由 `RenderPlanExecutor` 和 resolver 完成。

材质 JSON 经插件 validator 通过后，运行时可编码为 opaque bytes 传回同一插件。编码格式和版本由插件拥有，但必须有上界、确定性并在错误时返回结构化失败；宿主不反序列化插件材质。

## P13 编辑器扩展

### 独立 sidecar

需要 GUI 表单的插件在 `plugin.json` 同目录提供 `plugin.editor.json`。它不进入运行时 manifest，不改变 `PluginType`，app 和运行包可完全忽略它。

```json
{
  "schema": 1,
  "plugin_id": "org.example.battle.timeline",
  "editor_contract": 1,
  "documents": [
    {
      "type_id": "org.example.battle.timeline.document.encounter.v1",
      "roots": ["plugins/example_battle/data"],
      "descriptor": "editor/encounter-fields.json"
    }
  ],
  "locales": {
    "zh-CN": "editor/locales/zh-CN.json",
    "en": "editor/locales/en.json"
  },
  "icons": "editor/icons.json"
}
```

sidecar 路径相对插件安装根解析；`plugin_id` 必须精确匹配相邻运行时 manifest。所有 roots 必须是运行时 `data_roots` 的相同或更窄子集，不能借 editor 扩大文件权限。`editor_contract` 与编辑器 SDK 独立版本化，不提升 `engine_contract`。

### 声明式字段描述

字段描述只允许：

- JSON pointer、值类型、集合形状、required/read-only、数值/长度/枚举约束。
- label/help/placeholder 的 namespaced i18n key。
- host 已登记的控件 role，例如 text、number、select、list、object、resource-reference。
- host semantic theme recipe id；插件不得提供私有 RGBA、字号或布局像素。
- 受限的 visible/enabled 条件，只能引用同文档字段并使用声明式比较；第一版可完全不支持条件，不能嵌入脚本。

字段描述不是 validator。任何编辑都先由 descriptor 做交互级约束，再调用插件运行时 `ValidateData`/`ValidateMaterial` 作权威校验；两者冲突时 validator 胜出并产生 error。

没有 sidecar 或 descriptor 无效时，编辑器仍可显示插件状态、文件和 validator 诊断，但私有文档只读。禁止自动退化为可保存的自由 JSON 编辑器。

### 主题、语言和命令

- 插件扩展只能使用 host semantic tokens 和 component recipes，不能覆盖全局主题 token。
- 插件 locale key 必须位于 `plugin.<plugin-id>.*`；不同 locale 的 key 与 placeholder 集完全一致。
- 插件图标通过资源 id 和有界 manifest 声明，禁止绝对路径、网络 URL 和运行时下载。
- editor command id 使用 `<plugin-id>.editor.<name>.v<major>`；命令只形成类型化 edit/selection/preview 请求，不直接写文件或启动 shell。
- 自定义源码面板属于后续可选 editor SDK Adapter；即使引入，也只能输出 host `UiCommand`/`DrawList`、消费 theme/i18n，不得直接访问 SDL、RHI 后端或项目文件。

### 安全与预算

sidecar 清单解析与基础合同校验已落地：当前实现覆盖 schema/editor_contract、插件 ID、文档 type id/根/descriptor 路径、locale 路径、图标路径、重复项和数量上界，并验证 editor roots 不得超出运行时 data_roots。P13-0 实现前必须继续为 descriptor 文件的单文件大小、总字节、字段数、布局节点、locale 条目、图标尺寸和诊断数上界提供测试。未知 schema/contract、路径越界、重复 type id、namespace 冲突或引用缺失都使扩展不可加载，但不能阻止运行时项目在没有编辑器的环境中启动。

## 版本与兼容性

| 变化 | 应修改 | 不应修改 |
|---|---|---|
| 插件实现修复，无数据变化 | plugin `version` | engine contract、数据 schema |
| 插件私有文档不兼容变化 | 文档 schema、plugin version、迁移器 | engine contract |
| battle/render 公共 C++ interface 变化 | `kPluginEngineContract`、全部插件重编、兼容测试 | 静默保留旧调用路径 |
| runtime manifest 字段合同变化 | manifest schema、parser、模板、迁移和发布 lint | 只增加未知字段 |
| editor sidecar/descriptor 合同变化 | `editor_contract` 或 sidecar schema | engine contract |
| presentation payload 不兼容变化 | command id major | domain schema |

当前只支持精确 `engine_contract == 1`。不维护未经测试的多合同分支，也不承诺旧插件二进制继续加载。

## 构建、发布与许可

- 外部源码插件通过 `find_package(jrpgmaker CONFIG REQUIRED)` 和公开 target 消费 SDK；禁止 include `engine/*/src`、backend 私有头或仓库相对私有路径。
- 插件构建必须继承 C++20、warning-as-error 和宿主工具链；第三方依赖由插件自行声明并进行许可证审计。
- 运行包由发布脚本复制宿主、运行时 `plugin.json` 和私有 data；编辑器 sidecar/locale/icon 只进入独立编辑器资源包。
- AGPL 项目分发、插件自身许可证和第三方依赖义务分别记录；不得删除上游许可证或把不兼容素材混入参考包。
- manifest、私有数据和 editor sidecar 均不得包含密钥、用户绝对路径、网络凭据或机器特定缓存。

## 测试与验收矩阵

| 层 | 必测内容 |
|---|---|
| manifest | 正常解析；错 schema/type/contract；重复 root/capability；重复 id 注册 |
| factory/lifecycle | 创建、销毁、重复创建隔离、factory 异常、容量上限 |
| data validator | 正常数据、坏 schema、缺引用、路径越界、symlink 越界、文件/字节预算、异常 |
| battle | launch/input/output/snapshot 上界、取消、完成一次、result 映射、create/advance 异常 |
| render style | descriptor、材质好坏数据、RenderPlan 预算、未知资源、build/resolve 异常、双后端 golden |
| editor sidecar | plugin id 匹配、路径 containment、字段 descriptor、i18n key/placeholder、主题 token、坏扩展只降级只读 |
| SDK/发布 | 外部 `find_package` 构建、干净 checkout、确定性 package manifest、许可证和私有头审计 |

参考插件必须至少维持两个语义差异明显的实现：战斗用 turn-based 与 instant，渲染用 unlit 与 accent/style。测试替换插件时不得修改 domain、RHI backend 或 app 业务分支。

## 审查清单

- id、type、version、engine contract、data roots 和 capabilities 已登记且无重复。
- 新语义确实属于插件，而不是 core/domain/project 公共真相。
- 所有外部输入有 schema、上界和结构化错误；没有硬编码剧情、材质真相或宿主坐标。
- factory/session/adapter 的所有权、销毁、线程和异常路径有测试。
- validator 只经有界 `read_file`；不存在直接文件 I/O 或路径逃逸。
- 战斗调用经过 host wrapper；渲染只输出 RenderPlan；没有后端头泄漏。
- 用户可见文本、editor 字段和图标来自 sidecar/i18n/theme 资源，没有 C++ 自然语言或私有颜色。
- 文档、模板、兼容矩阵、发布脚本和测试与当前 contract 同步。

完成标准不是“插件能编译”，而是插件可被替换、可校验、可定位失败、可确定性打包，并且删除插件后核心 owner 不需要修改。
