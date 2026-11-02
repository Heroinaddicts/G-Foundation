# 资源删除

AssetOperations 持有删除命令和确认快照，AssetOperationsUI 仅注册浏览器通用菜单／键盘扩展，
所有菜单、快捷键与确认控件来自所属 .ui。ProjectAssetsUI 只路由用户动作，不删除项目文件。

- 仅删除完整源资源，子资源不能独立删除；多选目录覆盖已选后代。
- AssetsManager 主线程校验 GUID、相对路径、磁盘 Meta 及普通文件／目录边界，
  将源与 .meta 移入系统废纸篓。拒绝 Assets 根、符号链接和越界目录。
- 拒绝运行会话、Host 帧、Runtime 回调、当前场景、挂起主场景及当前 Prefab Stage 源删除。
  确认期间源移动或 Meta 身份变化会取消操作，不删除替换身份的文件。
- 删除使 Pipeline 旧任务失效；Library 的源备份、活动记录和产物索引在既有主线程事务提交。
  Remove 事件使 Runtime 资源版本变化，已加载缓存按原有规则失效；后续 watcher 删除通知幂等。
- Scene／Prefab 中的 Guid+LocalID 不清空，不删除节点、实例或 Override。资源缺失显示 Missing，
  源及 Meta 以原身份恢复后重新解析。依赖文档的 Missing 引用由管线处理。
- 文件删除不进入场景 Undo。系统废纸篓失败不降级永久删除；后续源失败会报告已完成的项数，
  DB 更新失败也明确报告，不能把部分完成伪装成成功。恢复时保留原 .meta 身份。

Unity 参考：ProjectWindowUtil.DeleteAssets 使用 MoveAssetsToTrash；ProjectBrowser 只收集用户选择
和发起确认，没有把引用它的场景对象连带删除。
