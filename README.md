# Aquarius Engine

Aquarius Engine is a desktop 3D scene editor built with C++20 and OpenGL. Use it
to create projects, import models, edit materials, add lights, and save scenes.

## Getting started

Launch the engine and create or open a project. Drag a model from the content
browser into the scene to add it. Press **Ctrl+S** or click **Save Project** to
save your changes.

## Camera and viewport navigation

Hold the **right mouse button** over the scene to look around and use the camera
movement keys:

| Control | Action |
| --- | --- |
| Mouse movement | Look around |
| **W / S** | Move forward/back in Perspective; zoom in/out in Orthographic |
| **A / D** | Move left/right |
| **Q / E** | Move up/down |
| **Left Shift** | Move faster |
| **Middle mouse + drag** | Pan parallel to the view plane |

Use the camera button in the viewport toolbar to switch between **Perspective**
and **Orthographic** projection. The camera pose is preserved when switching.

## Basic editor controls

- Click a model or light to select it; click empty scene space to clear the
  selection.
- With an object selected and the right mouse button released, use **Q** for
  selection mode, **W** to move, **E** to scale, or **R** to rotate.
- **Alt+Space** toggles the content browser, **F11** maximizes or restores the
  window, and **Ctrl+S** saves the project.

See [docs/editor-scene.md](docs/editor-scene.md) for more detail about editing
and saving scenes.
