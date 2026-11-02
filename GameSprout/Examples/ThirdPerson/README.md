# 第三人称可玩示例

此示例将原生用户脚本、组件引用、Jolt 刚体、输入、四段动画、跟随相机和 Prefab 放在同一项目中验证。它是项目游戏代码，不给 Editor 或 Runtime 新增角色专用业务。自带原创的极小 FBX 骨架和程序生成的方块角色，不读取或修改现有用户项目，也不需要下载模型。

## 创建并打开

```sh
python3 GameSprout/Examples/ThirdPerson/CreateProject.py --output /Users/max/Documents/GameSproutThirdPerson
open -n Bin/GameSproutEditor.app --args --ProjectRoot=/Users/max/Documents/GameSproutThirdPerson
```

目标目录只允许新建，存在时直接失败，绝不覆盖。生成器会创建 `Info/project.json`、GameLogic 身份、两个用户脚本、骨架、四个 `.anim`、`Playground.scene`、`Player.prefab` 和对应的当前格式 `.meta`。预处理产物及 GameLogic 动态库仍由现有导入和编译流程生成到项目 Library。首次启动会打开 Playground；等待资源与脚本处理完成后点击运行，再点击 Game 画面获取输入。

## 操作

- WASD：相机相对移动；Shift：跑步；Space：地面起跳。
- 按住鼠标右键移动：旋转观察方向。
- Escape 或切到其他窗口：释放 Game 输入；重新点击 Game 获取输入。
- 停止 Play 后由现有 PlayMode 恢复编辑场景；Play 属性修改不保存到源场景。

## 数据与职责

`ThirdPersonCharacter.hpp` 根据固定步控制 Dynamic Rigidbody，保留重力产生的 Y 速度，冻结物理旋转，并让独立 Visual 节点朝向移动方向。它使用普通 Raycast 排除自身物体，检查地面法线与坡度；输入边沿由 Update 锁存，在一个 FixedUpdate 中消费一次，避免多固定步重复跳跃。它是刚体角色例子，尚未实现专用 CharacterController 的自动跨台阶或移动平台补偿。

角色胶囊使用独立的 `Character.physicmaterial`：摩擦为 0、弹性为 0、摩擦合并为 Minimum。持续写入水平速度会使角色不断压向墙面，普通材质的接触摩擦可能抵消重力而产生“贴墙悬停”；角色材质避免这种摩擦支撑。墙和地面保持默认材质。零摩擦自身不足以保证合并结果为零，故明确使用 Minimum；若对方材质采用优先级更高的 Maximum，仍需按项目材质规则配置。游戏移动和刹停继续由脚本控制，角色仍与墙碰撞并受重力。

`ThirdPersonCamera.hpp` 通过 Runtime 输入读取右键旋转，夹紧俯仰角并平滑跟随。遮挡使用五条射线，立即回拉、平滑恢复，不能等同于完整球形 Sweep。父级变换通过公开数学接口转换；它不控制 Scene 的编辑相机。

角色结构为 Player（物理）→ Visual（游戏朝向）→ Rig（Animator）→ 骨骼及几何子节点，防止动画覆盖角色朝向和刚体位置。Idle/Walk/Run/Jump 经过现有 AssetsPipeline 预处理，以实际 Animator Clip 切换播放。可在 Inspector 更换为其他角色及对应骨架/动画。

场景预设了相机、刚体、碰撞体和 Animator 的脚本引用；Player.prefab 保留内部组件引用，外部场景相机引用为空。手动将 Prefab 拖入其他场景后，可为 ViewCamera 配置该场景的相机。

## 验证

`Tests/ThirdPerson` 的 `Gameplay.ThirdPerson` 使用真实 ScriptCompiler 编译并加载动态库，通过真实 FBX/动画导入和 Runtime 资源解码器运行场景；覆盖移动、对角线限速、跑步、单次跳跃、落地、贴墙持续按键下降和沿墙滑动、动画切换、右键/失焦、相机遮挡、Prefab 引用重映射、源文件隔离与作者快照恢复。测试数据库查询由隔离内存表提供，不改用户项目。可向测试可执行文件传入 PNG 绝对路径，额外执行真实 GPU 渲染读回。
