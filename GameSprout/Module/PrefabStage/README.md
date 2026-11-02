# Prefab Stage

PrefabStage 是独立的 Editor Module：通过源 GUID 读取当前 `.prefab`，维护舞台脏状态、原始磁盘 hash、保存事务与来源错误。PrefabStageUI 注册资源双击、Inspector Open 和通用文档保存命令，提供由 `.ui` 定义的工具条及保存/放弃/取消对话框，不创建独立停靠面板。

SceneManager 只管理通用独立文档会话。打开时先验证候选对象图，再暂存主场景的活对象身份、路径、稳定 ID、annotations、未保存状态及 Undo/Redo；舞台使用同一 Runtime 中的另一个 IScene 与独立历史。Hierarchy、Scene 和 Inspector 使用编辑内容根，Game 使用保留的主场景。主场景保持 Active 与可渲染，只暂停通用 simulation 更新，关闭舞台后恢复原值；舞台动画预览不会推进主世界。Runtime 和 GRHI 不识别 Prefab Stage，也不依赖 Editor 或 Qt。

保存捕获唯一内容根，保留已有节点/组件文件 ID 和 ID 分配高水位，验证当前 Prefab 格式后原子写回同一源文件。GUID 与 `.meta` 不由本模块更换或写入，导入仍由 AssetsManager/TaskApi 管线负责。源在打开或上次保存后被外部修改时拒绝覆盖；源移动跟随 GUID 更新路径，源移除保留舞台内容并禁用保存。

脚本字段按当前注册声明恢复：同 ID、同值种类保留，已删除字段忽略，新增或种类变化使用当前默认值；暂未注册的脚本类型完整暂存字段。打开或放弃不自动改写源，显式保存才输出当前字段集合。Prefab 的 Baseline、Current、Source 比较采用相同规则，字段声明变化不被误认为实例覆盖；有效的用户字段覆盖与对象引用保留。直接 SetField/ReplaceFields 命令仍严格拒绝未知字段和类型不匹配。

成功保存后通过 SceneManager::EditSuspendedDocument 在后台主文档上下文调用 PrefabManager::RefreshSource。更新使用来源基线与各实例 Override 合并，保留未覆盖来源变化和实例各自修改；不重建主世界，不切换前台舞台。实例更新冲突会保留相关实例并显示错误，不回滚已成功提交的源文件。返回主场景后再恢复先前选择与 Scene 观察视角。

舞台内容根受通用文档约束保护，不可删除或移动；默认新增节点位于内容根下。保存同时检查根外对象，避免静默遗漏。舞台禁止 Play、直接 Scene Save/SaveAs/Open，以及本版未支持的 Nested Prefab 实例操作。源资源切换、退出由 View 先询问未保存内容；Module 对未明确放弃的脏退出再次拒绝。主场景原有未保存修改不受舞台保存或放弃影响。

只在场景/资产/选择和明确操作事件中更新状态，Update 为空。释放顺序为 Stage View 注销扩展与事件，Stage Module 恢复主文档，随后释放 PrefabManager、SceneManager、RuntimeHost。没有旧格式兼容、Nested、Variant、InContext 或 Auto Save 分支。
