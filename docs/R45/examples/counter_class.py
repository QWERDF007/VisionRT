"""目标SDK类工具示例，init0与inputs0是不同方向。"""
from .visiontools_sdk import result, status

class Tool:
    def initialize(self, params):
        self.count = params[0]
        return status()

    def execute(self, inputs):
        if inputs[0] != 0:
            return result({2: None}, inputs[0], "上层工具未正常完成")
        self.count += 1
        return result({2: self.count})

    def release(self):
        return status()
