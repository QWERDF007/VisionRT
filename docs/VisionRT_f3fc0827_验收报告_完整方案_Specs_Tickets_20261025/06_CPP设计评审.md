# C++与简洁性评审

本次ClosePhase枚举配合既有coordinator是合适的状态封装，解决了前次删同步函数却缺调用的断链；没有引入不必要模式。ScopeTimer统一measured，DrainGuard恢复previous，也比复制成功/异常分支更清晰。

剩余改进集中在入口一致性：save具有写盘、替换工程和同步节点副作用，应服从同一个关闭状态规则。避免closing、ClosePhase、busy在不同入口各自决定能否工作；不必把所有阶段建立继承类，可用简单谓词表达允许操作。

保留IToolInstance虚接口、具体final/override工具与模板工厂；模板服务于类型与工厂复用，不替代运行时多态。DataRef/UMatData/capsule/BufferLease继续承担真实所有权，工具自行协调共享写。不要把可写合同改为const深只读，也不要新增std::any公共类型系统。

测试注释要与能力一致：固定sleep不是“已观察coordinator处理”；普通完成回调触发reset不自动等同outer drain嵌套。优先让测试捕获真实失效路径，不用测试数量替代覆盖。文件行尾变化尽量统一，避免巨大无意义diff。

工程依旧轻量：现有枚举、RAII、工厂、Qt queued通知足够。剩余一项P2可局部修改完成，不需要数据库、规则引擎、哈希管线或事件总线。
