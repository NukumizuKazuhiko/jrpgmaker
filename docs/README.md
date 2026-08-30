# 真源索引

本文件是本项目唯一文档登记处。新增真源文档必须在此登记；失效、被吸收或一次性的文档移入 `docs/archive/` 并在下表标注归档位置。未登记或已归档的文档不得作为事实依据。

## 当前有效

| 编号 | 文档 | 职责 | 状态 |
|---|---|---|---|
| 00 | [产品定义](00-product.md) | 定位、目标游戏形态、第一可玩闭环、边界、成功标准 | 当前 |
| 01 | [架构与 owner 合同](01-architecture.md) | 分层图、依赖规则、owner 边界合同表、关键合同设计、技术栈锁定、目录结构 | 当前 |
| 02 | [里程碑与验收门禁](02-milestones.md) | P0–P13 当前状态、阶段合同、阻断项、停止条件、风险清单与全局门禁 | 当前 |
| 03 | [引擎架构调研](03-engine-survey.md) | Godot/Unity/Bevy/Unreal 四引擎对照、对象模型重评证据、采纳/拒绝清单 | 当前 |
| 04 | [技术债务登记处](04-debt-register.md) | [未解决问题摘要](04-debt-register.md#未解决问题摘要2026-08-30)、全部债务明细、已接受噪音、不处理原因与后续入口 | 当前 |
| 05 | [插件 SDK 与发布合同](05-plugin-sdk.md) | P11 公开插件合同、兼容矩阵、预算边界和最小模板入口 | 当前 |
| 06 | [插件发布与排障](06-plugin-release.md) | P11 兼容性矩阵、发布包内容、确定性门禁和错误排查 | 当前 |
| 07 | [用户指南](07-user-guide.md) | 构建、项目校验、启动、输入、发布包和插件边界 | 当前 |
| 08 | [编辑器 GUI 计划](08-editor-plan.md) | P13 开发者项目编辑器的边界、owner、实施阶段和验收 | 当前 |
| 09 | [编辑器接口目录](09-editor-interface-catalog.md) | GUI 所需的现有接口、待抽取 seam、结构化工作区模型和数据 adapter 清单 | 当前 |
| 10 | [编辑器 UI 系统合同](10-editor-ui-system.md) | GUI 组件目录、主题文件、i18n、布局清单、状态与可访问性合同 | 当前 |
| 11 | [插件系统规范](11-plugin-system.md) | 插件 manifest、注册、生命周期、数据/错误边界、战斗/渲染 seam 与编辑器扩展合同 | 当前 |
| 12 | [编辑器 UI 框架 SDD](12-editor-ui-framework-sdd.md) | P13 retained-mode UI 框架的 owner、接口、不变量和验收门禁 | 当前 |
| CTX | [领域词汇](../CONTEXT.md) | 项目内当前支持平台、后续适配平台等统一术语 | 当前 |
| ADR-008 | [当前平台支持边界](adr/0008-current-platform-support.md) | Windows/Linux 当前支持与 macOS 后续适配决策 | 当前 |
| ADR-009 | [编辑器 UI 框架候选调研](adr/0009-editor-ui-framework-research.md) | RmlUi、Dear ImGui、Slint、Qt 的适配性、许可证、RHI 接入和 POC 建议 | 调研完成，候选待确认 |

> Git 边界警告：截至 2026-08-30，`CONTEXT.md`、`12-editor-ui-framework-sdd.md`、`docs/adr/` 与 `docs/archive/` 仍是未跟踪路径。它们在当前工作树可读，但尚不能视为干净 checkout 可复现的真源；由 DEBT-045 跟踪。提交前必须显式审核并逐路径纳入，禁止用 `git add .`。

根目录 [AGENTS.md](../AGENTS.md) 为项目宪法适配层，独立维护，不入本索引编号序列。

## 审计与治理入口

- [未解决问题摘要](04-debt-register.md#未解决问题摘要2026-08-30)：当前优先级、影响和停止条件。
- [完整债务登记](04-debt-register.md#早期主登记表debt-001029)：逐项证据、分级、后续入口与状态。
- [里程碑与阻断](02-milestones.md)：P0–P13 当前状态和完成门禁。
- [ADR-008 平台边界](adr/0008-current-platform-support.md)：Windows/Linux 支持范围与 macOS CI 现实。

根目录 [LICENSE](../LICENSE) 声明本项目默认采用 GNU AGPL v3.0 或更高版本（SPDX：`AGPL-3.0-or-later`）。第三方依赖和素材仍以其各自许可证为准。

## 归档

| 文档 | 归档原因 | 状态 |
|---|---|---|
| [2026-08 里程碑实施与审计记录](archive/02-milestones-history-2026-08.md) | 原 `02-milestones.md` 的逐轮实施、验收与审计证据已被当前状态文档吸收；保留用于历史追溯 | 历史证据，非当前真源 |
