bl_info = {
    # blender addon meta info, used by UI
    "name": "Export MeshBin (.meshbin)",
    "author": "Ryan Paillet",
    "version": (2, 0, 0),
    "blender": (2, 93, 0),
    "location": "File > Export > MeshBin (.meshbin)",
    "description": "Export active/selected mesh as custom binary with variable attribute header",
    "category": "Import-Export",
}

import bpy
import struct
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty
from bpy.types import Operator


def _get_export_armature(obj):
    # first look for an armature modifier because that is the normal skinning path
    for modifier in obj.modifiers:
        if modifier.type == 'ARMATURE' and modifier.object and modifier.object.type == 'ARMATURE':
            return modifier.object

    # parented meshes are also common in small rigs
    if obj.parent and obj.parent.type == 'ARMATURE':
        return obj.parent

    return None


def _build_bone_export_data(obj, armature_obj):
    # preserve Blender's armature order so mesh weights and animation files align by index
    armature_bones = list(armature_obj.data.bones)
    bone_indices_by_name = {
        bone.name: bone_index for bone_index, bone in enumerate(armature_bones)
    }

    mesh_from_world = obj.matrix_world.inverted()
    exported_bones = []
    for bone in armature_bones:
        parent_index = bone_indices_by_name[bone.parent.name] if bone.parent else -1
        world_head = armature_obj.matrix_world @ bone.head_local.to_4d()
        mesh_local_head = mesh_from_world @ world_head
        exported_bones.append((
            int(parent_index),
            float(mesh_local_head.x),
            float(mesh_local_head.y),
            float(mesh_local_head.z),
        ))

    return bone_indices_by_name, exported_bones


def _vertex_bone_influences(obj, vertex, bone_indices_by_name, max_influences=4):
    influences = []
    for group in vertex.groups:
        if group.group >= len(obj.vertex_groups):
            continue

        group_name = obj.vertex_groups[group.group].name
        bone_index = bone_indices_by_name.get(group_name)
        if bone_index is None or group.weight <= 0.0:
            continue

        influences.append((float(group.weight), float(bone_index)))

    influences.sort(key=lambda entry: entry[0], reverse=True)
    influences = influences[:max_influences]

    bone_ids = [0.0] * max_influences
    bone_weights = [0.0] * max_influences
    total_weight = sum(weight for weight, _ in influences)
    if total_weight > 1.0e-8:
        for influence_index, (weight, bone_index) in enumerate(influences):
            bone_ids[influence_index] = bone_index
            bone_weights[influence_index] = weight / total_weight

    return bone_ids, bone_weights


def _get_export_object(context):
    # try active object first, if its mesh
    # Prefer active object if it's a mesh
    obj = context.active_object
    if obj and obj.type == 'MESH':
        return obj

    # Otherwise, first selected mesh object
    # we just grab the first, no fancy pick UI here
    for o in context.selected_objects:
        if o.type == 'MESH':
            return o

    # none found, return None
    return None


def write_meshbin(context, filepath):
    # main export function, called by blender
    obj = _get_export_object(context)
    if obj is None:
        raise RuntimeError("No mesh object selected/active to export.")

    armature_obj = _get_export_armature(obj)
    bone_indices_by_name = {}
    exported_bones = []
    if armature_obj is not None:
        bone_indices_by_name, exported_bones = _build_bone_export_data(obj, armature_obj)

    # Evaluate modifiers so you export what you see
    depsgraph = context.evaluated_depsgraph_get()
    obj_eval = obj.evaluated_get(depsgraph)
    mesh_eval = obj_eval.to_mesh()

    try:
        # Ensure triangulation data is available without requiring manual triangulation.
        # loop_triangles represent the mesh as triangles for rendering/export.
        mesh_eval.calc_loop_triangles()

        tri_count = len(mesh_eval.loop_triangles)
        if tri_count == 0:
            raise RuntimeError("Mesh has no triangles to export (is it empty?).")

        # lists for attribute float payloads
        positions = []
        normals = []
        uvs = []
        bone_ids = []
        bone_weights = []
        uv_layer = mesh_eval.uv_layers.active.data if mesh_eval.uv_layers.active else None
        write_uv_block = uv_layer is not None or armature_obj is not None

        # export in object space and keep per-loop uv where available
        for tri in mesh_eval.loop_triangles:
            n = tri.normal  # already normalized
            loop_indices = tri.loops

            for li in loop_indices:
                vi = mesh_eval.loops[li].vertex_index
                # grab vertex position, add to list
                co = mesh_eval.vertices[vi].co
                positions.extend((float(co.x), float(co.y), float(co.z)))

                if uv_layer:
                    uv = uv_layer[li].uv
                    uvs.extend((float(uv.x), float(uv.y)))
                elif armature_obj is not None:
                    # skinned meshes still reserve UV attribute slot 2 so bone ids
                    # and bone weights stay at shader locations 3 and 4.
                    # if the artist did not author UVs, generate the same simple
                    # planar mapping the runtime uses as its shader fallback.
                    uvs.extend((float(co.x) * 0.35 + 0.5, float(co.z) * 0.35 + 0.5))

                if armature_obj is not None:
                    vertex = mesh_eval.vertices[vi]
                    vertex_bone_ids, vertex_bone_weights = _vertex_bone_influences(
                        obj, vertex, bone_indices_by_name)
                    bone_ids.extend(vertex_bone_ids)
                    bone_weights.extend(vertex_bone_weights)

            # Repeat triangle normal for each of the 3 vertices
            # this makes normals array same length as positions array / 3
            for _ in range(3):
                normals.extend((float(n.x), float(n.y), float(n.z)))

        # write v2 header: magic, version, triangle count, attribute count
        # then one uint32 component count per attribute and float payload blocks
        magic = 0x4D534842  # "MSHB"
        version = 3
        attr_components = [3, 3]
        if write_uv_block:
            attr_components.append(2)
        if armature_obj is not None:
            attr_components.extend((4, 4))
        flags = 1 if armature_obj is not None else 0

        with open(filepath, "wb") as f:
            f.write(struct.pack("<IIII", magic, version, tri_count, len(attr_components)))
            for comp in attr_components:
                f.write(struct.pack("<I", comp))
            f.write(struct.pack("<I", flags))

            if armature_obj is not None:
                f.write(struct.pack("<I", len(exported_bones)))
                for parent_index, head_x, head_y, head_z in exported_bones:
                    f.write(struct.pack("<ifff", parent_index, head_x, head_y, head_z))

            f.write(struct.pack("<{}f".format(len(positions)), *positions))
            f.write(struct.pack("<{}f".format(len(normals)), *normals))
            if write_uv_block:
                f.write(struct.pack("<{}f".format(len(uvs)), *uvs))
            if armature_obj is not None:
                f.write(struct.pack("<{}f".format(len(bone_ids)), *bone_ids))
                f.write(struct.pack("<{}f".format(len(bone_weights)), *bone_weights))

    finally:
        # Always free the evaluated mesh
        # important so blender doesnt leak memory
        obj_eval.to_mesh_clear()

    # blender expects this return set when operator finishes
    return {'FINISHED'}


class EXPORT_OT_meshbin(Operator, ExportHelper):
    # basic export operator class for blender UI
    bl_idname = "export_mesh.meshbin"
    bl_label = "Export MeshBin"
    bl_options = {'PRESET'}

    filename_ext = ".meshbin"
    # only show .meshbin in file picker
    filter_glob: StringProperty(default="*.meshbin", options={'HIDDEN'})

    def execute(self, context):
        # called when user hits Export button
        try:
            return write_meshbin(context, self.filepath)
        except Exception as e:
            # report to blender UI if it fails
            self.report({'ERROR'}, str(e))
            return {'CANCELLED'}


def menu_func_export(self, context):
    # hook into blender File > Export menu
    self.layout.operator(EXPORT_OT_meshbin.bl_idname, text="MeshBin (.meshbin)")


def register():
    # register addon classes and menu hook
    bpy.utils.register_class(EXPORT_OT_meshbin)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    # cleanup when addon is disabled
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(EXPORT_OT_meshbin)


if __name__ == "__main__":
    # allows running script directly in blender text editor
    register()
