# Strand

**Native spline assets, editing, and interchange for Unreal Engine 5.8.3**

Strand lets you create, import, edit, reuse, and export spline geometry as Content Browser assets while retaining Unreal Engine's native `USplineComponent` behavior. A Strand Spline asset (`USplineDataAsset`) stores spline-local control points, independent arrive/leave tangents, point rotations, point scales, native interpolation types, and the closed-loop flag. It does not store an Actor or Component transform and it does not produce rendered meshes.

Strand has two complementary editing workflows:

- **Strand Spline Editor:** Double-click a Strand Spline asset to edit its saved curve in a dedicated 2D/3D preview. These changes modify the asset, not existing level actors.
- **Native Level Editor spline editing:** Drag a Strand asset into the level or associate it with an existing native spline component. Continue using Unreal's native actor, spline, and viewport tools. Explicitly **Apply Asset** or **Update Asset** to synchronize the two copies.

**Package:** v0.6.11 source candidate. **Validation:** UE 5.8.3 compilation and live native-widget input QA for v0.6.11 are pending. The earlier v0.6.8 geometry tests (Bézier handle modes, group editing, Pen) passed; the v0.6.9 round-trip assertion was corrected to compare the component to its own snapshot. The v0.6.9 C4456 build error in `StrandTests.cpp` was separately corrected. Use [TEST_PLAN_0.6.11.md](TEST_PLAN_0.6.11.md) for the current acceptance checks and [FEATURE_ROADMAP.md](FEATURE_ROADMAP.md) for remaining milestones.

## Install

1. Close Unreal Editor and replace the **entire** `<Project>/Plugins/Strand/` folder with the `Strand/` folder from this ZIP. Do not merge the ZIP into an old source tree. UnrealBuildTool will compile leftover `.cpp` files from older versions.
2. Enable Unreal's **PCG** and **Alembic Importer** plugins. They are declared as enabled plugin dependencies by `Strand.uplugin`.
3. Regenerate project files if required, compile the project's Editor target for UE 5.8.3, and reopen the project.
4. Confirm that **Strand Spline** appears as a Content Browser asset type, that its asset editor opens, and that **Tools > Strand: Import SVG Curves...** and **Tools > Strand: Import Alembic Curves...** are present.

This archive contains C++ source, test fixtures, documentation, and editor artwork. It does **not** contain precompiled binaries or require Unreal Engine source modifications. `RemoveLegacyTracker.bat` and `RemoveRetiredStrandSources.bat` are provided for cleaning obsolete source files in installations that were previously overlaid rather than replaced. They are not required for a clean folder replacement.

## Quick start

### Create and edit a Strand spline from scratch

1. Create a **Strand Spline** asset in the Content Browser, or open an existing Strand Spline and click **New Spline**. The factory creates an initial open curve with two points at (0, 0, 0) and (100, 0, 0) centimeters.
2. Double-click the asset. Its dedicated editor has a native spline preview on the left and the selected-point inspector plus asset properties on the right.
3. Choose **Pen** in the viewport toolbar. Click to add anchors; click-drag to create symmetric incoming/outgoing Bezier handles. The highlighted **Pen** mode is active until you choose **Select** or press **Esc**. To start with no points, choose **Spline Tools > Clear All Points**, confirm, and draw a new path. Clear is a single Undo step.
4. In Pen mode, click an existing segment to add an anchor at the clicked location using a shape-preserving cubic split. Click an endpoint to select the side to extend, then click empty space to continue from that end. With three or more points in an open path, clicking the opposite endpoint joins the ends and exits Pen mode. The other end can be selected and extended similarly. **Snap: Off / 1 / 10 / 100 cm** controls Pen placement and handles.
5. Choose **Select**. Click an anchor to select it, **Shift-click** to extend an index range, or **Ctrl-click** to toggle individual anchors. Use **Box Select** to draw a rectangle around projected anchors; **Shift+drag** adds and **Ctrl+drag** toggles those in the box. A simple click in Box Select behaves like Select. **Esc** exits Box Select, and Ctrl+A selects all. The Selection menu and inspector All/None buttons offer the same actions.
6. In **Select**, choose **Move**, **Rotate**, or **Scale** from the toolbar, or press **Spacebar** inside the Strand viewport to cycle Move → Rotate → Scale → Move. The preview now requests **Unreal's native editor transform widget**, anchored at the selected point or center of selected points. It uses native widget hit-testing and drag deltas but edits the Strand asset via transactions; it does not select or move actors in the Level Editor. **W/E/R are not Strand shortcuts** so right-mouse **WASD flight navigation** remains with Unreal. Move translates point positions; Rotate/Scale transform a group about its centroid or, for one anchor, rotate its tangents/orientation and scale its tangent lengths in place. Scale is **uniform** even when an engine axis is dragged. For exact values use the local XYZ inspector or the relative rotation/scale fields. **F** frames the curve, **P** activates Pen, **B** activates Box Select, **V** returns to Select and **Delete** removes selected anchors when at least two remain. Keyboard commands require viewport focus; Space does not cycle during Pen or Box Select.
7. Select one or more anchors and choose a **Bézier Handles** mode in the right inspector: **Free** (separate handles), **Aligned** (shared direction, independent lengths), **Mirrored** (shared direction and length), **Auto (Unreal)** (native smoothing), or **Corner** (zero-length handles). On a single anchor, click the **Incoming Handle** or **Outgoing Handle** or pick its viewport marker to edit numerically or via the Strand axis handles. Linked constraints persist across subsequent drags and asset saves. The old one-shot Align/Mirror operations are still available as distinct commands; they switch that point back to Free.
8. Use **Insert After** for a single selected point or **Delete Selected** for a set that leaves at least two points. Whole-spline tools remain separate from selection tools. Save via the asset editor's standard Unreal Save command.

Point and tangent edits operate on the **asset's local geometry**, are backed by editor transactions, and refresh the transient preview. They do not automatically change placed level instances or modify the original SVG/Alembic source file.

### Place the asset in a level

1. Drag a Strand Spline asset from the Content Browser into a Level Editor viewport.
2. Strand's registered actor factory creates an `AStrandSplineActor` containing one ordinary native `USplineComponent` as its root. The drop location is controlled by Unreal's standard placement system.
3. Select the actor's **Spline Component** and use Unreal's standard spline editing tools. Strand's small per-component link is saved as `UStrandSplineUserData` on that component, not as an extra tracker component.
4. Select the **actor** and expand **Strand Links** in the actor Details panel. There you can assign a Strand asset for each native spline component, **Apply Asset**, **Update Asset**, **Save as New**, or **Compare**. Select the actual **Spline Component** to get **Unreal's original Selected Points widget and spline editing controls**, unchanged by Strand. Strand no longer replaces native spline component Details layouts.

For an empty native Strand actor, use **Quickly Add > Strand Spline Actor** or **Place Actors > Basic**. For multiple separate splines on one actor, use **Strand Multi Spline Actor**, the Content Browser multi-asset creation command, or the command to add native spline components to selected actors.

### Blueprint spline components and external-actor level saves

**Native UI preservation:** Strand no longer overrides `USplineComponent` or `USplineMeshComponent` class Details. For level actors, link editing is available in actor Details > Strand Links; built-in component Selected Points controls remain owned by Unreal. This is a deliberate compatibility change, not a removal of asset-link data or native functionality. Editing link metadata directly on the **Blueprint class component template** is not yet exposed through this additive actor-level surface; use Strand's native component assignment commands or place an instance and assign under Strand Links. A Blueprint-template link authored in earlier versions is still read at runtime and during repairs. A native-preserving Blueprint component-template link UI is a future integration task.

Strand's component link is stored as native Unreal `UAssetUserData`. Blueprint-instanced spline and spline-mesh components can otherwise inherit a link to their Blueprint component template, or a locally owned metadata copy with an archetype reference to that private template. Unreal's **One File Per Actor (OFPA)** save cannot serialize either kind of private cross-package reference.

Strand repairs loaded placed actors before Unreal saves external actors and also checks individual actor/component save paths. Repair replaces **only** an unsafe Strand user-data object with one owned by the placed component and based on Strand's class default object. It preserves the linked asset, placement settings and spline geometry; unrelated user data is untouched. Blueprint class templates are not modified by a placed-actor repair.

For a level that already fails to save, open the affected level and run the read-only `Strand.AuditUserData` command first. This logs the number of loaded actors, native spline/spline-mesh components, metadata entries, assigned assets, and unsafe owner/archetype references. Run `Strand.RepairUserData` to repair unsafe entries and report a before/after summary, then save the level again. The command only scans actors loaded in the active editor world. A count of zero means no currently loaded spline/spline-mesh metadata matched the unsafe-ownership check, not necessarily that a different save issue is absent.

**Clearing the Strand Spline field** must remove Strand's component metadata even when that field was already empty. Clearing the Blueprint class default alone may not clear overridden or previously instanced metadata on already-placed actors. The repair command is safe to run with the asset still assigned; you do not need to remove your Blueprint actor or clear spline geometry.

## Dedicated Strand Spline Editor

### Viewport and inspector

| Control | Behavior |
| --- | --- |
| **Select / Box Select / Pen** | Select supports single-click, Shift-click contiguous ranges, Ctrl-click toggles, Ctrl+A all, and Esc clear. Box Select drag selects projected anchors in a screen rectangle, Shift adds, Ctrl toggles, Esc returns to Select; click without drag also selects a single point. Pen adds, extends, closes and inserts anchors; Esc exits Pen. |
| **Move / Rotate / Scale** | Unreal's standard transform widget, located at the selected anchor/handle or the selected group's centroid. **Spacebar** cycles modes while Select is active. **W/E/R no longer switch Strand modes**, preserving RMB+WASD camera flight. With one anchor Rotate changes orientation/tangent directions and Scale adjusts tangent lengths in place; with multiple anchors Rotate/Scale use the group center. Scale always applies a uniform multiplier to Strand geometry. |
| **Pen Snap** | Optional placement/handle snapping to 1, 10, or 100 cm, or Off. Available while Pen is active. |
| **Camera** | Perspective, Top (XY), Front (XZ), Side (YZ). Controls the view, not saved geometry. |
| **Edit: 3D** | No movement-axis restriction. |
| **Edit: XY / XZ / YZ** | Constrains translation and numeric edits to the chosen two axes. Switching modes does **not** flatten existing positions or tangents; a locked numeric axis is disabled. Choosing a 2D mode also changes to the matching orthographic view. |
| **Frame All / Frame Primary** | Focuses the camera on the full curve or primary selected anchor. |
| **Points / Grid / Tangents** | Toggles overlays. Tangents default on in Select mode for one selected point and hide automatically during Pen drawing. |
| **Selection / Point Editor** | Navigate anchors, select All/None, edit point position or selection center, choose tangent handles and interpolation. With multiple points, use relative Rotate Z degrees and Scale multiplier fields. |
| **Toolbar groups** | Asset operations (New, Export, Reimport, Actors, Spline Tools), editing modes (Select, Pen, Box Select), native widget modes (Move, Rotate, Scale), and view settings are separate strips. Strand-owned monochrome SVG icons avoid missing Slate brush fallbacks. Spacebar cycles transform modes. Frequent Insert/Delete and Bézier mode controls remain in the point inspector. |
| **Bézier Handles** | Persistent Free, Aligned, Mirrored, Auto and Corner per-point authoring behavior, editable on one or multiple selected anchors. Selected mode or Mixed is displayed in the inspector. Auto uses native Unreal spline smoothing; dragging an Auto or Corner handle converts it to Free. |
| **Selected Tangent** | Incoming/Outgoing Handle buttons and clickable handles select the evaluated tangent for numeric or widget edits on one anchor. Linked handle modes apply to both widget and numeric changes. |
| **Asset Properties** | The native property view remains intact but starts collapsed under **Asset Properties / Source / Reimport**, separating authoring from infrequently changed import settings. |

**Transform widget:** The asset viewport now enables and requests Unreal's standard editor widget. `GetWidgetLocation`, `GetWidgetMode`, and `InputWidgetDelta` route the engine widget into Strand's selection and edit transactions. Strand no longer draws its own imitation XYZ arrows, colored rings, or white scale square. The preview has its own selection and does not take over Level Editor actor selection, mode managers, or native spline component gizmos. Rotation and scaling of auto-smoothed anchors may require changing the point to custom tangents before a visible handle shape change, because Unreal evaluates Auto tangents dynamically.

**Marker colors:** Start is green, end of an open spline is orange, and selected anchors are yellow with a larger primary marker. Preview point markers and sampled segments are capped for very dense curves; the primary point remains visible. Pen segment hit targets are disabled above 4,096 points to prevent large hit-proxy allocations; endpoint extension remains available.

**Point types:** Curve (Auto), Curve (Clamped), Curve (Custom Tangents), Linear, and Constant are offered as native Unreal interpolation types. **Constant** interpolation is not supported by Strand's SVG or Alembic exporters. Native point rotations and scales remain editable in the asset properties even though they are not part of the viewport translation widget.

### Spline Tools

| Tool | Behavior and important limitation |
| --- | --- |
| **Clear All Points** | Confirm and remove all points from the asset, allowing Pen to start from an empty curve. Undoable; assets with fewer than two points should not be placed or exported as usable geometry. |
| **Activate Spline Pen** | Enter viewport Pen mode directly from the Spline Tools menu. |
| **Extend Start** | Prepends a point to an open spline using the first segment's direction; does nothing for closed splines. |
| **Extend End** | Selects the final anchor and invokes Insert to append an open spline's endpoint. Closed splines are rejected (no geometry change). |
| **Subdivide All Segments** | Inserts a midpoint on each segment. Cubic segments use de Casteljau subdivision of the evaluated native spline. Linear and constant segments are handled separately. Maximum 8,192 input anchors; one Undo transaction. |
| **Reverse Direction** | Reverses anchor order and swaps/negates incoming and outgoing tangents. Also adjusts segment interpolation assignment. |
| **Set Selected as First (Closed)** | Cyclically rotates point order so the selected anchor is first, only on a closed spline. |
| **Align Selected X / Y / Z** | Aligns selected anchors to their mean coordinate on the chosen axis; requires two or more selected and an unlocked edit axis. |
| **Distribute Selected X / Y / Z** | Evenly distributes three or more selected anchors in ascending axis-coordinate order between their selected extremes. Does not distribute along curve arc length. |
| **Align Tangents Once / Mirror Tangents Once** | Legacy one-shot co-linear tangent commands. These modify the selected anchor and reset its authoring mode to Free, unlike persistent Aligned/Mirrored modes in the inspector. |
| **Snap Selected Points** | Rounds unlocked coordinates of all selected anchors to a 1, 10, or 100 cm grid. Explicit command, not live viewport snapping. |
| **Toggle Closed Loop** | Changes the native closed-loop flag. |
| **Linearize All** | Converts every point to Linear and zeros tangent vectors. Changes geometry. |
| **Auto-Smooth All** | Converts every point to native Curve (Auto). Changes geometry. |
| **Mirror X / Y / Z** | Reflects every point and both tangents around the selected axis of the control-point bounding-box center. |
| **Flatten to XY / XZ / YZ** | With confirmation, zeros the discarded axis for every position and both tangent vectors. Destructive to geometry but undoable. This is distinct from choosing a 2D edit plane. |

**Insert After** requires one selected anchor and uses an exact evaluated cubic split on a curved segment. **Delete Selected** removes the whole selection in one transaction only if at least two points remain; deleting anchors can change neighboring curve geometry. Numeric selection-center edits and group gizmo translation preserve unselected anchors and each selected point's tangent vectors. Pen-mode clicking on a spline segment inserts an anchor using parameter-refined de Casteljau splitting. All spline Pen interactions, their mouse capture, and Undo/Redo still require live editor validation.

### Editor toolbar and asset lifecycle

- **New Spline:** Create another Strand asset in the same Content Browser folder and open it.
- **Export:** Write Alembic or SVG files without modifying the stored asset.
- **Reimport:** For assets with an SVG or Alembic source link, confirm replacement of saved asset geometry from that source. Manual edits to the imported asset can be overwritten.
- **Actors:** Create Actor, Select Users, and Inspect Usage for currently loaded level actors.
- **Spline Tools:** The operations documented above.

Persistent handle modes are Strand-only authoring data saved per point in the Strand asset, not additional runtime component state. Native spline components continue to receive standard Unreal tangents and point types. Switching to Aligned or Mirrored may deliberately change the existing handle directions/lengths; switching to Corner collapses the handles; Auto re-evaluates native smoothing. Imported SVG/Alembic data retains its original geometry and starts with Free or effective Auto authoring semantics.

Spline Tools has three concise groups: **Selected Points**, **Create and Path**, and **Refine and Shape**. This leaves primary Select/Pen/Box Select and transformation controls visible instead of crowding a single long dropdown. The Bézier mode property is read-only in the raw asset point array; change it using the selection-aware inspector to keep geometry and stored behavior synchronized.

The editor uses a transient `USplineComponent` for preview. It does not spawn a persistent actor in the current level. An imported asset's **Reimport** does not push changes to existing native spline components. Save the edited Strand asset and apply it to level instances explicitly.

## Asset and native component synchronization

### Native `USplineComponent` Details

A **Strand** category is registered for native spline components, including currently loaded subclasses, while preserving the built-in spline component itself.

| Control | Result |
| --- | --- |
| **Strand Spline Asset** | Assign, browse, or clear the linked Strand asset. Linking alone need not overwrite local geometry. |
| **Sync Status** | Reports whether stored point data matches the linked component after its placement policy. A mismatch does not identify which copy was edited most recently. |
| **Apply Asset** | Restore asset points to the component after confirmation. |
| **Update Asset** | Save native component geometry back to the linked shared Strand asset after confirmation. Does not update other users automatically. |
| **Save as New** | Capture current component data into a new Strand asset and link it. |
| **Compare** | Inspect differences without editing points. |
| **Coordinate Mode** | **Absolute:** keep the saved spline-local positions. **Relative:** offset only spline-local point positions by a chosen anchor. |
| **Anchor options** | In Relative mode, use the average of points or an indexed control point as the component-local origin. |

Assets imported or newly authored normally use **Relative + Point Center** placement. Assets captured from an existing native component use **Absolute** placement to retain its original local coordinate frame. The point positions and tangent vectors remain in local units; the actor/component transform is not overwritten. Updating a linked Relative instance compensates for that placement offset.

Selection and right-click operations support applying an asset to an existing component, adding a native spline to a compatible actor, capturing a spline as an asset, and targeting individually named components when an actor has multiple splines. Usage queries and actor selection concern **loaded** actors; unloaded World Partition cells are not guaranteed to be scanned. Construction Scripts that regenerate spline data can take precedence over applied changes.

### Native spline mesh support

`USplineMeshComponent` retains its original Unreal Details customization. Strand adds a separate link picker and Apply command under the owning actor's **Strand Links** category. A spline mesh represents a **single segment with two endpoints** and is not a general multi-anchor native spline. Strand can capture/apply its endpoint positions and tangents using an **open two-point** Strand asset. Multi-point or closed assets are rejected. Mesh choice, materials, collision, and mesh-specific roll/scale stay with Unreal's spline mesh. Capturing a spline mesh into an existing Strand asset can reset fields absent from the mesh's endpoint representation; confirmation warns about this.

### Placement and multi-spline actors

- **Strand Spline Actor:** ordinary actor with one native root spline and a small editor-only billboard. The billboard uses the supplied Strand artwork and does not render in game.
- **Strand Multi Spline Actor:** holds multiple native spline components; supports authoring several independent curves without joining them into one spline.
- **Multi-asset drag:** after a completed drop of multiple distinct Strand assets, Strand can offer to combine the newly placed splines into a multi-spline actor. A single asset drop should not trigger that choice.
- **Asset usage:** Select Actors Using This Asset and Inspect Strand Usage scan loaded linked spline components. They never silently push shared-asset edits to other instances.

## Import, reimport, and export

### Import SVG

Use **Tools > Strand: Import SVG Curves...**, or import `.svg` files through the Content Browser. The source code handles multiple paths, cubic and quadratic Bézier commands, lines, polygons, rectangles (including rounded corners), ellipses/circles, arcs, supported internal `<use>` references, transforms, and open/closed paths. Independent incoming/outgoing tangents preserve hard joins. Each source curve is represented by its own Strand asset.

SVG import stores a 2D shape in Unreal's XY plane. The default **Mirror SVG X on Import** convention maps SVG `(x, y)` to Unreal `(-x, -y, 0)`, after SVG transforms, to match the intended top-down viewing convention. Disabling that setting and reimporting uses `(x, -y, 0)`. **Project Settings > Plugins > Strand** provides an SVG canvas origin setting (Top Left, Top Right, Bottom Left, Bottom Right, Center). Imported assets do not update their placed actor instances automatically.

### Import Alembic

Use **Tools > Strand: Import Alembic Curves...** for explicit spline import. The code reads supported Alembic `ICurves` representations, including linear and selected smooth, Bézier, and NURBS families. Source curve identity is recorded for per-curve reimport. Blender-authored Bézier Alembic curves have been tested in the earlier development workflow; dense linear sampling from other exporters can still produce unnecessarily large point arrays.

Content Browser import of `.abc` can offer **Strand Spline Importer**, **Unreal Alembic Importer**, or Cancel, allowing mesh/Geometry Cache workflows to remain with Unreal's importer. Source units, Y-up-to-Z-up conversion, and sample time are exposed as Alembic import options where applicable. Alembic has no universal length unit; check the source application's convention. Strand does not register an FBX spline importer.

### Export interchange files

From the Strand asset editor's **Export** menu or the Strand asset's Content Browser context menu:

| Format | Export behavior |
| --- | --- |
| **Alembic (.abc)** | Exports a static 3D cubic Bézier `ICurves` shape, including exact representation of native linear segments as straight cubics. Intended to round-trip as geometry through Strand's Alembic importer. |
| **SVG (.svg)** | Exports a 2D path, using straight lines and cubic commands with closure handling. For a curve not planar in XY, the exporter asks for XY, XZ, or YZ projection. Dropping depth for SVG is not reversible. |

The exporters reject **Constant** interpolation instead of silently changing its discontinuity. SVG/Alembic do **not** preserve all native Strand metadata (such as point rotations, scales, interpolation-mode labels, or unrelated actor information). They exchange curve geometry, not a complete `.uasset`. Export never changes the stored source curve or a linked source file.

Reimport regenerates an imported Strand asset from its external file and stored import settings. Confirm before reimporting an asset that has manual edits. Existing level instances still require explicit **Apply Asset**.

## Runtime and ecosystem integration

| Module / integration | Included API or behavior |
| --- | --- |
| **Strand (runtime)** | `USplineDataAsset`, point representation and placement options; `UStrandLibrary::ApplySplineDataAsset` and asset apply/capture helpers for native `USplineComponent`. |
| **StrandPCG (runtime)** | `UStrandPCGLibrary::ApplySplineDataAssetToPcg` returns transient native `UPCGSplineData` initialized from the asset's points and a caller-provided transform. This is not a custom PCG graph node. |
| **StrandEditor (editor only)** | Dedicated asset editor, additive actor Details link tools, placement commands, asset import/reimport, SVG/Alembic export, spline mesh and Landscape editor adapters. It never replaces native spline component class Details layouts or visualizers. |
| **Landscape adapter** | Has conversion/apply operations for compatible connected paths. Branched or topology-incompatible cases are not handled as general graph reconstruction. Landscape terrain deformation refresh can require additional native Landscape work. |

Strand does not replace native spline Details customizations, viewport visualizers, Water, Landscape, PCG, or any other system's internal spline implementation or its system-specific metadata. The native component and the owning system remain authoritative for runtime behavior.

## Testing and current limitations

After compiling in UE 5.8.3, run these console commands:

```text
Automation RunTests Strand.Data.RoundTripPointData
Automation RunTests Strand.Editor.Authoring.SplineEditingOperations
Automation RunTests Strand.Editor.Authoring.SplinePenGeometry
Automation RunTests Strand.Editor.Authoring.GroupEditingOperations
Automation RunTests Strand.Editor.Authoring.BezierHandleModes
Automation RunTests Strand.Editor.Export.SvgAndAlembicRoundTrip
```

Further fixtures and regression tests are in [TEST_PLAN.md](TEST_PLAN.md), [TEST_PLAN_0.6.8.md](TEST_PLAN_0.6.8.md), and `Tests/Fixtures/`. The existing authoring test exercises subdivision, mirror, reverse tangent direction, flattening, and point-type conversion. It does **not** fully validate live Slate input, mouse gizmo behavior, Undo/Redo, import UI, or all closed/mixed-interpolation cases. Perform the manual steps in the test plan as well.

**Current known gaps:**

- Persistent Free/Aligned/Mirrored/Auto/Corner handle editing is implemented in v0.6.8 source, but requires manual Unreal verification for drag behavior, exact tangent conversion and Undo/Redo. Segment dragging, custom pivots and procedural primitives remain unimplemented. Point alignment and spatial-axis distribution are under Spline Tools > Selected Points.
- Pen free-3D placement uses a **camera-facing construction plane** passing through the current endpoint, or through the local origin when the path is empty. It is NOT surface snapping or scene geometry picking. Constrained XY/XZ/YZ modes use a fixed coordinate plane at the selected endpoint's locked-axis depth, rather than flattening the curve.
- Pen click-drag creates symmetric Mirrored handles; click-only creation starts in Corner mode. Select mode can change either to Free, Aligned, Mirrored, Auto or Corner. Inserting on an existing cubic segment must preserve shape; the affected split controls are reset to Free when necessary so stored handle constraints cannot silently reshape the subdivided segment.
- The editor supports multiple selected anchors with one primary point. Pen point placement and group viewport input still need actual UE 5.8.3 testing with camera navigation, press/release capture, viewport resizing, axis selection and Undo/Redo.
- An empty or single-point Strand asset may exist while authoring; supply at least two anchors before exporting or placing for a meaningful spline.
- SVG projection of 3D curves discards a dimension. Geometry and interpolation metadata are not identical interchange concepts.
- UE 5.8.3 compilation and live input QA for v0.6.11 are pending, particularly native widget appearance/dragging, Spacebar cycling, RMB+WASD flight navigation, linked-handle mouse editing, Undo/Redo, save/reopen, Auto/Corner transitions, native spline compatibility, and SVG/Alembic round-trips. The v0.6.7 widget-mode warning fix and the v0.6.9 C4456 test compilation fix are retained.

The implementation roadmap and release criteria are in [FEATURE_ROADMAP.md](FEATURE_ROADMAP.md). The interaction research is recorded in [Docs/SPLINE_AUTHORING_TOOL_SURVEY.md](Docs/SPLINE_AUTHORING_TOOL_SURVEY.md). Previous changes and historical development notes are preserved in [CHANGELOG.md](CHANGELOG.md) and the versioned changelogs; they are not duplicated in this current-version README.
