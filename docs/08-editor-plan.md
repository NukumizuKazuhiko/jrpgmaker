# P13 编辑器 GUI 计划

> 状态：当前有效（已登记于 [README.md](README.md)）。本文件定义开发者项目编辑器的第一版边界、模块接口和验收顺序；它不修改 P12 运行时合同与当前平台完成定义。

## 目标

提供一个以 Windows/D3D12 为本轮实机平台、Linux/Vulkan 保持构建与合同测试的本地项目编辑器，让项目作者通过 GUI 打开、诊断和修改版本化数据，同时保持 CLI、数据 parser、validator、迁移器和运行时合同为唯一真源。

产品目标是服务现有运行时的 Unity 式游戏引擎编辑器，不是 JSON 表单编辑器。当前重新打开 P13，第一闭环：

`打开合法项目 → Project 资源 → Hierarchy 导航地图 → Scene 网格 → 共享 Selection → Inspector 修改 walkable → dirty/diff/diagnostics → ProjectWorkspace 校验与原子保存 → 重开复核 → 独立运行时预览`

## Owner 与 seam

- `tools/editor::EditorWorkspaceController` 是本轮编辑器应用 owner，统一装配当前项目、文档、Selection、各面板 projection、dirty/revision/diff、diagnostics、编辑命令和 preview process 状态；`EditorSession` 隐藏 `ProjectWorkspace`、adapter 与 preview process 的会话编排；`main.cpp` 只做 SDL/RHI 生命周期、系统目录对话框和事件转发。
- 项目文档与数据语义仍由 `engine/core`、`engine/domain`、`engine/plugin` 及对应 parser/validator 拥有。
- 新增 `tools/project` 库作为项目作者工具的公共 seam；当前藏在 `tools/projecttool/main.cpp` 的项目快照、诊断、diff、写回和迁移能力必须先迁入该深模块，CLI 与 GUI 都只能调用其结构化接口。
- `engine/ui` 只提供可复用的 CPU 控件/布局能力；编辑器不把编辑器状态写入运行时 domain。
- 只读运行时预览通过结构化快照或 `projecttool preview` 消费，不直接修改 GPU 资源。
- GUI 的组件外观、布局配方和所有自然语言分别由版本化 theme/layout/i18n 文件提供；C++ 中不得出现面向用户的颜色、字号、间距、控件文案或语言选择分支。

完整接口盘点见 [编辑器接口目录](09-editor-interface-catalog.md)，组件、主题与语言合同见 [编辑器 UI 系统合同](10-editor-ui-system.md)。两份合同在编码前一次性冻结 P13 第一闭环所需 seam，后续不得以“先在 GUI 里临时实现”为由绕开。

## 本轮进度记录（2026-08-31 至 2026-09-02）

本轮已按“先定 owner 与合同，再由 UI 拼装 projection”的 SDD 路线审查并推进最小闭环。产品目标保持为 Unity 式游戏引擎编辑器工作区，不退化为 JSON 表单编辑器；本次记录只覆盖导航网格编辑闭环，不扩展到通用 docking、角色/材质/渲染管线等后续能力。

- `tools/editor::EditorWorkspaceController` 继续作为编辑器工作区控制层，统一持有当前项目、文档、`SelectionTarget`、面板 projection、dirty/revision/diff、结构化 diagnostics、命令路由和 preview 状态。Project、Hierarchy、Scene、Inspector、Toolbar、Status 只消费 projection 并发送结构化 command。
- P13-0 的 `ProjectWorkspace` seam 保持不变：打开、解析、校验、revision、diff、`PrepareSave`/`Commit` 仍由 workspace 及现有 parser/validator 拥有；navigation adapter 继续是导航数据语义入口；GUI 不直接写文件、不修改裸 JSON、不解析 CLI 文本。
- Windows/D3D12 实机已完成一条真实操作链：打开合法临时项目，Project 显示真实文档，打开 Navigation 后 Scene 显示真实 5×5 网格；点击 `(1, 0)` 后 Hierarchy、Scene、Inspector 共享同一选择；Inspector 显示真实坐标和 `walkable`，切换后 dirty、revision、diff、diagnostics 可见；保存后状态清除，直接重开项目仍保持修改后的阻挡格状态；工具栏可启动并停止独立 runtime preview。窗口缩放、菜单/工具栏反馈和输入转发也已在真实窗口中检查，操作截图已留存于本轮验收记录。
- RmlUi 工作区已补齐 Unity 风格的三条可拖拽分隔条：Project/Hierarchy 与 Scene、Scene 与 Inspector 的垂直分隔，以及 Scene/Inspector 与 Diagnostics 的水平分隔。分隔条使用稳定 id、`role=separator`、方向和当前比例 ARIA 属性；比例、拖拽捕获和释放只保存在 editor UI 会话，重建文档与窗口 resize 会保留比例并以非负几何重新布局，极小窗口不会写入项目数据。
- 分隔条比例、四个周边面板可见性与左侧活动标签现由 `tools/editor::EditorUserSettings` 负责持久化：schema v3 的 `splitters`、`visibility` 与 `active_left_panel` 使用稳定 id，四个可见性默认均为 true，默认活动页为 Project；loader 对 schema v1/v2 做显式迁移，对缺失、类型错误、非有限值、越界和未知 schema 回退完整默认值并产生结构化诊断。View 只提供类型化导入/导出、会话切换和 DOM 重排，不访问文件。拖拽释放、Window 面板切换及 Project/Hierarchy 标签激活均复用 host 的 `persist_user_settings` 原子保存链路，个人设置永不写入项目目录、theme/layout 或 domain。
- Window 菜单提供 Project、Hierarchy、Inspector、Diagnostics 四个 `role=menuitemcheckbox` 面板切换项，Scene 常驻；命令统一为 `panel.toggle` 加稳定面板 id。Layout 二级菜单的 `layout.reset_default` 同时恢复四面板可见与三条 splitter，并立即复用 `persist_user_settings` 原子保存链路，`EditorWorkspaceController` 不拥有布局语义。隐藏面板时相邻主区延展且对应 splitter 消失，恢复后使用保存的 splitter 比例。
- Hierarchy 使用现有 `NavigationProjection` 的有界 cells 构成真实树：地图根可展开/折叠，最多投影共享合同允许的 2048 个单元；每行通过稳定 index 发送 `navigation.select`，选择继续由 `EditorSession::SelectionTarget` 统一投影到 Hierarchy、Scene 与 Inspector。展开状态仅由 `RmlUiEditorView` 保存为会话状态，树容器独立滚动，不进入项目数据、controller 或个人设置。
- 本轮最小修复补齐了失败文档命令的可见反馈：文档选择失败时，`EditorWorkspaceController` 重新投影结构化 diagnostics 并请求重绘，不把失败状态留在 session 内而不刷新 UI。对应合同测试为 `editor workspace controller redraws after a rejected document command`，已完成 red→green。
- 在进度快照之后的延续轮次中补齐三项 Unity 式工作区能力（均先红测后实现，未扩大 owner 边界）：聚焦面板最大化/还原——点击面板记录 session-only `focused_panel`，Shift+Space 或 Window → Maximize Focused Panel 在目标可见时铺满 toolbar 与 statusbar 之间区域并临时隐藏其他面板与 splitter，还原后恢复原 visibility 与比例；左侧 dock 的 Project/Hierarchy 标签化——`dock.activate` 切换活动页并立即持久化，两页都可见时也只渲染活动页；空项目首屏提供可聚焦 `file.open` 按钮，新启动的编辑器不再只能靠 CLI 路径进入。
- Hierarchy 树实机验收暴露并收敛了 RmlUi 渲染边界：`button` 是替换型控件不渲染内部子节点、flex/overflow 组合触发 Debug abort、`inline-block + nowrap` 裁剪标签；最终采用块级全宽 treeitem 行、父级裁切与默认 inline 文本流，并为 `hierarchy-tree/tree-children` 声明 `width: 100%` 使行占满左栏。这些约束已作为编辑器 RCSS 子集写入 [UI 系统合同](10-editor-ui-system.md)。
- 2026-09-02 收口复验证据：`cmake --build --preset win-debug` 全目标成功且 warning 为 0；Windows 全量 CTest `387/387` 通过；`pwsh ./tools/ci/check_private_headers.ps1` 报告 `OK (210 files scanned)`；`pwsh ./tools/ci/check_docs_index.ps1` 报告 `OK (19 local links; 16 current targets tracked)`；`git diff --check` 通过。本轮此前各子轮的定向证据（editor/RmlUi/layout/input 定向 `82/82`、splitter 行为 2 用例 25 断言等）由对应实现轮次留存。
- Linux/Vulkan `jrpgmaker_unit_tests` 目标构建成功；项目/插件/工作区相关定向测试已通过，面板显隐轮 Linux 定向测试亦通过。2026-09-02 收口时已在 WSL2（lavapipe 离屏 Vulkan）运行 Linux 全量 `ctest --preset linux-debug`：388/388，0 失败，构成本轮最终 Linux 全量门禁闭合证据（总数比 Windows 多 1 项为平台专属测试）。
- 2026-09-02 截图文件化记录已落盘为 [`assets/p13/2026-09-02/`](assets/p13/2026-09-02/01-project-loaded.png) 下 15 张截图——项目加载、文件菜单打开/关闭、窗口菜单与布局二级菜单、根菜单切换、Project 过滤命中（"nav"→仅显示导航文档）、导航 5×5 网格、共享 Selection（坐标 (1,0)）、Inspector `walkable` 切换后的 dirty/revision/差异诊断、保存后状态清除、预览运行中/停止（独立 `jrpgmaker_app` 进程退出）、聚焦面板最大化/还原。GUI 保存结果另经 `projecttool validate` 与数据文件复核（`navigation_demo.json` `walkable[1]=false`）确认与 CLI 同源一致。2026-09-04 集成审查确认其中弹出菜单被 Project 搜索框局部遮挡违反本页冻结的 GUI 验收合同；`#menubar` 现建立高于 workspace panel/splitter 的顶层 stacking context，并由 `[editor][rmlui][menu][z-order]` 回归测试锁定。
- 2026-09-05 菜单层叠修复已在 Windows 真实窗口复验：以 `5bc7d3b` 构建 `win-debug` editor，打开 `C:\Users\Vens_\AppData\Local\Temp\jrpgmaker_p13_gui_menu_20260905_1700`（`project.json` SHA-256 `cf110bbdac834ad17bcc45ceb90951296904c41db847ef9a19da109a1eabc671`），`projecttool validate` 退出 0；展开 Project 菜单时，弹层在与 Project filter 重叠的区域保持完整可见，随后点击“项目概览”成功关闭菜单并刷新诊断。当前截图为 [`assets/p13/2026-09-05/01-project-menu-above-project-filter.png`](assets/p13/2026-09-05/01-project-menu-above-project-filter.png)，1282×752、253542 bytes、SHA-256 `83012b51ed76f445535f294e27009c07d0c9aa90d494a09b194b0c56d4fa11bb`。该证据只闭合具体菜单遮挡复验，不替代窗口 resize 命中、错误态与保存重开的一致性证据。
- 2026-09-06 Windows 真窗布局复验：同一 fixture（`project.json` SHA-256 `cf110bbdac834ad17bcc45ceb90951296904c41db847ef9a19da109a1eabc671`）经 `projecttool validate` 返回 0；三个定向用例此前已通过——`Editor host reflows the workspace when its pixel size changes`（1 case / 5 assertions）、`RmlUi editor splitters capture drags and preserve ratios across refresh and resize`（1 case / 16 assertions）、`editor workspace controller keeps project scene hierarchy and inspector selection shared`（1 case / 14 assertions）。Windows 真实窗口初始为 1282×752，导航文档点击 `(0,0)` 后 Scene/Inspector 同步；标题栏空白双击触发原生最大化，窗口变为 2560×1400，Project、Hierarchy、Scene、Inspector、Diagnostics、Status 均保持可见。旧布局 `(4,4)` 位置 `(943,592)` 在新几何中实际命中 `(1,2)`；按新 Scene 范围计算并点击 `(4,4)` 中心 `(1892,1093)`，再激活 Hierarchy 页，Hierarchy 行、Scene 橙色格与 Inspector 坐标三者均为 `(4,4)`。证据文件如下：`02-before-resize-cell-0-0-selected.png`（1282×752，65267 bytes，SHA-256 `344b34e8242259df722491585cff7b22c04c1b37af305cf8c3577da1516064f7`）、`03-after-resize-layout.png`（2560×1400，129047 bytes，SHA-256 `9cf0349607533910c32959688715e52634809217c9a454f3aa624736983e0334`）、`04-after-resize-stale-point-not-cell-4-4.png`（2560×1400，127617 bytes，SHA-256 `719d0646f555f17d8cca51be56c1ec33a5325dd008b80d7552aa504ec667a96b`，Inspector 实际 `(1,2)`）、`05-after-resize-cell-4-4-selected.png`（2560×1400，147173 bytes，SHA-256 `1234b9d4dc4bc9cca620a462e9ef5541cf1bbcc3b62376545ba47c661cd01e55`）。本证据闭合的是原生窗口尺寸变化后的布局重排与命中复验；错误态、保存重开和 P13 后续功能仍保持独立范围。
- 2026-09-04 集成审查暂停原收口声明；2026-09-05 已补齐菜单层叠真窗证据，但本轮其余阻断审查闭合前仍不恢复“第一条导航编辑闭环完成”声明，也不进入 docking、其他文档编辑器或 P13 后续功能。P13-5 的创建→编辑→校验→构建→运行→迁移发布回归与 DEBT-042 构建可选性仍开放，作为后续独立轮次入口。

## 本轮唯一范围

1. 打开合法项目，Project 显示 `ProjectWorkspace::DescribeDocuments` 的真实资源，并按数据合同提供资源分类与有界搜索；Hierarchy 显示当前导航地图及可选择单元。
2. Scene/Map View 显示 parser 产生的真实导航网格；单元投影最多 2048 个，超出部分裁剪，保证单帧 DrawList、Diagnostics 与 Status 仍有容量。
3. Project、Hierarchy、Scene 和 Inspector 共享一个稳定 `SelectionTarget`；Inspector 只显示真实坐标并发送 walkable 切换命令。
4. 修改统一进入 navigation adapter 与 `ProjectWorkspace::Apply`，dirty、revision、稳定 diff 和结构化 diagnostics 从同一会话状态投影。
5. 保存只调用 `PrepareSave`/`Commit` 的统一写回链；`PrepareSave` 在检查 revision 后对完整项目 working copy 执行同源 `Diagnose`，包括插件 sidecar 的运行时 validator 输入，重开复核不能替代该门禁。
6. Toolbar 提供打开、保存、运行、停止；runtime preview 始终为独立进程。
7. 空项目、加载失败、校验失败和保存失败均显示结构化状态；窗口 resize 后由共享布局重新计算命中区域。
8. 菜单栏和工具栏采用 Unity 式工作区信息架构：小功能进入二级菜单，菜单项只映射到已存在的结构化 command；Project Settings 只暴露已有 document adapter（项目、材质/渲染、相机、输入、本地化和插件数据）。
9. `EditorWorkspaceController` 是本轮唯一编辑器应用 owner；`engine/ui::MenuController` 只拥有通用菜单模型、布局、键盘/鼠标路由和 DrawList 投影，不拥有项目语义。角色调整暂不提供菜单入口，因为当前只有运行时 `CharacterController`，没有项目文档、parser、validator 和 adapter 合同。

## 非目标

- 不做通用 3D 建模器、DCC、联网协作、云端项目格式或二进制热加载。
- 不把 GUI 作为运行时启动依赖；目标上关闭 `tools/editor` 后不得影响核心构建、测试和发布包。当前根 CMake 无条件加入该目录，单元测试也直接链接 `jrpgmaker_editor_host`，所以构建层可删除性尚未成立（DEBT-042）。
- 不通过自由 JSON 文本编辑绕过 parser、validator、迁移器、资源预算或插件私有校验。
- 编辑器 UI 使用已登记的 RmlUi Core（仅 `tools/editor`，CMake 固定 commit）；不把 RmlUi 引入 `engine/*`、运行时或项目数据 owner。

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
- `tools/editor` 的 layout 已转换为独立 shell projection，输入映射使用不依赖 SDL 的 `InputMap` 合同；key binding 来自版本化 editor action 资源。
- editor action map 已纳入启动 manifest，由 `engine/ui` 有界解析并由 host 构造 `InputMap`；重复 key 和非法 action 资源在启动阶段拒绝。
- `engine/ui::MenuController` 已提供有界 MenuBar/PopupMenu 模型、根菜单与任意一层受限子菜单、鼠标命中、Alt/方向键/Enter/Escape 路由和结构化 `UiCommand`；菜单资源仍由 layout 节点的 `command` 字段声明，host 只负责将 command 映射到真实 session 操作。
- editor shell 已提供 Unity 式顶部菜单栏、工具栏和窗口聚焦命令；Project projection 从 `DocumentDescriptor.category_key` 生成分类头、搜索行和有界资源行，显示的项目文档、路径、类型与诊断计数均来自 workspace/session projection。
- 编辑器 host 使用 RmlUi DOM/RCSS、SDL 输入转发和项目自有 RHI `RenderInterface`；`engine/ui` 的有界 `UiTree`/`DrawList` 合同继续服务运行时与遗留测试。当前已完成主题字体候选、FreeType 灰度栅格化、CJK glyph atlas、实际文字/矩形提交、Project/Hierarchy/Scene/Inspector/Diagnostics/Toolbar/Status 纵向切片；显式 docking/z-order 系统不属于本轮导航闭环。
- 无项目启动状态保留 Unity 式 editor shell 的首个入口：空态使用现有 `editor.action.open` 本地化文案投影为可聚焦 `file.open` 按钮；鼠标点击及 Tab 后 Enter/Space 均通过 RmlUi 结构化 command 交给 host，由 host 映射到目录选择对话框，视图不直接执行文件 I/O。
- 表单区域已由布局资源中的 `Form` 节点提供边界；adapter 字段被投影为有界控件行，焦点状态通过 theme recipe 的 `focused` 状态绘制，SDL 键盘、文本输入和表单鼠标命中通过 `EditorSession` 进入类型化 Apply。
- 已实现命令行初始项目路径、SDL3 异步目录选择、工作区加载、字段值/诊断码（含路径）/预览指标的 locale 参数 projection，以及带 bounded 日志的错误面板和资源化状态栏。
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
 - 目标保存门禁是在 `ProjectWorkspace::PrepareSave` 内对完整工作副本执行 parser、跨文件引用、插件 validator、资源预算和迁移检查；任何 error 不签发 token。当前实现已在签发 token 前调用同源 `Diagnose`，核心项目文档的 parser、跨文件引用、本地化覆盖、已注册 adapter/plugin validator error 以及插件 sidecar working copy 的运行时 validator error 都会阻断保存；注册 editor descriptor 未提供专用 validator 时，workspace 还会校验其声明的 object、required、字段类型与 select choices 约束。
- 验收：GUI 保存结果可被 CLI 无损打开；非法引用、越界值和插件 validator error 在保存前阻断且原文件不变；崩溃/取消不破坏原文件。

 当前进度：integer `NumberField` 已通过文本输入和有限步进进入统一 `ProjectWorkspace::Apply`；导航宽高由 adapter 与 `walkable` 原子归一化并记录复合 diff；布尔字段已提供资源化 `toggle` action 与类型化切换 seam；select 字段已支持 adapter 提供有界候选值并由左右动作循环提交。跨文档工作副本、原子写入与失败回滚已落地，标签页 dirty 状态按文档来源变化；`PrepareSave` 已在签发 token 前执行完整核心项目 `Diagnose`，对没有专用 validator 的 editor descriptor 执行声明约束校验，并让插件 validator 消费 sidecar working copy，非法核心或插件文档不再写盘。

### P13-4 地图/材质/插件扩展

- 在已有数据合同上增加交互点、碰撞、导航、相机区域和材质实例编辑。
 - 加载并 lint `plugin.editor.json`、字段描述、插件 locale/icon 资源；sidecar 清单与 descriptor 的 schema、contract、插件 ID、roots、路径、重复项和数量上界校验已由 plugin owner 提供，descriptor 到 project editor adapter 的类型化转换已落地。sidecar 资源存在性、路径 containment 和单文件/总字节上界已落地；`ProjectWorkspace` 可注入 `PluginRegistry` 并在 Diagnose 阶段运行插件 validator，sidecar working copy 通过有界 overlay reader 进入同一运行时 validator 输入；EditorSession 已按项目插件列表发现并装载相邻 sidecar/descriptor，使用其私有有界遍历 helper 扫描每个插件数据根最多 128 个 entry、递归深度最多 16 层，并以 `editor.document_root.entry_budget`/`editor.document_root.depth_budget` 结构化诊断暴露触顶，再生成最多 128 个 JSON 文档标签页。有效 descriptor 的标签页可编辑；sidecar/descriptor 缺失或无效时，私有文档通过只读 adapter 投影并附带可定位诊断，所有编辑入口拒绝写回。字段描述只生成类型化 EditCommand；缺少专用 validator 时，workspace 会从 descriptor 生成最小声明约束校验，插件私有 schema 仍不得提升为核心 schema。
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
- P13-0 的 `ProjectWorkspace` seam 保持有效，不推倒重建。只有本页“本轮唯一范围”的完整 Windows 实机操作、Windows/Linux 门禁和文档证据都闭合后，才允许声明“第一条导航编辑闭环完成”；随后立即停止，不进入 docking、其他文档编辑器或 P13 后续功能。2026-09-02 的历史收口声明已由 2026-09-04 集成审查暂停，当前状态以本页进度记录末条为准。
- 本轮停止条件还包括：菜单打开/关闭、二级菜单切换、Project 过滤、窗口聚焦和工具栏反馈在真实 Windows 窗口中可观察；未有数据合同的角色、通用渲染管线资产导入和任意资源编辑不得以假入口提前接入。完成后立即停止，不继续扩展 Unity 重合功能。
