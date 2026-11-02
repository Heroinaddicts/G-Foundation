# 编辑与运行会话

`PlayMode` 通过公开接口协调 SceneManager、RuntimeHost、AnimationPreview 和 ScriptEditor。
它不包含其他 Module 的私有实现，不调用 GRHI，也不在 Update 中查询数据。
`PlayModeUI` 使用 `.ui` 工具栏发送命令，订阅模式事件更新显示。

状态为 `Edit → EnteringPlay → Playing ⇄ Paused → ExitingPlay → Edit`。
Play 先请求当前源文件的增量编译，等待编译及加载完成后，在独立的宿主事件回合进入游戏；
编译错误保留编辑世界，准备期间可以取消，迟到的完成回调不会重新进入游戏。
模式通知、Runtime 回调、Host 帧回调期间拒绝同步切换。

进入时结束并还原动画预览，由 SceneManager 保存当前内存中的所有编辑场景和文档会话，
包括未保存内容、路径、FileID、修改基线、Undo/Redo 和选择，再建立运行世界。
Runtime 实例与设备保持不变，Scene 自由相机和 Game 主相机绘制同一个当前世界。
正式脚本启动遵循全部构造和字段恢复、全部 OnCreate、提交本批结构修改、全部 OnInitialized 的顺序。
OnInitialized 保证本批对象的组件数据及父子关系已就绪，不保证其他组件的 OnInitialized 已经执行；
未完成初始化的组件不能进入 Update 或 FixedUpdate，运行中新增组件也遵守同一装配边界。

Host 是唯一的游戏帧入口，固定步长为 1/60 秒，并对长时间停顿限制追帧。
暂停保留脚本实例，停止游戏脚本与动画推进，仍派发事件及视口帧；单步在下一 Host 帧
推进一次固定帧和一次 Update 后保持暂停。绘制两个视口不会执行两次游戏帧。
Game 视口有焦点时才提交平台无关的键鼠输入；Scene 相机输入独立。
失焦、隐藏、暂停、停止会清理按键状态，Esc 释放游戏输入焦点。

`Runtime.CreateGameObject(name)` 可以先创建无父节点、无场景归属的临时对象，配置组件后再
通过 `SetParent`／`AddChild` 挂接。只有父链最终连接到存活场景根的对象才进入初始化和模拟；
挂到另一个临时对象下面不会保活，非激活但仍在场景树中的对象则继续保留。
`SetParent(nullptr)`／`RemoveChild` 真正解除场景归属，同一帧重新挂接不会重复初始化。
RuntimeHost 在全部视口帧回调返回后统一调用一次 `EndFrame`，回收最终仍无归属的子树；
编辑、暂停和无可见视口时也执行。固定子步和单个视口绘制不会分别回收临时对象，
裸指针不延长对象寿命，下一帧不能继续使用已经被回收对象的指针。

停止先结束游戏执行，保持 DLL 加载直到 OnRelease 和实例析构完成，再恢复编辑会话。
运行中新增的场景和对象被清理；运行数据不进入作者 Undo、不触发作者场景全量序列化，
也不能保存覆盖 `.scene`。第一版运行 Inspector 为只读，仍可选择、观察和折叠组件。
恢复后按稳定 FileID 重新选择对象，View 不保留上一个世界的节点指针。
恢复失败保留快照和停止状态，可以重试，不静默丢弃编辑内容。

运行期间冻结当前原生游戏模块，源代码修改记为待处理；停止恢复后自动增量编译和加载。
这不会每帧轮询源文件，也不会在游戏进行中弹出脚本编译遮罩。
每次运行重新创建脚本实例；C++ DLL 的静态/全局变量不因场景重建自动清零，
此行为不等同于 Unity 的托管 Domain Reload。用户创建的外部线程和全局订阅需在 OnRelease 结束。

回归覆盖于 `GameSprout/Tests/PlayMode`、`Tests/SceneDocument`、
`GameSproutRuntime/Tests`、`Module/RuntimeHost/Tests` 和 `View/PlayModeUI/Tests`。
