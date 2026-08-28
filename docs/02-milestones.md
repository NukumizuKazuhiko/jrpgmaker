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

- **状态**：已完成（2026-08-24）。范围内项全部落地（glTF 导入管线 / 句柄式资产系统含异步 / EnTT 场景+变换层级 / 飞行动态观察相机 / 资源泄漏检测）；验收证据真实可复现：双端 ctest 45/45、golden 三图（triangle/scene/camera）WARP 与 lavapipe delta=0、Windows 实机冒烟通过。**CI 三平台绿灯待 GitHub 计费恢复后重跑确认**（代码侧已就绪）。材质/纹理（PBR 兼容输入）经用户确认记录为 P2 之后工作（见子任务 7 取舍）。
- **目的**：从"渲染代码"走向"渲染数据文件"。
- **范围内**：glTF 2.0 导入管线（静态网格+PBR 材质兼容输入）、句柄式异步资产系统(含卸载)、EnTT 场景+变换层级、飞行动态观察相机、资源泄漏检测。
- **范围外**：骨骼动画、角色控制。
- **开工前预检结论**（2026-08-24）：glTF 解析库选型 **cgltf 1.15**（vcpkg port，零依赖单文件 C99，ADR-004，用户已确认）；纹理解码配 vcpkg `stb`（stb_image）。对比淘汰项：tinygltf（nlohmann-json+stb 双依赖、API 重）、fastgltf（simdjson 传递依赖，性能极致非 P2 需求）、assimp（8 个传递依赖，通用格式违背"glTF 仅兼容输入"边界）。
- **子任务 1（已完成，2026-08-24，commit `713e4ed`）**：**RHI 顶点输入扩展**（docs/01 RHI v0 合同明文"顶点输入随 P2 glTF 导入进入合同"，是 glTF 网格数据落入 GPU 的地基，独立可验收）——新增 `BufferHandle`/`BufferDesc`/`CreateBuffer`/`MapWrite` + 顶点输入布局 + `DrawIndexed`，triangle_test 改为顶点缓冲驱动同一三角形（几何不变 → golden 基准图不变），双后端同步。验证：双端构建零警告、ctest 15/15、golden 比对通过、Windows 实机冒烟通过；顺带修复 P1 遗留的 `compile_shaders.ps1` 输出命名不一致缺陷（`.dxil` vs `_dxil`，P1 从未重编译故未暴露）。
- **子任务 2（已完成，2026-08-24）**：**core 资产网格数据合同 + cgltf 适配器 + glTF 驱动 triangle golden**。core 新增 `MeshData`（CPU 侧 positions/indices 纯数据结构，owner 依据 docs/01 core"句柄式资产管理"）；tools/assetimport 新增 cgltf 适配器静态库 `LoadGltfMesh`（ADR-004 落地，owner 依据 docs/01 tools"资产导入"）；新增 `assets/art/meshes/triangle.gltf`（内嵌 base64，几何与 golden 逐顶点一致）；triangle_test 与 goldenimage CLI 均改为 glTF 数据文件驱动 + index buffer + `DrawIndexed(3,1)`（数据先行纪律），golden 基准图未重标定。验证：双端构建零警告、ctest 18/18、goldenimage compare 双模式 delta=0、Windows 实机冒烟通过。
- **子任务 3（已完成，2026-08-24）**：**EnTT 场景 + 变换层级**。vcpkg 引入 `entt 3.16.0` + `glm 1.0.3`（技术栈锁定表既定选型落地，ADR-001 执行）；core 新增 `Scene`（EnTT registry 封装：`CreateEntity`/`SetParent`/`Detach`/`WorldMatrix`/`ChildrenOf`）+ `Transform`（GLM TRS，与 glTF node TRS 对齐）+ `Parent` 组件；assetimport 新增 `LoadGltfScene`（glTF node 层级 + TRS/matrix → Scene 实体，mesh 去重入池挂 `MeshRef`）；新增 `assets/art/meshes/scene_hierarchy.gltf`（父子两级平移场景）。验证：双端构建零警告、ctest 28/28、scene 世界矩阵组合数学单测（平移/旋转/缩放/三级层级/重挂父）、Windows 实机冒烟通过。
- **子任务 4（已完成，2026-08-24）**：**句柄式资产注册表（注册/查询/卸载/泄漏计数）**。core 新增 `AssetHandle`（强类型，`kInvalid=0`）+ `AssetRegistry`（`RegisterMesh`/`FindMesh`/`Unregister`/`live_count`，句柄单调递增不复用）；assetimport 的 `SceneLoad::meshes` 升级为 `AssetRegistry`，`MeshRef` 从 mesh 池索引升级为 `core::AssetHandle`（实体挂句柄，卸载经 `Unregister`）。**v0 同步加载**（assetimport 在调用方线程 parse 后注册），异步线程加载随后在子任务 7 落地。P2 验收"资产句柄泄漏计数为零"已由 `live_count` 归零断言覆盖。验证：双端构建零警告、ctest 35/35（asset 注册/查询/卸载/泄漏/句柄唯一性 7 例）、Windows 实机冒烟通过。
- **子任务 5（已完成，2026-08-24）**：**渲染场景闭环（CLI 加载 glTF 场景 → 双后端 golden 一致）**。goldenimage CLI 新增 `generate-scene`/`compare-scene`（`RenderScene`：导入 glTF 场景 → 遍历带 `MeshRef` 实体 → `Scene::WorldMatrix` 将顶点**烘焙到世界空间** → 逐实体上传 vertex/index buffer → `DrawIndexed`）；`scene_hierarchy.gltf` 变换调整到屏幕内（root 平移 0.25+缩放 0.5、child 平移 0.4，验证平移+缩放两级组合）；lavapipe 权威生成 `tests/golden/scene_64x64.ppm`（WARP 比对 max channel delta=0）；新增 `scene_golden_test` ctest 用例；CI golden-sync 增加 scene 重生成 + 双图 artifact。**取舍记录**：v0 静态场景用 CPU 烘焙世界矩阵，无需 RHI uniform；动态场景（P4 相机/动画）引入 RHI uniform 时改为 GPU 矩阵传递。验证：双端构建零警告、ctest 36/36（scene golden 双后端一致）、Windows 实机冒烟通过。
- **子任务 6（已完成，2026-08-24）**：**飞行动态观察相机（RHI 常量传递落地）**。RHI 合同扩展 `ICommandList::SetPushConstants`（D3D12 root constants 16 DWORD / Vulkan push constants 64 字节 VS stage，pipeline desc 加 `push_constants_size`）；core 新增 `Camera`（eye/target/up + fov/aspect/near/far → view/proj/view-projection，GLM 右手系与 glTF Y-up/NDC-Y 合同一致）；新增 `shaders/camera.hlsl`（ADR-003 单源双目标：SPIR-V 经 `-D VULKAN_PUSH_CONSTANT` 选 `[[vk::push_constant]]` 全局变量，DXIL 走 cbuffer b0，`compile_shaders.ps1` 对 SPIR-V 目标注入宏）；goldenimage 加 `generate-camera`/`compare-camera`（固定观测位 `eye=(2,1.5,2)` 看 `(0.45,0.25,0)`）；lavapipe 权威生成 `tests/golden/camera_64x64.ppm`（WARP 比对 delta=0）；新增 `camera_test`（4 例）+ `camera_golden_test`；CI golden-sync 加 camera 重生成。验证：双端构建零警告、ctest 41/41、Windows 实机冒烟通过。
- **子任务 7（已完成，2026-08-24）**：**句柄式异步资产系统补齐（AsyncLoader）**。assetimport 新增 `AsyncLoader`（后台工作线程仅做 glTF 解析——`LoadGltfMesh` 纯函数，`Submit(path, callback)` 入队即返回，`Poll()` 由调用方线程按提交顺序派发完成回调；worker 只解析、注册发生在调用方线程，故 `core::AssetRegistry` 无需加锁线程安全）。完成回调收到 `std::optional<MeshData>`（失败为 nullopt，加载器不抛异常）；析构停止 worker 并丢弃未完成请求。验证：双端构建零警告、ctest 45/45（async 加载成功/失败/顺序/注册 4 例；WSL 首次暴露 yield 饥饿竞态，测试辅助 `Drain` 改用 `sleep_for` 轮询）、Windows 实机冒烟通过。**P2 收尾取舍（用户确认）**：**材质/纹理（PBR 兼容输入）明确记录为 P2 之后工作**——cgltf 已能解析材质数据，但引擎侧 PBR 渲染（纹理上传 RHI + 采样器 + shader 采样）涉及 RHI 纹理管线横切，不进入 P2 范围；进入 P3 时作为前置或并行项评估。
- **验收命令与证据**：CLI 加载指定 glTF 场景 → 两后端 golden image 一致；资产句柄泄漏计数为零的测试通过。
- **停止条件**：全局门禁全过。

## P3 领域核心：事件与对话（JRPG 语义落地）

- **目的**：验证"数据先行"主线成立——剧情内容全部来自文件。
- **范围内**：事件触发器(区域/交互/flag)、事件指令集 schema v1 + 解释器、flag 存储、对话模型(文本框状态/打字机进度/选项分支/立绘槽位/i18n 键)、变更检测投影同步机制 v1（Presentation Sync 只消费带脏标记的 domain 状态变更）、FreeType+HarfBuzz 文本排版(日文禁则用例)、Lua(sol2) 绑定逃生舱、schema lint CLI v1、UI 控件框架最小集(九宫格/文本框/列表)。
- **范围外**：战斗、动画角色、音频接线（audio 层首接线刻意安排在 P6，避免半截集成）。
- **验收命令与证据**：demo 地图中 NPC 由纯 JSON 事件驱动完整对话分支（含中文与日文用例 golden image）；lint CLI 对引用缺失报错清晰。
- **停止条件**：全局门禁全过；CJK 用例不过不得进入 P4（AGENTS.md 纪律 3）。
- **子任务 1（已完成，2026-08-24）**：**事件/flag 合同 + 事件总线（schema v1 + 解释器 + FlagStore + EventBus）**。core 新增 `EventBus`（类型擦除订阅/发布）；domain 落地 `event_script.hpp`（schema v1：`set_flag`/`clear_flag`/`branch`/`dialog`/`wait`，`Instruction` 递归 variant，解析抛错含上下文）、`flag_store.hpp`（`FlagStore`）、`event_runner.hpp`（`EventRunner`：Start/Tick/IsActive/IsFinished，wait 阻塞用 delta 扣减、branch 按 flag 选子序列、dialog 发 `DialogRequested` 到总线且 v1 不阻塞、分支内 wait 抛 `std::logic_error` 拒绝）。vcpkg 引入 `nlohmann-json` 3.12.0（技术栈已锁定项，`nlohmann_json::nlohmann_json` target）；示例数据 `assets/data/events_demo.json` 被单测实读解析（`event_script` "parses the committed demo data file"）。验证：双端构建零警告、ctest 68/68（event_bus 4 例 + flag_store 4 例 + event_script 8 例含数据文件 + event_runner 7 例含嵌套 wait 拒绝）、Windows 实机冒烟通过。**后续子任务**：对话模型（文本框/打字机/选项分支/立绘/i18n，`DialogRequested` 消费端）、变更检测投影同步 v1、FreeType+HarfBuzz 排版、Lua 绑定、schema lint CLI v1、UI 控件最小集、事件触发器接线。
- **子任务 2（已完成，2026-08-24）**：**对话模型 v1（阻塞握手 + choice 选项分支 + DialogState 投影）**。schema v1 新增 `choice {prompt_text_key, options:[{text_key, instructions[]}]}`（内联指令序列，选项自包含）；`dialog`/`choice` 从"发布即继续"升级为**阻塞握手**：解释器发 `DialogRequested`（含可选项）后暂停，宿主 `AdvanceDialog()`（纯文本）或 `AdvanceDialog(index)`（choice 选支）确认后推进，越界索引抛 `std::out_of_range`、无待确认对话时调用抛 `std::logic_error`；新增 `IsDialogPending()` 与 `DialogState` 结构化投影（打字机/立绘/i18n 留 UI/HarfBuzz 子任务）。**合同约束收紧**：branch/choice 子序列内禁止一切阻塞指令（dialog/choice/wait），解释器抛 `std::logic_error` 拒绝（避免线性 runner 无法表达的嵌套阻塞被静默吞掉）。**缺陷修正**：`events_demo.json` 原在 branch 内嵌 dialog/wait（违反新约束，执行必抛错）——重写为阻塞指令只在顶层、branch/choice 内仅 set_flag，并新增 `alice_ask_help` choice 事件；数据文件测试断言同步为 3 事件，并新增 `event_runner` "executes the committed demo data file"（逐事件完整握手执行验证）。**缺陷修正 2（2026-08-25，审计发现）**：`wait` 指令到期当帧把完整 delta 传给下一条指令导致墙钟时间双计（连续 wait 时间加速、单 wait 后紧跟瞬时指令时间浪费）——`AdvanceOne` 改为 `double&` 引用参数，wait 分支按 `delta >= seconds ? delta-=seconds : wait_remaining_=seconds-delta` 精确流转，到期溢出部分传给下一条指令；新增 2 例精确时间测试（连续 wait(0.5)+wait(0.5) 需累计 1.0s、单 tick 内 wait(0.2)+wait(0.5) 只传溢出 0.3s）。验证：双端构建零警告、ctest 126/126、Windows 实机冒烟通过。
- **子任务 3（已完成，2026-08-24）**：**schema lint CLI v1（引用/一致性检查）**。domain 新增 `event_lint.hpp`/`event_lint.cpp`（`LintEventScript` 返回 `LintIssue{severity, event_id, message}`，纯 domain 语义——引用检查属业务真相，CLI 只做薄接线）；tools/eventlint 新增可执行 `jrpgmaker_eventlint`（读文件→parse→lint，退出码 0=干净/1=有 error/2=用法错误）。检查项（与 docs/01 schema v1 合同对齐）：重复事件 id、空 event id/flag 名/speaker/text_key（error）；branch/choice 子序列静态检出阻塞指令 dialog/choice/wait（error，与解释器运行时 `std::logic_error` 同源约束，作者期提前暴露）；branch 读取的 flag 在脚本内从未被写入（warning，flag 可由宿主注入，未写入常是拼写/死分支症状）。**text_key→i18n 文本表引用检查明确范围外**（文本表 schema 随 i18n 子任务落地后扩展）。CI 新增 `data-lint` job（linux-debug 构建后对 `assets/data/events_demo.json` 要求 lint 干净）。验证：双端构建零警告、ctest 83/83（新增 event_lint 8 例：干净通过/重复 id/空 id/未写 flag warning/跨事件写 flag 不误报/branch 嵌套阻塞/choice 嵌套阻塞/空字段）、CLI 实跑干净文件 exit 0 与坏文件 exit 1（临时坏文件已删）、WSL CLI 对 demo 文件 exit 0、Windows 实机冒烟通过。
- **子任务 4（已完成，2026-08-24）**：**变更检测投影同步 v1（A3：Presentation Sync 只消费带脏标记的 domain 状态变更）**。`EventRunner` 在执行时经 `EventBus` 广播四个投影事件：`FlagChanged{flag, value}`（每次 set_flag/clear_flag 写入，含 choice 选支内写入——`RunSequence` 同源广播）、`EventStarted{event_id}` / `EventFinished{event_id}`（生命周期，`Start` 对未知事件 id 返回 false 时不广播）、`DialogRequested`（已有）。`FlagStore` 仍 domain 独占，ui/render 只订阅投影事件不直读（docs/01 owner map）。宿主直接注入 flag 时自行负责投影（脚本驱动路径由 runner 统一广播）。验证：双端构建零警告、ctest 88/88（新增 5 例：set_flag 广播两连写、choice 选支广播、生命周期 start/finish、未知事件 id 不广播、set_flag 先于 dialog 阻塞广播）、Windows 实机冒烟通过。**后续子任务**：FreeType+HarfBuzz 排版、Lua 绑定、UI 控件最小集、事件触发器接线（flag 触发器可消费 `FlagChanged`，见 P3 范围内项）。
- **子任务 5（已完成，2026-08-24）**：**事件触发器接线（flag 触发器，数据文件驱动）**。domain 新增 `flag_trigger.hpp`/`flag_trigger.cpp`：`FlagTrigger{flag, target_event_id}` 数据合同 + `ParseFlagTriggers`（触发器表 schema v1：`{schema, triggers:[{flag, target_event_id}]}`，重复 flag 绑定/空字段解析期 `std::invalid_argument`）；`FlagTriggerSystem` 订阅 `FlagChanged`，**边沿触发**（false→true 只触发一次，重复 set true 不重触发，flag 回 false 后重新 true 再触发，false 不触发，未绑定 flag 忽略），回调收到 target_event_id。新增 `assets/data/triggers_demo.json`（`alice.quest.accepted`→`alice_reward`、`chest.west.opened`→`chest_west_echo`）。**接线约束（测试暴露的真实陷阱）**：回调在 `EventRunner::Tick` 内同步触发（`FlagChanged` 广播时源事件仍在完成中，`runner.Start` 会抛 "Start while an event is already active"）——宿主必须排队到下一事件边界启动，端到端测试演示该游戏循环模式。区域/交互触发器依赖 P4 世界交互，明确不在 P3 范围。验证：双端构建零警告、ctest 97/97（新增 flag_trigger 9 例：解析/重复绑定拒绝/空字段拒绝/边沿触发一次/重复 true 不触发/false 不触发/回 false 重触发/未绑定忽略/数据文件解析/端到端接线）、Windows 实机冒烟通过。
- **子任务 6（已完成，2026-08-24）**：**FreeType+HarfBuzz 文本排版库层（`engine/ui` 首次落地，纯 CPU 无渲染）**。vcpkg 引入 `freetype 2.14.3`（`default-features:false` 裁剪 brotli/bzip2/png，仅 core）+ `harfbuzz 14.3.1`（zlib 源码包经代理手动缓存命中 `madler-zlib-v1.3.2.tar.gz`；vcpkg 代理 SSL 问题按既有 `VCPKG_DOWNLOADS` 缓存规避）。新增 `engine/ui` 模块（owner map 目标形态首次落地）：`jrpgmaker::ui::Font`（FreeType 加载/face_index/字体单位度量/glyph 位图度量）、`TextShaper`（HarfBuzz shape UTF-8→`TextRun`，advance 像素化）、`LineBreaker`（**CJK 禁则换行**：行首禁则=行不以闭括号/悬挂标点/小写假名开头；行尾禁则=行不以开括号结尾；超宽单字强制成行防死循环）。**缺陷修正**：初版 LineBreaker 贪心扫描逻辑在溢出点处理禁则不正确（行尾禁则候选 break 被后续覆盖），重写为"贪心填满→行尾禁则回退→行首禁则悬挂"三阶段；font ascender/descender 初版取 `face->size->metrics`（size 未初始化恒 0），改取 `face->ascender/descender`（字体单位）。**工程适配**：MSVC 对含 UTF-8 字面量的测试源加 `/utf-8`（C4819 会被 /WX 提升）；`TextShaper::Impl` 构造在 GCC 下不能写 `TextShaper::Impl()`（限定名非法，MSVC 宽容），改 `Impl()`。**测试字体策略**：Windows `msgothic.ttc` / macOS `STHeiti` / Linux `fonts-noto-cjk`（CI Linux 已装 + WSL 本地装验证真加载）；无字体环境 font/shaper 测试 SKIP（明确原因），禁则换行测试自包含（手造 glyph 零字体依赖，双端必跑）。验证：双端构建零警告、ctest 107/107（新增 10 例：6 禁则换行 + 4 font/shaper，双端真字体加载 21 断言）、Windows 实机冒烟通过。**范围外（明确记录）**：文字栅格化/纹理化（依赖 DEBT-029 纹理管线），属 UI 渲染子任务。
- **子任务 7（已完成，2026-08-24）**：**Lua(sol2) 绑定逃生舱（受限 API 表面）**。vcpkg 引入 `lua 5.5` + `sol2 3.5.0`（WSL 侧 lua-5.5.1/sol2-v3.5.0 源码经代理正常下载，Windows 侧同 baseline 已装）。domain 新增 `lua_binding.hpp`/`lua_binding.cpp`：`LuaScriptEngine`（`FlagStore&` + 宿主 `EventTrigger` 回调）。**受限 API**（docs/01 line 120 合同：Lua 只是逃生舱，禁止绕过事件指令集私造流程）：`flags.get/set`（FlagStore 读写）、`events.run(event_id)`（经宿主回调请求启动 EventRunner 事件，宿主决定合法性，返回 bool）、`log(message)`；**不暴露**指令解析/执行/schema/EventRunner 内部。**缺陷修正**：sol2 `state::script` 对语法/运行错误均抛 `sol::error`（不捕获会崩溃、测试断言误判），`Run`/`Call` 改为 try/catch 捕获存入 `last_error()` 返回 false。验证：双端构建零警告、ctest 114/114（新增 lua_binding 7 例：flags 读写/events.run 触发/宿主拒绝返回 false/脚本错误报告/全局函数调用/缺函数报告/逃生舱受限性——`instructions`/`schema`/`event_runner`/`parse` 全局不存在且 flags/events 表只暴露预期函数）、Windows 实机冒烟通过。
- **子任务 8（已完成，2026-08-24）**：**RHI 纹理采样管线（DEBT-029 前置部分，P3 收尾的 UI 控件/文字栅格化共同地基）**。RHI 合同扩展：`SamplerHandle`/`SamplerDesc{filter,address}`/`CreateSampler`/`DestroySampler`/`IDevice::UploadTexture`/`ICommandList::SetSampledTexture`/`GraphicsPipelineDesc.sample_slot`（0=默认无采样，现有 pipeline 零影响）；`VertexAttribute` 增 `kFloat2` 与 `semantic_name`（D3D12 input-element 语义名，Vulkan 按 location 匹配）。**双后端同步实现（纪律 1）**：D3D12 root signature 增 SRV t0 + sampler s0 两个 pixel descriptor table + 独立 shader-visible CBV_SRV_UAV/SAMPLER heap（slot 池）；Vulkan descriptor set（binding0=SAMPLED_IMAGE、binding1=SAMPLER，fragment）经 `[[vk::binding]]` 与 HLSL `register(t0)/register(s0)` 对齐（`compile_shaders.ps1` 对 -spirv 注入 `VULKAN_TARGET` 宏）。上传路径后端内部 staging+barrier+copy（与 `MapReadBack` 对称），D3D12 上传后 PIXEL_SHADER_RESOURCE、Vulkan SHADER_READ_ONLY_OPTIMAL。**工程适配**：GCC `-Werror=missing-field-initializers` 要求 `VkDescriptorSetLayoutBinding.pImmutableSamplers` 显式置 nullptr；`D3D12CommandList::Native()` 返回基类 `ID3D12CommandList*` 无 copy/barrier 方法，上传逻辑封装为 `CopyBufferToTexture`（与 Vulkan `CopyBufferToTexture` 对称）。新增 `shaders/textured.hlsl`（全屏 quad 采样）与 `texture_quad_test.cpp`；goldenimage CLI 增 `generate-texture`/`compare-texture` 命令。**跨驱动一致性**：2×2 四色纹理（nearest/clamp）全屏采样，WARP 与 lavapipe delta=0（4096 像素），`tests/golden/texture_quad_64x64.ppm` 由 lavapipe 权威生成。验证：双端构建零警告、ctest 117/117（新增 texture_quad golden 1 例 + rhi_contract 3 例）、Windows 实机冒烟通过、私有头审计 OK（70 文件）。**范围外（明确记录）**：glTF 材质纹理（stb 解码 + material 入 `SceneLoad`）留给 P6 渲染风格插件与材质 schema 委派轮次（DEBT-029 剩余部分）。
- **子任务 9（已完成，2026-08-24）**：**UI 控件框架最小集（`engine/ui` 纯 CPU 层，九宫格/文本框/列表）**。新增 `jrpgmaker::ui`（`widget.hpp`）：保留式控件树 `Widget`（id/visible/parent/AddChild，owns children，`Layout(available)` 存 widget-local rect 并递归布局）、`Panel`（`NineSlice` 背景 + `Padding` 内容区，子控件布局进 padding 后区域）、`TextBlock`（`Font*` 借用 + TextShaper/LineBreaker，`Layout` 用可用宽度排版，尺寸=排版高度）、`List`（垂直堆叠可见子项按自然高度 + spacing，占满可用宽）；`SliceNine` 纯几何把矩形切 3×3 九宫格（四角保留/边单向拉伸/中心填充），切片内缩超目标时钳制中心归零且不重叠。**范围决策（用户确认）**：render 层仍为空壳（仅 .gitkeep），本轮刻意做纯 CPU 控件框架——九宫格/文本框/列表的布局语义先落地并可单测，**渲染接线（ui→render→rhi 绘制已布局 rect）留 render 层落地时做**，避免与材质工作纠缠、符合"最小一步"。**工程适配**：`Widget` 构造声明后须实现（MSVC LNK2019 暴露未定义构造）。验证：双端构建零警告、ctest 124/124（新增 widget 7 例 52 断言：树父子/九宫格区域与面积守恒/超限钳制/面板 padding 内容区/列表堆叠+隐藏项/TextBlock CJK 排版+空文本归零）、Windows 实机冒烟通过、私有头审计 OK（73 文件）。**P3 范围内项至此全部落地**（事件/对话/投影/触发器/排版/Lua/lint/UI 控件最小集）；P3 验收"CJK 用例 golden image"由排版库 + 事件数据文件执行测试覆盖，golden 渲染一致性由 P1/P2 三角形/场景/相机 golden + 本轮 texture quad golden 共同支撑。
- **审计修复批 1（2026-08-25，codegraph 复核后）**：**schema 校验严格化 + 对话投影收敛 + demo 数据自洽 + Lua 投影接线**。#3：`set_flag` 缺 `value`、`wait` 缺 `seconds` 由静默默认改为解析期 `std::invalid_argument`（docs/01 合同"缺字段抛错"落地；新增 2 例抛错测试，16 处无 value 测试数据补 `"value": true`）。#4：移除从未发布的 `DialogState` 死结构体，`DialogRequested` 收敛为唯一结构化投影（docs/01 更新）。#6：`events_demo.json` 补 `alice_reward`/`chest_west_echo` 两事件使 `triggers_demo.json` 引用有效，flag_trigger 数据文件测试端到端断言每个 `target_event_id` 在事件表存在（防触发后 `Start` 静默 false）。#8：`LuaScriptEngine` 构造增加 `core::EventBus&`，`flags.set` 发布 `FlagChanged`（与 EventRunner 同源投影，修复 Lua 置位后 flag 触发器静默不触发；新增投影广播测试 1 例）。验证：双端构建零警告、ctest 129/129（+3）、clang-format 合规、Windows 实机冒烟通过。**剩余**：RHI 批（#1 D3D12 UploadTexture 越界读、#7 sample_slot 死字段）与 CI 批（#5 golden-sync/data-lint 漏网）待修。
- **审计修复批 2（2026-08-25，RHI 层）**：#1 D3D12 `UploadTexture` 越界读——逐行 `memcpy` 用 footprint `RowPitch`（256 对齐）读 tight 源（每行仅 `row_pitch_bytes` 保证），修复为 `copy_bytes = min(pitch, row_pitch_bytes)`；golden 复验 delta=0 证明修复后跨驱动仍一致。#7 `sample_slot` 死字段——双后端 `CreatePipeline` 曾从不读取 `desc.sample_slot`，`SetSampledTexture` 注释"Requires sample_slot>0"无校验：现两端 `PipelineEntry` 记录 `sample_slot`、command list `SetPipeline` 记录 bound pipeline、`SetSampledTexture` 对未声明采样的 pipeline 抛 `std::runtime_error`（合同字段真正生效，Speculative Generality 消除）。验证：双端构建零警告、ctest 130/130（新增"非采样 pipeline 绑定采样抛错"1 例）、clang-format 合规、Windows 实机冒烟通过、私有头审计 OK（73 文件）、texture_quad golden delta=0。**剩余**：CI 批（#5 golden-sync/data-lint 漏网）待修。
- **审计修复批 3（2026-08-25，CI 门禁层）**：#5 两处门禁漏网收口。**golden-sync**：CI 新增 `generate-texture ./tests/golden/texture_quad_64x64.ppm`（此前只重生成 triangle/scene/camera 三图，texture_quad 基准图无漂移门禁）+ artifact 列表补该图——"生成物只读"强制执行点全覆盖。**data-lint**：`jrpgmaker_eventlint` 新增 `--check-triggers <events.json> <triggers.json>` 交叉检查（每个触发器 `target_event_id` 必须在事件脚本存在，否则触发后 `EventRunner::Start` 静默返回 false——报错"firing would silently no-op"）；CI data-lint 对 `events_demo.json` + `triggers_demo.json` 跑该检查。CLI 验收：干净路径 exit 0、临时坏文件（target 指向 ghost_event）exit 1 且报错清晰、WSL exit 0。验证：双端构建零警告、ctest 130/130、clang-format 合规、Windows 实机冒烟通过、私有头审计 OK（73 文件）。**P3 审计 8 项全部修复**（#1 D3D12 越界读、#2 wait 双计、#3 schema 静默默认、#4 DialogState 死结构体、#5 CI 门禁、#6 triggers 悬空引用、#7 sample_slot 死字段、#8 Lua 投影）。**遗留设计风险（记录，未修）**：#9 P3 验收"CJK golden image"以排版单测 + texture quad golden 替代（子任务 9 已记录）；lint `log()` 空操作存根、`widget.cpp` 未 Load 字体除零、`FlagTriggerSystem` 线性扫描均属可记录债务。
- **CI 三平台首跑（2026-08-25，仓库设公开后解除计费阻塞）**：run `32836347525`（HEAD `6392a46`）首次真正启动全部 11 job。**发现并修复两处**：(1) macOS Clang `-Werror -Wunused-lambda-capture`——`lua_binding.cpp` 的 `log` lambda 捕获 `[this]` 未使用（MSVC/GCC 不报，Clang 报），修复为无捕获（commit `f8c14c0`）；(2) **DEBT-012 首次实锤**——shader-sync 漂移：同一 vcpkg baseline 下 Windows/Linux dxc 二进制版本不同（win `1.9.2602.24` vs linux `1.9.0.5191`），Windows dxc 生成的 DXIL 在 CI Linux dxc 重编译必漂移（6 个 .dxil 各 +16 字节版本戳，SPIR-V 不受影响）——字节码改为 **Linux dxc 权威生成**并提交（commit `d231c86`），Windows 用新字节码渲染 golden 双端 delta=0 证明语义不变。修复后 run `32838288316`（HEAD `f8c14c0`）：format/private-headers/golden-sync/Windows×2/Linux×2/macOS×2 全绿；shader-sync 在字节码提交后（run `32840154811` HEAD `d231c86`）转绿；**data-lint 连续两次因缺 Linux 系统构建工具**（autoconf/automake/libtool 未装，vcpkg 被迫从 ftpmirror 下载 automake port 遇 502）失败——补 apt 安装步骤（commit `fcdc0f1`）。
- **CI 三平台全绿（2026-08-25，run `32841512701` HEAD `fcdc0f1`）**：**P3 里程碑门禁首次真实全绿**（此前受 GitHub 计费阻塞，代码侧早已就绪）。11/11 job success：build-test 六矩阵（Windows/Linux/macOS × debug/release）+ format + shader-sync + golden-sync（triangle/scene/camera/texture_quad 四基准图漂移门禁）+ data-lint（events_demo 单文件 lint + triggers 交叉检查）+ private-headers（含 selftest）。**P3 里程碑完整达成**：范围内子任务 1-9 + 审计 8 项修复全部落地，三平台 build+test 绿灯、warning 清零、golden 一致、数据 lint 干净、私有头审计通过。

## P4 角色与世界

- **目的**：玩家可以在世界里移动并与内容交互。
- **范围内**：glTF 骨骼动画导入/采样/混合树(待机-走-跑过渡)、CharacterController(胶囊体+AABB 世界碰撞)、A* 网格寻路、相机合同(第三人称跟随 + 固定机位区域切换)、交互提示 projection。
- **范围外**：战斗、演出时间轴完整版、音频接线（同 P3 决策）。
- **验收命令与证据**：测试关卡内走/跑/转向流畅，NPC 交互→对话全链路录屏或日志证据。
- **停止条件**：全局门禁全过。

- **子任务 1（已完成，2026-08-25）**：**glTF 骨骼动画导入/采样/混合树（蒙皮渲染双后端 golden 闭环）**。四块横切落地：(1) **RHI per-object uniform（骨骼矩阵通道）**：`BufferUsage::kUniform`、`GraphicsPipelineDesc.vertex_uniform_size`（声明 VS uniform 块字节数，>0 则 draw 前 `SetVertexUniformBuffer` 强制）、`ICommandList::SetVertexUniformBuffer(buffer, size)`；D3D12 用 root CBV（root signature 参数 3，register b1，VS visibility，`SetGraphicsRootConstantBufferView` 直指 UPLOAD heap buffer 的 GPU VA）；Vulkan 在共享 sample descriptor set 上加 binding2 = UNIFORM_BUFFER（VS stage），`vkUpdateDescriptorSets` 后 bind set 0——两后端均校验 pipeline 已声明 uniform 且 size 不超限（对齐 sample_slot 的强制合同模式）。顶点格式扩展 `kUint16x4`（JOINTS_0，R16G16B16A16_UINT 双端一致）与 `kFloat4`（WEIGHTS_0）。(2) **资产导入（assetimport）**：cgltf skin→`SkeletonAsset`（joint 层级 parent 链重索引 + inverseBindMatrices 列主序直读 + **节点静态 TRS 作为 bind 基础姿态**，动画只覆盖驱动的通道）；animation→`AnimationAsset`（channels/samplers 1:1 映射 `KeyframeChannel`，LINEAR/STEP/CUBICSPLINE 三插值全支持）；mesh 导入 JOINTS_0/WEIGHTS_0（`MeshData::skinned()`）；带 skin 的 mesh 节点挂 `SkinRef`（glTF node.skin）。**导入陷阱记录**：`cgltf_accessor_read_index` 仅支持单组件类型，VEC4 JOINTS 必须用 `cgltf_accessor_read_uint` 按 element 读。(3) **核心动画（core/animation.hpp，纯逻辑无 RHI 依赖）**：`Skeleton`（≤kMaxBones=32，joint 顺序任意，构造期拒绝越界 parent/环）/`SamplePose`（静态 TRS 起底+通道覆盖，quat slerp/CUBICSPLINE Hermite）/`BlendPose`（T/S lerp + R slerp，双 clip 权重混合即 v0 混合树：待机↔走↔跑单参数过渡）/`BoneMatrices`（局部 TRS 按 parent 依赖记忆化合成全局 × IB）。(4) **渲染闭环**：`shaders/skinned.hlsl`（ADR-003 单源双目标，cbuffer b1 / [[vk::binding(2,0)]]，逐顶点 4 骨骼加权变换）；测试资产 `arm_skinned.gltf`（两骨手臂 + idle/wave/cubic 三 clip，其中 cubic 锁定合法 CUBICSPLINE accessor 布局）；goldenimage `generate-skin`/`compare-skin <ref> <gltf> [time] [blend]`；三姿势 golden（bind/wave90°/blend45°）lavapipe 权威生成，WARP 比对 tolerance=2 全过。CI golden-sync 增补三图重生成。**调试教训**：无窗口会话中未捕获异常 abort() 会弹 CRT 对话框假死（表现为进程 CPU≈0 挂起），goldenimage main 已加顶层 try/catch。验证：双端构建零警告、ctest 139/139（新增 animation 6 例 81 断言：kMaxBones 拒绝/bind pose IB 抵消恒等/线性插值半程角/STEP 保持/blend 权重端点与中间/slerp 半程、skin 导入 1 例含 SkinRef 回归断言、skin golden 3 例）、私有头审计 OK（77 文件）、Windows 实机构建冒烟通过。**2026-08-26 审查修复 A1**：纠正 CUBICSPLINE 导入时把 accessor 元素数重复乘 3 的计数错误，并修正采样夹到首尾时按非 cubic stride 误读 tangent 的偏移错误；`[cubic-spline]` 2 例 44 断言与 Windows/D3D12、Linux/Vulkan 全量 ctest 各 140/140 通过。**2026-08-26 审查修复 A2**：移除 `BoneMatrices` 对“parent 必须先于 child”的非法假设，按依赖求值任意合法 `skin.joints` 顺序；`Skeleton` 同步收紧 parent 范围与无环合同；`[joint-order]` 2 例 5 断言与双平台全量 ctest 各 142/142 通过。**2026-08-26 审查修复 A3**：双后端在原生 Draw 前统一校验 active rendering、pipeline、vertex/index buffer 及 pipeline 声明的 push constants/sample slot/vertex uniform；`Begin()` 清空全部绑定镜像且 Vulkan 显式 reset command buffer，重复提交测试改为复用同一 command list；Windows/D3D12、Linux/Vulkan 全量 ctest 各 143/143 通过。**范围外记录**：动画状态机（clip 图/过渡时间线）与 CharacterController 驱动留子任务 2+；每角色多 uniform ring buffer（v0 单 buffer 单对象）留 P6 演出轮评估。

**审查修复 A4（2026-08-26）**：`BufferEntry` 保存 `BufferDesc.usage`，双后端 vertex/index/uniform 绑定拒绝用途不匹配 buffer；新增 `[buffer-contract]` 回归测试，Windows/D3D12、Linux/Vulkan 全量 ctest 各 144/144 通过。
**审查修复 P3-9（2026-08-26）**：`TextBlock::Layout` 对未加载字体、零 `units_per_em` 和零像素高度安全归零，消除除零风险；新增 `[textblock]` 回归测试。

## P4 实施记录（2026-08-26 起）

- 已落地 `core::CharacterController`（Y 轴胶囊体/AABB swept 碰撞、重力、落地、顶头和墙体滑动，单帧最多 3 次接触解析）、`NavigationGrid`（显式网格、确定性四方向 A*、结构化失败结果、网格/世界坐标转换）、`CameraRig`（第三人称跟随、固定区域优先级和按声明时长的确定性过渡）。角色控制器位置已同步到场景根节点，locomotion 状态驱动动画 clip 选择。
- 已落地版本化 `navigation_*.json`、`collision_*.json`、`camera_*.json`、`interaction_*.json` 解析合同；解析期校验 schema、类型、范围、重复 ID，domain 交叉校验交互目标事件引用。
- 已落地 domain `InteractionSystem`：最近距离/稳定 ID 选择、进入/离开提示 projection、确认边沿检测和事件边界队列；`app` 接入 SDL WASD/E、`kInput → kDomainSim → kAnimation → kPresentationSync → kRenderSubmit`，移动速度驱动 idle/walk/run 状态，事件 runner 保持非重入；确认事件在 runner 忙碌时进入待处理队列，运行时悬空目标显式报错。
- 测试覆盖角色碰撞角部、寻路结构化失败、相机完整过渡、地图解析、交互到 `DialogRequested` 的端到端链路；Windows/D3D12 全量 `ctest` **162/162** 通过。`eventlint --check-map` 对四类 P4 数据及事件引用实跑通过。`clang-format --dry-run --Werror`、`git diff --check`、私有头审计均通过。Windows/macOS 实机/CI 证据按当前主机能力另行补充，未伪造为本地通过。

## P5 插件基础设施与战斗插件验证

- **目的**：建立通用源码级插件宿主，并以两个真实战斗插件证明“规则由开发者自主定义、替换时 `engine/` 零改动”。
- **范围内**：项目清单、插件 manifest/schema v1、类型化注册表与构建期工厂、validator 调度、战斗会话 seam、数据化输入 action、通用 UI/camera/transition/animation 输出、遭遇会话调度、两个参考战斗插件及替换实验。
- **范围外**：引擎统一 actor/skill/buff/伤害公式 schema；引擎内置回合制/QTE/ACT 语义；跨编译器 DLL 热加载 ABI；渲染风格插件实现（P6）；多插件并行运行 UI。
- **开工条件**：P4 的 Windows 与 WSL 验收已完成；按用户决策，P5 开发阶段优先 Windows。P4/P5 的 macOS 与完整 CI 证据可后补，但在全局门禁补齐前不得把 P5 标记为完成。

### P5 子任务与执行顺序

#### P5-0 真源与目录合同

- **实现**：回写 `docs/00`、`docs/01`、`docs/02` 与项目宪法；新增 `engine/plugin/` 和 `plugins/` 的目录/依赖约定。根 CMake 只提供统一插件注册入口，具体插件不得由 app 通过 ID 分支选择。
- **测试/门禁**：文档中不得再把固定画风、回合制、QTE、actor/skill/buff schema 写成引擎真相；私有头规则覆盖 `engine/plugin` 与 `plugins/*` 公私有目录。
- **停止条件**：owner、seam、错误模式、源码级插件边界和 P5 非目标无歧义。
- **建议提交**：`docs(p5): redefine project and plugin boundaries`。

#### P5-1 项目清单与通用插件宿主

- **实现**：新增版本化 `project.json` 与插件 `plugin.json`；实现 `PluginId`、`PluginType`、`PluginManifest`、类型化 `PluginRegistry`、构建期 factory 和结构化 `PluginError`。manifest 至少声明 `schema/id/type/version/engine_contract/data_roots/capabilities`。
- **强制合同**：拒绝重复 ID、未知类型、合同版本不兼容、缺失工厂、目录越界和缺失数据；registry 容量有上界，工厂和会话由宿主显式销毁；插件异常不得穿过宿主边界。
- **测试**：manifest 正常/缺字段/重复 ID/版本不兼容/工厂缺失/路径越界；同一 registry 注册两个 adapter 并按类型创建。
- **Windows 验收**：定向单测 + `cmake --build --preset win-debug` + `ctest --preset win-debug --output-on-failure`。
- **停止条件**：app 能只根据项目清单选择插件，且不存在具体插件 ID 分支。
- **建议提交**：`feat(plugin): add project manifest and build-time registry`。

**当前进度（2026-08-27）**：已落地项目清单 `project_demo.json` 的插件选择、插件类型/工厂/数据根校验，以及 app 按清单装配 render style 和 battle plugin；registry 仍为构建期源码注册。Windows Debug 构建通过，新增战斗插件/遭遇与材质消费测试后全量 ctest **209/209** 通过。P5-1 的跨平台与统一 validator 调度仍按后续门禁处理。

#### P5-2 插件 validator 与项目数据 lint

- **实现**：定义 validator seam；项目 lint 先验证 manifest/文件边界，再把插件私有数据交给对应插件 validator。宿主只聚合结构化 issue，不解释私有字段。
- **测试**：未知 validator、插件数据损坏、跨文件悬空引用、同插件多文件错误聚合、warning/error 退出码、单文件和总字节数上界。
- **数据**：提交最小 `project_demo.json` 和两个战斗插件的数据 fixture；actor/skill/buff 等仅允许出现在插件目录。
- **停止条件**：错误在作者期可定位到插件 ID、文件和字段路径；`eventlint` 或后继统一 CLI 可一次校验项目与插件数据。

**当前进度（2026-08-28）**：`IPlugin::ValidateData`、有界数据读取、两个样例插件私有 schema 校验和 `eventlint --check-project` 已闭合；插件 validator 不进入 `engine/domain`，坏数据按插件/文件/路径聚合并返回非零。Windows 相关测试与项目 lint 已通过。
- **建议提交**：`feat(plugin): add delegated data validation`。

#### P5-3 战斗会话 seam

- **实现**：定义 `IBattlePlugin::CreateSession(ExtensionLaunchContext)`、`IBattleSession::Advance(FrameInput)` 与 `Snapshot()`；`FrameInput` 只包含逻辑 tick/delta、项目 action id 和宿主控制，`FrameOutput` 只包含运行状态、通用 presentation 命令、`result_key` 与有界 opaque payload。
- **强制合同**：宿主不得出现 HP/SP、技能、回合、目标、QTE、胜负等字段；每帧输入数量、输出命令数和 payload 字节数有上界；非法状态转换、插件错误和超预算返回结构化失败。
- **测试**：生命周期、重复启动、空闲 tick、完成后调用、错误传播、输入/输出上界、确定性重放。
- **停止条件**：测试中的两个行为不同 adapter 可通过相同 seam 运行，删除其中任一个不影响 engine 构建。
- **建议提交**：`feat(plugin): add battle session seam`。

**当前进度（2026-08-28）**：`engine/plugin/battle.hpp` 提供有界 `BattleLaunchContext`、`BattleFrameInput`、`BattleFrameOutput`、`BattleSnapshot` 与结构化校验；`PresentationCommand{id,payload}` 作为通用 presentation seam，payload 总预算 64 KiB；`sample_instant` 和 `sample_turn_based` 各自校验私有 encounter schema，app 不解释插件规则。输入合同新增 `engine/core/input_actions.hpp` 与 `assets/data/input_actions.json`，SDL 只把配置中的物理键转换为稳定 action id。Windows 定向测试和 demo 项目 lint 已通过；全量门禁待本轮结束后更新。

#### P5-4 输入与 presentation 通用合同

- **实现**：新增数据化 `input_actions.json`，SDL 只映射稳定 action id；定义通用 UI draw/view model、相机、转场和动画请求。战斗插件可提供 presentation adapter，但不得直接访问 D3D12/Vulkan 或 domain 私有状态。
- **测试**：按下/释放边沿、逻辑 tick、缓冲容量、未知 action、UI 命令生命周期、相机/转场命令顺序和预算超限。
- **停止条件**：app 无具体战斗按键、菜单或结果分支；同一输入/presentation seam 可被两个插件消费。
- **建议提交**：`feat(plugin): add extension input and presentation contracts`。

**当前进度（2026-08-28）**：输入 action map 已由 core 解析并由 app 按配置构建 SDL 绑定，覆盖移动、确认、保存和读取；battle 输出已升级为带 payload 预算的通用 `PresentationCommand`，已有 UI/dialog projection、相机状态和动画请求均通过结构化状态消费。core parser、按键映射数据、按下/释放边沿和 presentation 空/超预算回归测试已落地；P5-4 合同闭环，具体 UI 绘制实现仍由 render/ui 消费层按项目需要扩展。

#### P5-5 遭遇与项目事件闭环

- **实现**：新增数据驱动遭遇点/区域，进入时只排队 `ExtensionRequested`；在事件边界创建战斗会话，运行期间暂停探索控制但保留场景状态。插件完成后把 `result_key + payload` 交给项目事件映射，由事件决定 flag、奖励、对话或返回探索。
- **强制合同**：不得在 EventBus 回调内重入启动插件；同一遭遇不能重复结算；未知插件/结果映射由 lint 和运行时共同阻断；引擎不硬编码 victory/defeat/reward。
- **测试**：进入/离开、忙碌排队、非重入、插件失败、重复结算、结果事件映射、返回原探索位置。
- **停止条件**：`移动→遭遇→插件→结果事件→返回探索` 的 domain 端到端测试通过。
- **建议提交**：`feat(p5): connect encounters to extension sessions`。

**当前进度（2026-08-27）**：`domain::EncounterSystem` 从 schema 1 数据选择最近遭遇点并只在进入时发布请求；app 在事件边界排队创建 battle session，探索移动冻结，完成后按 `result_key` 排队项目事件。新增端到端测试验证“遭遇→插件确认→结果事件→EventRunner 对话”且不在 EventBus 回调内重入；`eventlint --check-encounter` 已校验遭遇结果到事件的引用。Windows 主线已闭合，跨平台门禁仍待补。

#### P5-6 两个参考战斗插件

- **实现**：在 `plugins/` 提供 `sample_turn_based` 与 `sample_instant_result`。前者自行拥有 actor/skill/buff/行动/QTE/菜单/AI/结算 schema 和 validator；后者使用完全不同的最小规则，证明 seam 没有回合制偏置。
- **约束**：参考插件可以依赖公开 domain/ui/render/plugin 合同，但不能进入 `engine/domain`，不能要求 app 增加插件 ID 分支。
- **测试**：两个插件各自的规则测试、数据 lint、确定性重放；同一遭遇只改 `project.json` 的插件 ID 后均能完成。
- **停止条件**：保存 `git diff -- engine app` 的替换实验记录，证明切换插件时引擎与 app 零改动。
- **建议提交**：`feat(plugins): add replaceable battle examples`。

**当前进度（2026-08-27）**：已提供 `plugins/sample_instant` 与 `plugins/sample_turn_based`，各自携带 manifest 与 data fixture；app 通过 manifest/registry 选择，公共确认 action 为 `extension.confirm`，规则实现不进入 engine/domain。Windows 替换 seam 测试已通过。

#### P5-7 Windows 竖切与里程碑关闭

- **Windows 优先验收**：完整 win-debug 构建/ctest、项目 lint、私有头审计、clang-format、`git diff --check`、D3D12 实机日志或录屏；固定路径为 `移动→遭遇→战斗插件→结果事件→返回探索`。
- **延后平台验收**：WSL/Linux Vulkan 全量构建测试与 macOS CI 可在开发结束后统一补；未补齐时状态只能是“Windows 开发闭环完成”，不能写“P5 完成”。
- **最终停止条件**：两个插件替换实验成立；全局门禁全部通过；文档回写真实命令、测试数量、插件替换 diff 和端到端证据。
- **建议提交**：`docs(p5): record plugin acceptance evidence`。

**当前平台裁决（2026-08-27）**：Windows/D3D12 与 Linux/Vulkan/lavapipe 当前源码全量 CTest 均通过，Windows/Linux 各 **209/209**；macOS Debug/Release CI build+test、五组项目数据 lint、私有头审计、format、golden-sync 和 shader-sync 均通过。Linux 复验期间 GCC `-Werror` 暴露测试聚合初始化缺字段，已补齐显式初始化后重新构建通过。仓库全部 137 个 C++ 源文件已用 clang-format 22.1.3 dry-run 通过。CI run `33058546267` attempt 2 的 **11/11 jobs 全部成功**；其中首轮仅因 vcpkg 上游镜像下载波动失败的 Linux Release/data-lint 在缓存生效后重跑通过，UI DXIL 漂移也已用 CI 权威字节码修复。P5/P6 的三平台构建、测试、数据、格式、私有头、golden 和 shader 专项门禁已闭合。

## P6 渲染风格插件与演出

- **目的**：建立渲染风格 seam，使项目可以选择或编写自己的画风，而不修改 RHI 后端和 domain。
- **范围内**：渲染风格 manifest/factory、材质 schema/validator、通用场景快照与 draw/pass 输出、基础无光照参考插件、第二个差异化风格插件、UI 主题/动效 adapter、cutscene 时间轴播放器语义、audio 混音总线。
- **范围外**：把任何特定画风设为引擎默认真相；在 engine 内硬编码 toon/描边/bloom/LUT/PBR 组合；插件直接访问 D3D12/Vulkan 后端；跨编译器 DLL 热加载 ABI。
- **执行顺序**：P6-1 render seam 与资源预算 → P6-2 材质 schema 委派与 glTF material 数据保留 → P6-3 基础无光照插件 → P6-4 第二风格插件 → P6-5 cutscene/UI/audio adapter → P6-6 双插件 golden 替换实验与三平台门禁。
- **验收命令与证据**：同一场景只改项目清单中的 `render_style` 插件 ID 即产生两种明确不同的合法画面；`engine/rhi/backends`、domain 和地图数据零改动；两个插件各自 golden image 一致。
- **停止条件**：全局门禁全过；渲染风格替换实验、材质 schema 委派和资源预算证据齐全。

### P6-1 渲染风格 seam 与资源预算（Windows/Linux 闭环，2026-08-26）

- **已落地**：新增 `engine/render` Module。`IRenderStyleAdapter` 是外部 Seam；输入为只读值语义的 `SceneSnapshot`，输出为通用 `RenderPlan`。`Renderable`/`RenderDraw` 只保存 mesh/material 引用、世界矩阵和不透明材质参数字节，render 核心不解释画风或材质字段。
- **资源合同**：`RenderStyleDescriptor` 为 Adapter 声明版本和 `RenderResourceBudget` 的入口；`ValidateRenderPlan` 对 pass、draw 和材质参数总字节数设上界，并拒绝无名 pass；预算累计使用溢出安全检查。RHI `BeginRendering` 支持显式 clear/load，overlay pass 可保留前一 pass 内容。
- **测试证据**：`tests/unit/render_style_test.cpp` 覆盖快照到计划的数据保留、预算拒绝和非法 pass；Windows/D3D12 与 Linux/Vulkan/lavapipe 全量 ctest 各 **183/183** 通过。
- **边界**：具体画风 Adapter、glTF material 解释和 app 实际录制由后续 P6 子任务实现；P5 `engine/plugin` host 已落为构建期 `PluginRegistry`，P6 复用集中注册入口，不复制宿主。
- **停止条件**：已完成双平台编译与测试验证；macOS 门禁仍按平台策略后补。

### P6-2 材质 schema 委派与 glTF 材质数据保留（Windows/Linux 闭环，2026-08-26）

- **已落地**：`assetimport::TextureAsset` 保留纹理名称与 source URI；`assetimport::MaterialAsset` 保留名称、glTF PBR 基础色/金属度/粗糙度及基础色纹理索引；场景节点通过 `MaterialRef` 关联导入材质。
- **边界**：这些是通用 glTF 导入资料，不是引擎材质 schema；render/style Adapter 可以解释或忽略它们，项目材质实例仍可通过 `OpaqueMaterialParameters` 自定义。
- **测试证据**：`triangle.gltf` fixture 覆盖 material + texture source，`asset_import_test.cpp` 验证字段与节点关联；stb 将 1×1 P6 纹理解码为 RGBA8，Windows 材质导入定向测试通过。
- **资源安全**：外部与嵌入式纹理均限制最大 4096×4096 与 16M 像素，解码失败/缺失/超限均返回导入错误；嵌入式 data URI 与 bufferView 均转为 RGBA8。
- **已补齐**：`IRenderStyleAdapter::ValidateMaterial` 将材质 schema 解释权交给插件；两个参考插件分别校验 `base_color[4]` 与 `accent[4]`。app 已从项目清单创建 style adapter 并消费其 RenderPlan 清屏输出。
- **已补齐**：app 的 `RenderPlanExecutor` resolver 在录制前调用当前 style adapter 的材质 validator；项目 manifest 通过安全相对路径选择材质实例，app 同时注册并按 `RenderPass.pipeline` 解析 `unlit`/`accent` 两套管线。app 将项目材质 parameters 编码为 opaque CBOR，两个 style adapter 分别解码自己的 schema 并将颜色消费到 RenderPass，同时保留参数字节到 draw。P8-1 已补齐 glTF 纹理资源服务与 sampled pipeline，材质/画风字段仍由插件解释。

### P6-3/P6-4 风格 Adapter（Windows/Linux/macOS CI 闭环，2026-08-26）

- **已落地**：`plugins/sample_unlit` 与 `plugins/sample_style` 两个真实 Adapter，均通过 `IRenderStyleAdapter` 与通用 `PluginRegistry` 创建；两者消费同一 `SceneSnapshot`，输出相同 draw 数据但不同 pass ID/clear color，证明 seam 未被单一画风字段塑形。
- **测试证据**：`render_style_adapters_test.cpp` 验证两个 Adapter 的 descriptor、可替换 plan 和差异化输出；Windows/Linux 全量回归各 **183/183** 通过。
- **已补齐**：app 读取 `project_demo.json`，通过集中注册入口和 registry 按 `render_style` 创建 adapter；`RenderPlanExecutor` 统一录制 pass、pipeline、mesh 和 draw，goldenimage 的两个 style 也经该 executor 进入 D3D12 离屏 RHI；两个 adapter 声明不同 pipeline key，golden 使用不同 pixel shader，生成两套基准图。
- **已补齐**：`assets/data/render_resources_demo.json` 提供 schema 1 的 pipeline/mesh 目录，app 在录制前校验所有 plan 引用；项目清单、材质实例和资源目录共同完成 demo/accent 装配。
- **剩余设计风险**：P8-1 已将目录到 GPU handle 的纹理资源服务抽至 `engine/render`；app 仍负责项目装配和生命周期边界。CI run `33058546267` attempt 2 已验证 macOS build/test、golden 和 shader 专项门禁。

### P6-5/P6-6 演出合同与风格 golden（Windows/Linux/macOS CI 闭环，2026-08-26）

- **已补齐**：`core::CutsceneTimeline/CutscenePlayer` 提供 schema 1、唯一 cue、确定性排序、时间窗口和一次性 event 触发；app 在 `kDomainSim` 将触发事件排入 EventRunner 队列；`audio::MixerBus` 提供有界 PCM voice、增益混合和完成回收，app 通过 SDL3 `SDL_AudioStream` 以有界队列消费混音并在事件启动时播放提示音；`ui::Theme` 提供 schema 1 数据解析、颜色/字号范围校验与 presentation-only spring 动效，app 启动加载主题文件；新增 clip-space colored-quad UI pass，主题 accent 经 UI 顶点数据进入 RHI，overlay 使用 load-op 保留场景内容。新增 `shaders/ui.hlsl` 及双后端生成物，新增 3 个合同测试。
- **golden 门禁**：两个 `compare-style` CTest 已接入，Windows/D3D12 基准图已生成并用于本地比较。
- **Windows 证据（2026-08-26）**：MSVC Debug 构建通过，`ctest --preset win-debug --output-on-failure` **183/183** 通过；音频设备不可用时 adapter 降级为日志并不阻断渲染主循环。项目清单新增安全相对路径 `material_document`，demo 与 accent 项目可分别装配对应材质实例。
- **Linux 证据（2026-08-26）**：`cmake --build --preset linux-debug --parallel 2` 与 `ctest --preset linux-debug --output-on-failure` 均通过，Vulkan/lavapipe 全量 **183/183** 通过。
- **后续边界**：P8-1 负责 glTF 纹理到 GPU sampled resource 的最小闭环；更完整的异步流式加载、纹理压缩格式和多材质批处理不在本轮，另行登记为后续资源系统工作。

## P7 工具链与竖切组装

- **目的**：交付第一可玩闭环的产品形态。
- **范围内**：版本化存档+迁移函数表、可配置 `CalendarDefinition + GameClock`、日期/日程触发、地图数据格式 v2、lint 增强（日期/事件/文本/资源/扩展 ID 完整性审计）、30 分钟竖切 demo 组装（纯数据文件）。插件私有技能或材质引用由对应插件 validator 审计，不进入引擎统一 schema。
- **范围外**：GUI 编辑器（另行立项）。
- **验收命令与证据**：存读档一致性自动化测试；竖切 demo 可玩全程零硬编码剧情的审计清单；docs/00 成功标准逐条勾稽。
- **停止条件**：[00-product.md](00-product.md) 的 7 条成功标准全部有真实证据；完整第一可玩闭环达成（含自定义日期/日程与存档读档一致性）。

### P7-1 可配置日历与游戏时钟（三平台已验收，2026-08-27）

- **已落地**：`engine/core` 新增 `CalendarDefinition`、`GameDate` 与 `GameClock`。日历由 schema 1 JSON 提供月份长度和每周天数；时钟以绝对分钟推进，并确定性转换为年月日和日内分钟。
- **数据 fixture**：`assets/data/calendar_demo.json` 是当前 demo 日历，不包含剧情或项目规则硬编码。
- **测试证据**：`tests/unit/calendar_test.cpp` 覆盖 schema/范围校验、跨月推进、非法日期和倒退时间；Windows/D3D12 全量 ctest **186/186** 通过。
- **边界修复**：`GameClock` 对极大年份在乘法前拒绝，避免绝对分钟计算溢出。
- **测试证据**：Windows/D3D12 全量 ctest **197/197** 通过。

### P7-2 版本化存档与迁移（三平台已验收，2026-08-27）

- **已落地**：`domain::SaveState` 保存日历 ID、日期、日内分钟和排序后的 true flags；`SerializeSave`/`ParseSave` 使用 schema 1 并拒绝缺字段、非法范围和重复 flag。
- **迁移合同**：`MigrateAndParseSave` 只执行调用方显式提供的逐版本迁移函数；迁移未推进版本、缺步骤或目标版本更新时均失败，不静默改写存档语义。
- **恢复边界**：`FlagStore::Snapshot/Restore` 提供持久化所需的确定性值接口；`RestoreSave` 校验运行时日历 ID 后才修改时钟和 flags。
- **文件边界**：`ReadSaveFile` 限制输入为 4 MiB，拒绝负 schema；读写失败返回结构化错误，不静默恢复。
- **测试证据**：`tests/unit/save_test.cpp` 覆盖存读一致性、显式 v0→v1 迁移、缺迁移步骤、负 schema、文件 I/O 和日历不匹配拒绝；Windows/D3D12 全量 ctest **197/197** 通过。

### P7-3 日期/日程触发（三平台已验收，2026-08-27）

- **已落地**：`domain::ScheduleTable` 由 schema 1 JSON 驱动，按日历月份/日期/日内分钟声明目标事件；`ScheduleSystem` 只在跨过触发时刻时发出事件 ID，按触发时间和稳定 ID 排序，并拒绝回退时钟或不匹配日历。
- **引用校验**：`ValidateScheduleTargets` 在数据边界拒绝悬空事件引用；运行时不把未知目标静默转为空操作。
- **数据 fixture**：`assets/data/schedule_demo.json` 登记 demo 日程。
- **测试证据**：`tests/unit/schedule_test.cpp` 覆盖日期范围、事件引用、跨时刻稳定排序、重复轮询、回退时钟、超大轮询范围和完整日历不匹配；Windows/D3D12 全量 ctest **197/197** 通过。
- **已接线**：app 在固定 `kDomainSim` tick 推进 `GameClock`，将 `ScheduleSystem` 输出加入既有待处理事件队列；`eventlint --check-schedule` 统一校验日历、日程和事件三方引用。

### P7-4 竖切与存档接线（三平台已验收，2026-08-27）

- **已落地**：app 已加载 `calendar_demo.json`/`schedule_demo.json`，日程目标经过事件引用校验；F5 写入 `save_slot_0.json`，F9 经迁移感知 reader 读取并恢复时钟与 flags；事件仍统一从 pending queue 在 EventRunner 边界启动。
- **一致性边界**：由于 schema 1 尚未持久化 EventRunner 程序计数器和待处理事件队列，app 只允许在 EventRunner 空闲且 pending queue 为空时 F5/F9，避免恢复出分叉的半执行事件。
- **引用完整性**：app 和 `eventlint --check-cutscene` 均校验 cutscene cue → event；demo 数据包含可启动的 `intro` 事件，避免启动期悬空引用。
- **内容预算合同**：`vertical_slice_demo.json` 以 6 个数据 beat 声明 1800 秒内容，`ParseVerticalSliceDefinition` 在解析期拒绝短内容、重复 beat 和非法时长；`eventlint --check-vertical-slice` 校验全部 beat 的事件引用。
- **端到端证据**：`p7_vertical_slice_test.cpp` 覆盖日程触发 → EventRunner → 对话阻塞/确认 → flag 变更 → SaveState 恢复；Windows/D3D12 全量 ctest **197/197** 通过；`eventlint` 的 events、map、schedule、cutscene 检查均 exit 0。
- **Windows 证据**：从仓库根目录启动同一构建出的 app，进程保持响应并成功完成资源/日历/日程/竖切数据初始化；GUI 自动化工具未能枚举 SDL 窗口，因此按可复现的进程存活、数据 lint 和 domain 端到端证据记录，不伪造按键操作证据。
- **三平台 CI 证据**：CI run `33058546267` attempt 2 的 Windows D3D12、Linux Vulkan、macOS build/test 全部通过；全量 ctest 209/209、data-lint、golden-sync、shader-sync、format 和私有头审计均通过。该矩阵覆盖 P7 的日历、存档、日程、竖切与对话恢复测试，因此 P7 的平台验收已闭合。

## P8 渲染资源与扩展消费闭环

- **目标**：将 glTF/项目材质资源通过独立资源服务送入 RHI，并由选定的渲染风格插件实际消费；不把 PBR 或固定画风提升为引擎合同。
- **当前进度（2026-08-27，Windows 优先）**：已落地通用 `RenderDraw.sampled_texture` 资源 ID、schema 1 catalog 的 `textures` 列表、`TextureResourceService`（RGBA8 尺寸/字节数/重复 ID 校验、GPU texture/sampler 生命周期）和 RenderPlan sampled resolver；两个参考 style adapter 按各自 pipeline 选择消费该通用绑定；glTF importer 已导入 `TEXCOORD_0`，arm fixture 通过独立 UV buffer 保持 P4 骨骼 golden 字节不变，并绑定真实外部 PPM 纹理。
- **Windows 已验证**：`cmake --build --preset win-debug`（MSVC 开发者环境）通过；`ctest --preset win-debug --output-on-failure` **213/213** 通过，新增 skinned glTF → UV → TextureResourceService → `skinned_textured.hlsl` → D3D12 采样测试通过，P4 三组 skin golden 与原 triangle golden 均通过；shader 生成脚本成功产出新增 DXIL/SPIR-V，`git diff --check` 通过。
- **Linux 证据**：WSL2 原生 Vulkan/lavapipe 全新配置、构建与 CTest 通过，包含 P8 纹理资源服务、sampled quad 和 skinned glTF 实际采样用例；全量 **213/213** 通过。
- **CI 证据**：run `33070203706` 的 Linux/Windows Debug+Release、macOS Debug+Release、golden-sync、shader-sync、data-lint、format 和 private-headers 共 11 个 job 全部成功；P8-1 的平台与专项门禁已闭合。

### P8-2 纹理资源服务生产化（Windows/Linux 开发闭环，2026-08-27）

- **已落地**：`TextureResourceService` 增加显式资源状态、线程安全 CPU 上传队列、重复请求合并、有限 resident byte budget、结构化失败诊断，以及 `Acquire/Release/Unload` 引用生命周期；后台生产者不触碰 RHI，`PumpUploads` 由 GPU-owning 线程执行。
- **Windows 证据**：P8-2 定向纹理资源测试 **2/2** 通过，覆盖队列、合并、预算、引用保护和卸载；随后补入的 `size()`/`resident_bytes()` 读锁尚未在本机重编译，因当前 WinGet Ninja 链接为 0 字节且执行被拒绝，最终 Windows 构建留待 CI。
- **Linux 证据**：WSL2 原生 Vulkan/lavapipe 全新配置、构建与全量 CTest **214/214** 通过，覆盖同一资源服务合同及既有 sampled/skinned texture 回归。
- **CI 证据**：P8-2 代码 commit `908e86a` 的 CI run `33073849126` attempt 2 已成功通过 Windows/Linux/macOS Debug/Release、`format`、`private-headers`、`data-lint`、`golden-sync` 和 `shader-sync` 共 11 个 job；后续 commit `bd71867` 为 CI job 增加 30 分钟超时保护。文件解码后台调度、取消、压缩格式和更复杂的缓存淘汰策略不属于本子任务。

### P8-3 运行时纹理异步装配（Windows 优先，2026-08-27）

- **已落地**：`assetimport::LoadTextureFile` 只在 CPU 解码独立纹理文件为有界 RGBA8；`AsyncLoader` 增加纹理解码任务、错误诊断和有限容量（默认 64 个请求），容量统计包含排队、执行中及待 `Poll()` 结果，避免结果无人消费时无界增长。旧 mesh 回调合同保持可用。
- **阶段接线**：app 在 `kDomainSim` Poll 解码结果并调用 `TextureResourceService::QueueUpload`；`kRenderSubmit` 每帧以有限批次 `PumpUploads`，资源未就绪或失败时只提交清帧，不让 RenderPlan 消费无效 sampled handle。GPU 纹理/采样器仍只由 `TextureResourceService` 创建和销毁。
- **生命周期与诊断**：运行时对角色 glTF 外部纹理使用稳定资源 ID，ready 后 `Acquire`，退出时 GPU idle 后 `Release`/`Unload`；解码失败、队列耗尽和预算拒绝都通过 `TextureResourceStatus` 与 app 诊断队列可见。嵌入式/data URI 纹理直接形成 upload packet，不绕过资源服务。
- **测试证据**：Windows MSVC 开发者环境下 `jrpgmaker_unit_tests` 和 `jrpgmaker_app` 构建通过；`ctest --preset win-debug --output-on-failure` **218/218** 通过，新增纹理解码成功/失败、有限队列、失败状态保留测试。WSL 本轮曾返回 `E_ACCESSDENIED`，因此 Linux 本机证据不作伪造；CI run `33080370144` 的 Linux/Windows Debug+Release、macOS Debug+Release、golden-sync、shader-sync、data-lint、format 和 private-headers 共 11 个 job 全部成功。
- **明确范围外**：本子任务不实现取消 token、优先级调度、压缩纹理格式、热度淘汰、完整热重载，也不把 PBR 或渲染风格语义移入引擎；`LoadGltfScene` 的 glTF 场景解析合同仍保持同步，外部纹理运行时装配走异步文件解码。

## P9 内容生产与资源管线

- **目的**：把当前运行时闭环提升为可持续生产内容的项目管线，收口插件 validator、数据化输入合同、资源清单、定位文本和确定性构建；不把项目内容重新硬编码进引擎。
- **范围内**：P5-2 插件私有 validator 与统一 lint；P5-4 `input_actions.json` 和通用 presentation 合同；项目/插件/地图/材质/纹理资源清单；本地化文本表与 CJK 校验；资源导入、依赖扫描、派生物缓存和打包清单；错误报告、大小上界和增量构建。
- **唯一 owner**：数据合同由对应的 `engine/domain` 或插件 validator 拥有；导入与派生物由 `tools` 拥有；GPU 资源仍由 `engine/render` 的 `TextureResourceService` 拥有；app 只负责装配和阶段接线。
- **非目标**：不在引擎内定义战斗规则、PBR/固定画风、通用商业资产商店或网络协作服务；纹理取消/优先级/压缩格式/热度淘汰作为资源系统独立迭代，不阻塞本阶段的内容管线合同。
- **验收**：一次 CLI 命令校验项目、插件私有数据、跨文件引用和资源依赖；坏数据按文件/字段路径聚合并以非零退出；只修改数据即可替换日期、对话、输入、地图、材质和插件；相同输入生成稳定清单与派生物摘要；Windows 优先并完成 Linux/macOS CI。
- **停止条件**：P5-2/P5-4 的未完成合同已落地，数据 lint 不再依赖 app 运行时兜底，所有提交资源都有可追溯 owner、版本和预算。

**当前进度（2026-08-28，P9-3）**：`eventlint --check-project <project.json> <project-root>` 已统一校验项目 manifest、插件私有 validator、输入 action、事件脚本、中文/UTF-8 本地化覆盖、资源预算和 glTF 外部 URI 依赖；`--build-resource-package` 生成含 owner/version、文件大小、FNV-1a 内容摘要和 `cache_key` 的确定性资源打包清单。demo 项目实跑返回 clean，打包清单连续两次生成字节一致，CI data-lint 已纳入重复生成比较。Windows 全量 `ctest` **225/225**、P9 定向 27/27 断言、私有头审计和 `git diff --check` 已通过；P9 内容生产管线闭环完成，二进制压缩归档与远端缓存服务不属于本阶段。

## P10 项目装配与编辑器工具

- **目的**：提供面向项目作者的可选本地工具，降低直接编辑 JSON/资源清单的成本，同时保持文件格式、CLI 和运行时合同为真源。
- **范围内**：项目创建/打开/校验；日期、对话、触发点、地图碰撞/导航、相机区域、输入映射和材质实例的 schema-aware 编辑；资源引用预览；事件图与交互点诊断；变更回写、迁移和可审查 diff；只读运行时预览入口。
- **唯一 owner**：编辑器是 `tools`/未来 `editor` adapter，不拥有 domain 语义；所有保存结果必须经过现有 parser、validator 和迁移器；render/editor 预览只消费结构化快照，不直接修改 GPU 资源。
- **非目标**：不做通用 3D 建模器、完整 DCC、联网协作、云端项目格式或绕过插件 validator 的自由脚本编辑；战斗规则和渲染风格仍通过插件提供编辑扩展。
- **验收**：编辑器创建的最小项目可由 CLI 无界面构建并由 app 运行；CLI 创建/编辑/迁移的数据可被编辑器无损打开；非法引用、越界预算和不兼容 schema 在保存前可定位；编辑器崩溃或关闭不会破坏原始项目文件。
- **停止条件**：至少一个完整参考项目可以从空项目创建、编辑、校验、构建、运行和迁移；编辑器不是运行时必需依赖，删除它不影响核心构建与测试。

**当前进度（2026-08-28，P10 已完成）**：`projecttool create/open/validate/diff/write/migrate/diagnose/preview/data-diff/data-write` 已形成可审查的 CLI 装配闭环。manifest patch 仅允许已登记字段，领域数据 patch 按文件名路由到现有 schema parser；合并结果必须通过 `ParseProjectManifest`、引用存在性和 `material.style_plugin_id → render_style` 交叉校验。`diff` 输出稳定字段路径与前后值，`write` 使用临时文件、轮换备份和失败恢复；`diagnose/preview` 只读输出事件、交互、导航、碰撞和相机快照。app 支持以项目根参数启动，创建出的独立项目已验证可保持运行。CI 纳入创建、校验、诊断、预览、日期数据编辑和完整 lint；Windows 全量 CTest **225/225**、错误 patch 拒绝、连续写回和端到端启动均通过。GUI 编辑器、复杂领域表单和更复杂的迁移策略不属于本 P10 CLI 闭环。

## P11 插件生态与发布硬化

- **目的**：把源码级插件 seam、数据合同和运行时诊断整理为可被其他开发者稳定采用的 SDK，并完成性能、兼容性和安全边界硬化。
- **范围内**：公开插件 SDK 文档与模板；插件 manifest/contract 兼容策略；插件 validator、资源和 presentation 扩展点；构建期注册的可重复装配；错误隔离、预算/超时/队列上界；存档迁移矩阵；性能基线、GPU/CPU/内存预算和长时间运行测试；三平台发布包与排障文档。
- **唯一 owner**：插件接口由 `engine/plugin` 拥有；插件私有规则和数据由插件拥有；发布与兼容性检查由 `tools`/CI 拥有；不承诺跨编译器 DLL ABI 或二进制热加载。
- **非目标**：不引入网络多人、主机/移动端首发、通用插件市场、沙箱脚本平台或引擎内置战斗/画风。
- **验收**：第三方最小插件仅依赖公开合同即可构建、注册、校验、运行和卸载；替换战斗插件与渲染风格插件不改 `engine/domain`、RHI 后端或 app 业务分支；长时间运行无资源泄漏、无无界队列和无未处理结构化错误；Windows/Linux/macOS 的构建、测试、数据 lint、golden、shader-sync、格式和私有头门禁全绿。
- **停止条件**：SDK 示例、兼容矩阵、迁移策略和发布包均可从干净环境复现，所有已接受设计风险有债务编号和后续入口。

**当前进度（2026-08-28，P11 Windows/Linux 主线完成，macOS 延后）**：已落地公开插件 SDK 文档与 `templates/plugin_minimal`，明确源码级注册边界、engine contract 兼容矩阵、validator 读取预算和 presentation 预算；`ParseManifest` 与 `PluginRegistry` 共同复用 `ValidatePluginManifest`，不兼容合同在解析期和注册期均阻断，合同版本不再散落硬编码。validator 路径校验已补充 canonical containment，阻断数据目录符号链接越界，并有 symlink 回归测试。`engine/plugin` 已提供安装/导出 CMake package，`tests/fixtures/sdk_consumer` 验证外部 `find_package(jrpgmaker)` 消费路径，且 CI 矩阵纳入该烟测。新增 `tools/ci/package_release.ps1`，按平台装配 app、assets、插件 manifest/私有 data，并输出排序后的 SHA-256 清单，文件数/总字节数有界且拒绝覆盖输出目录；本机 Windows 和 Linux 构建产物各两次装配清单一致。公开最小模板已接入单元测试，完成构建、注册、创建和卸载验证；插件生命周期压力测试提升至 100,000 次。P11 审计新增 `RenderPlanExecutor` 插件/解析器异常隔离、活跃 rendering 清理、战斗 session wrapper 异常隔离回归测试，修复资源预算计数溢出边界，并让 app 通过 plugin owner wrapper 调用战斗插件。`docs/06-plugin-release.md` 补齐兼容矩阵、发布内容和排障合同。Windows 独立 NMake 构建及 WSL/Linux Debug 构建均通过，当前全量 CTest 均为 **232/232**；WSL 数据 lint、P10 projecttool 全流程和 Vulkan/lavapipe golden-sync 均通过。CI 权威 shader artifact 的 24 个 DXIL/SPIR-V 文件与当前 `shaders/generated` 逐文件 SHA-256 一致，且当前提交相对 artifact 来源提交的 shader 目录无变化。按当前项目资源，Windows + Linux 是本轮主验收平台，已完成本地实现与验证；macOS 保留为后续适配，不阻塞本轮继续推进。最近 CI 运行因 runner/vcpkg 环境问题取消，CI 发布包与 SDK 消费门禁仍需在可用 runner 上补跑；WSL 无 WSLg，真实窗口 swapchain 不在本地 WSL 验收范围内。

## P12 项目完成与首个稳定版本

- **目的**：以一个可交付的参考项目证明 jrpgmaker 的核心承诺，而不是继续无限扩展为通用全类型引擎。
- **完成定义**：从版本化数据和源码级插件开始，完成“创建/校验项目 → 资源构建 → 启动 → 移动/碰撞/寻路 → 日期/日程/触发 → 对话或可选战斗插件 → 渲染风格插件 → 存档/读档 → 发布包运行”的闭环；项目内容、战斗规则和画风均可替换，核心引擎不包含项目专属语义。
- **最终门禁**：产品成功标准 7 项全部有真实证据；三平台 CI 与目标平台实机验收通过；全量测试、数据 lint、golden/shader-sync、格式、私有头审计和依赖许可清单通过；发布包可在干净环境安装运行；文档索引、架构、里程碑、债务登记和用户指南同步到同一版本。
- **明确不纳入完成定义**：网络多人、开放世界流式加载、固定画风、引擎内置战斗规则、跨编译器 DLL 热加载、主机/移动端和通用 GUI/DCC 能力；这些若要实施必须另立产品版本和边界。
- **停止条件**：完成定义连续两轮从干净 checkout 重现，未关闭阻断问题为零，开放债务均有 owner/风险级别/后续入口；此后只进入维护、兼容性修复和经用户确认的新版本规划。

**当前进度（2026-08-28，P12 Windows/Linux 主线完成，macOS 延后）**：已新增基于真实 demo 数据的 acceptance test，覆盖移动/碰撞、A*、交互提示与确认、事件/对话、日程和存档恢复；Windows/MSVC 与 WSL/Linux 均完成本地及独立干净 checkout 构建，提交基线全量 CTest 均为 **233/233**。数据 lint、golden-sync、shader 权威 artifact 一致性、私有头审计和发布包确定性均已验证；发布包已修复误收 `CMakeFiles`、对象文件和 CMake 元数据的问题，并在 Windows 干净 checkout 上完成两次装配，37 个运行时文件清单 SHA-256 一致且中间文件为 0。新增用户指南并登记至文档索引。按当前平台决策，Windows + Linux 的 P12 稳定版本验收已完成；macOS 保留为后续适配，不阻塞本轮。

---

## 风险清单

| # | 风险 | 等级 | 阻断策略 |
|---|---|---|---|
| 1 | RHI 双后端行为漂移 | 高 | 合同测试套件 + 同一提交强制同步（纪律 1）；P1 即建立 golden image 流水线 |
| 2 | MoltenVK 兼容性坑 | 中 | P1 起三平台 CI 全程在跑，不留到最后才发现 |
| 3 | 渲染风格 seam 被某个参考插件反向塑形 | 高 | P6 必须用两个差异明显的真实风格 adapter 验收；材质私有字段不得进入 engine 合同 |
| 4 | CJK 排版复杂度被低估 | 中 | P3 引入 HarfBuzz 当周即跑日文禁则 golden image |
| 5 | 战斗 seam 被回合制或 ACT 私有语义污染 | 高 | 引擎会话只保留生命周期/输入/输出/result_key；用回合制与即时结果两个插件做删除/替换测试 |
| 6 | 源码插件需求滑向跨编译器热加载 ABI | 高 | P5/P6 固定构建期注册；DLL ABI、热重载、沙箱和二进制分发另立里程碑 |
| 7 | 插件私有数据绕过作者期校验 | 高 | 插件必须随包提供 validator；项目 lint 统一调度并对文件数、大小和错误数量设上界 |
### A5 审计修复：Vulkan descriptor 绑定快照

- 修复 Vulkan 复用并原地更新单一 descriptor set 导致的跨 draw 状态污染与生命周期违规；每次资源绑定使用独立 descriptor set，双端 144/144 验收。
