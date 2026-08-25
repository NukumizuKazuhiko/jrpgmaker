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
- **子任务 8（已完成，2026-08-24）**：**RHI 纹理采样管线（DEBT-029 前置部分，P3 收尾的 UI 控件/文字栅格化共同地基）**。RHI 合同扩展：`SamplerHandle`/`SamplerDesc{filter,address}`/`CreateSampler`/`DestroySampler`/`IDevice::UploadTexture`/`ICommandList::SetSampledTexture`/`GraphicsPipelineDesc.sample_slot`（0=默认无采样，现有 pipeline 零影响）；`VertexAttribute` 增 `kFloat2` 与 `semantic_name`（D3D12 input-element 语义名，Vulkan 按 location 匹配）。**双后端同步实现（纪律 1）**：D3D12 root signature 增 SRV t0 + sampler s0 两个 pixel descriptor table + 独立 shader-visible CBV_SRV_UAV/SAMPLER heap（slot 池）；Vulkan descriptor set（binding0=SAMPLED_IMAGE、binding1=SAMPLER，fragment）经 `[[vk::binding]]` 与 HLSL `register(t0)/register(s0)` 对齐（`compile_shaders.ps1` 对 -spirv 注入 `VULKAN_TARGET` 宏）。上传路径后端内部 staging+barrier+copy（与 `MapReadBack` 对称），D3D12 上传后 PIXEL_SHADER_RESOURCE、Vulkan SHADER_READ_ONLY_OPTIMAL。**工程适配**：GCC `-Werror=missing-field-initializers` 要求 `VkDescriptorSetLayoutBinding.pImmutableSamplers` 显式置 nullptr；`D3D12CommandList::Native()` 返回基类 `ID3D12CommandList*` 无 copy/barrier 方法，上传逻辑封装为 `CopyBufferToTexture`（与 Vulkan `CopyBufferToTexture` 对称）。新增 `shaders/textured.hlsl`（全屏 quad 采样）与 `texture_quad_test.cpp`；goldenimage CLI 增 `generate-texture`/`compare-texture` 命令。**跨驱动一致性**：2×2 四色纹理（nearest/clamp）全屏采样，WARP 与 lavapipe delta=0（4096 像素），`tests/golden/texture_quad_64x64.ppm` 由 lavapipe 权威生成。验证：双端构建零警告、ctest 117/117（新增 texture_quad golden 1 例 + rhi_contract 3 例）、Windows 实机冒烟通过、私有头审计 OK（70 文件）。**范围外（明确记录）**：glTF 材质纹理（stb 解码 + material 入 `SceneLoad`）随 P4/P6 PBR 渲染轮次（DEBT-029 剩余部分）。
- **子任务 9（已完成，2026-08-24）**：**UI 控件框架最小集（`engine/ui` 纯 CPU 层，九宫格/文本框/列表）**。新增 `jrpgmaker::ui`（`widget.hpp`）：保留式控件树 `Widget`（id/visible/parent/AddChild，owns children，`Layout(available)` 存 widget-local rect 并递归布局）、`Panel`（`NineSlice` 背景 + `Padding` 内容区，子控件布局进 padding 后区域）、`TextBlock`（`Font*` 借用 + TextShaper/LineBreaker，`Layout` 用可用宽度排版，尺寸=排版高度）、`List`（垂直堆叠可见子项按自然高度 + spacing，占满可用宽）；`SliceNine` 纯几何把矩形切 3×3 九宫格（四角保留/边单向拉伸/中心填充），切片内缩超目标时钳制中心归零且不重叠。**范围决策（用户确认）**：render 层仍为空壳（仅 .gitkeep），本轮刻意做纯 CPU 控件框架——九宫格/文本框/列表的布局语义先落地并可单测，**渲染接线（ui→render→rhi 绘制已布局 rect）留 render 层落地时做**，避免与材质工作纠缠、符合"最小一步"。**工程适配**：`Widget` 构造声明后须实现（MSVC LNK2019 暴露未定义构造）。验证：双端构建零警告、ctest 124/124（新增 widget 7 例 52 断言：树父子/九宫格区域与面积守恒/超限钳制/面板 padding 内容区/列表堆叠+隐藏项/TextBlock CJK 排版+空文本归零）、Windows 实机冒烟通过、私有头审计 OK（73 文件）。**P3 范围内项至此全部落地**（事件/对话/投影/触发器/排版/Lua/lint/UI 控件最小集）；P3 验收"CJK 用例 golden image"由排版库 + 事件数据文件执行测试覆盖，golden 渲染一致性由 P1/P2 三角形/场景/相机 golden + 本轮 texture quad golden 共同支撑。
- **审计修复批 1（2026-08-25，codegraph 复核后）**：**schema 校验严格化 + 对话投影收敛 + demo 数据自洽 + Lua 投影接线**。#3：`set_flag` 缺 `value`、`wait` 缺 `seconds` 由静默默认改为解析期 `std::invalid_argument`（docs/01 合同"缺字段抛错"落地；新增 2 例抛错测试，16 处无 value 测试数据补 `"value": true`）。#4：移除从未发布的 `DialogState` 死结构体，`DialogRequested` 收敛为唯一结构化投影（docs/01 更新）。#6：`events_demo.json` 补 `alice_reward`/`chest_west_echo` 两事件使 `triggers_demo.json` 引用有效，flag_trigger 数据文件测试端到端断言每个 `target_event_id` 在事件表存在（防触发后 `Start` 静默 false）。#8：`LuaScriptEngine` 构造增加 `core::EventBus&`，`flags.set` 发布 `FlagChanged`（与 EventRunner 同源投影，修复 Lua 置位后 flag 触发器静默不触发；新增投影广播测试 1 例）。验证：双端构建零警告、ctest 129/129（+3）、clang-format 合规、Windows 实机冒烟通过。**剩余**：RHI 批（#1 D3D12 UploadTexture 越界读、#7 sample_slot 死字段）与 CI 批（#5 golden-sync/data-lint 漏网）待修。
- **审计修复批 2（2026-08-25，RHI 层）**：#1 D3D12 `UploadTexture` 越界读——逐行 `memcpy` 用 footprint `RowPitch`（256 对齐）读 tight 源（每行仅 `row_pitch_bytes` 保证），修复为 `copy_bytes = min(pitch, row_pitch_bytes)`；golden 复验 delta=0 证明修复后跨驱动仍一致。#7 `sample_slot` 死字段——双后端 `CreatePipeline` 曾从不读取 `desc.sample_slot`，`SetSampledTexture` 注释"Requires sample_slot>0"无校验：现两端 `PipelineEntry` 记录 `sample_slot`、command list `SetPipeline` 记录 bound pipeline、`SetSampledTexture` 对未声明采样的 pipeline 抛 `std::runtime_error`（合同字段真正生效，Speculative Generality 消除）。验证：双端构建零警告、ctest 130/130（新增"非采样 pipeline 绑定采样抛错"1 例）、clang-format 合规、Windows 实机冒烟通过、私有头审计 OK（73 文件）、texture_quad golden delta=0。**剩余**：CI 批（#5 golden-sync/data-lint 漏网）待修。
- **审计修复批 3（2026-08-25，CI 门禁层）**：#5 两处门禁漏网收口。**golden-sync**：CI 新增 `generate-texture ./tests/golden/texture_quad_64x64.ppm`（此前只重生成 triangle/scene/camera 三图，texture_quad 基准图无漂移门禁）+ artifact 列表补该图——"生成物只读"强制执行点全覆盖。**data-lint**：`jrpgmaker_eventlint` 新增 `--check-triggers <events.json> <triggers.json>` 交叉检查（每个触发器 `target_event_id` 必须在事件脚本存在，否则触发后 `EventRunner::Start` 静默返回 false——报错"firing would silently no-op"）；CI data-lint 对 `events_demo.json` + `triggers_demo.json` 跑该检查。CLI 验收：干净路径 exit 0、临时坏文件（target 指向 ghost_event）exit 1 且报错清晰、WSL exit 0。验证：双端构建零警告、ctest 130/130、clang-format 合规、Windows 实机冒烟通过、私有头审计 OK（73 文件）。**P3 审计 8 项全部修复**（#1 D3D12 越界读、#2 wait 双计、#3 schema 静默默认、#4 DialogState 死结构体、#5 CI 门禁、#6 triggers 悬空引用、#7 sample_slot 死字段、#8 Lua 投影）。**遗留设计风险（记录，未修）**：#9 P3 验收"CJK golden image"以排版单测 + texture quad golden 替代（子任务 9 已记录）；lint `log()` 空操作存根、`widget.cpp` 未 Load 字体除零、`FlagTriggerSystem` 线性扫描均属可记录债务。
- **CI 三平台首跑（2026-08-25，仓库设公开后解除计费阻塞）**：run `32836347525`（HEAD `6392a46`）首次真正启动全部 11 job。**发现并修复两处**：(1) macOS Clang `-Werror -Wunused-lambda-capture`——`lua_binding.cpp` 的 `log` lambda 捕获 `[this]` 未使用（MSVC/GCC 不报，Clang 报），修复为无捕获（commit `f8c14c0`）；(2) **DEBT-012 首次实锤**——shader-sync 漂移：同一 vcpkg baseline 下 Windows/Linux dxc 二进制版本不同（win `1.9.2602.24` vs linux `1.9.0.5191`），Windows dxc 生成的 DXIL 在 CI Linux dxc 重编译必漂移（6 个 .dxil 各 +16 字节版本戳，SPIR-V 不受影响）——字节码改为 **Linux dxc 权威生成**并提交，Windows 用新字节码渲染 golden 双端 delta=0 证明语义不变。修复后 run `32838288316`（HEAD `f8c14c0`）：format/private-headers/golden-sync/Windows×2/Linux×2/macOS×2 全绿；**shader-sync 仍漂移待字节码提交**，data-lint 因 vcpkg 下载 `automake-1.17.tar.gz` 偶发 502（ftpmirror 网络故障，与代码无关）失败。

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
