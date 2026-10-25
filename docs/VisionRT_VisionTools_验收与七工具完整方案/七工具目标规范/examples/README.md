# JSON 草案使用说明

本目录包含10个目标schema 3工具声明和1个工程扩展片段，不是已可运行的工具包。工具声明使用与正文一致的端口定义；工程片段有说明用包装字段，不能作为完整工程加载。实现时工程节点扩展需并入 ProjectStorage 的真实 format 4 结构。

1. 先完成 DefinitionJson、节点契约、ProjectStorage 及对应工单，再落声明。
2. 动态表达式端口从 expression_config 物化，不能把本例空 inputs/outputs 当作没有业务端口。
3. select的基础Int端口被select_type统一替换；不允许运行时据值推断。
4. images/mask的显示元数据应沿用当前声明校验器支持的颜色及几何规则。多模式读图输出颜色不能硬编码“BGR”；需由已存在的结果显示规则识别灰度/原样输出，图像没有新增权限字段。
5. 未填默认值的端口没有JSON null占位，运行时缺省值按既有Missing编码处理；示例不新增第二套值序列化。
6. 所有源文件为UTF-8 JSON；没有YAML、HALCON依赖或构建生成器。
