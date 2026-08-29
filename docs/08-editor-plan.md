# P13 编辑器 GUI 计划

> 状态：当前有效（已登记于 [README.md](README.md)）。本文件定义开发者项目编辑器的第一版边界、模块接口和验收顺序；它不修改 P12 稳定运行时的完成定义。

## 目标

提供一个 Windows/Linux 优先的本地项目编辑器，让项目作者通过 GUI 创建、打开、诊断和修改版本化数据，同时保持 CLI、数据 parser、validator、迁移器和运行时合同为唯一真源。

第一闭环：

`创建项目 → 打开项目 → 查看诊断 → 编辑一个合法数据字段 → 预览 diff → 校验 → 原子保存 → CLI 校验 → app 启动`

## Owner 与 seam

- `tools/editor` 是 GUI Adapter，只负责窗口、输入、面板状态、命令映射和结构化结果展示。
- 项目文档与数据语义仍由 `engine/core`、`engine/domain`、`engine/plugin` 及对应 parser/validator 拥有。
- 新增 `tools/project` 库作为项目作者工具的公共 seam；当前藏在 `tools/projecttool/main.cpp` 的项目快照、诊断、diff、写回和迁移能力必须先迁入该深模块，CLI 与 GUI 都只能调用其结构化接口。
- `engine/ui` 只提供可复用的 CPU 控件/布局能力；编辑器不把编辑器状态写入运行时 domain。
- 只读运行时预览通过结构化快照或 `projecttool preview` 消费，不直接修改 GPU 资源。
- GUI 的组件外观、布局配方和所有自然语言分别由版本化 theme/layout/i18n 文件提供；C++ 中不得出现面向用户的颜色、字号、间距、控件文案或语言选择分支。

完整接口盘点见 [编辑器接口目录](09-editor-interface-catalog.md)，组件、主题与语言合同见 [编辑器 UI 系统合同](10-editor-ui-system.md)。两份合同在编码前一次性冻结 P13 第一闭环所需 seam，后续不得以“先在 GUI 里临时实现”为由绕开。

## 第一版范围

1. 项目工作区：打开/创建项目，显示 schema、插件、数据文件和校验状态。
2. 诊断面板：展示事件、交互点、导航、碰撞、相机、日程和资源引用错误，并保留文件/字段路径。
3. schema-aware 表单：先覆盖项目 manifest、日期/日程、事件文本与触发点；地图和材质编辑在同一保存合同下逐步加入。
4. 变更安全：编辑前保留原始快照；保存前显示稳定 diff；写回使用现有临时文件、备份和失败恢复策略。
5. 只读预览：调用已有诊断/preview 入口显示结构化场景摘要；不在编辑器内私造运行时结论。
6. 插件扩展：插件按 [插件系统规范](11-plugin-system.md) 提供相邻的 `plugin.editor.json`、字段描述和 namespaced i18n 资源；插件私有数据仍必须由插件 validator 校验，战斗规则和渲染风格不进入核心编辑器语义。

## 非目标

- 不做通用 3D 建模器、DCC、联网协作、云端项目格式或二进制热加载。
- 不把 GUI 作为运行时启动依赖；删除 `tools/editor` 不得影响核心构建、测试和发布包。
- 不通过自由 JSON 文本编辑绕过 parser、validator、迁移器、资源预算或插件私有校验。
- 不引入新的 GUI 第三方依赖，除非先在 `docs/01-architecture.md` 登记并获得明确确认。

## 实施阶段

### P13-0 公共 seam 与资源合同（编码前置门禁）

- 将 `projecttool/main.cpp` 的项目加载、诊断、diff、迁移和原子写回实现抽到 `tools/project`，统一返回结构化结果；CLI 仅负责 argv/stdout/exit code 映射。
- 建立文档 adapter registry，把现有 parser、跨文件 validator 和插件 validator 注册为数据类型 adapter；禁止继续按 GUI 面板或终端文本分叉验证逻辑。
- 落地独立的 editor theme、layout、i18n schema 与 loader；编辑器启动资源缺失时返回结构化启动诊断，不以内置英文文案兜底。
- 验收：CLI 行为与退出码不变；共享模块测试覆盖正常/损坏/越界项目、稳定 diff 和原子恢复；主题、布局、语言包坏 schema 均在创建窗口前被拒绝。

### P13-1 编辑器壳与工作区

- 新增独立 `tools/editor` 可执行目标，使用 SDL3 窗口、已验证的 editor theme/layout/i18n 资源，并通过 `ProjectWorkspace` 完成可选项目打开与诊断；SDL 事件只由 host 消费，业务语义仍由 workspace/domain 提供。
- `editor::EditorSession` 统一持有工作区打开状态、字段选择、诊断/预览 projection、revision 和 dirty 状态；GUI 动作通过该 adapter 进入 `ProjectWorkspace::Apply` 与 `PrepareSave`/`Commit`。
- `PreviewProcess` 通过参数数组启动独立 runtime，Windows 使用 `CreateProcess`、Linux 使用 `posix_spawn`，编辑器只消费有界运行状态和日志，不共享 runtime 的 ECS/RHI 或可变文件句柄。
- `tools/editor` 的 layout 已转换为独立 shell projection，输入映射使用不依赖 SDL 的 `InputMap` 合同；具体 key binding 由后续版本化 editor action 资源提供。
- editor action map 已纳入启动 manifest，由 `engine/ui` 有界解析并由 host 构造 `InputMap`；重复 key 和非法 action 资源在启动阶段拒绝。
- `engine/ui` 已提供有界后端无关 `DrawList`（矩形、localized text 参数与 glyph quad）；`render` 已分别编译面板 NDC packet 和带 atlas UV 的 sampled-text packet，并由两个 RHI pipeline 上传/绘制。editor host 已完成 SDL 窗口、D3D12/Vulkan swapchain、主题字体候选、FreeType 灰度栅格化、CJK glyph atlas、CPU 文本裁剪和一帧实际文字提交；显式 z-order 与多字体逐字 fallback 仍待实现。
- 表单区域已由布局资源中的 `Form` 节点提供边界；adapter 字段被投影为有界控件行，焦点状态通过 theme recipe 的 `focused` 状态绘制，SDL 键盘、文本输入和表单鼠标命中通过 `EditorSession` 进入类型化 Apply。
- 已实现命令行初始项目路径、工作区加载、字段值/诊断码/预览指标的 locale 参数 projection；项目路径选择、状态栏和完整错误面板仍待实现。
- 验收：空项目、正常项目和损坏项目均能显示结构化状态；关闭编辑器不修改文件。

### P13-2 文档模型与诊断面板

- 在 P13-0 的公共 seam 上完成文档标签页、诊断筛选、字段定位和稳定 diff projection；GUI 不解析终端文本。
- 面板只消费结构化诊断，不解析终端文本。
- 验收：同一项目由 CLI 和 GUI 产生相同诊断摘要与字段路径。

当前进度：workspace 的结构化诊断与预览指标已经进入 DrawList，并能通过 locale 参数显示；manifest 驱动的文档标签、诊断 code/path 过滤与文档归属、稳定 diff projection 已接入，鼠标可切换有 adapter 的文档标签并点击诊断定位文档/字段。workspace/editor 现可切换已登记且有 adapter 的数据文档，并通过对应 schema-aware 字段编辑和保存；输入动作、本地化、材质、资源清单也已进入 adapter registry，整数步进命令和资源化增减绑定已接入，导航宽高与 `walkable` 已由 adapter 原子调整并记录复合 diff；布尔字段已提供资源化 `toggle` action 与类型化切换 seam；select 字段已支持 adapter 提供有界候选值并由左右动作循环提交。工作副本按文档保留，多个文档可在同一编辑会话中修改，提交阶段先写入全部临时文件，再统一替换并在失败时回滚。
预览进程状态现已投影为运行中/退出码/错误文案及有界 stdout/stderr，并只在轮询状态变化时请求重绘；GUI 以本地化状态行显示结果日志。
`ProjectWorkspace::Diagnose` 现读取并校验 manifest 引用的材质、输入动作、本地化和资源清单，并对事件脚本执行本地化覆盖检查；诊断不再只覆盖地图五件套。

### P13-3 第一组 schema-aware 编辑

- 先实现 project manifest、日历/日程和事件文本字段编辑。
- 保存前执行 parser、跨文件引用、插件 validator、资源预算和迁移检查。
- 验收：GUI 保存结果可被 CLI 无损打开；非法引用和越界值在保存前阻断；崩溃/取消不破坏原文件。

当前进度：integer `NumberField` 已通过文本输入和有限步进进入统一 `ProjectWorkspace::Apply`；导航宽高由 adapter 与 `walkable` 原子归一化并记录复合 diff；布尔字段已提供资源化 `toggle` action 与类型化切换 seam；select 字段已支持 adapter 提供有界候选值并由左右动作循环提交。跨文档工作副本和原子提交已闭合，标签页 dirty 状态按文档来源变化。

### P13-4 地图/材质/插件扩展

- 在已有数据合同上增加交互点、碰撞、导航、相机区域和材质实例编辑。
- 加载并 lint `plugin.editor.json`、字段描述、插件 locale/icon 资源；字段描述只生成类型化 EditCommand，保存仍调用运行时插件 validator，不把插件私有 schema 提升为核心 schema。
- 验收：替换数据即可改变运行时项目；核心 domain、RHI 后端和 app 业务分支不改。

### P13-5 只读运行预览与发布回归

- GUI 通过 `editor::BuildFormProjection` 展示 adapter 驱动表单，并通过 `editor::BuildWorkspacePreview` 展示 workspace 诊断指标；必要时启动独立 app 进程验证项目。
- 发布包、CLI、运行时和编辑器分别构建，编辑器不进入发布包。
- 验收：Windows 与 Linux 完成创建→编辑→校验→构建→运行→迁移；P12 全量测试和发布包门禁保持通过。

当前进度：`PreviewProcess` 已提供有界独立进程启动 seam，editor 可触发预览；运行中/退出码/启动错误及限长 stdout/stderr 已进入 GUI projection。Windows 本机已验证发布包重复装配确定性、包根 `project.json` 和 `eventlint --check-project`；跨文档提交已覆盖多文件临时写入、备份、替换和失败回滚。

## 测试与停止条件

- 核心文档模型、diff、校验和写回使用公开接口测试，不测试控件私有实现；UI 测试通过命令、状态和 draw list 穿过同一控件 seam。
- 主题测试至少覆盖默认/高对比两份 theme；i18n 测试至少覆盖 `zh-CN`/`en`、缺 key、命名参数、UTF-8/CJK 换行和运行时切换。
- GUI 使用可重复的临时项目 fixture 验证打开、编辑、取消、非法数据、原子保存和恢复。
- Windows 优先；Linux 使用 WSL 做构建和无窗口合同测试，真实 SDL 窗口需要 WSLg/桌面环境，不伪造为本地通过。
- clang-format、`git diff --check`、私有头审计、全量 CTest、data-lint 和 P12 发布启动回归必须保持通过。
- P13-0 未闭合不得开始窗口实现；P13-1 至 P13-3 完成后才允许声明“编辑器第一闭环完成”；P13-4/5 未完成时不得声称完整编辑器交付。
