# 表达式与分支 Specs

## S-EXPR-01 共同引擎

使用 Python builtin 类工具共享 `ExpressionProgram`，在 initialize 编译并校验 AST，execute 仅更新变量和求值。复用 simpleeval 的解析、运算、函数扩展及缓存 AST 能力；用 AST 白名单和明确函数表收紧语义，不直接执行 Python eval/exec，不把完整 NumPy 模块交给表达式。

支持常量、输入名、先前结果名、括号、四则运算、比较、Bool 运算、条件表达式、列表字面量、下标与切片、登记的纯函数。拒绝属性访问、方法调用、import、赋值语句、推导式、lambda 和访问宿主对象。虽然上游 EvalWithCompoundTypes 提供更多功能，VisionRT 子集不随其默认自动扩大。

固定资源边界用于防止普通配置错误卡住 UI/运行线程：建议初始最大公式 4096 字符、AST 512 节点、单次生成集合 100000 元素，另约束整数幂和字符串扩张。具体上限放在引擎单一配置中，超过时报明确错误；这些上限是首版设计值，需要性能测试确认，不是既有测量结果。

| 表达式 | 本方案语义 |
|---|---|
| `5 / 2` | 2.5，真除法 |
| `5 // 2` | 2，向下整除 |
| `a[1:3]` | 结束下标不包含 3 |
| `a == b` | Python 标量 / 列表整体比较，不隐式向量比较 |
| `v_gt(a, 10)` | 显式逐元素比较，返回 list[bool] |
| `all_nonempty(v_gt(a, 10))` | 全部满足且非空；空集合 false |
| `is_empty(a)` | 合法集合是否为空 |
| `is_missing(a)` | 缺失输入状态检查 |

函数首版：abs、min、max、sum、len、round、sqrt、sin、cos、tan、atan2、floor、ceil、mean、median、std（总体标准差）、is_empty、is_missing、all_nonempty、any、v_add/v_sub/v_mul/v_div/v_gt/v_ge/v_lt/v_le/v_eq、select、concat。三角函数单位弧度，与 ROI 角度不同，编辑器提示单位。mean/median/std 对空输入报错，不回传 NaN。v_* 首版支持一维数值列表与标量或等长列表，不广播不等长列表；除零报错。select(values, bool_mask) 两者等长。正则不是首版必需，不引入无界 regex 搜索。

输入支持 Int/Float/Bool/String/Array，首版不允许 Image/ROI 作为表达式变量，图像统计和 ROI 几何应先用专用工具输出普通数值。避免对大图隐式遍历。Missing 只能被 is_missing 显式判断；参与普通算术报错。Bool 不作为 Int 接受。

## S-EXPR-02 多表达式与结果

program 顺序执行，后项可引用前项 symbol；所有输出类型设计时指定。隐藏结果 exposed=false 可供后续表达式使用，但不成为节点输出端口。最终返回以稳定 port_id 为键的 Values；不把 symbol 当返回键。

类型严格按 TypeSpec 检查：Float 可在声明明确允许的数值提升规则下把有限 int 提升为 float；Int 不截断 float；Array 检查 rank、叶子 dtype 及数值范围，空数组使用声明类型。一个公式失败时整个工具失败，业务输出 Missing，不发布前半组成功值。

输入变量 symbol 必须为合法 Python identifier，可中文；不得占用 True/False/None、函数名、__ 前缀、既有变量或结果名。名称 name 可任意合法 UI 文本，与 symbol、port_id 分离。编辑器提供从上游选择变量、手动输入、公式列表、类型、隐藏开关、可搜索函数提示与位置诊断。预检查调用同一个公共引擎的编译功能，不在 QML 重做表达式解析；不在编辑每个字符时实例化正式运行工具。

例：输入 `radii` 为 Array(float64,1)，结果依次为 `count=len(radii)`、`ok=all_nonempty(v_gt(radii, 5.0))`。空输入得到 count=0、ok=false。`mean(radii)` 对空输入明确失败。更高维输入先用明确下标选择一组，首版不自动 flatten。

## S-BRANCH-01 条件分支

`visiontools.logic.branch` 使用相同 variables/program 初始化表，program 恰好一行、TypeSpec=Bool、exposed=false，固定输出 on_true/on_false。成功时两者必须严格互斥且完备：`on_true = result`，`on_false = not result`。

结果必须为 Python bool；不接受非空列表、整数1、np.bool_或任意对象的 truthiness。公式语法错误、缺失变量或除零均工具失败，两个输出 Missing。绝不“出错自动走否则”。条件工具只计算真假，不直接操作图、不调用下游工具、不创建执行线程。

RT 的 execution_guard 引用相应 Bool 输出。分支执行范围由每个节点的显式门控和实际数据依赖决定，不能因为某节点“在画布右边”就自动归入某条支路。嵌套分支自身先受外层 guard 控制；外层 false 时内层为 Skipped。

## S-BRANCH-02 合流

示例：读图 → 条件 → 旋转或亮度 → select → 测量。select.selector 来自 on_true；if_true 来自旋转输出，if_false 来自亮度输出。两条支路各自配置对应 guard，且所有边严格向下。运行时等待两个包装任务终态后只选存在且被选中的结果。

普通节点两路 required 输入不是合流，未选支路 Missing 会使其跳过；需要保留条件数据时必须使用 select。选择结果的图像 Owner、数组形状、ROI 都原样传递。控制 Bool 不承担图像/ROI 数据载体角色。

已有跨流程接收需要观测本轮终态，不能把未选支路当作还在运行。Loop 每轮条件独立，不把上轮 true 分支输出带到本轮 false。可见 badge 区分成功、失败、跳过、阻塞；“未检测到圆”等合法空结果仍是工具执行成功，可由表达式决定下一条支路。
