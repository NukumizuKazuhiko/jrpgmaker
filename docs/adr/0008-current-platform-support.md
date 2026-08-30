# ADR-008：当前支持 Windows 与 Linux，macOS 作为后续适配

- 状态：已接受
- 决策日：2026-08-29
- 当前事实复核：2026-08-30
- 关联：[产品定义](../00-product.md)、[架构合同](../01-architecture.md)、[里程碑](../02-milestones.md)、[DEBT-044](../04-debt-register.md)

## 背景

项目需要对“源码里存在平台入口”“CI 能编译某个平台”和“产品正式支持该平台”作出区分。当前具备可重复开发、实机运行和发布验收条件的平台是 Windows/D3D12 与 Linux/Vulkan。仓库保留 macOS preset、Vulkan 条件分支和 CI 构建尝试，但缺少被本项目认可的 macOS 实机窗口、输入、音频、MoltenVK swapchain 与发布包验收闭环。

如果仅因代码可编译或 CI 曾通过就宣称 macOS 支持，会把未经实机验证的平台假设提升为产品承诺；如果为 macOS 建立独立业务或渲染语义，又会破坏现有 RHI、domain、plugin 和 app 的单一合同。

## 决策

当前产品支持范围固定为：

- Windows：D3D12 后端，进入构建、测试、实机运行、发布和完成门禁。
- Linux：Vulkan 后端，进入构建、测试和发布门禁；真实桌面 swapchain 仍需按已有债务单独保留证据边界，不能用 WSL 无窗口测试代替。
- macOS：不是当前产品支持平台，不进入版本完成定义或发布承诺。未来具备真实环境后，只允许复用现有 Vulkan RHI 并通过 MoltenVK 作为独立平台适配接入。

macOS 适配不得：

- 新增 Metal 平行业务后端或另一套 shader/材质语义；
- 在 domain、render、plugin 或 app 中建立 macOS 专属业务分支；
- 降低或绕过 Windows/Linux 的合同测试、golden、数据校验和发布门禁；
- 用“能编译”替代窗口、输入、音频、GPU、打包和实机运行证据。

## 当前 CI 现实

产品边界与 CI 合并语义目前没有完全对齐：`.github/workflows/ci.yml` 的 `build-test` matrix 仍包含 `mac-debug` 和 `mac-release`，并且没有 `continue-on-error` 或条件豁免。因此 macOS job 失败会使整个 workflow 失败，实际形成工程硬门禁。

在 DEBT-044 关闭前，必须同时陈述以下两点：macOS 不是当前产品支持承诺；macOS CI 失败仍会阻断当前 workflow。不得把该 job 描述为“非阻断历史检查”。后续 CI 治理必须明确选择将其改为非阻断兼容性信号，或正式提升为工程合并门禁并同步修订本 ADR；不能长期保持文字与行为分叉。

## 后果

- 产品文档、用户指南和发布说明只承诺 Windows/Linux。
- CMake 可保留 `mac-*` preset 和条件编译入口，但它们只代表适配表面存在。
- macOS 相关修复不得以牺牲当前双平台合同为代价。
- 任何“macOS 已支持”的结论必须先修订本 ADR，并提供真实环境的完整验收证据。

## 复审条件

只有同时满足以下条件才复审本决策：

1. 有稳定可重复的 macOS 开发和 CI/实机环境；
2. Vulkan/MoltenVK 设备、surface、swapchain、窗口 resize 和呈现通过实机验证；
3. SDL3 输入、音频与 CJK 字体链通过真实应用验收；
4. SDK consumer、项目 lint、双次确定性发布装配和发布包启动通过；
5. 没有新增平台专属业务语义，Windows/Linux 门禁保持不变。
