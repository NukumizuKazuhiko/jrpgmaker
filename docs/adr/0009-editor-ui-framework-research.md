# ADR-009：编辑器 UI 框架候选调研与重构入口

- 状态：调研完成，RmlUi 已获确认并进入编辑器实现
- 调研日：2026-08-30
- 关联：[架构与 owner 合同](../01-architecture.md)、[P13 编辑器计划](../08-editor-plan.md)、[P13 UI 系统合同](../10-editor-ui-system.md)、[UI 框架 SDD](../12-editor-ui-framework-sdd.md)
- 约束：本 ADR 记录候选和验证结论；用户已明确授权 RmlUi 作为 editor-only 依赖，具体接入合同见 `docs/12-editor-ui-framework-sdd.md`

## 问题

当前 `tools/editor` 已有 `UiTree`、`DrawList`、菜单和工作区 controller，但面板仍由 editor-specific projection 直接拼装。继续在该层增加 Project、Hierarchy、Scene、Inspector 和实时预览功能，会重复实现布局、焦点、命中、滚动、菜单、文本输入和组件状态。

目标是选择一个适合现有 C++20、SDL3、自研 RHI、FreeType/HarfBuzz、Windows/D3D12 与 Linux/Vulkan 边界的开源 UI 框架，再把项目语义保留在 `EditorWorkspaceController`/`EditorSession`/`ProjectWorkspace` 这一条 owner 链中。

## 筛选标准

候选必须同时评估：

- retained-mode 或等价的稳定元素/组件树；
- 按窗口尺寸布局、命中、焦点、键盘/鼠标事件和菜单/子菜单；
- 自定义渲染接口，不能接管项目数据或 RHI 语义；
- C++、SDL3、Windows/Linux 和 CJK 文本可验证；
- 许可证、依赖、构建和长期维护风险可控；
- 能承载 Unity 式 Project、Hierarchy、Scene/Viewport、Inspector、Diagnostics 工作区。

## 候选比较

| 候选 | 与本项目的匹配点 | 主要阻力 | 结论 |
|---|---|---|---|
| **RmlUi** | C++；HTML/CSS 风格文档和 DOM/元素层级；自带布局、控件、事件、模板、本地化、数据绑定；生成顶点/索引/绘制命令并通过 `RenderInterface` 接入；有 SDL3、Vulkan、DirectX 12 相关 backend；核心 MIT；近期 6.3 release 增加 RCSS 变量、自定义属性、媒体查询、`@font-face` 和完整 DX12 renderer | 不是 Unity Editor 现成工作区；需要以项目 RHI 实现 render/system interface；官方 Vulkan renderer 目前会创建自己的 Vulkan instance/device，不能直接当作现有 Vulkan RHI 的 drop-in；CJK shaping 需与现有 HarfBuzz 链做专项验证 | **已选；editor-only 实现** |
| **Dear ImGui** | MIT；SDL3、Vulkan、DirectX 12 backend；输入和 renderer backend 边界清楚；表格、调试工具和原型速度快 | immediate-mode，不是 VisualElement 式 retained tree；docking 为独立分支/额外状态模型；长期 Inspector/资源树/可访问性/主题资源化需要项目再建一层 | 作为调试工具或 POC 参考，不作为主编辑器框架 |
| **Slint** | 声明式 `.slint` UI；支持 C++；响应式布局、绑定、live preview 和稳定 1.x API | 许可证为 Royalty-free/GPLv3/商业选项，需单独核法务与发布方式；引入 DSL 编译器和运行时；现有 SDL3/RHI/字体链路接入成本和自由度需验证 | 暂不选 |
| **Qt** | 成熟的 tree/model/view、dock、property editor 和桌面可访问性能力 | LGPL/GPL/商业多许可证；体量和部署面显著扩大；窗口、事件、字体和渲染生命周期会与现有 SDL/RHI 平行；中心游戏 viewport 需要额外嵌入 seam | 不符合当前引擎编辑器边界 |

## 证据与事实

### RmlUi

RmlUi 官方仓库声明其核心是 C++ HTML/CSS UI 包，拥有自己的布局引擎、元素层级/DOM、事件系统和控件，并把 UI 编译成顶点、索引和绘制命令，由应用负责渲染。它还列出动态布局、模板、本地化、数据绑定和空间导航能力；核心依赖为标准库与可替换的 FreeType font engine，要求 C++17 以上。[RmlUi README](https://github.com/mikke89/RmlUi)

RmlUi 的公开集成步骤是实现或选择 `RenderInterface`、`SystemInterface`，创建 context，加载文档，然后由宿主循环更新、提交输入并渲染。[RmlUi integration](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integration.html)、[RmlUi RenderInterface](https://github.com/mikke89/RmlUi/blob/master/Include/RmlUi/Core/RenderInterface.h)、[RmlUi SystemInterface](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/system.html)

官方 README 列出了 SDL3、Vulkan 和 DirectX 12 组合 backend，并说明核心和相关 backend 采用 MIT；但官方维护者在 Vulkan 集成讨论中明确表示当前 Vulkan renderer 会创建自己的 instance/device，接入已有 Vulkan instance/device 需要修改 renderer。因此本项目应优先实现自己的 RmlUi `RenderInterface`，不要直接接管现有 RHI 生命周期。[RmlUi backend matrix](https://github.com/mikke89/RmlUi)、[RmlUi Vulkan integration discussion](https://github.com/mikke89/RmlUi/discussions/811)、[RmlUi license](https://github.com/mikke89/RmlUi/blob/master/LICENSE.txt)

RmlUi 的数据绑定文档采用 MVC，并说明视图会在变量变 dirty 时同步；这可以作为 UI projection 的参考，但本项目不能把 RmlUi model 变成第二个项目数据 owner，实际写回仍必须走 `ProjectWorkspace::Apply` 和 `PrepareSave/Commit`。[RmlUi data binding](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html)

### 视觉风格复核

RmlUi 的官方示例使用的是示例资源和示例 RCSS，不代表最终编辑器的视觉风格。RML 文档可以挂载一个或多个 RCSS 样式表，样式表也可以在运行时从文件或字符串实例化；因此可以把项目的颜色、字体、间距、边框、圆角、状态色、过渡和面板装饰集中到自己的主题资源中，而不是接受示例皮肤。[RmlUi style sheets](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/rcss.html)、[RmlUi RML style sheets](https://mikke89.github.io/RmlUiDoc/pages/rml/style_sheets.html)

RCSS 支持自定义属性和变量，变量可以参与级联、继承、动画和过渡；官方 6.3 release 还明确提到可配合媒体查询切换主题。装饰器扩展允许使用图片、九宫格、渐变以及自定义 decorator，因此可以做成更接近 Unity 深色编辑器、JRPG 风格深靛蓝/炭灰主题，当前 POC 的默认外观不应成为审美约束。[RmlUi 6.3 release](https://github.com/mikke89/RmlUi/releases)、[RmlUi decorators](https://mikke89.github.io/RmlUiDoc/pages/rcss/decorators.html)

这意味着当前问题更像是“没有建立项目自己的主题层”，而不是 RmlUi 的换肤能力不足。正式 POC 应先用项目色卡建立一套 editor theme：深色工作区底色、冷银灰文字、电光青选中态、火焰橙运行态、警戒红诊断态，并验证中英文、缩放、键盘焦点和选中/悬停/禁用状态。

### 新近候选复核

Slint 是更现代的声明式 UI 候选，支持 C++、响应式布局和自定义 platform/backend；但官方许可证是 GPLv3、Royalty-free 或商业许可三选一，并非简单 MIT 依赖。它值得做概念对比，但在本项目尚未确认发布许可、DSL 编译链和自研 RHI 接入成本前，不应替换 RmlUi 主线。[Slint repository](https://github.com/slint-ui/slint)、[Slint backends and renderers](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/)、[Slint license](https://github.com/slint-ui/slint/blob/master/LICENSE.md)

Qt Quick/QML 的定制能力也很强，可以替换单个控件或定义完整自定义 style；但它会带来 Qt 运行时、QML 工具链、窗口生命周期和许可证核对，仍不符合当前 SDL3 + 自研 RHI 的最小接入边界。[Qt Quick custom styles](https://doc.qt.io/qt-6/qtquickcontrols-customize.html)

### Dear ImGui

Dear ImGui 官方 backend 文档将平台 backend 与 renderer backend 分开，列出 SDL3、DirectX 12 和 Vulkan，并明确其核心主要消费输入、纹理和带裁剪的 indexed textured triangles。该边界适合快速接入现有窗口和 RHI，但它本质上仍是 immediate-mode API。[Dear ImGui backends](https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md)

Dear ImGui 核心采用 MIT 许可证。[Dear ImGui license](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)

### Slint 与 Qt

Slint 官方仓库提供 C++ 绑定、声明式 UI、属性绑定和响应式布局，但其许可选项包含 GPLv3、Royalty-free 和商业许可，不等同于简单的 MIT/BSD 依赖。[Slint repository](https://github.com/slint-ui/slint)

Qt 官方许可页说明不同模块分别涉及 LGPLv3、GPLv3、商业许可和第三方代码；即使只使用开源选项，也需要逐模块核对发布义务。[Qt licensing](https://doc.qt.io/qt-6/licensing.html)

## 决策建议

本轮已将 `editor-ui-rmlui-poc` 落为 `tools/editor` 的正式视图适配；不替换既有项目 owner，且使用项目自有 RCSS 主题而非 RmlUi 默认 demo：

```text
ProjectWorkspace / parser / validator
                ↑
EditorSession → EditorWorkspaceController → panel projections / commands
                ↑
RmlUi adapter: RML document + RCSS theme + event bridge + RHI RenderInterface
```

POC 只验证一个窗口中的固定工作区：MenuBar、Project、Hierarchy、中央 SceneViewport、Inspector 和 StatusBar；用真实 navigation projection 完成选择高亮和 walkable toggle，检查中文/英文、resize、菜单、键盘、鼠标、D3D12 与 Vulkan draw submission，并用项目自己的 RCSS 主题验证深色工作区、选中态、诊断态、字体和间距。RmlUi model 只接受只读 projection，编辑动作转成项目已有的结构化 command。

`engine/ui::UiTree` 保留为运行时/遗留后端无关 UI 合同；RmlUi 不进入 engine/domain/runtime，也不形成第三套项目状态模型。

## 未决风险

- RmlUi 自定义 `RenderInterface` 是否能完整表达当前 RHI 的 texture、scissor、font atlas、clip mask 和 draw lifetime，需要小范围代码验证。
- RmlUi 文档/样式资源与项目现有 editor layout/theme/i18n schema 的边界需要确定，避免两个资源系统同时成为 UI 真源。
- CJK HarfBuzz shaping、IME、剪贴板和高 DPI 在 Windows 实机上必须有证据；不能只凭 FreeType 加载成功判断通过。
- RmlUi 的 DX12/Vulkan reference backend 不能直接证明与本项目 RHI 可组合；必须以项目 backend 实测为准。
- RmlUi 已登记到 `docs/01-architecture.md` 并固定 FetchContent commit；仍需在后续发布打包时保留其 MIT 许可证与源版本记录。

## 停止条件

本 ADR 的调研阶段已结束；本轮不修改 P0–P12 owner、不修改 `docs/11-plugin-system.md`、不提交或 stage。下一阶段唯一建议是继续完成本轮已定义的 Windows/Linux 门禁与实机证据，随后停止，不扩展其他编辑器功能。
