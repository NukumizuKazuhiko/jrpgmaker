# P13 编辑器 UI 框架 SDD

> 状态：当前有效（登记于 [README.md](README.md)）。本文件是 P13 重开工作的 UI 框架规格；它先于实现和面板拼装生效。

## 目标

在 `engine/ui` 建立无第三方依赖、后端无关的 retained-mode UI 框架，服务 Unity 式编辑器和运行时 UI。框架只拥有控件树、布局、焦点、事件路由、命令输出和 DrawList 投影，不拥有项目数据、导航规则或保存语义。

## 参考模式

- Unity UI Toolkit：参考官方 `VisualElement` 的视觉树、布局/样式/事件职责集中，以及事件 target/传播阶段；不引入 UXML/USS/C# runtime，也不复制其实现。
- Godot：参考官方 `Control` 的父子树、尺寸变化、焦点、输入和主题失效边界，以及 Viewport 统一命中/焦点路由；不引入 Godot Object/Variant/SceneTree，也不复制其实现。
- 采用的是可验证的架构模式：一棵有限控件树、一个输入/焦点上下文、布局后命中、结构化 command、后端无关 DrawList。第三方源码仅作设计证据，项目实现保持现有 C++20/SDL/RHI 合同和自身命名。

设计证据：Unity 官方 [VisualElement 源码](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Modules/UIElements/Core/VisualElement.cs)；Godot 官方 [Control 源码](https://github.com/godotengine/godot/blob/master/scene/gui/control.cpp) 与 [Viewport GUI 路由源码](https://github.com/godotengine/godot/blob/master/scene/main/viewport.cpp)。

## 深模块与 owner

`engine/ui::UiTree` 是唯一 UI 框架 owner。外部 interface 只提供：

1. `SetRoot(UiNode)`：安装有限大小的节点树。
2. `Layout(Rect)`：按窗口实际尺寸计算 bounds。
3. `Dispatch(UiEvent)`：按命中、焦点和传播规则产生有界 `UiCommand`。
4. `BuildDrawList()`：投影为后端无关 DrawList。

节点只声明稳定 id、布局约束、recipe、label key、可见/启用状态和 children。编辑器 adapter 通过 command 与 projection 接入，不能直接修改节点私有状态。

## 不变量

- 节点 id 非零且树内唯一；节点、事件、命令和 DrawList 均有上界。
- Layout 不读取 SDL、项目 JSON 或业务对象；无效窗口尺寸不产生负矩形。
- 命中测试只使用上一轮 Layout 的 bounds；越界命中返回空。
- 焦点由一个 `UiContext` 管理，Tab 顺序确定；不可见/禁用节点不能获得焦点。
- 输入只产生结构化 command；UI 不写文件、不调用 validator、不修改导航模型。
- 文本只携带 localized key 和参数；C++ 不硬编码用户可见文案、颜色或字号。

## 第一阶段复用

复用并深化已有 `Widget`、`Panel`、`List`、`TextBlock`、`UiContext`、`DrawList`。新增框架接口必须保持 backend-agnostic，并覆盖 resize、pointer hit、focus、command、recipe 状态、有限列表和 CJK 文本投影。编辑器各面板只是 `tools/editor` adapter。

## 菜单与工作区组合

`engine/ui::MenuController` 是通用菜单框架的深模块：输入有限 `MenuBarModel` 和实际窗口 bounds，输出菜单的 open/hover 状态、后端无关 DrawList 与结构化 `UiCommand`。它支持根菜单、受限层级子菜单、鼠标命中、Alt/方向键/Enter/Escape 和菜单外关闭；它不认识项目文档、导航、保存或预览命令的业务含义。

`tools/editor::EditorWorkspaceController` 是组合层：从 layout 的 `MenuBar`/`Menu`/`MenuItem` 节点生成菜单模型，依据 session projection 计算 enabled 状态，再把 command 转给 `EditorSession`。Project、Hierarchy、Scene、Inspector、Toolbar、Status 不保存自己的选中真相；菜单切换文档也通过同一个 session selection 完成。Project 的分类和过滤是 projection，不是第二份资源目录。

该组合参考 Unity UI Toolkit 的视觉树、布局、事件路由职责，以及 Unity Editor 的菜单栏、Project、Hierarchy、Scene、Inspector 信息架构；不引入 UXML/USS，不复制 Unity/Godot 源码，也不将菜单框架变成项目数据 owner。

## 验收与停止

单元/合同测试覆盖树唯一 id、resize、命中、焦点、事件 command、容量上界、主题和 CJK；菜单额外覆盖根菜单/二级菜单打开、键盘导航、菜单外关闭、禁用项和 DrawList 覆盖顺序。框架通过后，才用预留接口拼装第一条导航网格编辑闭环；不扩展 docking、3D gizmo、对话/事件图或通用撤销实现。
