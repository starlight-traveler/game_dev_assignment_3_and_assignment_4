bl_info = {
    # blender reads this dictionary to figure out how the addon should
    # appear inside the ui. none of this affects the export logic itself,
    # but blender uses these fields to list the addon, show compatibility,
    # and place the operator in the export menu.
    "name": "Export AnimBin (.animbin)",
    "author": "Ryan Paillet",
    "version": (1, 0, 0),
    "blender": (2, 93, 0),
    "location": "File > Export > AnimBin (.animbin)",
    "description": "Export one armature action as a compact binary animation clip",
    "category": "Import-Export",
}

import re
import struct

import bpy
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty
from bpy.types import Operator


_ANIM_MAGIC = 0x414E494D  # "ANIM"
_ANIM_VERSION = 1
# this regex matches blender fcurve paths that look like:
# pose.bones["bone_name_here"].rotation_quaternion
# the capture group pulls out just the bone name so later code can map
# animation channels back to actual bones in the armature.
_ROTATION_QUATERNION_PATH = re.compile(r'^pose\.bones\["(.+)"\]\.rotation_quaternion$')


def _get_export_armature(context):
    # first choice: if the active object is already an armature, use it
    # immediately. this is the most direct case and avoids extra guessing.
    obj = context.active_object
    if obj and obj.type == 'ARMATURE':
        return obj

    # otherwise, build a list of selected objects to inspect. if the active
    # object somehow is not in the selected list, insert it at the front so
    # it still gets priority during the search.
    candidates = list(context.selected_objects)
    if obj and obj not in candidates:
        candidates.insert(0, obj)

    # common workflow: the user selects a mesh instead of the rig. in that
    # case we try to recover the controlling armature by checking armature
    # modifiers first, then falling back to the mesh parent relationship.
    for candidate in candidates:
        if candidate.type == 'MESH':
            for modifier in candidate.modifiers:
                if modifier.type == 'ARMATURE' and modifier.object and modifier.object.type == 'ARMATURE':
                    return modifier.object
            if candidate.parent and candidate.parent.type == 'ARMATURE':
                return candidate.parent

    # final fallback: maybe the active object was something else, but one of
    # the selected objects is still an armature. if so, return the first one.
    for candidate in candidates:
        if candidate.type == 'ARMATURE':
            return candidate

    # returning none lets the caller raise a user-facing error with a more
    # helpful message instead of crashing here.
    return None


def _get_export_action(armature_obj, action_name):
    # if the caller explicitly named an action, trust that request and look it
    # up directly in blender's action datablock collection.
    if action_name:
        action = bpy.data.actions.get(action_name)
        if action is None:
            raise RuntimeError("Action '{}' was not found.".format(action_name))
        return action

    # otherwise, use the action that is currently active on the armature's
    # animation data. this matches the common blender workflow where a rig
    # has one action selected in the action editor or dope sheet.
    animation_data = armature_obj.animation_data
    if animation_data and animation_data.action:
        return animation_data.action

    # exporting without an action would produce an empty or meaningless file,
    # so fail early with a message that tells the user what to fix.
    raise RuntimeError("No action selected. Set action_name or make one action active on the armature.")


def _find_export_slot(action, armature_obj):
    # blender's newer slotted action system can store multiple independent
    # channel sets inside one action. when exporting from an armature, prefer
    # the slot already associated with that object's animation data.
    slots = list(getattr(action, "slots", ()))
    if not slots:
        return None

    animation_data = armature_obj.animation_data if armature_obj is not None else None
    candidate_identifiers = []
    if animation_data is not None:
        current_slot = getattr(animation_data, "action_slot", None)
        if current_slot is not None and getattr(current_slot, "identifier", ""):
            candidate_identifiers.append(current_slot.identifier)

        last_slot_identifier = getattr(animation_data, "last_slot_identifier", "")
        if last_slot_identifier:
            candidate_identifiers.append(last_slot_identifier)

    for identifier in candidate_identifiers:
        for slot in slots:
            if getattr(slot, "identifier", "") == identifier:
                return slot

    for slot in slots:
        users_fn = getattr(slot, "users", None)
        if users_fn is None:
            continue
        try:
            users = users_fn()
        except Exception:
            continue
        for user in users:
            if user == armature_obj:
                return slot

    active_slot = getattr(getattr(action, "slots", None), "active", None)
    if active_slot is not None:
        return active_slot

    if len(slots) == 1:
        return slots[0]

    return None


def _collect_action_fcurves(action, armature_obj):
    # blender 4.x actions may be layered/slotted internally. direct
    # action.fcurves access is documented as legacy compatibility behavior and
    # may be absent or insufficient depending on the build and action layout.
    #
    # collect curves in this order:
    # 1. the direct legacy collection when available
    # 2. the layered channelbag data from action layers/strips/slots
    direct_fcurves = None
    try:
        direct_fcurves = list(action.fcurves)
    except Exception:
        direct_fcurves = None

    layered_fcurves = []
    target_slot = _find_export_slot(action, armature_obj)
    target_slot_handle = getattr(target_slot, "handle", None)
    for layer in getattr(action, "layers", ()):
        for strip in getattr(layer, "strips", ()):
            for channelbag in getattr(strip, "channelbags", ()):
                if target_slot_handle is not None:
                    slot = getattr(channelbag, "slot", None)
                    slot_handle = getattr(channelbag, "slot_handle", None)
                    if slot is not None and getattr(slot, "handle", None) != target_slot_handle:
                        continue
                    if slot is None and slot_handle is not None and slot_handle != target_slot_handle:
                        continue
                layered_fcurves.extend(list(getattr(channelbag, "fcurves", ())))

    if layered_fcurves:
        return layered_fcurves
    if direct_fcurves is not None:
        return direct_fcurves
    return []


def _animated_bone_names(action, armature_obj):
    # scan every fcurve in the action and keep only the ones whose data path
    # points at a pose bone quaternion channel. a set is used so each bone
    # name is stored only once even though quaternion animation usually has
    # four separate fcurves for x, y, z, and w.
    bone_names = set()
    for fcurve in _collect_action_fcurves(action, armature_obj):
        match = _ROTATION_QUATERNION_PATH.match(fcurve.data_path)
        if match:
            bone_names.add(match.group(1))
    return bone_names


def _animated_bones_in_armature_order(armature_obj, action):
    # start from the set of animated bone names found in the action.
    animated_names = _animated_bone_names(action, armature_obj)
    if not animated_names:
        raise RuntimeError("Action '{}' has no pose bone quaternion curves.".format(action.name))

    # the exporter writes bone ids, not bone names, so it needs a stable order
    # that matches the armature's internal bone list. walking the armature in
    # order and filtering to animated bones gives us that stable mapping.
    ordered_bones = []
    for bone_index, bone in enumerate(armature_obj.data.bones):
        if bone.name in animated_names:
            ordered_bones.append((bone_index, bone.name))

    # it is possible for an action to mention bones that are not part of the
    # chosen armature. if that happens, stop instead of exporting invalid ids.
    if not ordered_bones:
        raise RuntimeError(
            "Action '{}' does not animate bones that belong to armature '{}'.".format(
                action.name, armature_obj.name))

    return ordered_bones


def _keyframe_frames(action, armature_obj, bone_names):
    # gather every frame index that has quaternion keys for the bones we are
    # actually exporting. using a set removes duplicates caused by multiple
    # quaternion components landing on the same frame.
    keyframes = set()
    for fcurve in _collect_action_fcurves(action, armature_obj):
        match = _ROTATION_QUATERNION_PATH.match(fcurve.data_path)
        if not match or match.group(1) not in bone_names:
            continue

        for keyframe in fcurve.keyframe_points:
            keyframes.add(float(keyframe.co[0]))

    # no keyframes means there is nothing meaningful to serialize.
    if not keyframes:
        raise RuntimeError("Action '{}' does not contain any keyed quaternion frames.".format(action.name))

    # sorting matters because the binary file is written frame-by-frame in time
    # order, and later code assumes the first entry is the earliest keyframe.
    return sorted(keyframes)


def _set_scene_frame(scene, frame_value):
    # blender frame_set takes an integer frame plus an optional subframe, so
    # split a floating-point frame like 12.5 into 12 and 0.5 before sampling.
    # this lets the exporter handle non-integer key times correctly.
    whole_frame = int(frame_value)
    subframe = frame_value - float(whole_frame)
    scene.frame_set(whole_frame, subframe=subframe)


def write_animbin(context, filepath, action_name):
    # resolve the armature first because almost everything else depends on it:
    # the active action, bone ordering, and sampled pose transforms.
    armature_obj = _get_export_armature(context)
    if armature_obj is None:
        raise RuntimeError("No armature found. Select an armature or a skinned mesh.")

    # decide which action to export and derive the exact subset of bones and
    # frames that should appear in the output file.
    action = _get_export_action(armature_obj, action_name)
    ordered_bones = _animated_bones_in_armature_order(armature_obj, action)
    bone_name_lookup = [bone_name for _, bone_name in ordered_bones]
    keyed_frames = _keyframe_frames(action, armature_obj, set(bone_name_lookup))

    # convert blender's fps / fps_base values into an actual frames-per-second
    # number. this is necessary because timestamps in the file are written in
    # seconds, not raw frame numbers.
    scene = context.scene
    fps = scene.render.fps / scene.render.fps_base if scene.render.fps_base else float(scene.render.fps)
    if fps <= 0.0:
        raise RuntimeError("Scene FPS must be positive.")

    # preserve the current scene state so the exporter does not leave blender
    # on a different frame or with a different action selected after finishing.
    previous_frame = scene.frame_current_final
    animation_data = armature_obj.animation_data_create()
    previous_action = animation_data.action
    previous_action_slot = getattr(animation_data, "action_slot", None)
    export_slot = _find_export_slot(action, armature_obj)

    try:
        # temporarily force the target action onto the armature so sampling the
        # pose at each keyframe pulls data from the clip we are exporting.
        animation_data.action = action
        if export_slot is not None and hasattr(animation_data, "action_slot"):
            try:
                animation_data.action_slot = export_slot
            except Exception:
                pass

        with open(filepath, "wb") as output:
            # header:
            # magic, version, total armature bones, animated bones, keyframe count
            #
            # this means the file starts with enough metadata for a loader to
            # sanity-check the format and know how many records to read later.
            # the little-endian format string "<IIIII" says "write five unsigned
            # 32-bit integers in little-endian byte order".
            output.write(struct.pack(
                "<IIIII",
                _ANIM_MAGIC,
                _ANIM_VERSION,
                len(armature_obj.data.bones),
                len(ordered_bones),
                len(keyed_frames)))

            # after the header, write the armature bone indices for every
            # animated bone exactly once. this avoids repeating the ids in each
            # frame block and keeps the file smaller.
            for bone_index, _ in ordered_bones:
                output.write(struct.pack("<I", bone_index))

            # the first keyframe becomes time zero in the exported clip. every
            # later timestamp is measured relative to that starting point.
            first_frame = keyed_frames[0]
            for frame_value in keyed_frames:
                # move the blender scene to the frame being exported so reading
                # pose_bone.rotation_quaternion returns the sampled pose at that
                # exact moment in the action.
                _set_scene_frame(scene, frame_value)

                # timestamps are relative to the first exported keyframe, which
                # makes the clip start at 0.0 seconds instead of some arbitrary
                # frame number from the original blender timeline.
                timestamp_seconds = (frame_value - first_frame) / fps
                output.write(struct.pack("<f", float(timestamp_seconds)))

                for _, bone_name in ordered_bones:
                    # get the pose bone by name so we can sample the live pose
                    # value at the current frame. pose bones are the animated
                    # runtime bones, not the static armature definition bones.
                    pose_bone = armature_obj.pose.bones.get(bone_name)
                    if pose_bone is None:
                        raise RuntimeError(
                            "Bone '{}' from action '{}' was not found on armature '{}'.".format(
                                bone_name, action.name, armature_obj.name))

                    # copy the quaternion before normalizing so we do not mutate
                    # blender's pose data directly. normalizing is important
                    # because tiny floating-point drift can accumulate, and many
                    # animation systems assume unit quaternions for rotation.
                    #
                    # export order is x, y, z, w because that is what the game
                    # engine expects to read from disk.
                    quaternion = pose_bone.rotation_quaternion.copy()
                    quaternion.normalize()
                    output.write(struct.pack(
                        "<ffff",
                        float(quaternion.x),
                        float(quaternion.y),
                        float(quaternion.z),
                        float(quaternion.w)))
    finally:
        # always restore blender state, even if an exception happens midway
        # through export. the finally block guarantees cleanup runs.
        if hasattr(animation_data, "action_slot"):
            try:
                animation_data.action_slot = previous_action_slot
            except Exception:
                pass
        animation_data.action = previous_action
        _set_scene_frame(scene, previous_frame)

    return {'FINISHED'}


class EXPORT_OT_animbin(Operator, ExportHelper):
    # this operator is the ui-facing wrapper around write_animbin. blender
    # registers operators as commands that can show up in menus and dialogs.
    bl_idname = "export_anim.animbin"
    bl_label = "Export AnimBin"
    bl_options = {'PRESET'}

    filename_ext = ".animbin"
    # only show .animbin in the file picker by default so the export dialog
    # nudges the user toward the correct extension.
    filter_glob: StringProperty(default="*.animbin", options={'HIDDEN'})
    # optional explicit action selection for rigs with multiple clips. if this
    # stays empty, the exporter falls back to the armature's active action.
    action_name: StringProperty(
        name="Action",
        description="Optional action name. Leave empty to export the armature's active action",
        default="")

    def execute(self, context):
        # blender calls execute after the user confirms the export dialog.
        # returning {'FINISHED'} tells blender the operation succeeded, while
        # {'CANCELLED'} indicates failure or user cancellation.
        try:
            return write_animbin(context, self.filepath, self.action_name)
        except Exception as exc:
            # push the error into blender's report system so the message shows
            # up in the interface instead of disappearing into the console.
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}


def menu_func_export(self, context):
    # this function is attached to blender's file > export menu during
    # registration. when blender draws that menu, this adds our entry.
    self.layout.operator(EXPORT_OT_animbin.bl_idname, text="AnimBin (.animbin)")


def register():
    # register the operator class first so blender knows about it, then append
    # the menu callback so users can launch it from the export menu.
    bpy.utils.register_class(EXPORT_OT_animbin)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
    # undo what register did so disabling or reloading the addon does not leave
    # stale menu entries or registered classes behind.
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(EXPORT_OT_animbin)


if __name__ == "__main__":
    # this makes the file convenient to test directly from blender's text
    # editor, where running the script should register the addon immediately.
    register()
