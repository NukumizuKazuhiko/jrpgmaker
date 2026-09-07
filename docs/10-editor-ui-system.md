# P13 编辑器 UI 系统合同

> 状态：当前有效（登记于 [README.md](README.md)）。本文定义编辑器 GUI 的组件行为、主题、布局和 i18n seam。目标是允许开发者替换语言与视觉配置，而不修改 C++、项目数据或运行时语义。

## 不硬编码合同

C++ 可以实现通用控件行为，但不得硬编码以下内容：

- 面向用户的标题、标签、按钮、菜单、提示、空态、错误描述、日期/数字展示文本。
- RGBA/十六进制颜色、字体文件、字号、间距、圆角、边框、控件高度、图标资源和动画时长。
- `if (locale == ...)` 语言分支、按语言选择窗口布局或直接把 i18n key 当最终文案。
- 为某个项目文档写死的面板位置、字段标签或 filename 分支。

允许在代码中固定的只有：schema 版本、资源/集合上界、控件状态机、键盘语义、最小可访问性约束和稳定错误 code。所有自然语言通过 `LocalizedText{key, named_args}` 进入 projection。

## 四类独立资源

编辑器安装资源与游戏项目资源隔离，默认位于 `tools/editor/resources/`，发布时复制到编辑器包：

```text
editor.json                         # 启动清单：默认 theme/locale/layout 与 fallback 顺序
actions/editor.json                 # 版本化 action id 到 key name 映射
themes/editor_default.json          # 视觉 token + component recipes
themes/editor_high_contrast.json    # 第二份合同 fixture
layouts/editor_workspace.json       # 工作区区域和组件组合
locales/zh-CN.json                  # 中文语言包
locales/en.json                     # 英文语言包
```

四类文件都声明 `schema: 1`、稳定 `id`，并受文件大小、条目数、字符串长度、嵌套深度和重复 id 上界约束。启动顺序为 manifest → locale → theme → layout → font；任一步失败都返回结构化 `EditorStartupDiagnostic`，不创建半初始化窗口。

游戏项目的 `theme_demo.json`/`localization_*.json` 继续服务运行时 presentation；editor 资源使用独立 schema 和 namespace，禁止互相 fallback。

## 启动清单

`editor.json` 只做资源选择，不携带控件或业务语义：

```json
{
  "schema": 1,
  "id": "editor.desktop",
  "default_locale": "zh-CN",
  "locale_fallbacks": ["en"],
  "default_theme": "editor.default",
  "default_layout": "editor.workspace",
  "available_locales": ["zh-CN", "en"],
  "available_themes": ["editor.default", "editor.high-contrast"]
}
```

用户设置只保存所选资源 id、窗口位置/尺寸、布局比例与周边面板可见性，不复制主题或文案。当前合同由 `tools/editor::EditorUserSettings` 实现：schema v3 的 `splitters` 仅允许 `splitter-left-center`、`splitter-scene-inspector`、`splitter-diagnostics`，`visibility` 仅允许 `workspace.project`、`workspace.hierarchy`、`workspace.inspector`、`workspace.diagnostics`，`active_left_panel` 只允许 `workspace.project` 或 `workspace.hierarchy`；四个 visibility 默认均为 true，默认活动页为 Project。schema v1 通过显式迁移保留比例并补齐 visibility 与活动页，schema v2 保留比例和 visibility 并补齐活动页；字段缺失、类型错误、非有限值、越界值、坏 JSON、未知 schema 或超过 64 KiB 均产生结构化诊断并回退完整默认设置。默认值唯一来自 `EditorUserSettings{}`；RmlUiEditorView 仅接收/导出该 typed settings、切换会话状态并重排 DOM，不做文件 I/O。路径由 SDL 偏好目录 Adapter 提供；项目目录内不写编辑器个人设置。写入采用临时文件加原子替换，替换失败保留旧设置。Window 菜单的四个 panel checkbox 发送 `panel.toggle` 与稳定面板 id；Scene 常驻。Project 与 Hierarchy 共用左侧 dock：`dock.activate` 切换活动标签并立即保存，两页都可见时也只渲染活动页；隐藏活动页会选择另一可见页，重新显示一页会激活该页。Window → Layout → Reset Default Layout（中文为“窗口 → 布局 → 恢复默认布局”）发送 `layout.reset_default`，host 同时恢复四面板可见、Project 活动页与三条 splitter 并立即保存。

RmlUiEditorView 还持有不进入 `EditorUserSettings` 的 session-only `focused_panel` 与最大化目标。点击 Project、Hierarchy、Scene、Inspector 或 Diagnostics 面板记录最近焦点；Shift+Space 或 Window → Maximize Focused Panel（再次触发为 Restore）仅在目标仍可见且存在时切换。最大化把目标铺满 toolbar 下沿至 statusbar 上沿，暂时隐藏其他工作区面板和全部 splitters；还原重新应用原 visibility 与 splitter ratios。Reset Default Layout、隐藏当前目标、目标缺失或 markup 重建失败均安全退出最大化。普通 Space 仍交给 RmlUi 控件与文本输入。

## 主题 schema

现有 `ui::Theme{id, accent, text_pixel_height}` 只够运行时 demo，不能作为编辑器主题。P13 新增独立 `EditorTheme` loader；它可以复用解析工具，但不得扩张运行时 Theme 的语义。

主题由三层组成：

1. primitives：命名颜色、字体、space、radius、stroke、duration。
2. semantic tokens：`surface.canvas`、`surface.panel`、`text.primary`、`text.error`、`focus.ring` 等语义角色。
3. component recipes：控件各状态引用 semantic token，不直接写颜色或像素。

第一版必需 token：

| 类别 | token |
|---|---|
| surface | `canvas`、`panel`、`raised`、`input`、`selection`、`overlay` |
| text | `primary`、`secondary`、`disabled`、`accent`、`warning`、`error`、`success` |
| border/focus | `subtle`、`strong`、`focus.ring`、`invalid` |
| spacing | `xs`、`sm`、`md`、`lg`、`xl` |
| typography | `body`、`label`、`caption`、`heading`、`code` |
| motion | `fast`、`normal`、`slow`、`focus.spring` |

默认主题优先使用项目宪法色卡中的石墨黑、炭灰、冷银灰、骨白、电光青、警戒红和酸性绿；具体值只出现在主题 JSON，源码与 layout 不重复这些值。

每个 recipe 至少声明 normal/hover/pressed/focused/disabled；输入类控件还声明 invalid/read-only。引用不存在、循环 alias、非法颜色、负尺寸、字号超限或对比度低于合同阈值时主题加载失败。高对比 theme 是强制 fixture，用于证明组件没有私有颜色。

## i18n schema 与解析

编辑器语言包与现有 domain localization 使用相同的 key-value 思路，但 namespace 和 loader 独立：

```json
{
  "schema": 1,
  "locale": "zh-CN",
  "strings": {
    "editor.window.title": "JRPGMaker 编辑器",
    "editor.action.open": "打开项目",
    "editor.diagnostic.count": "发现 {count} 项诊断"
  }
}
```

合同：

- key 使用 `editor.<area>.<name>`；owner 模块只发布 key 和命名参数。
- 第一版仅支持 `{name}` 命名占位符和 `{{`/`}}` 转义，不引入位置参数、运行时代码格式串或语言特定 if/else。
- 每个 locale 必须与启动清单登记的基准 key 集一致；缺 key、额外孤儿 key、重复 key、无效 UTF-8、未知占位符或不同语言占位符集合不一致都在资源 lint 阶段报错。
- fallback 顺序完全来自 `editor.json`。全部 fallback 失败时 projection 显示稳定 key 并记录 diagnostic code，不能临时嵌入英文。
- locale 可在运行时切换；切换使文本测量、布局和 draw list 失效重建，不重开项目、不丢编辑状态。
- 路径、数字、日期和按键名先形成类型化参数，再由 locale formatter 格式化；P13-1 只实现字符串/整数/路径，复数和复杂日期格式另立 schema 版本。

## 布局清单

`layouts/editor_workspace.json` 声明区域组合，不声明颜色和自然语言：

```text
root SplitPane(horizontal)
├─ left: WorkspaceTree
└─ right SplitPane(vertical)
   ├─ center: DocumentTabs
   └─ bottom: DiagnosticPanel
status: StatusBar
```

节点只含稳定 component type、instance id、i18n key、theme recipe id、子节点和受限布局参数。未知 component type、重复 id、循环引用、非法比例或超出最大节点数必须拒绝。布局清单不允许脚本、任意文件路径、项目 JSON pointer 或业务条件表达式。

## 组件目录

### 通用控件行为

运行时通用控件仍由 `engine/ui` owner；编辑器工作区的 retained-mode 控件由 `tools/editor::RmlUiEditorView` 通过 RML/RCSS 提供。两者共享文案、主题和数据 owner 边界，但不共享第二份项目状态。

| 组件 | 状态/命令 | 第一进入阶段 |
|---|---|---|
| `Text` | localized text、选择禁用、测量结果 | P13-0；编辑器由 RmlUi 文档消费 |
| `Panel` | children、padding、recipe | 已有基础，P13-0 主题化；编辑器由 RCSS 主题化 |
| `Button`/`ToggleButton` | hover/pressed/focus/disabled、activate | P13-1；编辑器通过 RmlUi command listener |
| `TextField` | UTF-8 文本、selection、caret、IME composition、commit/cancel | P13-1（core 状态、键盘/IME host、字段命中、真实 glyph 与 theme 驱动 caret/selection 装饰已落地） |
| `NumberField` | 文本编辑态、类型化 commit、范围诊断 | P13-3（integer 已接入 adapter 校验与复合归一化；浮点/范围控件仍待补） |
| `CheckBox`/`Select` | value、focus、change command | P13-3 |
| `ScrollView` | offset、viewport、wheel/keyboard scroll、clamp | 不进入本轮；导航网格采用 2048 单元有界裁剪 |
| `TreeView` | expanded/selected ids、activate/rename command | Hierarchy 已消费有界 `NavigationProjection`，提供地图根折叠、独立滚动、真实单元选择与稳定 selection；不实现通用重命名 |
| `Table`/`ListView` | rows、selection、排序 projection、虚拟化上界 | P13-2 |
| `Tabs` | active id、close/select command、dirty marker | P13-2 |
| `SplitPane` | ratio、min sizes、drag command | P13-1（RmlUi 工作区已实现三条稳定 splitter：垂直 Project/Hierarchy↔Scene、Scene↔Inspector 与水平主区↔Diagnostics；比例和捕获为 UI 会话状态） |
| `Dialog`/`Menu`/`Toast` | modal/focus trap/command、受限队列 | P13-2 |
| `Toolbar`/`StatusBar` | command items、structured status | P13-1 |
| `MenuBar`/`PopupMenu` | 有界根菜单/子菜单、hover/open、键盘导航、结构化 command | P13-1（`MenuController` 已落地） |

控件 interface 统一为输入 `UiEvent`、输出零到多个 `UiCommand`、查询 `UiState` 和生成 `DrawList`；调用方不直接改控件私有状态。所有 widget id 是稳定非零 id，焦点、捕获和可见性由一个 `UiContext` 管理。

### 编辑器功能模块（`tools/editor` owner）

| 模块 | 只消费 | 只发出 |
|---|---|---|
| `WorkspaceTree` | `ProjectSnapshot`/`DocumentDescriptor` | open/select/refresh command |
| `DocumentTabs` | manifest 驱动的 document projection、revision、dirty state、diagnostic count | edit/undo/redo/close command |
| `InspectorForm` | field descriptors、候选值、diagnostics | 类型化 `EditCommand` |
| `DiagnosticPanel` | `DiagnosticSet` | filter/navigate-to-field command |
| `DiffPanel` | `ChangeSet` | accept/cancel/save command |
| `PreviewPanel` | `PreviewSnapshot` | refresh/run-project command |
| `PluginPanelHost` | [插件系统规范](11-plugin-system.md) 定义的 editor extension descriptors | namespaced plugin command |

本轮菜单与工作区组合规则：layout 资源声明菜单层级和 `command`，locale 提供菜单文本，theme 提供 `menu.root_width`、`menu.row_height`、`menu.popup_width` 及各状态 recipe；`MenuController` 在布局后的 bounds 上处理鼠标/键盘输入并输出 `UiCommand`。菜单 DrawList 在 shell/panel 后绘制，保证弹出层可见且不改变面板 owner。Window 菜单的 Layout 子菜单沿用同一键盘/鼠标访问路径；其 `layout.reset_default` 只由 host adapter 消费并映射到 typed user settings。项目菜单只启用当前 workspace 中确实存在的 document/category，禁用项仍可见并提供明确 disabled 视觉状态。

Project 面板采用有界的 filter row、category header 和 document row，不为每个资源创建无界控件。搜索仅过滤 `DocumentDescriptor` projection；资源路径、类型、dirty 标记和诊断计数仍来自结构化 session 状态。

这些模块只做 projection 和命令映射，不直接打开文件、解析 JSON、调用 plugin 私有类型或生成自然语言。

## 输入、焦点与文本

- SDL event 只在 editor host 转换为平台无关 `UiEvent`；控件不得 include SDL 头。
- 编辑器 host 分离逻辑窗口坐标与物理 framebuffer 像素：RHI 和 RmlUi context 使用实际像素尺寸，RmlUi 使用 SDL display scale 作为 density-independent pixel ratio，指针命中经过同一输入 seam 缩放；窗口尺寸或显示缩放变化时同步重算，保证 CSS 布局、视觉区域与命中区域一致。
- 键盘快捷键由版本化 `actions/editor.json` 提供，不在 event loop 写 scancode 分支；`tools/editor` 只把 SDL key name 映射到 action id，保留系统级文本输入、IME 和窗口关闭事件的 Adapter 映射。
- 焦点顺序来自布局树和显式 `tab_index`，modal 使用 focus trap；Esc/Enter 等语义先映射为 action id。
- `TextField` 必须使用 SDL 文本输入/IME composition，内部保存 UTF-8，光标移动按 grapheme/cluster 语义；CJK 测量复用 `Font`、`TextShaper`、`LineBreaker`。
- 所有列表、日志、toast 和撤销栈有上界；超限返回诊断或淘汰最旧的 presentation-only 项，不丢项目变更真相。

## 渲染与字体 seam

- `engine/ui` 输出后端无关 `DrawList`：矩形、localized text 参数和 glyph quad；当前已落地有界矩形/`recipe`/状态、占位符参数、glyph atlas UV、有序 primitive 合同和 CPU 文本/矩形视口裁剪。编辑器的 RmlUi `RenderInterface` 只在 `tools/editor` 将有界 RmlUi 几何录制到现有 RHI；不把 D3D12/Vulkan 语义泄漏到 UI 资源或项目 owner。
- 布局节点可声明受校验的像素 `bounds`；`editor::BuildShellDrawList` 仅把布局 bounds、recipe 和 label key 投影到 DrawList，不在 host 中写面板坐标或文案。
- `render::BuildUiDrawPacket` 将 DrawList 按原始顺序解析为有界 NDC 顶点/索引上传包，并把 recipe/state、semantic token 和颜色错误作为结构化诊断返回；`UploadUiDrawPacket`/`RecordUiDrawPacket` 负责 RHI buffer 上传、绑定与 indexed draw，主题只提供资源 id/token，不持有 GPU handle。
- 字体资源由 theme 声明候选文件列表和像素规格；启动时验证候选文件并按声明顺序加载可用字体，`ui::Font` 提供 FreeType 灰度 bitmap 与 pitch 输出，`GlyphAtlas` 以有界容量生成 UV，RHI text batch 已完成纹理上传/采样绘制，文本投影已支持按字符的有序 fallback，并由真实 glyph advance 生成 theme 驱动的 selection/caret 装饰。额外字体预热优化不进入本轮。
- Windows/Linux 首批都至少验证拉丁、简体中文和日文标点；不能把“字体加载成功”当作 CJK 真实渲染通过。

## 可访问性与可测试性

- semantic token 的文本/背景组合在 lint 时做对比度检查；默认和高对比主题都必须通过。
- 交互控件提供 role、accessible-name i18n key、enabled/focused/invalid 状态；纯图标按钮必须有可本地化名称。
- 组件测试不读取真实颜色或中文字符串，断言 recipe/key、状态迁移、命令和 draw primitive。
- 资源合同测试覆盖坏 schema、缺 token、循环引用、缺 key、placeholder 漂移、语言热切换、主题热切换和布局节点上界。
- Windows 真实窗口验收覆盖鼠标、键盘、IME、缩放和错误态；WSL 无显示环境只跑 loader、布局、组件状态与 draw list 测试，不伪造窗口通过。
- Windows 真实窗口还必须检查：顶层菜单展开/关闭、二级菜单切换、菜单外点击关闭、工具栏 hover/pressed/focused、Project 搜索以及窗口缩放后的命中区域；弹出菜单不能被 toolbar 或 panel DrawList 覆盖。

## P13-0 交付顺序

1. 先定义 `EditorStartupDiagnostic`、`LocalizedText`、theme/i18n/layout 值对象与 parser。当前已实现 editor manifest/locale/layout/theme 的独立值对象、有界 parser、启动诊断聚合与资源驱动字体候选。
2. 提交默认/高对比 theme、`zh-CN`/`en` locale 和 workspace layout fixture，并建立资源 lint。当前资源 fixture 已提交并由 editor 资源合同测试实读验证；`jrpgmaker_editorlint <editor-resource-root>` 已提供独立 lint 入口。
3. 扩展 `engine/ui` 的事件、焦点、命令和 DrawList seam；保留现有 Widget/Text 测试。
4. 完成 `tools/project` 工作区 seam 后，才允许 `tools/editor` 用这些资源创建窗口。

停止条件：源码扫描不出现用户可见自然语言或私有颜色/字号；两份主题和两种语言无需重编译即可切换；坏资源在窗口创建前产生可定位结构化诊断。
