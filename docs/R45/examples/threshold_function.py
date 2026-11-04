"""目标 SDK 接口示例；图像算法由工具作者补充，不是可用阈值算法实现。"""
from .visiontools_sdk import result
UPSTREAM_STATUS, THRESHOLD, IMAGES, ROI = 0, 1, 2, 3
OUT_IMAGES = 2

def execute(inputs):
    upstream = inputs[UPSTREAM_STATUS]
    if upstream != 0:
        return result({OUT_IMAGES: None}, upstream, "上层工具未正常完成")
    threshold, images, rois = inputs[THRESHOLD], inputs[IMAGES], inputs[ROI]
    # 作者在此调用 OpenCV 或其他开源内核；读/写方式与共享并发由工具决定。
    return result({OUT_IMAGES: None}, 1001, "工具逻辑尚未实现")
