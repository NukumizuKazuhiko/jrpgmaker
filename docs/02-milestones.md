# 里程碑与验收门禁

> 状态：当前有效（登记于 [README.md](README.md)）。本文只维护当前阶段合同、完成状态、阻断项与停止条件；2026-08 的逐轮实施和审计证据见 [归档记录](archive/02-milestones-history-2026-08.md)，归档不得覆盖当前源码、测试与本文结论。

## 全局门禁

以下门禁自 P0 起持续生效；阶段状态必须由当前命令、运行日志、截图或用户侧证据支持，历史测试数量不能代替当前复验。

1. 当前支持平台 Windows/D3D12 与 Linux/Vulkan 的 build + test 绿灯；macOS 仅保留为后续 Vulkan/MoltenVK 适配，不进入当前完成定义。
2. 编译 warning 清零：MSVC `/WX`，GCC/Clang `-Werror`。
3. `clang-format` diff 为空，`.editorconfig` 生效。
4. golden image 门禁通过；新增渲染行为必须重新标定对应基准与容差。
5. 数据 lint、schema、迁移与插件 validator 门禁通过。
6. 模块私有头审计通过，生成物由生成命令产生且无漂移。
7. 本轮触达的产品、架构、合同、用户行为和债务状态已回写对应真源。
8. 当前阶段的未关闭阻断问题为零；无法执行的关键验收必须显式记录原因、影响和替代证据。

## 当前状态总览

| 阶段 | 当前状态 | 当前结论 |
|---|---|---|
| P0 奠基 | 已闭合 | 构建、测试、CI、格式与私有头门禁骨架已建立。 |
| P1 RHI 垂直切片 | 已闭合 | RHI 合同、D3D12/Vulkan 后端、离屏 golden 与 SDL 主循环已建立。 |
| P2 资源与场景 | 已闭合 | glTF、资产句柄、场景层级、相机和泄漏检测闭环。 |
| P3 事件与对话 | 已闭合阶段范围 | domain 事件/对话、CJK 文本合同与数据 lint 已落地；runtime overlay GPU golden 的测试、固定字体、许可文本和基准图均已纳入 git，并由 golden-sync 专项命令重生成和检查。 |
| P4 角色与世界 | 已闭合 | 角色控制、碰撞、寻路、交互、动画与镜头闭环。 |
| P5 插件与战斗 seam | 已闭合 | 源码级构建期注册、插件数据/validator 与两类战斗插件验证完成。 |
| P6 渲染风格与演出 | 已闭合 | 渲染风格插件、表现计划、后处理和双风格替换验证完成。 |
| P7 工具链与竖切组装 | 已闭合阶段范围 | 日期、日程、存档、Lua 与参考项目装配完成；配置的 30 分钟内容量不等于真实连续试玩证据。 |
| P8 渲染资源消费 | 已闭合 | 材质、纹理、动画、蒙皮、粒子及后处理资源消费闭环。 |
| P9 内容生产管线 | 已闭合 | 资源构建、增量缓存、产物清单与错误报告闭环。 |
| P10 项目装配工具 | 已闭合 | 创建、校验、构建、运行、迁移和稳定 diff 的 CLI 闭环。 |
| P11 插件 SDK 与发布硬化 | 未闭合 | SDK consumer、运行时合同和样例布局的发布装配已有验证；DEBT-040 的任意安全 `data_roots` 逐项装配已实现并有行为自测，仍需随 P11 其他发布门禁刷新最终 runner 证据。 |
| P12 首个稳定版本 | 未闭合 | 缺真实连续 30 分钟可玩证据；中日文 runtime overlay GPU golden 已由固定字体、专项生成命令和 golden-sync 纳入可复现门禁。 |
| P13 开发者项目编辑器 | 重新打开/进行中 | 以 Unity 式游戏引擎编辑器为目标重建第一条导航地图编辑纵向闭环；GUI 仍不得成为第二语义 owner。 |

## 阶段合同

### P0–P2：工程与渲染基础

- **目的**：先证明构建、RHI 双后端和数据驱动渲染链路成立，再引入 JRPG 语义。
- **唯一 owner**：图形 API 语义属于 `engine/rhi`；CPU 资产句柄生命周期与运行时场景结构均属于 `engine/core`（`AssetRegistry`/`Scene`）；`tools/assetimport` 只负责把兼容输入转换为这些 core 合同。仓库当前不存在 `engine/assets` 或 `engine/scene` target。
- **停止条件**：Windows/Linux 构建测试、双后端合同测试、golden、资源释放和私有头门禁全部通过。

### P3–P4：JRPG 领域闭环

- **目的**：建立事件、对话、CJK 文本、角色移动、碰撞、寻路、交互和动画的共享领域合同。
- **唯一 owner**：JRPG 业务真相属于 `engine/domain`；UI、render、audio 和平台层只消费结构化 projection 或命令。
- **停止条件**：数据 lint 与领域测试覆盖正常、错误和边界路径；文本改动必须包含中日文用例，产品发布前还须有被 git 跟踪、由 CI 可重生成并在双后端比对的真实 GPU golden。

### P5–P6：插件扩展边界

- **目的**：证明战斗规则和渲染风格可由源码级插件替换，参考插件不享有内置特权。
- **唯一 owner**：插件生命周期与公共 seam 属于 `engine/plugin`；战斗规则、材质 schema 和私有数据属于各插件。
- **停止条件**：至少两类差异明显的战斗/渲染插件可替换，且不修改 `engine/domain` 或 RHI 后端；失败、预算与资源释放均有结构化验证。

### P7–P10：项目数据与生产工具

- **目的**：完成日期、日程、存档、Lua、资产构建和项目装配，使版本化项目数据可被校验、构建、运行和迁移。
- **唯一 owner**：核心 parser、schema、validator、迁移器和数据模型拥有语义；CLI 只做接线与呈现。
- **停止条件**：干净项目可重复完成 create → validate → build → run → migrate，输出稳定且所有输入、队列、缓存和报告有界。

### P11：插件 SDK 与发布硬化

- **目的**：让第三方开发者只依赖公开合同即可构建、注册、校验、运行和卸载插件，并获得可复现的 Windows/Linux 发布包。
- **范围外**：跨编译器 DLL ABI、二进制热加载、通用插件市场和沙箱脚本平台。
- **当前状态**：`package_release.ps1` 已逐项解析并装配每个 manifest 声明的安全相对 `data_roots`，保留 root 的包内相对路径；缺失、越界、重复、嵌套冲突、输出目录冲突和无法保持包内路径的 root 均会拒绝。独立 `selftest_package_release.ps1` 覆盖自定义/多 root、拒绝场景和确定性清单。
- **停止条件**：SDK consumer、兼容矩阵、迁移策略、发布包、长时间运行与资源预算均可从干净环境复现；发布装配必须逐项消费 manifest 声明并拒绝缺失、越界或无法装配的 root，随后刷新 runner 证据。

### P12：项目完成与首个稳定版本

- **目的**：以一个可交付参考项目证明“项目数据 + 源码级插件 + 可替换画风”的完整产品承诺。
- **完成定义**：从创建/校验项目开始，完成资源构建、启动、移动/碰撞/寻路、日期/日程/触发、对话或可选战斗、渲染风格、存读档和发布包运行闭环。
- **当前阻断**：真实连续 30 分钟可玩证据；发布包和完整试玩仍需按当前代码在干净 checkout 重现。
- **停止条件**：[`00-product.md`](00-product.md) 的成功标准全部有真实证据，未关闭阻断问题为零，开放债务均有 owner、风险级别和后续入口。

### P13：开发者项目编辑器 GUI

- **目的**：在 P10 CLI 合同之上提供运行时非必需的本地 GUI，降低直接编辑 JSON 与资源清单的成本；当前尚无 CMake 开关排除 editor，构建层可选性由 DEBT-042 跟踪。
- **唯一 owner**：GUI 属于 `tools/editor` adapter；项目数据语义仍由 parser、validator、迁移器、插件与运行时合同拥有。
- **本轮完成边界**：Windows/D3D12 实机完成打开合法项目、真实 Project/Hierarchy/Scene 导航投影、共享 Selection、Inspector 修改 walkable、dirty/revision/diff/diagnostics、`PrepareSave`/`Commit` 原子保存、重开一致和独立 runtime 启停；Linux/Vulkan 保持构建与合同测试。闭合后立即停止，不扩展其他编辑器。
- **当前状态**：`ProjectWorkspace::PrepareSave` 已在检查 revision 后通过同源 `Diagnose` 对完整 working copy 执行 parser、跨文件引用、资源预算、adapter 与插件 validator；插件 sidecar working copy 经有界 overlay reader 进入同一运行时 validator 输入，任何 error 均不签发 token。DEBT-039 已关闭；P13 保存链仍需结合其他当前阻断项的整体门禁判断。
- **非目标**：通用 3D 建模器、DCC、联网协作、云端格式、运行时 GUI 依赖或绕过插件私有校验的自由脚本编辑。
- **详细合同**：见 [`08-editor-plan.md`](08-editor-plan.md)、[`09-editor-interface-catalog.md`](09-editor-interface-catalog.md) 与 [`10-editor-ui-system.md`](10-editor-ui-system.md)。

## 最近一次验收快照

> 下列数字只记录 2026-09-02 已执行证据；代码变化后必须重新运行，不能永久视为通过。

- Windows `ctest --preset win-debug --output-on-failure`：387/387。
- Linux/WSL：本轮仅构建与定向测试证据，未运行全量 CTest；上一轮全量证据为 311/311（2026-08-29）。
- 私有头审计：210 个文件通过；`git diff --check` 通过。
- 文档索引门禁：`pwsh ./tools/ci/check_docs_index.ps1` 通过（19 local links; 16 current targets tracked）。
- WSL 无 WSLg，未把 SDL 实窗口路径伪装为本地通过；Linux Vulkan 离屏测试使用 lavapipe。

## 风险清单

| # | 风险 | 等级 | 阻断策略 |
|---|---|---|---|
| 1 | RHI 双后端行为漂移 | 高 | 合同测试与同提交同步；新增渲染行为重新标定 golden。 |
| 2 | 后续 macOS/MoltenVK 适配污染当前合同 | 中 | 仅经现有 Vulkan RHI 接入，独立验收，不回退 Windows/Linux 门禁或引入平行业务语义。 |
| 3 | 渲染风格 seam 被参考插件反向塑形 | 高 | 保持至少两个真实风格 adapter；插件材质私有字段不得进入 engine 合同。 |
| 4 | CJK 排版复杂度被低估 | 高 | 中日文单元/布局测试加真实 GPU golden；缺失时阻断 P12。 |
| 5 | 战斗 seam 被具体玩法污染 | 高 | engine 仅保留会话生命周期与结构化输入输出；以差异插件持续做替换测试。 |
| 6 | 源码插件滑向不稳定 DLL ABI | 高 | 当前固定构建期注册；DLL ABI、热重载和二进制分发必须另立边界。 |
| 7 | 插件私有数据绕过作者期校验 | 高 | 插件随包提供 validator；统一 lint 并限制文件数、大小、错误数、超时和队列。 |
| 8 | 自动化测试被误当作真实可玩证据 | 高 | P12 单独保留人工连续试玩、真实窗口/GPU 和干净发布包验收记录。 |
