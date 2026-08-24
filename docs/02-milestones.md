# 里程碑与验收门禁

> 状态：当前有效（登记于 [README.md](README.md)）。每阶段跨入下一阶段前，验收证据必须真实存在且可复现（AGENTS.md 纪律 5）。

## 全局门禁（P0 起对每个阶段生效）

1. CI 三平台（Windows/Linux/macOS）build + test 绿灯。
2. 编译 warning 清零：MSVC `/WX`，GCC/Clang `-Werror`。
3. `clang-format` diff 为空；`.editorconfig` 生效。
4. golden image 测试通过（自 P1 起）。
5. 数据 lint CLI 通过（自 P3 起）。
6. 本轮触达的行为变更已回写文档并登记进 `docs/README.md`。
7. 模块私有头审计通过：`engine/*` 各模块 `src/` 私有头不被外部 include（CI 脚本强制，自 P0 起）。

---

## P0 奠基

- **状态**：已完成（2026-08-23）。证据：CI run `32626959709` 全绿（六矩阵 + format + 私有头审计）；本地 win-debug/win-release 双配置零警告、ctest 2/2；AGENTS.md @@COMMAND@@ 已回填。
- **目的**：让"构建-测试-CI"的骨架先于一切功能存在。
- **范围内**：git init、`.gitignore`、`.gitattributes`（P0 执行时补充的跨平台换行治理，超出原清单在此显性记录）、`vcpkg.json`、根 `CMakeLists.txt`、`CMakePresets.json`(win/linux/mac × debug/release)、目录骨架（docs/01 §目录结构，各 engine 模块含 include/src 分离模板）、GitHub Actions 三平台工作流（含模块私有头审计脚本）、`.clang-format`/`.editorconfig`、一个冒烟单测。
- **范围外**：任何引擎功能代码。
- **验收命令与证据**：CI 三平台绿灯；本地 `cmake --preset <triple>-debug && cmake --build --preset <triple>-debug && ctest --preset <triple>-debug` 通过。
- **停止条件**：全局门禁 1–3 通过；AGENTS.md @@COMMAND@@ 节回填实际命令组。

## P1 垂直切片〇：三角形（最高风险期）

- **状态**：进行中。已落地：ADR-003 shader 选型、RHI 合同 v0 头文件（commit `507105c`）、D3D12 后端骨架（设备/命令队列/围栏/空提交链路，WARP 回退路径）、Vulkan 后端骨架（instance→物理设备选择→graphics 队列族→逻辑设备→pool/fence→空提交，volk 加载器，pre-submit fence 等待规避连续提交误用）、清屏基线 golden 实验（R8G8B8A8 离屏 render target clear→readback→逐像素断言；D3D12 真机与 Vulkan/lavapipe 各自落在期望色 ±1 LSB 内，跨后端一致性为间接等价证明——单 CI job 单后端，靠"两后端均命中同一期望色"成立）、**pipeline+三角形 golden 实验**（HLSL 单源经 DXC 编译 DXIL+SPIR-V 字节码提交入库；两后端各自实现 pipeline 创建/绑定/绘制；统一 NDC 方向约定——Vulkan 用负高度 viewport 翻转 Y；D3D12 需显式 `RenderTargetWriteMask` 否则 BlendState 默认 0 导致颜色不写入；三角形逐像素断言双后端一致）、**swapchain+SDL3 主循环**（ISwapchain 双后端实现：D3D12 DXGI FLIP_DISCARD、Vulkan FIFO+sRGB；back buffer 注册进 device texture 表，`AcquireTexture` 返回可直接 `BeginRendering` 的 handle；app 用 SDL3 建窗+固定时间步 60Hz 主循环+Stage 框架占位。本机 D3D12 实机验收：800×600 窗口渲染深灰背景+中央蓝三角形，窗口开关/循环 4 秒无崩溃，截图证据存本机；Vulkan surface 路径在无显示环境（WSL/CI）不可实机跑，仅离屏+错误路径测试覆盖）。CI 已接入软件光栅器：Linux 用 mesa-vulkan-drivers（lavapipe），macOS 用 rerun-io/lavapipe-build arm64 预编译包（`VK_DRIVER_FILES` 指向 ICD）。已落地 shader-sync 门禁 job（Linux 重新生成字节码→`git diff` 为空）。待做：CI golden 流水线（截图+全帧比对，含 DEBT-016/017 闭环）。
- **目的**：打通主循环 + RHI 双后端最小闭环，验证合同层设计成立。
- **范围内**：固定时间步主循环(SDL3 窗口/输入接线)；Stage 合同框架落地（Input→Domain Sim→Animation→Presentation Sync→Render Submit 显式阶段序列，空阶段占位，系统注册须声明所属 Stage 与 within-stage 顺序 order；before/after 依赖图随 P3 落地，见 docs/01 §Stage 运行框架合同）；RHI 合同 v0（device/swapchain/command list/graphics pipeline/buffer 最小集）；D3D12 与 Vulkan 后端各自实现；合同测试框架 v0 + golden image 流水线。
- **开工前预检结论**（2026-08-23）：
  - 依赖可用性：vcpkg 树内已有 `sdl3 3.4.14`、`vulkan`、`volk`、`directx-headers`；无 `dxc` port。shader 编译器选型是 P1 第一个 ADR：候选主线 = DXC 单源双目标（HLSL → DXIL 供 D3D12 + `-spirv` SPIR-V 供 Vulkan），保证双后端 shader 语义同源。
  - CI 渲染策略（golden image 前提）：Windows runner 用 WARP 软件光栅化创建 D3D12 设备；Linux runner 安装 mesa lavapipe；macOS runner 使用 arm64 lavapipe 预编译包（rerun-io/lavapipe-build，2026-02 起）作为 ICD——同一 Vulkan 后端代码，CI 用 lavapipe、真机用 MoltenVK，仅 ICD 不同。三平台均为软件光栅化，golden 基准图必须由同环境生成并标定跨驱动容差。
  - 头号技术风险：WARP 与 lavapipe 的像素输出差异容差标定。golden 流水线第一个实验应为"纯色清屏帧"基线，先证明零几何场景可跨后端零容差一致，再引入三角形与插值容差。
  - 债务联动：DEBT-001（action 升级）、DEBT-002（审计脚本自测）计划在 P1 内顺手关闭（见 [04-debt-register.md](04-debt-register.md)）。
- **范围外**：材质、纹理、3D 数学以外的场景概念。
- **验收命令与证据**：两后端各渲染同一三角形，golden image 比对通过（截图+测试日志）；窗口开关、resize 不崩溃。swapchain 验收为 app 实机证据（本机 D3D12 窗口渲染截图；Vulkan surface 需真实桌面环境，WSL/CI 无显示不可跑，纳入 CI golden 流水线前的离屏近似覆盖）。
- **停止条件**：全局门禁全过。**若合同设计不稳，宁可延期重构，不带病进入 P2。**

## P2 资源与场景

- **目的**：从"渲染代码"走向"渲染数据文件"。
- **范围内**：glTF 2.0 导入管线（静态网格+PBR 材质兼容输入）、句柄式异步资产系统(含卸载)、EnTT 场景+变换层级、飞行动态观察相机、资源泄漏检测。
- **范围外**：骨骼动画、角色控制。
- **验收命令与证据**：CLI 加载指定 glTF 场景 → 两后端 golden image 一致；资产句柄泄漏计数为零的测试通过。
- **停止条件**：全局门禁全过。

## P3 领域核心：事件与对话（JRPG 语义落地）

- **目的**：验证"数据先行"主线成立——剧情内容全部来自文件。
- **范围内**：事件触发器(区域/交互/flag)、事件指令集 schema v1 + 解释器、flag 存储、对话模型(文本框状态/打字机进度/选项分支/立绘槽位/i18n 键)、变更检测投影同步机制 v1（Presentation Sync 只消费带脏标记的 domain 状态变更）、FreeType+HarfBuzz 文本排版(日文禁则用例)、Lua(sol2) 绑定逃生舱、schema lint CLI v1、UI 控件框架最小集(九宫格/文本框/列表)。
- **范围外**：战斗、动画角色、音频接线（audio 层首接线刻意安排在 P6，避免半截集成）。
- **验收命令与证据**：demo 地图中 NPC 由纯 JSON 事件驱动完整对话分支（含中文与日文用例 golden image）；lint CLI 对引用缺失报错清晰。
- **停止条件**：全局门禁全过；CJK 用例不过不得进入 P4（AGENTS.md 纪律 3）。

## P4 角色与世界

- **目的**：玩家可以在世界里移动并与内容交互。
- **范围内**：glTF 骨骼动画导入/采样/混合树(待机-走-跑过渡)、CharacterController(胶囊体+AABB 世界碰撞)、A* 网格寻路、相机合同(第三人称跟随 + 固定机位区域切换)、交互提示 projection。
- **范围外**：战斗、演出时间轴完整版、音频接线（同 P3 决策）。
- **验收命令与证据**：测试关卡内走/跑/转向流畅，NPC 交互→对话全链路录屏或日志证据。
- **停止条件**：全局门禁全过。

## P5 战斗框架（可插拔合同首次实战）

- **目的**：BattleRules 插件合同成立并有一个真实实现。
- **范围内**：数值表 schema(actor 属性/技能/buff/伤害公式管线)、BattleRules 插件接口 + 回合制状态机实现、QTE 时间轴钩子 v0 与输入缓冲合同钩子 v0(各自最小演示)、战斗转场与运镜命令(domain→render 结构化命令)、战斗 UI(HP/SP 条、指令菜单、目标选择)。
- **范围外**：ACT 物理实现（ADR-002：远期插件）、多规则并存切换 UI。
- **验收命令与证据**：数据驱动遭遇战闭环：遇敌→战斗→胜负结算→返回探索；成功标准 3 的插件替换实验记录（证明"新规则零核心改动接入"路径成立，为 ADR-002 远期 ACT 插件背书）。
- **停止条件**：全局门禁全过；战斗闭环达成（[00-product.md](00-product.md) §第一可玩闭环 时点拆分的 P5 子集：移动→遇敌→战斗→胜利返回获得道具，不含存档）。

## P6 风格化渲染与演出

- **目的**：Persona 式视觉形态成型。
- **范围内**：toon shading + 描边、bloom/LUT 后处理栈、UI 缓动动效系统 + UI shader 效果、cutscene 时间轴播放器语义(镜头轨/字幕轨/BGM/SFX 轨)、audio 混音总线、立绘演出完善。
- **范围外**：写实光照、阴影级联优化等非 JRPG 必需项。
- **验收命令与证据**：60 秒 cutscene 时间轴由纯数据驱动播放，双后端 golden image 一致；UI 动效演示。
- **停止条件**：全局门禁全过。

## P7 工具链与竖切组装

- **目的**：交付第一可玩闭环的产品形态。
- **范围内**：版本化存档+迁移函数表、地图数据格式 v2、lint 增强(事件引用的立绘/技能/文本键完整性审计)、30 分钟竖切 demo 组装(纯数据文件)。
- **范围外**：GUI 编辑器（另行立项）。
- **验收命令与证据**：存读档一致性自动化测试；竖切 demo 可玩全程零硬编码剧情的审计清单；docs/00 成功标准逐条勾稽。
- **停止条件**：成功标准 5 条全部有真实证据；完整第一可玩闭环达成（[00-product.md](00-product.md) 完整定义，含存档读档一致性）。

---

## 风险清单

| # | 风险 | 等级 | 阻断策略 |
|---|---|---|---|
| 1 | RHI 双后端行为漂移 | 高 | 合同测试套件 + 同一提交强制同步（纪律 1）；P1 即建立 golden image 流水线 |
| 2 | MoltenVK 兼容性坑 | 中 | P1 起三平台 CI 全程在跑，不留到最后才发现 |
| 3 | Persona 风 UI shader 需求膨胀 | 中 | P6 开始前先做一次 UI 技术预览 spike，锁定效果清单再排期 |
| 4 | CJK 排版复杂度被低估 | 中 | P3 引入 HarfBuzz 当周即跑日文禁则 golden image |
| 5 | 战斗 ACT 化范围蔓延 | 高 | QTE 只做时间轴合同钩子；完整 ACT 物理不在 P0–P7，需用户显式修订 @@BOUNDARY@@ 后立项 |
