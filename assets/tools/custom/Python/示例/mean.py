import numpy as np

def execute(inputs):
    images = inputs.get('images')
    if images is None:
        return {'__status_code': 1, '__status_message': 'Missing images', 'means': None}
    means = [float(np.mean(img)) if img.size > 0 else 0.0 for img in images]
    return {'__status_code': 0, '__status_message': '', 'means': np.array(means, dtype=np.float64)}
