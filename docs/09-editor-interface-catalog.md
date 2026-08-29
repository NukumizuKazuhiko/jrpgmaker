# P13 编辑器接口目录

> 状态：当前有效（登记于 [README.md](README.md)）。本文一次性列出 P13 GUI 第一闭环需要跨越的项目接口、现状、缺口和目标 seam。编码时先更新本目录，再新增依赖；禁止在面板实现中临时发现一个接口就复制一个调用链。

## 设计结论

GUI 不直接依赖 `projecttool` 可执行文件，也不解析 stdout/stderr。P13-0 新增 `tools/project` 深模块，以一个结构化工作区接口隐藏文件系统安全、JSON 读取、parser 调度、跨文件引用、插件 validator、稳定 diff、迁移和原子写回。`jrpgmaker_projecttool` 与 `jrpgmaker_editor` 是这个 seam 上的两个 Adapter。

删除 `tools/project` 后，复杂度会重新散落到 CLI 和 GUI，因此该模块具备真实 depth；测试与调用方只穿过同一 interface，保持 locality。

## 目标目录与依赖

```text
tools/project/
  include/jrpgmaker/project/workspace.hpp   # 唯一公共 interface
  include/jrpgmaker/project/document.hpp    # 值对象与诊断类型
  src/                                      # 文件、parser 调度、diff、保存实现
tools/projecttool/main.cpp                  # argv/stdout/exit-code Adapter
tools/editor/                               # SDL3 + UI Adapter
engine/{core,domain,plugin,render,ui}/       # 既有语义 owner
```

依赖只能是 `editor/projecttool → tools/project → owner 公开头`。`engine/*` 禁止反向依赖 `tools/project` 或 `tools/editor`。

## 公共工作区 interface

以下为目标语义，不要求类名逐字照搬；实现前若调整必须先更新本文。

| 类型/操作 | 输入 | 结构化输出 | 隐藏的实现复杂度 |
|---|---|---|---|
| `ProjectWorkspace::Open` | root、只读选项、插件 registry | `WorkspaceOpenResult` | canonical 路径、安全相对路径、文件/总字节预算、manifest 解析、文档发现 |
| `Snapshot` | 无 | `ProjectSnapshot` | 项目 id、schema、所选插件、排序后的 `DocumentDescriptor`、当前 revision |
| `Diagnose` | 可选文档/严重度过滤 | `DiagnosticSet` | parser、lint、跨文件引用、插件私有 validator、资源依赖和预算聚合 |
| `Apply` | `EditCommand{document_id, field_path, value}` | `EditResult{revision, changes, diagnostics}` | JSON pointer 安全、字段可编辑性、类型/范围验证、内存快照与撤销边界 |
| `PrepareSave` | expected revision | `SavePlan{token, changes, diagnostics}` | 全项目重验、稳定 diff、冲突检测、目标文件排序；有 error 时无 token |
| `Commit` | `SaveToken` | `CommitResult` | 临时文件、备份、rename、失败恢复、禁止覆盖已有临时/备份、revision 更新 |
| `Migrate` | 目标 schema 或 current | `MigrationPlan`/`CommitResult` | 迁移链、预览、验证与同一原子写回路径 |
| `BuildPreview` | snapshot revision | `PreviewSnapshot` | 只读场景/事件/资源摘要；不持有 GPU 或启动 app |

interface 规则：

- 所有输出是值对象，不向调用方借出可变 JSON 引用。
- 所有操作都有 revision；旧 revision 的保存必须返回冲突诊断，不能覆盖新状态。
- `Diagnostic` 固定字段为 `code`、`severity`、`document_id`、`file_path`、`field_path`、`message_key`、命名参数和可选 source span。自然语言不进入工作区模块。
- `Change` 固定字段为 document/field path、before/after JSON value 和稳定序号；排序规则为规范化相对路径，再按 JSON pointer。
- 工作区内存、文件数、单文件大小、诊断数和变更数均有显式上界；达到上界返回 error，不截断后伪装为 clean。
- 只读打开和 `Diagnose` 不创建目录、不写缓存、不轮换备份。

## 现有接口盘点

### 项目、插件与资源

| 数据 | 当前 owner/interface | GUI 用法 | 状态/动作 |
|---|---|---|---|
| 项目 manifest | `plugin::ParseProjectManifest`、`ValidateProjectPlugins`、`ValidateProjectDataRoots`、`ValidateProjectPluginData` | 项目概览、插件选择、文档发现 | 可复用；由 workspace 统一调度；`navigation`/`collision`/`camera`/`interaction` 路径显式声明，禁止按目录和文件名推断 |
| 插件 manifest/实例 | `plugin::ParseManifest`、`ValidatePluginManifest`、`PluginRegistry` | 插件状态与私有数据入口 | 可复用；错误统一映射为 `Diagnostic` |
| 渲染资源 catalog | `render::ParseRenderResourceCatalog` | 资源树与引用诊断 | 可复用 |
| 材质实例 | `IRenderStyleAdapter::ValidateMaterial` | opaque JSON 表单与保存阻断 | validator 可复用；字段描述按 [插件系统规范](11-plugin-system.md) 的 editor sidecar 提供 |
| glTF/纹理导入 | `assetimport` 公开导入接口、render 资源预算 | 资源诊断/只读预览 | 第一闭环只读；导入命令后续接线 |

### Core 数据

| 文档类型 | 当前 parser | 编辑器投影 |
|---|---|---|
| calendar | `core::ParseCalendarDefinition` | 月份长度、周长度和日期字段 |
| input actions | `core::ParseInputActionMap` | action 表与键位列表 |
| navigation | `core::ParseNavigationGrid` | 网格元数据、walkable 单元 |
| collision | `core::ParseCollisionAabbs` | AABB 列表 |
| camera | `core::ParseCameraRigData` | 跟随参数与固定区域 |
| cutscene | `core::ParseCutsceneTimeline` | cue 时间线 |
| save | `domain::ParseSave`/`MigrateAndParseSave` | 仅诊断，不作为项目作者主表单 |

### Domain 数据与跨文件校验

| 文档类型 | parser | 引用/语义 validator |
|---|---|---|
| events | `domain::ParseEventScript` | `LintEventScript` |
| interactions | `domain::ParseInteractionPoints` | `ValidateInteractionTargets` |
| triggers | `domain::ParseFlagTriggers` | event lint 的触发引用检查 |
| schedule | `domain::ParseScheduleTable` | `ValidateScheduleTargets` |
| encounters | `domain::ParseEncounterPoints` | `ValidateEncounterTargets` |
| vertical slice | `domain::ParseVerticalSliceDefinition` | `ValidateVerticalSliceTargets` |
| cutscene targets | core parser | `ValidateCutsceneTargets` |
| localization | `domain::ParseLocalizationTable` | `ValidateLocalizationCoverage` |

这些 parser 的错误形状目前不统一：一部分返回 result，一部分抛异常，一部分返回 `PluginError`，lint 又返回 issue 列表。workspace 内部允许使用多个 Adapter，但对外只暴露统一 `DiagnosticSet`；GUI 不得知道某个 parser 是否抛异常。

## 当前必须抽取的 CLI 私有实现

`tools/projecttool/main.cpp` 目前私有拥有以下行为，均不得被 GUI 复制：

| 当前函数 | 问题 | P13-0 归属 |
|---|---|---|
| `CreateProject`/`CopyTreeBounded` | 结果只写 stderr，复制预算与 staging 隐藏在 CLI | workspace create implementation |
| `ProjectSnapshot`/`LoadSnapshot` | 私有模型，错误直接格式化英文 | 公共值对象 + 结构化 open result |
| `ValidateSnapshot` | 只聚合 bool，缺文件和材质交叉检查不可定位复用 | diagnostic pipeline |
| `EditableFields`/`ApplyManifestPatch` | 字段许可与 CLI patch 耦合 | manifest document adapter |
| `BuildEditedDocument`/`PrintManifestDiff` | diff 与终端输出耦合 | `ChangeSet` + CLI formatter |
| `WriteEditedManifest`/`EditData` | 原子写回实现重复，数据 patch 仍按文件名路由 | 单一 transactional writer + document registry |
| `MigrateProject` | 迁移没有结构化 plan | migration pipeline |
| `DiagnoseProject` | 只产生终端摘要 | `PreviewSnapshot`/`DiagnosticSet` |
| `LoadJsonDocument`/`ValidateDataDocument` | I/O、异常、类型路由混合 | bounded document store + adapters |

抽取时先做行为保持测试，再移动实现；CLI 的命令名、退出码和稳定文本输出保持兼容，但文本 formatter 不进入公共 interface。

## 文档 Adapter registry

每种项目文档注册一个 `DocumentAdapter`，这是存在多个实现的真实 seam：

- 身份：稳定 `type_id`、支持的 schema、owner、可编辑能力、文件预算。
- 读取：从 JSON 值解析为 owner 模型并返回统一诊断。
- 投影：返回 `FieldDescriptor`/`CollectionDescriptor`，只描述字段类型、约束和 i18n key，不携带控件颜色或自然语言。
- `editor::BuildFormProjection` 将 adapter 字段描述与 workspace 当前 JSON 合成为表单 projection；GUI 只消费 `path`、`value_type`、`label_key`、`recipe`、当前值和只读/必填状态，提交仍必须回到 `ProjectWorkspace::Apply`。
- `editor::BuildWorkspacePreview` 将 `DiagnosticSet` 转为结构化预览指标；诊断失败时只返回诊断，不生成伪造指标，GUI 不得自行统计项目内容。
- `editor::EditorSession` 持有 GUI 工作区状态，统一编排 `Open`/`Refresh`/字段选择/`Apply`/`Save`；它只把类型化字段值交给 `ProjectWorkspace`，不直接写文件或复制 validator 语义。
- 字段焦点由 `ui::UiContext` 按布局注册顺序管理；editor session 只把焦点 widget 映射为当前 projection 索引，不复制焦点环或 tab 排序规则。
- `editor::BuildShellDrawList` 消费布局节点的 bounds/recipe/label key，作为窗口绘制前的唯一 shell 几何投影入口。
- `editor::BuildFormDrawList` 消费 `FormProjection`、布局 bounds、theme 提供的行高和当前焦点索引，输出控件矩形与 label key；它不拥有字段约束或自然语言。
- `editor::BuildPreviewDrawList` 消费 `PreviewProjection` 与诊断面板 bounds，将结构化指标或诊断 code 投影为有界状态行；它不重新统计项目数据。
- `project::ProjectWorkspace::DescribeDocuments` 根据已打开的 `ProjectManifest` 引用和已注册 adapter 生成稳定文档目录；`editor::BuildDocumentTabsProjection`/`BuildDocumentTabsDrawList` 消费该目录，标签顺序、路径、可编辑性、dirty 和诊断数量均来自结构化状态。
- `editor::BuildDiagnosticPanelProjection` 将诊断按稳定文档 id 归属并支持 code/path 过滤；`editor::BuildDiffProjection` 按 workspace change sequence 输出确定性变更 projection。
- `project::ProjectWorkspace::SelectDocument` 只允许在无 pending changes 时切换到 manifest 引用且存在 adapter 的文档；切换后 `CurrentDocument` 与 `CurrentDocumentId` 始终成对更新，Apply/Commit 使用当前文档路径，未保存切换返回结构化诊断。
- editor host 只负责从 SDL 窗口取得平台句柄、创建 RHI 资源并消费 render packet；布局、主题和文案仍由版本化资源提供。
- 修改：把类型化 `EditCommand` 应用到候选文档；未知字段、只读字段和类型漂移立即拒绝。
- 验证：单文档 parser 后执行跨文档 validator；插件私有 adapter 最终仍调用插件 validator。

文档类型由 project manifest 引用和 adapter 声明解析，禁止继续由 GUI 根据文件名写 if/else。未知但合法的插件私有文档可只读显示并运行 validator；没有字段描述时不得退化为无约束自由 JSON 保存。插件 editor sidecar、字段描述和资源 namespace 的完整合同见 [插件系统规范](11-plugin-system.md)。

## 预览与进程 seam

- `BuildPreview` 只返回结构化摘要：地图尺寸、碰撞/交互/相机区域数量、事件和资源状态、可选场景快照引用。
- 真正运行项目由独立 process Adapter 发起，参数数组传递，不拼 shell 字符串；捕获 exit code、启动错误和有界日志。
- `editor::PreviewProcess` 是该 process Adapter 的最小实现：只接收已解析的 executable/project root，拒绝空路径和非普通文件/目录，提供 `Start`/`Poll`/`Stop` 与结构化状态。
- GUI 不嵌入 app 主循环，不共享可变 ECS、RHI device 或插件 session；编辑器崩溃不得带走项目运行进程，反之亦然。

## 完成门禁

- 本目录中的每一类文档都有 owner、parser、validator 或明确只读策略。
- `projecttool/main.cpp` 不再拥有项目语义，仅保留命令解析、格式化和退出码映射。
- CLI/GUI 对同一 fixture 的 snapshot、diagnostic code/path、change set 与保存结果完全一致。
- 未登记的新文档类型、面板私有 JSON 路径判断、stdout 解析和直接文件写入均作为阻断缺陷。
