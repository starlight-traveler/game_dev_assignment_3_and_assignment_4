Objective B: Armatures in Mesh Export and Animation Export
The second aspect of kinematics to build is for animating rigged character models. Lecture
18 provides a description of “parenting” bones to a mesh in Blender, as well as the general
5
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
process of building out bones for a mesh to rig with an armature. Once rigged, animations
can be built out by selecting the “Animation” tab and entering “Pose” mode:
The result provides a working environment where bones can be transformed, and their
transformed states stored in keyframes of the timeline found at the bottom of the window.
This information is relevant because you will need to build a mesh with an armature to unit
test your animation system. The final requirements of this system will be listed in a
moment but first let’s briefly discuss building an action in Blender – an animation with
keyframes and select set of bones animated.
Building a keyframe is straightforward. You can modify a bone – through the normal affine
transformations (‘g’ key to translate a bone; ‘r’ key to rotate a bone; ‘s’ key to scale a bone)
locked to a specific axis if you type ‘x’, ‘y’, or ‘z’ after starting a transformation – and then
save the bone state as a keyframe by hitting the ‘i’ key while in the animation editor window
(i.e., Blender chooses which hotkey to execute based on where the mouse cursor is – in
this the cursor should be in the window where the bones are being modified). Once you hit
the ’i’ key, a window will pop up asking you which data you want to store in the keyframe.
The keyframe data can work for a single bone, or for many bones – effectively as many as
are selected. The keyframe insertion window appears as follows:
6
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
As discussed in Lecture 18, these bones describe something referred to as “vertex
groups”, which has a set of vertices in the group – and each vertex in the group has a
weight associated with how much this group will affect the vertex. The convenient feature
of these groups is they are ordered sequentially in a way that satisfies going through the
hierarchy of bones in a forward pass – i.e., no child bones appear sequentially before their
respective parent bone. This simplification will make writing a new mesh exporter, and
animation file exporter much simpler – as the weight bone outputs can bijectively align by
index between the mesh and animation files. Again, observe the following to see the
location of vertex groups in Properties window:
7
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
Once you have a sample mesh with a parented armature, then the following data in Python
can be explored to determine how to extend your mesh export as well as define a new file
type for animations associated with this mesh.
Mesh Export:
Extending your mesh export will require both modifying your Python script as well as your
C++ parsing code.
Note, not every mesh will necessarily have armatures (unless you decide to enforce it as
a requirement in your engine). This means you will need to determine how to store
conditional information in your mesh export. For instance, adding a keyword like “bones”
which will then have bone weights and indices for each vertex. If you parse your file into a
string using std::getline, then you can use the find function of the string object to determine
the index where the keyword was found, or a std::npos if nothing was found. Of course, the
boneID and boneWeight attributes associated with your mesh are arrays – because
more than one bone will influence a vertex – which means your parser will need to be
able to determine how large this attribute array will be. The bone maximum for this
assignment will be 4 weights – so consider using this as a constant for both writing to your
mesh file and parsing from it.
An example of this new mesh data would look as follows:
• Position data (3-part vector)
8
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
• Normal data (3-part vector)
• UV data (2-part vector)
• Identifier conditional if bones preset
o Bone indices[4]
o Bone weights[4]
The following field data in Blender will be useful to write this new information in your
exporter:
• bpy.data.objects[‘NameOfObject’].vertex_groups
o This is the array of vertex_groups created from the armature
• bpy.data.meshes[‘NameOfMesh’].vertices
o Inside this array of vertices, group data can be found
▪ E.G., some v in …vertices
▪ v.group
• Array of bone groups associated with this vertex
• E.G., some g in v.group
• g.group
o Group index (i.e., index into vertex_groups array)
• g.weight
o Fractional weight this group has on this vertex
This is almost all of the information we need to pass into the game engine for a mesh with
bones. But, because we are going to be interpolating between keyframes at runtime (with
an unknown/variable frametime) we will need to store additional spatial information about
the bones in this armature – building out a data structure similar to a scene graph in order
to keep track of where child bones translate as their parent bones are transformed.
The bare minimum expected in your animation controller is to define an armature that can
have its bones rotated (no need to deal with translations or scaling bones). Your Shape
class should be extended with bone armature data – storing information to ascertain child
bone translations given parent rotations. Notably, a Shape object with bones should
have an array of bones that matches the bone array order from Blender – which will
then match the arrays of the vertex shader. We will need to perform a forward pass
through an object-instance with bones to ensure bones are repositioned according to their
parent bone’s rotations. To make this possible, at a minimum, you should consider storing
a parent-to-child vector and bone origins.
This parent-to-child vector is just a vector between the head of a parent bone to the head
of the child bone. If a parent bone rotates, then that rotation should be applied to the
parent-to-child vector to figure out where a child bone’s head has been rotated to. This is
not enough to define the translation portion of a bone’s 4x4 matrix – we need no translation
to occur if the child bone has not be displaced by any parent bones. So, we need to also
9
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
keep track of the original head position of a bone – the bone’s origin inside the local space
of the mesh. So, for a Shape that has bones, some additional variables will be important to
consider:
• Bones[N]
o Bone local position
o Parent-child separation
o Parent bone index (more on this in Animation File section)
To generate the set of bone matrices to stream to a vertex shader, ensure you work through
parent bones before children, converting the current bone’s quaternion to a 4x4 matrix –
and storing any change in position in the 4th column of the 4x4 matrix. Pseudocode follows:
1. 2. Set root bone 4x4 to identity matrix
For i = 1; i < boneCount; ++i
a. Animation interpolation (more on this in Animation Playback section)
b. Current rotation = parent rotation * this rotation
c. Displacement = Parent Displacement + Current Rotation * parent-to-child
vector
d. This Matrix is Current Rotation and Displacement – Bone local position
The vector math to work through is effectively carrying forward parent rotations and local
space position changes to child bones. This task is flexible to your current designs and
aspirations for your engine tools – but at a minimum will be tested to verify that a character
with an armature can be successfully animated from an animation file within your engine.
Animation Files:
Game engines that accept filetypes like FBX can possess both the general mesh and
armature data as well as all of the animations affiliated with this mesh. This is because FBX
is a fairly flexible filetype – storing name identifiers alongside type identifiers and byte
counts to be able to segment a file with arrays of data affiliated to some named attribute.
However, it can be convenient to decouple animations from mesh files – for a few reasons:
• Tedious to re-export a mesh just because an animation was added
• Meshes can have shared bone structures
o Meaning animations can be mesh agnostic
The challenge of this portion of the assignment is to define you own animation file
structure – storing the minimum information you need to perform Forward Kinematics for
your mesh. This means having, at least, the following in the animation file – and ultimately
stored by your game engine:
• Animation
o Keyframes[N]
10
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
▪ BoneIDs[M]
▪ Quaternions[M]
▪ Time stamp
That is, an animation stores an array of keyframes. Each keyframe has an array of BoneIDs
as well as Quaternions for those bones. The keyframe as has a Time stamp – signifying how
much time must elapse from the start of the animation to reach this keyframe.
Note, the description above is one of infinite options. But to narrow it down, consider
using the above outline OR restricting animations to a consistent set of limbs –
thereby moving the BoneIDs outside of the Keyframe array. Performing animations
this way is beneficial in that multiple disjoint animations can be played
simultaneously – affecting different portions of an armature. I would argue for
imposing this constraint in the animation development pipeline for this flexibility.
In building an exporter for the animation file, with sufficient information to read into your
engine, the following Blender data can be accessed via Python:
• bpy.data.actions[]
o Array of the stored animations in actions (see description and image below)
o .groups[]
▪ List of bones keyframed in this action
▪ .name
• Name of bone – need to map to index
o .fcurves[].keyframes[]
▪ Assuming consistent animation states per limb
• All fcurve data will have a consistent length array of keyframes
▪ .co[0] is the frame at which a keyframe is stored
• bpy.context.scene.frame_set(integer)
o Function to change the current frame in animation timeline
▪ Useful to switch between known keyframes
o Note: default timeline starts at frame 1
• bpy.context.scene.render.fps
o Framerate – use to determine timestamps of keyframes
▪ E.G., keyframe begins at frame 13 and fps is 24
• Time stamp of keyframe is 0.5 seconds
• bpy.data.objects[‘NameOfArmature’].pose.bones[]
o Array of bones, bijectively aligned by index with vertex groups
o .head
▪ Object space location of bone
o .rotation_quaternion
11
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
▪ Quaternion representation of bone (where <1, <0,0,0>> is original
state
o .parent
▪ .w .x .y .z are the components of the quaternion
▪ Links to the parent bone
When making action named animations, toggle the animation timeline to “Action Editor”
as seen on the left, and create a new action with the paper stack icon seen on the right.
This action can be named, where the name can be used to look up the action in
bpy.data.actions[].
The member data of various objects listed in Blender should be sufficient to build a file
exporter for animations (consider doing this like your object exporter, one animation file at
a time rather than all of them). Of course, feel free to parse through Blender
documentation and objects in Python to assess other useful variables.
Once an exporter and parser are written, the information from the files can be stored in
Animation objects and used to playback the animation for a character model.
Animation Playback:
Parsing the animation files does not need to be too different from predominantly reading
numerical values. You can choose to organize the data in a sufficiently serialized manner,
such as:
• Number of Frames (call it N), Number of Bones Animated (call it M)
• TimeStamp
o BoneID
o Quaternion data
o Repeats for M bones
• Repeat parsing TimeStamp and Bones for N frames
An animation class, roughly outlined above, can then be used to store this data. The
affiliation with a character model might also be important, but it might be sufficient to
describe this as an animation that works with an armature of (at least) N bones. This would
allow for the animation to be shared between different meshes that might have other Type
12
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
associations – as will be important for event-driven function execution of colliding objects
described in Objective C.
Assuming your animations are composed entirely of bone rotations, the interpolation
technique necessary will be a SLERP. You can read David Eberly’s excellent paper on
SLERPing two quaternions with a parametric term (range of [0, 1]). Note, you do not need to
implement the version that avoids the transcendental functions – just heed the top of the
paper in the Introduction section.
Playback of the animation will mean needing to store a monotonically increasing time
value – incrementing by the delta time of your game engine. This time point can be used to
determine which two keyframes to interpolate between – wherein the accrued time value is
greater than some keyframe A’s timestamp, and less than keyframe B’s timestamp. Given
the minimal number of keyframes in animations – it is likely sufficient to define this as a
linear search through the keyframes of the animation (though optimizations are viable ).
When the accrued time is greater than the last keyframe’s timestamp, then you know the
animation has concluded.
At a minimum, your engine should be able to play an animation through one time –
requiring a manual restart or reset of an animation to play a new animation. This will mean
adding additional structures for storing an active animation – but it can be enforced on the
end user’s update function to test if an animation is finished playing – however, your
system must at least work safely if the accrued time is greater than the length of the
animation (e.g., picks last quaternion as bone state).
In preparing for this set of tasks, you must create a flow chart describing how your
animation controller works, how animation states are stored for a character model, and
where in the pipeline (e.g., update function and GameObject states) an end user can
create/reset/invoke an animation.
Animation Vertex Shader:
As discussed in Lecture 18, we have the capacity to provide GLSL with arrays in the vertex
attributes as well as in the uniform variables. However, the standard limitation for
animations is up to 4 weights from bones per vertex. This rationale is two-fold:
• Sufficient deformation of a mesh can occur with a limited number of bones
• We have limits on the total number of attributes available for use in a Shader
The second point is a strong consideration regarding the portability of our tools, as well as
for the performance of the Vertex Shader. GPGPU execution is optimal under a few
conditions. This optimality can be summarized as follows: avoid random access fetches
from VRAM, and keep ALU workers in lockstep.
13
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
GPUs have groups of ALU workers that rely on a shared memory. Our objective is to
preserve good SIMD/MIMD execution – attempting to limit the size of independent data to
work on to fit within the confines of a group’s local memory (think of this like a cache). The
ALUs share a clock execution – working in lock step. We want to make sure that all ALUs
are working to meet a goal and want to minimize the total number of clock executions for a
group to finish its work. So while the former statement is in part why there are maximum
bounds on attributes the latter provides an example of something you should avoid when
applying the weights of your bones.
To keep the total execution to a minimum but understanding that thread execution in a
group is in lock step, avoid testing out the extra zero weights of a vertex. That is, be sure
that if a vertex only has one or two bone weights that it still has valid additional bone
indices and that they are weighted at zero. Then, perform the linear weighting across all 4
matrices and sum the result together. By adding conditions which might test out some
matrices that would otherwise be zeroed out, you are not guaranteeing better performance
compared to having all threads perform exactly the same set of operations in lock step.
The reason being is thread divergence. If some threads are given different tasks to work on,
then other threads might effectively be stalled for a few clocks – resulting in a greater total
number of clock cycles compared to threads remaining synchronized in their executions.
In short, you should consider writing your animation Vertex Shader to utilize vertex
attributes which limit bone weights to 4, for example:
• layout (location = …) in ivec4 boneID;
• layout (location = …) in vec4 boneWeights;
Note, for simplicity you can keep bone IDs as floats if you want consistency for the type of
data your vertex shader is storing – these floats can be cast to integers to be used to look
up bone matrices from a uniform array.
Note that in blender, you can enter “Weight Paint” mode, click the “Weights” button, and
select “Limit Total.” This will limit the total groups a vertex can be a part of, note in the
included image that the operation opens a dialog at the lower left of the viewport. Opening
the dialogue will let you select 4 as the limit.
14
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
Your vertex program will otherwise look near identical to your other vertex programs, but
you will also need to include a uniform array:
• uniform mat4 bones[COUNT_OF_BONES];
Refer again to lecture slides for more information about uniform arrays – but effectively you
will need to stream your 4x4 matrices generated when forward iterating through your bones
to the vertex shader in order to utilize them. As GLSL is a C-style language that utilizes
stacks – the count of bones must be known at compile-time – i.e., you will need to define
one animation vertex shader for each variation of armature bone count.
Objective C: Spatial Graph and Broad-phase Collisions
Provided you have implemented the bounding volume for a GameObject described in
Objective A, you can begin comparing whether two objects are tentatively colliding. For
some collision scenarios, this broad collision is sufficient, but as we get into topics of
physics simulation – or for fine precision regarding action games – it is important to also
test with more precise collision algorithms (narrow-phase collisions).
15
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
Let’s start with the first part of this objective, ensuring you have a working spatial graph
structure.
Spatial Graph:
The purpose of the spatial graph is to accelerate the process of collision detection over N-
objects. The goal is to avoid N2 tests. The choice of spatial graph is up to you, but refer to
Lecture slides for some examples. If you are looking for a substantial challenge, then build
the parallel BVH structure from Nvidia (3-part read). However, building a BVH does not
need to be so strenuous – consider instead that most of your scene is static and only a few
moving objects exist at a time. In this scenario, the moving objects could be tested in the
static spatial graph (avoiding N2) comparisons and having some minimal N2 comparisons
to perform against dynamic objects.
Regardless of choice, your spatial graph structure will need to utilize the bounding
volumes of GameObjects. For AABBs, the collision test is simply true if all of the conditions
hold:
• A.min.x <= B.max.x
• A.min.y <= B.max.y
• A.min.z <= B.max.z
• A.max.x >= B.min.x
• A.max.y >= B.min.y
• A.max.z >= B.min.z
This is from Separating-axis Theorem (SAT). The theorem states that if a hyperplane exists
between two objects – never intersecting either object – then the objects cannot be
colliding. SAT can be used for narrow-phase collision detection as well, but it becomes a
bit more complicated when working from arbitrary polyhedron (it is a bit simpler with
polytopes, and trivial with AABBs).
Building out a spatial graph – say a BVH or an octree – will mean that both internal and leaf
nodes will have their own bounding volumes. It will be testing these AABBs for a collision
which will indicate which path in the graph is followed. I will advise picking a graph
structure where final GameObjects are the leaf nodes – providing a clear indication of
when traversal will need to end.
You will need to reflect on your traversal algorithms from prior courses – particularly when
an object needs to follow multiple paths from an internal node.
Event-driven Responses:
The simulation phase of you DES model will mean running some type of “simulation”
update. Note that the word “simulation” here does not imply phenomenal interactions –
though these are allowed – and instead mean that we will execute some described
16
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
interaction between objects within our simulation model. In this case, the objects are
GameObjects – instances of elements that the game engine is managing. The responses
need to be user defined, just as the user was provided the opportunity to write update
functions for classes inheriting from GameObjects – and your engine utilized
polymorphism to invoke those object’s updates.
The objective is as follows:
• End user provides pairwise response function
o I.E., when Type X collides with Type Y, run this function
• System performs broad-phase collision after all GameObject updates
o If an instance of X and an instance of Y are colliding, invoke the user function
The challenge in this is straightforward, the signature of the function needs to be compile-
time evaluate-able such that a 2D table of object Types can be used to look up appropriate
user defined functions. That is, given type strictness, we would like to build a table that can
use a Type of GameObject as an index.
Your objective is to utilize your polymorphic behavior, or Shape to GameObject
implementation to organize a Type of object as a unique index – monotonically increasing
this index value for every new Type introduced into the game engine.
When you have these type distinctions and the total number of unique Types, you will need
to define a Table-like structure – reduced to only a diagonal number of elements. That is,
which object caused a collision is somewhat ambiguous, so X colliding with Y is the same
as Y colliding with X. For the sake of memory reduction, we do not need to redundantly
store a full 2D table, just the upper or lower triangle of the table.
The table will need to store a function pointer, with a default value of nullptr – indicating no
event-driven function will be invoked. You will need to provide the API interface for the user
to pass in a their defined function to this table – the constraint being that the user defined
function must match the function signature of the function pointer in the table. You are
free to decide what the function pointer signature should look like.
An example might look like the following:
void (*funptr) (GameObject& typeA, GameObject& typeB)
The example assumes the choice of polymorphism you implemented would allow for the
downcast of the GameObjects to their respective types inside the function pointer – of
course, a more general extreme might be to make these of void* types, but this will still
force the end user to cast to the appropriate type afterward. The rabbit hole can be
quite exhaustive here – so for the time being, choose the simplest option for your approach
to utilizing polymorphism. If for instance your bounding volumes are inside GameObject
17
Notre Dame CSE 40739 Adv Game Dev, Daniel Rehberg, Assignment 3 ver. 1.1
instances, and the GameObject instances are base classes to derived types, then the
above suggestion would be viable.
Alternatively, if you are choosing to mimic an automated system of message
passing/signaling through a composition approach to GameObject features, then it is
perfectly reasonable to not have the function pointer table invoke a user defined function.
Instead, your table could be of pairwise collision interactions with the appropriate signal
responses – e.g., flipping a Boolean state by reference. Note, this message passing
approach is succinct when delaying the event-response by one frame – waiting for the
message to be propagated and then an appropriate response being handled during the
DES update state again (though it is viable to still invoke a user defined function in this
      mn.     Objective C is rather flexible based on your current design choices and will require some
amount of planning to organize the flow of information. For this reason, it will be required
to mockup a flowchart to plan your design approach to implementing/utilizing a Spatial
Graph, Updating Bounding Volumes, Testing Collisions of those Bounds, and Executing
Functions from this event table.