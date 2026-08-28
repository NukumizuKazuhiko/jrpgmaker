# 最小源码级插件模板

此目录是 P11 SDK 示例，不会被根工程自动编译。将本目录复制到插件工程后，把 `plugin.json` 纳入项目数据，并在宿主构建期调用 `PluginRegistry::Register`。

模板没有绑定战斗规则、渲染风格或剧情语义；这些由插件自行定义并通过公开合同输出。

使用步骤：

1. 把 `vendor.minimal` 改为全局稳定的反向域名式插件 id，并同步 C++ 注册代码。
2. 根据插件类型实现 `IBattlePlugin` 或 `IRenderStyleAdapter`，不要直接调用 SDL、RHI 后端或 domain 私有实现。
3. 在 `data_roots` 内放置版本化私有数据，并通过 `ValidateData` 使用宿主提供的有界 `read_file` 校验。
4. 在宿主启动装配阶段注册 manifest 与 factory，再由项目 manifest 选择插件 id。
5. 若需要 P13 编辑器表单，另放一个 `plugin.editor.json` sidecar；不要把编辑器字段写进运行时 `plugin.json`。

仓库内完整规范见 `docs/11-plugin-system.md`，SDK 构建入口见 `docs/05-plugin-sdk.md`。复制到仓库外使用时，应随发布的 SDK 文档读取对应 engine contract 版本。
