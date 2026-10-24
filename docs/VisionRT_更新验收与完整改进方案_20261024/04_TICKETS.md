# 整改Tickets

本轮10张差量工单；已有正确机制保留。每张必须先有能暴露问题的测试，再改源码，最后真实集成验收。产品源码本轮未改。

## AC-01 · P1

- 来源：F01/F02；规范：S01/S02。
- 归属/落点：RuntimeEngine、AppControllerReadback/Display/Roi。
- 实施：先增加Loop无GUI推进、单次timer关闭完成的失败测试；把循环逐轮读回移除，保留最终读回；文本完成发事件。
- 完成条件：Loop稳定阶段新重转换计数0；GUI阻塞期间轮数继续增长；停止后安全显示。
- 状态：待实施/待实际验收。

## AC-02 · P1

- 来源：F03；规范：S03/S04。
- 归属/落点：RuntimeEngine、AppController、DurationText。
- 实施：收敛会话/轮时钟；运行中now-start，结束freeze；清工程缓存并处理Missing/0；底部三项保持约定格式。
- 完成条件：长算子和停止等待期间elapsed增长；换工程不串值；并行flow时间非sum/max。
- 状态：待实施/待实际验收。

## AC-03 · P1

- 来源：F04；规范：S05/S06。
- 归属/落点：ProjectEditor、ProjectStorage、运行启动/编译。
- 实施：单一层级校验覆盖加载、粘贴、单节点/全flow执行；移动临时候选布局；旧非法引用保留诊断。
- 完成条件：手改同行/逆向JSON不能执行，可断开/移动修复；常规合法绑定回归。
- 状态：待实施/待实际验收。

## AC-04 · P1

- 来源：F07及Qt点击风险；规范：S07。
- 归属/落点：OutputPanel、AppControllerBindings。
- 实施：明确submenu id/targetNode；连接状态查询单源，声明context独立；补搜索；名称与数据区职责统一。
- 完成条件：真实Qt点击绑定到正确节点，已连项勾选去重，无publication依赖；不存在菜单警告。
- 状态：待实施/待实际验收。

## AC-05 · P2

- 来源：F05/F06；规范：S08/S09。
- 归属/落点：UI类型模块、Metadata/Bindings/OutputModel/QML。
- 实施：displayTypeName离开OutputModel私有职责，所有展示复用；去掉输入ID与英文，message不附detail。
- 完成条件：所有输入输出/绑定提示中文，无机器ID；message含中文/英文/换行均保持。
- 状态：待实施/待实际验收。

## AC-06 · P1

- 来源：F08；规范：S10。
- 归属/落点：PythonRuntime/Storage、AppController关闭。
- 实施：就绪检查接入可重试关闭状态，冻结新调用/锚点，释放内部持有者后检查外部；不得先进析构。
- 完成条件：同线程外部owner阻止关闭可返回并释放重试，无GIL死锁；就绪后析构正常。
- 状态：待实施/待实际验收。

## AC-07 · P1

- 来源：F09；规范：S11。
- 归属/落点：PythonImages/ImageStorage。
- 实施：保留当前正确NumPy/Capsule路径；需租约的buffer用私有RAII或明确拒绝；异常回收一次。
- 完成条件：租约计数/resize受控验证；Mat浅拷贝/ROI延寿；不自动copy。
- 状态：待实施/待实际验收。

## AC-08 · P2

- 来源：F10；规范：S12。
- 归属/落点：DurationText/Snapshot/节点tooltip。
- 实施：明确每字段范围与已测标志，输入准备、初始化、调用、校验分别展示或有明确聚合；保持轻量更新。
- 完成条件：失败准备/首轮/热轮/Python案例均无伪造0和重复累计。
- 状态：待实施/待实际验收。

## AC-09 · P2

- 来源：设计评审；规范：S13。
- 归属/落点：core/ui/python内部模块。
- 实施：以规则、时钟、显示、Python生命周期为边界深封装；精简状态API；模板SSOT映射/必要concepts；删除旧分叉路径。
- 完成条件：已有公共合同不变；组件外无法任意改核心状态；无新增无用途接口/通用框架。
- 状态：待实施/待实际验收。

## AC-10 · P1验收门

- 来源：全部；规范：全部。
- 归属/落点：tests/真实安装包/Qt UI。
- 实施：执行05全矩阵、原生bridge和Windows GUI；记录SHA、构建配置、ctest原始日志、性能分项。同步已有WORKLOG/docs，不新增第二进度账本。
- 完成条件：所有P1通过真实测试；未执行项明确not_run，不据纯Python成功签整体验收。
- 状态：待实施/待实际验收。

## 依赖与交付

AC-01/02联动；AC-03为AC-04合法候选基础；AC-05可独立；AC-06/07互相关联owner，AC-08独立；AC-09按上述边界随功能修正收敛，不在功能未正确时做大范围无关重构；AC-10最后签收。

Tools本轮空路径与声明名称修改保留，后续主要提供真实CPU包和Python新图/ROI/原地修改夹具，不承担宿主调度或UI逻辑。Contracts无需为本轮UI/运行状态变更升级ABI。
