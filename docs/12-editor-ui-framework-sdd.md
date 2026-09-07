# P13 编辑器 UI 框架 SDD

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是 P13 重开工作的 UI 框架规格；它先于实现和面板拼装生效。

## 目标

编辑器采用 RmlUi 的 retained-mode DOM/RCSS 框架，服务 Unity 式工作区体验；`engine/ui` 继续保留无第三方依赖、后端无关的运行时/遗留 UI 合同。编辑器框架只拥有文档树、布局、焦点、事件路由、命令输出和 RHI 绘制适配，不拥有项目数据、导航规则或保存语义。

## 参考模式

- Unity UI Toolkit：参考官方 `VisualElement` 的视觉树、布局/样式/事件职责集中，以及事件 target/传播阶段；不引入 UXML/USS/C# runtime，也不复制其实现。
- Godot：参考官方 `Control` 的父子树、尺寸变化、焦点、输入和主题失效边界，以及 Viewport 统一命中/焦点路由；不引入 Godot Object/Variant/SceneTree，也不复制其实现。
- 采用的是可验证的架构模式：一棵有限 RML 文档树、一个 RmlUi context、布局后命中、结构化 command、项目自有 RHI `RenderInterface`。RmlUi 只作为 `tools/editor` 的编辑器依赖，项目数据仍通过 projection 和 command 进入既有 owner 链。

设计证据：Unity 官方 [VisualElement 源码](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/UIElements/Core/VisualElement.cs)；Godot 官方 [Control 源码](https://github.com/godotengine/godot/blob/master/scene/gui/control.cpp) 与 [Viewport GUI 路由源码](https://github.com/godotengine/godot/blob/master/scene/main/viewport.cpp)。

## 深模块与 owner

P13 现在有两个不重叠的 UI owner：

- `tools/editor::RmlUiEditorView` 是编辑器 UI 框架 owner，封装 RmlUi context、RML/RCSS、输入转发和 RHI 绘制批次。
- `engine/ui::UiTree` 仍是运行时/遗留后端无关 UI owner，不被编辑器语义反向依赖。

编辑器 owner 只提供：

1. `Initialize(config, command_handler, size)`：创建有限资源预算的 RmlUi context。
2. `SetMarkup(markup, source_url)`：安装由 editor projection 生成的 RML 文档和 RCSS 主题。
3. `Process*`/`Resize`/`Update`：接收 SDL 转换后的输入并按实际窗口尺寸布局。
4. `Record(command_list, pipelines)`：将 RmlUi 产生的有界几何转成项目 RHI 绘制命令。

RML 节点只声明稳定 command、argument、可见/启用状态和有限 children。编辑器 adapter 通过 command 与 projection 接入，不能让 RmlUi model 成为项目数据副本。

## 不变量

- 节点 id 非零且树内唯一；节点、事件、命令和 DrawList 均有上界。
- Layout 不读取 SDL、项目 JSON 或业务对象；无效窗口尺寸不产生负矩形。
- 命中测试只使用上一轮 Layout 的 bounds；越界命中返回空。
- 焦点由一个 `UiContext` 管理，Tab 顺序确定；不可见/禁用节点不能获得焦点。
- 输入只产生结构化 command；UI 不写文件、不调用 validator、不修改导航模型。
- 文本只携带 localized key 和参数；C++ 不硬编码用户可见文案、颜色或字号。

## 第一阶段复用

运行时继续复用已有 `Widget`、`Panel`、`List`、`TextBlock`、`UiContext`、`DrawList`。编辑器不再在 `main.cpp` 继续堆叠这些面板行为，而由 `RmlUiEditorView` 提供 resize、pointer hit、focus、command、RCSS 状态、有限列表和 CJK 文本渲染；编辑器各面板仍只是 `tools/editor` projection/command adapter。

## 菜单与工作区组合

`engine/ui::MenuController` 是通用菜单框架的深模块：输入有限 `MenuBarModel` 和实际窗口 bounds，输出菜单的 open/hover 状态、后端无关 DrawList 与结构化 `UiCommand`。它支持根菜单、受限层级子菜单、鼠标命中、Alt/方向键/Enter/Escape 和菜单外关闭；它不认识项目文档、导航、保存或预览命令的业务含义。

RmlUi 工作区的 `SplitPane` 由三个稳定 splitter 节点组成：Project/Hierarchy 与 Scene、Scene 与 Inspector 的垂直分隔，以及主区与 Diagnostics 的水平分隔。`RmlUiEditorView` 在 UI 会话内保存比例，按下后捕获指针、移动时以当前窗口尺寸计算并裁剪到面板边界、抬起后释放；文档重建和 resize 只重新投影该比例，不把布局状态写入项目文件。节点声明 `role="separator"`、`aria-orientation` 和 `aria-valuenow`，面板尺寸由 RCSS 默认配方与已布局 bounds 驱动。持久化通过 editor-owned `EditorUserSettings` 的类型化导入/导出完成：host 负责从 SDL 偏好目录取得路径，loader/writer 负责 schema 与有限输入校验及临时文件原子替换，View 不做文件 I/O；拖拽释放后的值变化和退出流程触发有界写入。

`tools/editor::EditorWorkspaceController` 是组合层：从 session projection 生成 RML 文档，依据 session 状态计算 enabled 状态，再把 command 转给 `EditorSession`。Project、Hierarchy、Scene、Inspector、Toolbar、Status 不保存自己的选中真相；菜单切换文档也通过同一个 session selection 完成。Project 的分类和过滤是 projection，不是第二份资源目录。

RmlUi 工作区菜单使用 `data-menu-role` 标记根菜单和二级菜单；`RmlUiEditorView` 维护临时的 `menu-open` 视觉状态。根菜单由点击切换，二级菜单由点击展开或收起，菜单标签可进入 Tab 焦点链并用 Enter/Space 激活，菜单项使用程序化焦点参与上下移动，左右键切换根菜单或返回父级菜单，Escape 或点击菜单外关闭；命令仍只通过统一的结构化 command 路由，不把菜单状态提升为项目或 session 数据。

整页 RML 因命令刷新而重建时，Project、Hierarchy、Scene 和 Inspector 的交互节点使用稳定 `id` 与程序化焦点；`RmlUiEditorView` 在卸载旧文档前记录当前面板焦点，并在新文档加载后恢复对应节点。菜单焦点属于瞬时导航状态，不跨命令重建恢复，避免命令执行后菜单残留为视觉状态。

带 `role="button"` 或 `role="checkbox"` 的投影节点可通过 Enter、NumpadEnter 或 Space 激活；键盘激活仍只触发节点声明的结构化 command，表单输入和菜单继续由各自的 RmlUi 事件路径处理。

导航网格只把当前 selected cell 放入 `tabindex="0"` 和 `autofocus` 焦点链，其余单元保持 `tabindex="-1"`；这样大网格不会把所有单元扩张为 Tab 序列，文档重建后键盘焦点仍回到当前 Selection。

该组合参考 Unity UI Toolkit 的视觉树、布局、事件路由职责，以及 Unity Editor 的菜单栏、Project、Hierarchy、Scene、Inspector 信息架构；使用 RML/RCSS 作为项目自己的视图资源格式，不复制 Unity/Godot/RmlUi 源码，也不将 UI 框架变成项目数据 owner。RmlUi 源码由 CMake FetchContent 固定到已审计 commit，并且只链接 editor host。

## 验收与停止

单元/合同测试覆盖 RML 文档命令、共享 selection projection、resize、命中、焦点、事件 command、容量上界、RCSS 主题和 CJK；菜单额外覆盖根菜单/二级菜单打开、禁用项和编辑器 command 路由。RmlUi 适配通过后，才用预留接口拼装第一条导航网格编辑闭环；不扩展 docking、3D gizmo、对话/事件图或通用撤销实现。
