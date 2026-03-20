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
        uv_layer = mesh_eval.uv_layers.active.data if mesh_eval.uv_layers.active else None

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

            # Repeat triangle normal for each of the 3 vertices
            # this makes normals array same length as positions array / 3
            for _ in range(3):
                normals.extend((float(n.x), float(n.y), float(n.z)))

        # write v2 header: magic, version, triangle count, attribute count
        # then one uint32 component count per attribute and float payload blocks
        magic = 0x4D534842  # "MSHB"
        version = 2
        attr_components = [3, 3]
        if uv_layer:
            attr_components.append(2)

        with open(filepath, "wb") as f:
            f.write(struct.pack("<IIII", magic, version, tri_count, len(attr_components)))
            for comp in attr_components:
                f.write(struct.pack("<I", comp))

            f.write(struct.pack("<{}f".format(len(positions)), *positions))
            f.write(struct.pack("<{}f".format(len(normals)), *normals))
            if uv_layer:
                f.write(struct.pack("<{}f".format(len(uvs)), *uvs))

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
