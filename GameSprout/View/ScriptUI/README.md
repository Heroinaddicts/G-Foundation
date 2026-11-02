# ScriptUI

ScriptUI 负责脚本资源创建交互和脚本组件的 Inspector 控件。脚本类型、反射字段、引用解析、容器修改和可撤销事务由 `IScriptEditor` 提供；本 View 不读取源代码、不解析场景文档、不直接改写 Runtime 字段。

- `ScriptComponentEditor` 订阅字段事件，在选择时读取一次快照，收到事件或明确操作后更新。没有定时器或 Update 查询。
- `ScriptPropertyRow.ui` 定义标量、64 位有符号/无符号整数、枚举、数学值、资源和场景引用的控件。
- `ScriptCompositeField.ui` 定义结构折叠和容器 Size / 添加入口；`ScriptContainerItem.ui` 定义删除与重排按钮。结构、数组、序列、字典可以继续嵌套。
- 固定数组显示固定 Size，禁止增删和改长；允许交换元素顺序。动态序列支持 Size、添加、删除、上移、下移。字典键值各自递归显示，重复键由 Module 拒绝，不覆盖已有条目。
- 递归编辑传递根字段 ID 和值路径。Module 从最新快照修改指定叶子，以一个根字段事务处理 Undo / Redo，避免使用者修改一个轴时覆盖其他轴的最新值。
- `SceneObjectField` 展示 GameObject 或 Component 引用。组件候选按 Runtime 类型规则过滤；对象拖入组件字段时必须恰好有一个匹配组件。多个匹配时使用选择器或拖入具体组件标题。
- 拖拽载荷只含会话、世界、对象和组件实例 ID。悬停仅查询候选；明确放下或确认选择时才由 Module 在编辑模式取得文档 FileID。Play 使用已有 FileID 或会话内实例身份，不分配作者 ID。
- 丢失引用显示 Missing 并保留身份，可清空；定位选择对应对象。完整名称和路径放在 tooltip，窄 Inspector 使用省略显示。

验证入口：`GameSprout/Tests/ScriptInspectorUI` 的 `ScriptInspectorUI.RecursiveFields` 使用真实 `.ui`、ScriptEditor 和 ObjectEditing，测试宿主提供隔离的场景事务。所有测试库输出到构建树，不部署到运行中的 Editor。
