# jrpgmaker 项目宪法

> 本文件是用户级全局宪法（`~/.config/opencode/AGENTS.md`）在本项目的适配层。全局宪法继续完全生效，本文只写项目特化条款；冲突时以更严格者为准。

## 基本信息

- 正式名称：`jrpgmaker`（CMake project 名与 C++ 命名空间均为 `jrpgmaker`；早期代号 `jrpgengine` 已废弃，仅存于历史记录）
- 一句话定位：为 JRPG 服务的可复用 3D 引擎
- 工作语言：文档、评审、沟通用中文；代码标识符、commit message 用英文

## 真源入口

@@TRUTH:docs/README.md@@

| 文档 | 职责 |
|---|---|
| `docs/README.md` | 真源索引：所有当前有效文档的唯一登记处 |
| `docs/00-product.md` | 产品定义、目标游戏形态、边界、成功标准 |
| `docs/01-architecture.md` | 分层 owner 与边界合同、技术栈锁定、目录结构 |
| `docs/02-milestones.md` | P0–P7 里程碑、每阶段验收门禁与停止条件、风险清单 |
| `docs/03-engine-survey.md` | 四引擎架构调研、对象模型重评证据、采纳/拒绝清单 |

规则：新增真源文档必须先登记进 `docs/README.md`；未登记或已归档的文档不得作为事实依据。

## 产品边界 @@BOUNDARY:goal/non-goal@@

做（主线）：

- Persona 式视觉形态的 JRPG 引擎能力：3D 场景/迷宫探索 + 高度风格化 2D UI + 角色立绘演出
- 可插拔战斗规则合同：回合制状态机为首个插件实现；QTE 时间轴 / ACT 输入缓冲以预留合同承载，不属于任何内置特权
- 目标平台：Windows(D3D12) + Linux(Vulkan) + macOS(Vulkan/MoltenVK)
- 数据驱动工作流：事件、对话、数值表、演出全部可由纯数据文件表达

不做（禁止漂移方向；修订必须先改本文并经用户确认）：

- 通用全类型引擎、网络多人、开放世界流式加载
- 照片级写实渲染管线（glTF-PBR 仅作为资产导入的兼容输入，不是美术目标）
- 主机、移动端首发
- GUI 编辑器（远期另行立项；第一阶段一律数据文件 + CLI 工具）
- 主线内实现 ACT 物理战斗（QTE 时间轴/输入缓冲以合同钩子承载；完整 ACT 战斗作为远期 BattleRules 插件植入，见 docs/01-architecture.md ADR-002）

## 技术栈锁定 @@STACK@@

| 领域 | 选型 | 锁定理由摘要 |
|---|---|---|
| 语言 | C++20 | 用户决策；行业生态最全 |
| 构建/依赖 | CMake ≥ 3.24 + CMakePresets + vcpkg manifest 模式 | 三平台一致构建 |
| ECS | EnTT | header-only、成熟、社区标准 |
| 数学 | GLM | 与 GLSL 语义对齐 |
| 窗口/输入 | SDL3 | 三平台窗口、输入、剪贴板一站式 |
| 图形 | 自研 RHI 合同层 + D3D12 后端(Win) + Vulkan 后端(Linux/macOS, 经 MoltenVK) | 平台边界决定的双后端结构 |
| 脚本 | sol2 + Lua 5.4 | 复杂逻辑逃生舱；主线仍是数据文件事件指令集 |
| 序列化 | nlohmann/json（文本）+ schema 校验 CLI | 数据先行工作流的基础设施 |
| 字体排版 | FreeType + HarfBuzz | JRPG 必需的中日文禁则处理与整形 |
| 音频 | miniaudio | 起步够用；混音总线自研 |
| 测试 | Catch2 + golden image 渲染比对 | 渲染行为双后端一致性门禁 |

规则：引入上表之外的第三方库前，必须在 `docs/01-architecture.md` 登记选型理由并获用户确认。

## 验收命令 @@COMMAND@@

全局命令组（P0 起生效；各阶段补充项以 `docs/02-milestones.md` "验收命令与证据"为准）：

- 配置：`cmake --preset <win|linux|mac>-<debug|release>`（前置：`VCPKG_ROOT` 指向 vcpkg 实例；Windows 需在 x64 MSVC 开发者环境中）
- 构建：`cmake --build --preset <同名>`
- 测试：`ctest --preset <同名>`
- 模块私有头审计：`pwsh ./tools/ci/check_private_headers.ps1`
- CI 三平台门禁：`.github/workflows/ci.yml`（push/PR 触发；build-test 六矩阵 + clang-format + 私有头审计三 job）

本机（Windows）实况注记，供后续会话复用：MSVC 在 `F:\code`（经 `F:\code\Common7\Tools\VsDevCmd.bat -arch=x64` 进入环境）；CMake 4.4.2 经 winget 安装于 `C:\Program Files\CMake\bin`，Ninja 位于 `%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_*`（两者均需显式注入 PATH）；vcpkg 于 `C:\Users\Vens_\vcpkg`（builtin-baseline 已锁 commit）；代理需设 `HTTP(S)_PROXY=http://127.0.0.1:7897`。

全局门禁（P0 起生效）：CI 三平台 build+test 绿灯、编译 warning 清零（MSVC `/WX`、GCC/Clang `-Werror`）、`clang-format` diff 为空。本节为摘要；完整门禁清单（golden image、数据 lint、文档回写、模块私有头审计等分阶段项）以 `docs/02-milestones.md` §全局门禁为准。

## Owner 边界速记（详见 docs/01-architecture.md）

- 图形 API 语义唯一 owner = `engine/rhi` 合同层。任何其他层禁止 include D3D12/Vulkan 头文件。
- JRPG 业务真相唯一 owner = `engine/domain`。`ui` / `render` / `audio` 只消费 domain 发出的结构化状态、命令与文案 projection。
- adapter（platform / tools / 未来编辑器）只做协议映射与接线，禁止私造业务真相。

## 项目纪律（在全局宪法之上的强化条款)

1. **RHI 双后端同步**：改动 RHI 合同必须同一提交内同步两个后端并通过合同测试套件，否则不得声称完成。
2. **数据先行**：一切剧情内容硬编码即缺陷。发现散落的对话文本、数值常量、事件逻辑时按阻断问题处理。
3. **CJK 是一等公民**：任何文本渲染相关改动必须覆盖中日文用例（换行、禁则、fallback 字体），不覆盖不得合并。
4. **生成物只读**：schema 校验输出、资产转换器产物、golden image 基准图禁止手改；变更必须经由生成命令。
5. **里程碑门禁**：跨入下一阶段前，当前阶段验收证据必须真实存在且可复现；无法复现的证据按未通过处理。
