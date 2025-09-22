from mayavi import mlab
import os

output_path = os.path.abspath("../result/test_mayavi.png")
os.makedirs(os.path.dirname(output_path), exist_ok=True)

mlab.figure(bgcolor=(1, 1, 1))
mlab.points3d([0, 1, 2], [0, 1, 2], [0, 1, 2], mode='cube')
mlab.savefig(output_path)
print(f"Test plot saved at: {output_path}")
mlab.show()
