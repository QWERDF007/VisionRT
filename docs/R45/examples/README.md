# 新协议示例

全部声明都是**目标 schema6**；当前基线生产解析器仍是schema5，不能直接加载本包新示例。文件名前的 threshold 只作为命名示例，不代表已有正式读图/阈值工具已被改造。

- threshold.json：编号1阈值、2图像、3ROI；输出2图像，公共端口不在文件中。
- threshold_reordered.json：显示顺序变为2、1、3，编号和取参不变。
- threshold_deleted_max_id.json：删3仍保存next.inputs=4。
- threshold_added_after_delete.json：新参数用4，不复用3。
- all_types.json：34个具体类型，输入1～34，输出2～35；完整显式映射见all_types_function.py。
- counter_class.json/.py：init0为起始值，inputs0是注入的上层状态；不同方向不冲突。
- project_binding_fragment.json：工程字段片段，node_fragment是说明容器，不是可直接运行的完整工程。
- migration_map_example.json：演示旧名称→ID映射，迁移用；不是运行期别名表。

Python文件为未来SDK的作者示例。threshold_function.py明确返回未实现业务状态，避免假装已实现ROI阈值算法。实际安装应将代码部署为各声明package下tool.py，并使用同版随包SDK。原型与参考验证不替代产品Qt解析、native ABI和真实算法验收。
