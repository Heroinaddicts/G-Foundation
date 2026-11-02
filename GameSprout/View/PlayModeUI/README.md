# 运行模式工具栏

`PlayModeUI` 通过 `IPlayMode` 发送运行、停止、暂停、继续和单步命令，先订阅模式事件再读取一次初始状态。它不持有 Runtime 节点，不负责场景备份、脚本加载或游戏帧推进。

- 控件、文字、工具提示和样式定义在 `PlayModeUI.ui`。
- `IDEWindow.ui` 提供通用工具栏容器，View 通过 `AttachToolbarWidget` 挂入自己的表单。成功挂入后由 IDE 持有；`RemoveToolbarWidget` 移除并销毁表单。
- 准备运行时可再次点击运行按钮取消；退出进行中禁用命令；退出失败时允许重试停止。
- 释放时先注销模式事件，再断开控件回调，最后移除表单。无定时器或 Update 数据轮询。
- Game 原有按钮只用于编辑时的场景动画预览；正式运行由本工具栏控制。

测试位于 `Tests/`，覆盖真实 IDE 工具栏挂载、模式事件、命令拒绝、准备取消、退出重试及释放。

Game 输入由 `GameViewportInput.cpp` 转换为 Runtime 的公共值事件。只有正式 Playing 且 Game 获得焦点时转发键盘、鼠标与滚轮；暂停、停止、隐藏、失焦和 Esc 释放输入，恢复运行不抢其他编辑器控件的焦点。Scene 自由相机的输入不进入游戏。键值为平台解析后的游戏键标识，不包含文本/IME输入。

可在配置测试时指定 `GAMESPROUT_TEST_RUNTIME_DIRECTORY`，增加真实 Runtime 动态库的输入集成检查；普通路由测试只记录公共输入事件，不模拟渲染后端。
