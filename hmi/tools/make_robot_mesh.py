#!/usr/bin/env python3
"""Generate resources/robot_mesh.bin - drawable geometry for the 3D pose view.

The view renders in software, with a depth buffer, not OpenGL: the delivered
machine is an industrial PC whose GPU drivers are an unknown, and a control
station must not fail to draw because of one (see Robot3DView.h). A depth
buffer removes the two limits the old painter's-algorithm renderer had - a few
hundred faces, and no intersecting geometry - so this script no longer has to
reduce each link to its convex hull. It emits the real shapes.

Reduction is by **vertex clustering on a single grid shared by the whole
robot**, not per part. Sizing the grid from each part's own bounding box looks
reasonable and is wrong: B2's shank is 0.42 m long but 0.055 m thick, so a grid
derived from its length collapses the entire cross-section into one cell and
the legs come apart on screen. One grid in metres treats a thin part like a
thin part.

At the 22 mm grid used here the whole robot is about 16 000 triangles. Rendered
side by side against the untouched 595 000, the difference is not visible at
the size this widget draws - a face is already smaller than a pixel.

Per-vertex normals are emitted alongside. Without them the view shades each
triangle by its own normal, and every facet reads as a distinct plane - the
robot looks chiselled rather than moulded, and nothing like the same model in
MuJoCo. Normals are averaged only across edges under CREASE_DEG, so a genuine
edge (the trunk corners, the flats of a machined housing) stays sharp instead
of being smeared into a curve.

B2's legs have no joint feedback in the protocol, so the quadruped is baked at
the pose it actually stands in: the FixStand posture the walking policy hands
over from, evaluated through the real MJCF rather than guessed.

    python3 make_robot_mesh.py --fr3-meshes <dir with fairino3_v6 STLs>

Output is binary rather than a generated header because 14 500 triangles of C++
initialiser is about 700 KB of source that no one can review and every
incremental build pays for. It ships through qt_add_resources like the other
assets. The format is little-endian and documented in RobotMesh.h.

Sources:
  B2   b2_simulation/mujoco/b2_mujoco/models/{b2.xml, assets/*.obj}
  FR3  fairino_description/meshes/fairino3_v6/*.STL  (FAIR-INNOVATION/frcobot_ros2)
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
import trimesh

B2_MODELS = Path(
    "~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/models"
).expanduser()

# Face budget per link. The trunk and the upper arm carry the shape a person
# recognises the robot by, so they get more; the small wrist links get few.
# Vertex-clustering grid, in metres, shared by every part. 22 mm puts the whole
# robot at ~14 500 triangles; see the module docstring for why one grid rather
# than one per part, and for what a coarser grid costs (nothing visible until
# about 35 mm, where the shanks start to thin).
GRID_M = 0.022

# Edges sharper than this keep their hard shading; anything flatter is averaged
# into a smooth surface. 50 deg keeps box corners crisp while rounding the
# cylinders that make up most of both robots.
CREASE_DEG = 50.0

# The pose B2 stands in when it hands control to the policy.
STAND_JOINTS = {"hip": 0.0, "thigh": 0.8, "calf": -1.5}

# Trim from B2. These read as detail on the real machine and as noise at the
# size this view draws: the printed logos are flat decals, and the connector
# housings are 20 mm blocks. Dropping them buys back a fifth of the face budget
# without changing the silhouette. The LiDAR mast stays - the operator needs to
# see which way the robot is facing, and it is the only thing on top.
SKIP = ("logo_left", "logo_right", "f_dc_link", "r_dc_link",
        "f_oc_link", "r_oc_link", "fake_imu_link",
        "thigh_protect")


def decimate(mesh: trimesh.Trimesh, cell: float) -> trimesh.Trimesh:
    """Weld vertices onto a fixed grid and drop the faces that collapse.

    `cell` is in metres and the same for every part - see the module docstring
    for why that matters.
    """
    v = np.asarray(mesh.vertices)
    f = np.asarray(mesh.faces)
    if cell <= 0 or len(f) == 0:
        return mesh

    # A part smaller than one cell would weld to a single point and vanish -
    # B2 has several of those (the head and tail shells, the LiDAR housing).
    # Give any such part a grid fine enough to survive rather than dropping it:
    # four cells across its thinnest dimension keeps a recognisable solid.
    extent = v.max(0) - v.min(0)
    thinnest = float(extent[extent > 0].min()) if (extent > 0).any() else 0.0
    if thinnest > 0.0:
        cell = min(cell, thinnest / 4.0)

    key = np.floor((v - v.min(0)) / cell).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    inv = np.asarray(inv).reshape(-1)

    merged = np.zeros((int(inv.max()) + 1, 3))
    np.add.at(merged, inv, v)
    merged /= np.bincount(inv, minlength=len(merged))[:, None]

    nf = inv[f]
    nf = nf[(nf[:, 0] != nf[:, 1]) & (nf[:, 1] != nf[:, 2]) & (nf[:, 0] != nf[:, 2])]
    if len(nf) == 0:
        raise RuntimeError(f"grid of {cell:.4f} m dissolved a {len(f)}-face part")
    return trimesh.Trimesh(vertices=merged, faces=nf, process=False)


def vertex_normals(mesh: trimesh.Trimesh, crease_deg: float) -> np.ndarray:
    """Per-vertex normals, averaged only across edges flatter than `crease_deg`.

    Returns one normal per *corner* (3 per face), not per vertex: a vertex on a
    crease needs a different normal for each side of it, and a per-vertex array
    cannot hold that. The renderer indexes them the same way it indexes the
    corners, so this costs one extra array and no special cases.
    """
    v = np.asarray(mesh.vertices)
    f = np.asarray(mesh.faces)

    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    area = np.linalg.norm(fn, axis=1, keepdims=True)
    unit = fn / np.maximum(area, 1e-16)

    # Area-weighted sum of every face touching each vertex; a big face should
    # pull the shared normal further than a sliver.
    acc = np.zeros_like(v)
    for k in range(3):
        np.add.at(acc, f[:, k], fn)
    smooth = acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-16)

    # Use the smoothed normal only where it still agrees with the face it
    # belongs to; past the crease angle, keep the face's own.
    limit = np.cos(np.radians(crease_deg))
    out = np.empty((len(f), 3, 3))
    for k in range(3):
        cand = smooth[f[:, k]]
        agree = np.sum(cand * unit, axis=1) >= limit
        out[:, k, :] = np.where(agree[:, None], cand, unit)
    return out


def pack_part(name: str, group: int, mesh: trimesh.Trimesh, xform: np.ndarray) -> bytes:
    """One part, vertices already transformed into the frame it is drawn in."""
    v = trimesh.transform_points(mesh.vertices, xform).astype("<f4")
    f = np.asarray(mesh.faces).astype("<u4")
    # Normals rotate with the part but must not translate or scale with it.
    n = vertex_normals(mesh, CREASE_DEG) @ xform[:3, :3].T
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-16)
    enc = name.encode()
    return (struct.pack("<H", len(enc)) + enc
            + struct.pack("<BB", group, 0)
            + struct.pack("<I", len(v)) + v.tobytes()
            + struct.pack("<I", len(f)) + f.tobytes()
            + n.astype("<f4").tobytes())


ARM_MOUNT = np.array([-0.05, 0.0, 0.13])


def b2_links():
    """Every B2 visual mesh and its transform, in base_link coordinates.

    Vertices and faces come from MuJoCo's own arrays rather than from the .obj
    files.
    The compiler re-centres each mesh on its inertial frame when it loads the
    model and records the shift in `mesh_pos`/`mesh_quat`; reading the .obj
    directly and placing it with `geom_xpos` alone silently drops that shift,
    which put B2's feet 16 cm below where they belong.
    """
    import mujoco

    m = mujoco.MjModel.from_xml_path(str(B2_MODELS / "b2.xml"))
    d = mujoco.MjData(m)
    for i in range(m.njnt):
        n = mujoco.mj_id2name(m, mujoco.mjtObj.mjOBJ_JOINT, i) or ""
        for key, val in STAND_JOINTS.items():
            if n.endswith(f"_{key}_joint"):
                d.qpos[m.jnt_qposadr[i]] = val
    mujoco.mj_forward(m, d)

    base = mujoco.mj_name2id(m, mujoco.mjtObj.mjOBJ_BODY, "base_link")
    base_pos, base_mat = d.xpos[base].copy(), d.xmat[base].reshape(3, 3).copy()

    out = []
    for g in range(m.ngeom):
        if m.geom_type[g] != mujoco.mjtGeom.mjGEOM_MESH or m.geom_group[g] != 1:
            continue  # group 1 is the visual mesh; 3 is collision
        mesh_id = m.geom_dataid[g]
        mesh_name = mujoco.mj_id2name(m, mujoco.mjtObj.mjOBJ_MESH, mesh_id)
        if any(k in mesh_name for k in SKIP):
            continue

        v0 = m.mesh_vertadr[mesh_id]
        f0 = m.mesh_faceadr[mesh_id]
        verts = m.mesh_vert[v0:v0 + m.mesh_vertnum[mesh_id]].astype(float)
        tris = m.mesh_face[f0:f0 + m.mesh_facenum[mesh_id]]

        gp, gm = d.geom_xpos[g], d.geom_xmat[g].reshape(3, 3)
        x = np.eye(4)
        x[:3, :3] = base_mat.T @ gm
        x[:3, 3] = base_mat.T @ (gp - base_pos)
        out.append((mesh_name, trimesh.Trimesh(vertices=verts, faces=tris, process=False), x))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fr3-meshes", type=Path, required=True)
    ap.add_argument("--grid", type=float, default=GRID_M, help="clustering grid, metres")
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[1] / "resources/robot_mesh.bin")
    args = ap.parse_args()

    parts: list[bytes] = []
    faces = {"b2": 0, "fr3": 0}

    # Ground is the lowest point of what is actually drawn - the feet - rather
    # than a figure off the spec sheet, so the robot never floats or sinks.
    b2_meshes, lowest = [], float("inf")
    for name, raw, x in b2_links():
        m = decimate(raw, args.grid)
        lowest = min(lowest, float(trimesh.transform_points(m.vertices, x)[:, 2].min()))
        b2_meshes.append((name, m, x))
    base_height = -lowest

    for name, m, x in b2_meshes:
        # Legs are coloured separately so the stance reads at a glance.
        leg = len(name) > 3 and name[2] == "_" and name[0] in "FR" and name[1] in "LR"
        parts.append(pack_part(name, 1 if leg else 0, m, x))
        faces["b2"] += len(m.faces)

    # FR3 link meshes are authored in their own joint frame; the chain that
    # places them lives in RobotDef.h, so they are emitted untransformed and in
    # chain order - the reader pairs part i with joint frame i.
    for stem in ("base_link", "shoulder_link", "upperarm_link", "forearm_link",
                 "wrist1_link", "wrist2_link", "wrist3_link"):
        f = args.fr3_meshes / f"{stem}.STL"
        if not f.is_file():
            raise SystemExit(f"missing {f}")
        m = decimate(trimesh.load(f, force="mesh"), args.grid)
        parts.append(pack_part(f"fr3_{stem}", 2, m, np.eye(4)))
        faces["fr3"] += len(m.faces)

    blob = (b"B2MSH2\0\0"
            + struct.pack("<f", base_height)
            + struct.pack("<3f", *ARM_MOUNT)
            + struct.pack("<I", len(parts))
            + b"".join(parts))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)

    print(f"wrote {args.out}")
    print(f"  grid {args.grid * 1000:.0f} mm")
    print(f"  B2  {faces['b2']:>6} faces, FR3 {faces['fr3']:>6} faces, "
          f"total {sum(faces.values()):>6}")
    print(f"  base_link stands {base_height:.4f} m above the ground")
    print(f"  {len(blob) / 1024:.0f} KB over {len(parts)} parts")


if __name__ == "__main__":
    main()
