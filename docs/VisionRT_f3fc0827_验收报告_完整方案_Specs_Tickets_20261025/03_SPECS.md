# Specs

## 本轮新增约束

| ID | 必须行为 | 验收 |
|---|---|---|
| S-D01 | ClosePhase非None，save不得写盘、弹保存路径对话框或触发同步 | AwaitOwners期间Ctrl+S/直接save均拒绝 |
| S-D02 | synchronizeNodes关闭阶段不得提交运行时管理 | 实例构造数不增加、无新Synchronize |
| S-D03 | 未保存确认中的保存仍有效；关闭前后排队SaveRequest结果明确 | 保存后关闭正常、取消/保留策略可观察 |
| S-D04 | 关键并发负例能捕获旧错误，而非仅依赖固定睡眠 | 旧Stop谓词/错误guard变异应使定向测试失败 |
| S-D05 | 运行证据分别标注测试宿主与实际应用、解释器来源 | 现有安装测试与实际工作台结果可追溯 |

## 不变的产品合同

Contracts提供可写数据与owner，Tools决定修改方式，RT传递引用并保证自身读取阶段和生命周期。Image为vector<cv::Mat> / list[np.ndarray]，Array为对应嵌套vector/list，ROI为三层double/float。无只读/读写属性、无COW或隐式像素复制。

JSON覆盖声明/工程/参数/输出；工具手写接口与工厂，RT解析JSON并根据builtin/custom、kind、entry加载。工具只实现业务算法。

Loop仅轻量更新，不逐轮重读回；停止最终图像/ROI正常显示。Once/Node/Loop最终读者ACK后才完成。底部当前flow、全流程墙钟、持续elapsed三项语义不变，未测不等于0。

连线source.y<target.y，同级/向上/自身禁止；Literal闲置source不是有效依赖。名称菜单使用声明context，数据操作使用结果context。工具message原样显示，技术上下文只进显式诊断。
