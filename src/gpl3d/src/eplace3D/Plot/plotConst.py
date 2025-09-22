import numpy as np
from mayavi import mlab
from tvtk.api import tvtk
from pyface.api import GUI  # 关键：强制 VTK GUI 退出

def draw_3d_modules(modules, fillers, core_x, core_y, core_z, output_path, show_fillers=False):
    """
    1) 绘制背景立方体 (线框)
    2) 绘制 modules (红色, 不透明, 仅显示真实边框)
    3) (可选) 绘制 fillers (绿色, 不透明, 仅显示真实边框)
    
    数据格式:
      - modules / fillers: [[x, y, z, w, h, d], ...]
        x, y => 立方体 X, Y 平面中心
        z    => 立方体 Z 方向底面 (最低点)
        w, h, d => 立方体 X, Y, Z 方向尺寸
      - core_x, core_y, core_z: 布局背景立方体尺寸
      - show_fillers: 是否绘制 fillers (默认为 True)
    """

    def generate_mesh_data(cubes):
        """ 生成立方体顶点数据 """
        if len(cubes) == 0:
            return None, None, None, None

        vertices = []
        faces = []
        OFFSETS = np.array([
            [-0.5, -0.5, -0.5], [ 0.5, -0.5, -0.5],
            [ 0.5,  0.5, -0.5], [-0.5,  0.5, -0.5],
            [-0.5, -0.5,  0.5], [ 0.5, -0.5,  0.5],
            [ 0.5,  0.5,  0.5], [-0.5,  0.5,  0.5]
        ])
        FACES_TEMPLATE = np.array([
            [0, 1, 2], [0, 2, 3],  # 底面
            [4, 5, 6], [4, 6, 7],  # 顶面
            [0, 1, 5], [0, 5, 4],  # 前面
            [2, 3, 7], [2, 7, 6],  # 后面
            [1, 2, 6], [1, 6, 5],  # 右面
            [0, 3, 7], [0, 7, 4]   # 左面
        ])

        for i, (cx, cy, z_b, w, h, d) in enumerate(cubes):
            cz = z_b + d * 0.5
            cube_vertices = np.column_stack([
                cx + OFFSETS[:, 0] * w,
                cy + OFFSETS[:, 1] * h,
                cz + OFFSETS[:, 2] * d
            ])
            vertices.append(cube_vertices)
            faces.append(FACES_TEMPLATE + i * 8)

        vertices = np.vstack(vertices)
        faces = np.vstack(faces)
        return vertices[:, 0], vertices[:, 1], vertices[:, 2], faces

    def add_cube_edges(x, y, z, faces):
        """ 仅绘制立方体边框，提高清晰度 """
        edges = mlab.triangular_mesh(x, y, z, faces, color=(0, 0, 0), opacity=1.0)
        edges.actor.property.representation = 'wireframe'
        edges.actor.property.line_width = 3.0  # 边框加粗，提高清晰度

    # === 1) 创建高清窗口 ===
    fig = mlab.figure(bgcolor=(1,1,1), size=(1200, 900))
    fig.scene.anti_aliasing_frames = 16  # 开启 4x 抗锯齿，提升清晰度

    # === 2) 处理背景立方体 ===
    bbox_data = [[core_x/2.0, core_y/2.0, 0.0, core_x, core_y, core_z]]
    x, y, z, faces = generate_mesh_data(bbox_data)
    if x is not None:
        mlab.triangular_mesh(x, y, z, faces, color=(0.8,0.8,0.8), opacity=0.3, representation='wireframe')

    # === 3) 处理 modules (红色) ===
    x, y, z, faces = generate_mesh_data(modules)
    if x is not None:
        mod_mesh = mlab.triangular_mesh(x, y, z, faces, color=(0.8,0,0), opacity=0.7, representation='surface')
        add_cube_edges(x, y, z, faces)

    # === 4) 处理 fillers (绿色)（可选） ===
    if show_fillers:
        x, y, z, faces = generate_mesh_data(fillers)
        if x is not None:
            fill_mesh = mlab.triangular_mesh(x, y, z, faces, color=(0,0.8,0), opacity=0.7, representation='surface')
            add_cube_edges(x, y, z, faces)

    # === 5) 坐标轴 + 视图调整 ===
    mlab.axes(extent=[0, core_x, 0, core_y, 0, core_z], xlabel='X', ylabel='Y', zlabel='Z')
    mlab.view(distance='auto')

    # === 6) 以超清分辨率保存 + 显示 ===
    mlab.savefig(output_path, size=(4000, 3000))  # 超高清图片
    # mlab.show()
    print(f"Plot saved at: {output_path}")
    