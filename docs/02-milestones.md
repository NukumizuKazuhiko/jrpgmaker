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

- **状态**：已完成（2026-08-24）。全部范围内项落地（ADR-003、RHI 合同 v0、D3D12/Vulkan 后端、清屏+三角形 golden、swapchain+SDL3 主循环、CI golden 流水线、shader-sync 门禁、Stage 框架落地并接线 kRenderSubmit）；DEBT-001/002 及审计轮 R1-R5 均收口。本地验收全绿：win-debug/WSL ctest 15/15、WARP 与 lavapipe golden 字节级一致、私有头审计+selftest 通过。**CI 全绿门禁待 GitHub 计费恢复后重跑确认**（代码侧已就绪）。
- **目的**：打通主循环 + RHI 双后端最小闭环，验证合同层设计成立。
- **范围内**：固定时间步主循环(SDL3 窗口/输入接线)；Stage 合同框架落地（Input→Domain Sim→Animation→Presentation Sync→Render Submit 显式阶段序列，空阶段占位，系统注册须声明所属 Stage 与 within-stage 顺序 order；before/after 依赖图随 P3 落地，见 docs/01 §Stage 运行框架合同）；RHI 合同 v0（device/swapchain/command list/graphics pipeline/buffer 最小集）；D3D12 与 Vulkan 后端各自实现；合同测试框架 v0 + golden image 流水线。
- **开工前预检结论**（2026-08-23）：
  - 依赖可用性：vcpkg 树内已有 `sdl3 3.4.14`、`vulkan`、`volk`、`directx-headers`；无 `dxc` port。shader 编译器选型是 P1 第一个 ADR：候选主线 = DXC 单源双目标（HLSL → DXIL 供 D3D12 + `-spirv` SPIR-V 供 Vulkan），保证双后端 shader 语义同源。
  - CI 渲染策略（golden image 前提）：Windows runner 用 WARP 软件光栅化创建 D3D12 设备；Linux runner 安装 mesa lavapipe；macOS runner 使用 arm64 lavapipe 预编译包（rerun-io/lavapipe-build，2026-02 起）作为 ICD——同一 Vulkan 后端代码，CI 用 lavapipe、真机用 MoltenVK，仅 ICD 不同。三平台均为软件光栅化，golden 基准图必须由同环境生成并标定跨驱动容差。
  - 头号技术风险：WARP 与 lavapipe 的像素输出差异容差标定。golden 流水线第一个实验应为"纯色清屏帧"基线，先证明零几何场景可跨后端零容差一致，再引入三角形与插值容差。**已闭环（2026-08-24）**：v0 三角形（纯色、无抗锯齿）在 WARP（本机）与 lavapipe（WSL/CI Linux）下全帧 max channel delta=0，两软件光栅在 64×64 光栅化规则一致，tolerance=2 保留余量；未来引入插值/抗锯齿/后处理场景时须重新标定，新增场景的基准图由 lavapipe 权威生成。
  - 债务联动：DEBT-001（action 升级）、DEBT-002（审计脚本自测）计划在 P1 内顺手关闭（见 [04-debt-register.md](04-debt-register.md)）。**均已于 2026-08-24 关闭**：DEBT-001 = checkout v4→v5 + upload-artifact v4→v6（node24）；DEBT-002 = `selftest_private_headers.ps1` 五用例 fixture + check 脚本 `Write-Error`/`Stop` 缺陷修复 + CI selftest step。
- **范围外**：材质、纹理、3D 数学以外的场景概念。
- **验收命令与证据**：两后端各渲染同一三角形，golden image 比对通过（截图+测试日志）；窗口开关、resize 不崩溃。swapchain 验收为 app 实机证据（本机 D3D12 窗口渲染截图；Vulkan surface 需真实桌面环境，WSL/CI 无显示不可跑，纳入 CI golden 流水线前的离屏近似覆盖）。
- **停止条件**：全局门禁全过。**若合同设计不稳，宁可延期重构，不带病进入 P2。**

## P2 资源与场景

- **目的**：从"渲染代码"走向"渲染数据文件"。
- **范围内**：glTF 2.0 导入管线（静态网格+PBR 材质兼容输入）、句柄式异步资产系统(含卸载)、EnTT 场景+变换层级、飞行动态观察相机、资源泄漏检测。
- **范围外**：骨骼动画、角色控制。
- **开工前预检结论**（2026-08-24）：glTF 解析库选型 **cgltf 1.15**（vcpkg port，零依赖单文件 C99，ADR-004，用户已确认）；纹理解码配 vcpkg `stb`（stb_image）。对比淘汰项：tinygltf（nlohmann-json+stb 双依赖、API 重）、fastgltf（simdjson 传递依赖，性能极致非 P2 需求）、assimp（8 个传递依赖，通用格式违背"glTF 仅兼容输入"边界）。
- **子任务 1（已完成，2026-08-24，commit `713e4ed`）**：**RHI 顶点输入扩展**（docs/01 RHI v0 合同明文"顶点输入随 P2 glTF 导入进入合同"，是 glTF 网格数据落入 GPU 的地基，独立可验收）——新增 `BufferHandle`/`BufferDesc`/`CreateBuffer`/`MapWrite` + 顶点输入布局 + `DrawIndexed`，triangle_test 改为顶点缓冲驱动同一三角形（几何不变 → golden 基准图不变），双后端同步。验证：双端构建零警告、ctest 15/15、golden 比对通过、Windows 实机冒烟通过；顺带修复 P1 遗留的 `compile_shaders.ps1` 输出命名不一致缺陷（`.dxil` vs `_dxil`，P1 从未重编译故未暴露）。
- **子任务 2（已完成，2026-08-24）**：**core 资产网格数据合同 + cgltf 适配器 + glTF 驱动 triangle golden**。core 新增 `MeshData`（CPU 侧 positions/indices 纯数据结构，owner 依据 docs/01 core"句柄式资产管理"）；tools/assetimport 新增 cgltf 适配器静态库 `LoadGltfMesh`（ADR-004 落地，owner 依据 docs/01 tools"资产导入"）；新增 `assets/art/meshes/triangle.gltf`（内嵌 base64，几何与 golden 逐顶点一致）；triangle_test 与 goldenimage CLI 均改为 glTF 数据文件驱动 + index buffer + `DrawIndexed(3,1)`（数据先行纪律），golden 基准图未重标定。验证：双端构建零警告、ctest 18/18、goldenimage compare 双模式 delta=0、Windows 实机冒烟通过。
- **子任务 3（已完成，2026-08-24）**：**EnTT 场景 + 变换层级**。vcpkg 引入 `entt 3.16.0` + `glm 1.0.3`（技术栈锁定表既定选型落地，ADR-001 执行）；core 新增 `Scene`（EnTT registry 封装：`CreateEntity`/`SetParent`/`Detach`/`WorldMatrix`/`ChildrenOf`）+ `Transform`（GLM TRS，与 glTF node TRS 对齐）+ `Parent` 组件；assetimport 新增 `LoadGltfScene`（glTF node 层级 + TRS/matrix → Scene 实体，mesh 去重入池挂 `MeshRef`）；新增 `assets/art/meshes/scene_hierarchy.gltf`（父子两级平移场景）。验证：双端构建零警告、ctest 28/28、scene 世界矩阵组合数学单测（平移/旋转/缩放/三级层级/重挂父）、Windows 实机冒烟通过。
- **子任务 4（已完成，2026-08-24）**：**句柄式资产注册表（注册/查询/卸载/泄漏计数）**。core 新增 `AssetHandle`（强类型，`kInvalid=0`）+ `AssetRegistry`（`RegisterMesh`/`FindMesh`/`Unregister`/`live_count`，句柄单调递增不复用）；assetimport 的 `SceneLoad::meshes` 升级为 `AssetRegistry`，`MeshRef` 从 mesh 池索引升级为 `core::AssetHandle`（实体挂句柄，卸载经 `Unregister`）。**异步线程加载明确记录为 P2 后续子任务**：v0 同步加载（assetimport 在调用方线程 parse 后注册），P2 验收"资产句柄泄漏计数为零"已由 `live_count` 归零断言覆盖。验证：双端构建零警告、ctest 35/35（asset 注册/查询/卸载/泄漏/句柄唯一性 7 例）、Windows 实机冒烟通过。
- **子任务 5（已完成，2026-08-24）**：**渲染场景闭环（CLI 加载 glTF 场景 → 双后端 golden 一致）**。goldenimage CLI 新增 `generate-scene`/`compare-scene`（`RenderScene`：导入 glTF 场景 → 遍历带 `MeshRef` 实体 → `Scene::WorldMatrix` 将顶点**烘焙到世界空间** → 逐实体上传 vertex/index buffer → `DrawIndexed`）；`scene_hierarchy.gltf` 变换调整到屏幕内（root 平移 0.25+缩放 0.5、child 平移 0.4，验证平移+缩放两级组合）；lavapipe 权威生成 `tests/golden/scene_64x64.ppm`（WARP 比对 max channel delta=0）；新增 `scene_golden_test` ctest 用例；CI golden-sync 增加 scene 重生成 + 双图 artifact。**取舍记录**：v0 静态场景用 CPU 烘焙世界矩阵，无需 RHI uniform；动态场景（P4 相机/动画）引入 RHI uniform 时改为 GPU 矩阵传递。验证：双端构建零警告、ctest 36/36（scene golden 双后端一致）、Windows 实机冒烟通过。
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
