# 实施规格

| ID | 必须行为 | 落点 | 验收用例 |
|---|---|---|---|
| S-C01 | Close明确提交coordinator重置；所有内部slot清理后检查外部owner | AppController/RuntimeEngine管理协议 | Python图像结果直接关窗，不预先Reset |
| S-C02 | 关闭阶段Resetting继续消费runtime完成，AwaitOwners才等待外部引用 | AppController::poll/advancePending | 慢release GUI响应、外部引用延后归还 |
| S-C03 | 时间字段从计时器到实际tooltip完整接通，异常执行也算已测 | NodeRuntime/DurationText/FlowCanvas | 冷/热/失败/真实0/未执行 |
| S-C04 | draining统一阻断派发；队列项有明确结果，guard不破坏外层状态 | AppControllerReadback | 异常+排队Save/Text+同步重入 |
| S-C05 | Stop负例允许coordinator实际处理请求，未ACK不完成 | runtime_readback契约 | 旧谓词或提前completion变异必须失败 |
| S-C06 | 安装态工具包通过与完整App通过分别记录 | 安装链路/真实工作台 | 解释器来源、真实运行、停止、直接关闭 |

## 已有不变量继续保持

- Loop无逐轮重资源token，轻量状态有界更新；最终Stop读回等待ACK。
- Once/Node的Stop不解除读者屏障，start与completion不接受未结token。
- GUI只在安全阶段读取图像、ROI及可写数组，owner保证寿命而非线程安全。
- 声明级上下文无publication，结果级上下文校验publication；ROI交互身份仍校验。
- 有效边只取Upstream，严格source.y<target.y；Literal闲置source不成为依赖。
- 图像vector<cv::Mat> ↔ list[np.ndarray]；ROI三层double/float列表；数据无访问权限属性、无自动复制/COW。

## 关闭的禁止路径

不能只删除同步释放函数却保留关闭跳过Reset；不能提前设置closing后让poll无法处理Reset完成；不能在control锁内调用用户release；不能未清内部owner就对用户报告“外部引用未释放”；不能把析构阻塞当正常异步收尾。

## 测量语义

measured表示阶段被实际计时，success表示执行结果；二者独立。异常退出有耗时但结果失败，未进入阶段才未测。宿主准备/校验不冒名算法预/后处理。所有阶段按实际嵌套关系展示，禁止重复求和。
