# Editor scene persistence

Each project stores the editor scene in `editor.aqenscene` at the project root.
The file records the camera position and orientation, plus each scene object's
editable name, transform, and model, landscape, or light data. Material assets
retain their texture maps and world texture-density settings in the referenced
`.aqenmat` file. Models, lights, and camera are restored when the project is
opened. Scene format version 7 adds tagged light records; versions 1 through 6
remain loadable.

Projects created with **Create project** start with one real Directional Light
and a flat procedural Ground receiver, so the initial scene has geometry to
show the illumination. The starting camera frames the ground and the light's
billboard icon. Saving and reopening that scene restores both objects without
duplication. Opening an older project or scene with no lights does not add one
automatically.

Save changes with the **Save Project** button in the content browser or press
**Ctrl+S**. The editor also saves automatically when it closes.
If one or more actors cannot be restored, successfully loaded actors remain
editable and the **Unresolved Scene Actors** panel offers **Retry**, **Relink**
for model assets, or confirmed **Remove**. Unresolved records are preserved
alongside loaded actors on save and close; relinking changes a record only
after the replacement asset loads successfully, and the original material
asset reference is retained if its descriptor cannot be read. The recovery
panel continues to capture its own mouse input when the content browser is
hidden. A scene that cannot be parsed as a whole remains protected from
saving.

Press **F11** in the launcher or editor to maximize the window. Press **F11**
again to restore it. If the launcher is maximized when a project is opened,
the editor starts maximized too.

Click a model or light icon in the scene to select it. Selected models retain
their deep-blue outline; selected lights are tinted and show the transform
gizmo. Click empty scene space to clear the selection. UI clicks and
right- or middle-mouse camera navigation do not change the selection. When a scene object
is selected, the **Object Details** panel appears on the right. Models expose
their name, location, rotation, scale, and material assignment. Light objects
expose their position and actual lighting properties; Directional, Spot, and
Rect Lights also have editable rotation. Light icons have a fixed display size
and are not scaled by the light transform. Rotation values are shown in degrees
from 0 to 359; the scene stores applicable rotations in radians. Press Enter or
leave a field to apply its value. Save the project to persist these edits. For
models, the material assignment uses a PBR preview
that combines its diffuse, normal, roughness, and metallic maps. Drag a
material asset onto the preview square to assign it; use **Browse...** or
**Clear** beneath the square to change the assignment.

Use the top-level **Lights** menu, then hover over **Add Light...** to choose
Directional, Spot, Point, Sky, or Rect Light. New lights are selected
immediately and use the normal scene selection, transform gizmo, and Delete
confirmation. The editor uses a Y-up coordinate system with the level grid on
the XZ plane, so new lights spawn at `(0, 10, 0)` to place them ten units above
the world origin. Light icons are the supplied PNG assets in
`source/graphics/light_icons`; they billboard toward the active editor camera
and are editor-only sprites, not emissive or shadow-casting scene geometry.
Hover over **Intensity**, **Range**, cone-angle, or surface-dimension values in
**Object Details** for explanations of their units and visual effect.

When selected, a Point Light shows a wire sphere with radius equal to its
Range. A Spot Light shows its outer beam cone and the inner cone boundary; both
follow the light direction, Range, and cone angles. A Rect Light shows the
rotated outline of its emitting surface. Directional and Sky Lights show a
short direction arrow instead of a finite-range shape. These wireframes are
editor overlays only: they are not selectable objects, do not affect
illumination or shadows, and are not saved into the scene.

Point and Spot Lights use inverse-square attenuation with a smooth finite-range
falloff; Spot Lights also use editable inner and outer cone angles. A scene can
contain up to 32 lights; attempts to add more are rejected with a status
message. Directional Lights are unbounded and use their rotation to set their
direction. Sky Lights receive light from fixed world +Y (rays travel down -Y),
perpendicular to the XZ level grid, and do not expose rotation. Rect Lights
emit from their rotated front side and
use four surface samples as an area-light approximation; they do not cast
shadows. The renderer reuses one 4096×4096 shadow map for one active source at
a time: the first enabled Spot Light takes priority, followed by the first
Directional Light and then the first Sky Light. Spot shadows use a perspective
projection fitted to the cone angle and range; Directional and Sky shadows use
a stabilized orthographic projection. Other lights continue contributing
without being suppressed by the selected shadow map. Point Lights require
omnidirectional cubemap shadows, and Rect Lights require per-sample area-light
shadows; neither is currently supported. In **Lit** view, only actual scene
lights contribute to geometry:
surfaces receiving no direct light render black, and no fixed fallback light
or ambient fill is added. The editor viewport background and the ground-grid
fill are black in Lit and Unlit views; the grid lines remain as editor guides.
**Unlit** view intentionally displays material base colors without lighting,
and **Wireframe** view continues to show its blue geometry edges. Existing scenes
without light records remain loadable and now render non-emissive geometry
black in Lit view. The material system has no emissive channel. Ambient
occlusion remains available in material assets and the dedicated material
preview, but does not add scene illumination without an ambient-light system.
With a model selected and the right mouse button released, switch transform
modes using **Q** (selection only), **W** (move), **E** (scale), and **R**
(rotate). Move mode shows colored X, Y, and Z arrows; drag one to move along
that world axis. Scale mode replaces arrowheads with cubes; drag one to scale
on its axis. Rotate mode shows three colored axis rings; drag a ring to rotate
around that world axis. Light objects can be moved and their applicable
rotations can be changed, but their editor icons cannot be scaled. Release the
left mouse button to finish a transform. Press **Delete** while a scene object
is selected to open a confirmation dialog. The object is removed from the scene
only after confirming.
The upper-left toolbar's **View** button opens a dropdown with **Lit** (the
default PBR materials and shadows), **Unlit** (diffuse colors/textures without
lighting or shadows), and **Wireframe** (bright blue model edges without
materials, shadows, the ground grid, or transform gizmos). The active mode is
marked in the dropdown; clicking outside or pressing Escape closes it. The
adjacent camera button shows **Perspective** or **Orthographic** for the
current projection. Switching projection preserves the camera pose. In
both modes, hold the middle mouse button and drag inside the 3D viewport to pan
parallel to the view plane; the pan follows the camera orientation. In
Orthographic mode, **W/S** zoom in and out while the camera is controlled with
the right mouse button; horizontal and vertical movement continues to pan.
View and projection are editor display preferences and are not saved with the
scene. In Lit and Unlit modes, the ground grid fades with distance and is
rendered translucently.
Drag a model asset from the content browser over the scene to preview it
at the cursor. Release the left mouse button to add and select it at that
position.
Use the **Work log** button in the upper-right corner to view project-save
and scene-add status messages. The log remains available when the content
browser is hidden. Entries are timestamped using the computer's local time,
saved in `editor-worklog.log` in the project folder, and displayed in a
scrollable history.

The content browser displays project assets as icon cards. Supported images
show image thumbnails, model files show geometry previews, and `.aqenmat`
files appear as material assets. Select a card to see its larger preview; drag
a model card into the scene to preview and add it. Use **Upload Model** to import an FBX, OBJ, glTF, or GLB model into the project.
`.log` and `.aqenproj` files are hidden from the browser; the work log remains
available from the **Work log** button in the upper-right corner.
Each import is placed in its own folder as a binary glTF (`.glb`) asset. The
original scene hierarchy, mesh/material assignments, UVs, and normals are
retained where Assimp and the renderer support them. Embedded textures stay
embedded; external textures are copied into the folder's `textures`
subdirectory and their GLB references are updated. GLB files are therefore
portable as part of their imported folder, but may still depend on those
packaged external texture files. The imported folder can be moved with the
project without depending on the original source model.

Use **Create Landscape** in the content browser to open a separate plane
creator window. Set a name, width, depth, and subdivisions, then choose
**Add Plane to Scene**. The creator closes and the editor adds an in-memory
subdivided plane directly to the scene; no asset or folder is created. The
plane's procedural dimensions and subdivisions are stored in the scene file,
so it is restored when the project reopens. Its subdivisions provide geometry
for future terrain editing and height displacement.

In the content browser, right-click empty space in the asset list to create a
folder or refresh the listing. Right-click a folder card to rename or delete
that folder. Folder deletion requires confirmation and removes the folder's
contents recursively.

The renderer supports static meshes, UVs, normals, base-color, normal,
roughness, metallic, and ambient-occlusion texture inputs, plus
base-color/roughness/metallic factors. The optional height map is applied as
static vertex displacement when a model loads; changing the height map or its
strength reloads the model. Height strength ranges from 0 to 10 and is centered
around mid-gray. The **Two-sided** material option controls back-face culling
for model and shadow rendering; it defaults on to preserve existing scenes.
Lit scene geometry receives only contributions from scene lights; the
asset-browser mesh thumbnails and material sphere retain their separate
editor-preview shading.
The renderer filters shadow comparisons with a 5×5 percentage-closer kernel
and uses a texel- and slope-scaled depth bias. It renders and reuses one depth
map per frame; Point and Rect Lights remain unshadowed because their correct
shadow representations are not part of the current renderer.
Skeletal animation and skinning are not implemented; animated source data is
not currently playable by the engine and should be treated as unsupported.
For a selected model, the **Object Details** panel provides one **Material**
field. Drag a `.aqenmat` asset onto the field, or choose **Browse...** to search
the project material library. **Clear** removes the assigned material. The
assignment is stored with the scene; save with **Ctrl+S** or **Save Project**.
Use **Create Material** in the content browser to open the material editor in
its own desktop window. It remains attached to the current project while it is
open and can be minimized, maximized, or closed independently; the main editor
window remains interactive when the detached window is open. In that window,
choose diffuse, normal, roughness, metallic, ambient-occlusion, and optional
height maps, name the material, set height strength and sidedness, then save a
reusable `.aqenmat` asset under `assets/Materials`. Map inputs reference
selected images directly without copying them; hover a texture thumbnail to
see its full path. The shaded sphere preview combines the diffuse, normal,
roughness, metallic, and ambient-occlusion maps; drag the sphere with the left
mouse button to orbit it. Set its X/Y/Z world texture density to
control how frequently each
map repeats along the corresponding world axis. Double-click a material card
to edit it; saving refreshes actors currently using that asset. Drag a material
asset from the browser onto a model in the scene to apply it immediately; save
the project to retain that assignment.
If a referenced texture file is missing, the actor still loads with a default
map for that input and the missing path is reported; the saved material
reference is retained so the asset can be repaired.

In the launcher, **Create project** opens a system folder chooser for the
parent location, then asks for the project name. **Load project** opens the
project browser, which lists only folders and `.aqenproj` files; it has no
all-files or alternate file-type option. The launcher also lists
recently opened and created projects for quick access. Recent project paths
are stored in the user's Aquarius Engine configuration folder. If a recent
project file has been moved or removed, the launcher offers to remove that
reference from the list. On startup, the Aquarius Engine splash image fades in
for one second, remains visible for five seconds, then fades out for one
second before the launcher or requested project opens. The splash image is
displayed at half its original dimensions. The launcher panel and its content
stay centered as the window is resized or maximized.

Paths that are inside the project are stored relative to the project root, so
moving the project folder does not break those references. Paths to files
outside the project are stored as absolute paths and may stop working if those
files are moved.

The `.aqenproj` `assets` entry must resolve to an existing subdirectory of the
project. The assets directory itself cannot be a symbolic link, and symlinked
path components are accepted only when their resolved target remains inside
the project. The default `assets=assets` path is unchanged; nested in-project
asset directories are also supported.

The scene file uses a versioned text representation (`AQENSCENE 7`); versions
1 through 6 remain supported. Unknown or invalid tagged light records prevent
the scene from being saved over the original file. Version 1 actor names are
initialized from the
source model filenames. Material descriptors use `AQENMAT 5` and retain
backward compatibility with versions 1 through 4. Scene files are written to
a temporary file and then replace the previous save, avoiding partially
written scene files if a save is interrupted.

## Building and testing

Configure and build with CMake and Ninja, then run the model-import tests:

```sh
cmake -S . -B build-glb-clean -G Ninja
cmake --build build-glb-clean --parallel
ctest --test-dir build-glb-clean --output-on-failure
```

`ProjectTests` validates default, nested, missing, and symlinked assets paths.
`ModelAssetsTests` exercises conversion and packaged-file validation without
creating an OpenGL context, and covers unresolved scene-record retention and
serialization. `ModelLoaderTests` also reopens exported GLBs through the
engine's `Model` loader using a hidden OpenGL context, including failed and
successful actor restoration/relinking; CTest marks that test skipped when
the host cannot create one. The
`compile_project.sh` helper performs the same configure, build, and test steps
before launching the editor; it does not delete or overwrite another build
directory.
