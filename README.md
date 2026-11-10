# Strand

**Native spline assets, editing, and interchange for Unreal Engine 5.8.3**

Strand lets you create, import, edit, reuse, and export spline geometry as Content Browser assets while retaining Unreal Engine's native `USplineComponent` behavior. A Strand Spline asset (`USplineDataAsset`) stores spline-local control points, independent arrive/leave tangents, point rotations, point scales, native interpolation types, and the closed-loop flag. It does not store an Actor or Component transform and it does not produce rendered meshes.

Strand has two complementary editing workflows:

- **Strand Spline Editor:** Double-click a Strand Spline asset to edit its saved curve in a dedicated 2D/3D preview. These changes modify the asset, not existing level actors.
- **Native Level Editor spline editing:** Drag a Strand asset into the level or associate it with an existing native spline component. Continue using Unreal's native actor, spline, and viewport tools. Explicitly **Apply Asset** or **Update Asset** to synchronize the two copies.

**Package:** v0.6.40 source, targeting Unreal Engine 5.8.3 on Windows 64-bit. The editor uses Unreal's host-provided asset toolbar, direct authoring buttons, separate Spline Details and Source & Import tabs, and contextual hover actions. See [TEST_PLAN_0.6.30.md](TEST_PLAN_0.6.30.md) for build and live-editor verification.

### Native screen rotation and soft-selection display (v0.6.40)

Strand uses **Unreal Engine's native screen-space rotation ring** rather than drawing a duplicate widget. If the white outer ring does not appear in Rotate mode, enable **Edit > Editor Preferences > Level Editor > Viewports > Enable Screen Rotate** (Look and Feel). This is an **Unreal Editor preference**, not a Strand option; Strand does not change it globally. Select an anchor and activate the rotation widget to use the native ring. Local red/X rotation on a single spline point remains Strand's separate tangent-axis roll behavior.

In the Strand Spline Asset Editor, enable **Viewport Options > Soft Selection** and choose **3D Distance** or **Spline Distance**. With Soft Selection active, the selected anchor is **full magenta with a yellow selection outline** (100% influence). Other anchors fade from white (0%) to magenta (100%) by their falloff weights. When soft selection is off, the ordinary selected/start/end/control colors are restored. Anchor selection, shape, and falloff are independent; color is only visual feedback. This convention also applies to selected anchors skipped by dense-preview marker subsampling.

### Curve Cleanup and Precision Modeling (v0.6.30)

**More > Curve Cleanup...** opens a non-destructive preview for **Simplify**, **Dissolve Selected**, and **Repair Degenerates**. Adjust a tolerance in centimeters: orange shows the proposed curve over the unmodified cyan spline, while the panel reports original/result anchor count and the measured sampled 3D deviation. Cancel does not modify the asset; Apply creates one Undo transaction. The dialog remembers the last tolerance for the current editor user. The source must contain at most 512 anchors for this preview path. Deviations are sample-based, not mathematically certified continuous maxima.

**More > Exact Subdivision** offers 1–32 cuts per segment, by equal parameter or approximate equal distance, using an exact de Casteljau split of each native cubic. It is different from **Resample by Arc Length**, which produces an exact **total** point count but approximates the curve. The cut count is remembered per user.

**More > Selected Points** provides **G1/C1 continuity**, **Recalculate Selected Tangents**, **Normalize Selected Handle Lengths**, **Select Duplicate Anchors**, and **Select by Segment Length**. **More > Create and Path > Weld Start and End** joins an open spline into a closed loop when the endpoints are within a chosen gap threshold. Contextual anchor right-click also exposes cleanup and continuity. These operations use native Undo/Redo and do not change the runtime spline schema. See [CURVE_CLEANUP_0.6.30.md](Docs/CURVE_CLEANUP_0.6.30.md) for behavior and limitations.

### Camera-aware Frame Entire Spline (v0.6.29)

**Frame Entire Spline (F)** now uses the *evaluated spline's projected silhouette* and the actual viewport aspect ratio rather than relying only on `FEditorViewportClient::FocusViewportOnBox`. In perspective, Strand preserves the current viewing direction, uses the current horizontal camera FOV, and solves for optical centering and a camera distance that leaves comfortable margins around all sampled curve positions. The spline fills at most approximately 74% of each viewport dimension; especially narrow or wide windows still fit. Initial framing defers until the Slate viewport has nonzero dimensions. Orthographic views continue to use Unreal's native framing behavior, with additional viewport-size-aware zoom and centering. This operation only changes the **preview camera**; it does not move control points, modify the asset, or reimport geometry.

**Select Sharp Corners** lives under **More > Selected Points** with angle thresholds 15°, 30°, 45°, 60°, and 90°. It replaces the current selection with anchors whose incoming and outgoing *evaluated* directions form at least that angle. Open-path endpoints are skipped; closed loops check the closing seam. Auto-tangents and explicit handles are respected, while linear/collapsed handles use adjacent chord directions. This is a non-destructive analysis/selection operation.

### Direct curve shaping

In **Select** mode, hold **Ctrl** and drag the middle of a spline segment to bend its interior. The segment's endpoints remain fixed; Strand reshapes the outgoing tangent of the first anchor and incoming tangent of the second. The edit is grouped into a single Undo/Redo transaction. Dragging an automatic or linear segment converts the affected endpoints to editable custom tangents. To avoid extreme tangent amplification, the bending region excludes the first and last 10% of a segment.

**Shift-click** continues to insert an anchor at the cursor, while **right-drag** and **RMB+WASD** remain viewport navigation. Ctrl-clicking an anchor or native gizmo retains its previous behavior.

The **left modeling rail** and top toolbar render their artwork through independent 21×21 icon layers in compact 36×27 action buttons. Both top and left dropdowns use independently rendered 16×16 icons and 9×9 chevrons (45×27), with an adjusted, balanced gap and 5 px right inset. No SVG is stretched to compensate for native Slate button padding.
### Joining two Strand spline assets

Select **exactly two open Strand Spline assets** in the Content Browser, right-click, and choose **Strand > Join Two Strand Spline Assets...**. Strand selects the closest endpoints in **asset-local coordinates**, reverses either path's traversal if required, and creates a **new** asset named with `_Joined`. The originals, their actor references, and their imported-source associations are unchanged. The new asset has no external reimport association.

If the two endpoints coincide to within **0.001 cm**, Strand welds the common anchor, retaining the first path's arriving tangent and the second path's leaving tangent. If the endpoints differ, Strand asks for confirmation and inserts a **straight segment** between them. This preserves existing curve shapes but does not automatically make a smooth G1/C1 join across independently authored paths. Use the handle tools after joining if a smooth bridge is desired. Strand assets have spline-local geometry only; it does not apply actor/component world transforms during this operation. Closed loops and assets with fewer than two points are rejected. The operation creates a new asset and does not modify either original through the asset editor's geometry Undo stack. Delete the joined asset if you do not want to keep it.

### Viewport and selection visual refinements

Dropdown glyphs are **25% smaller** than regular toolbar icons, with a consistent gap before the dropdown arrow and reduced right inset. The **Frame Entire Spline** icon now depicts a Bézier curve inside framing corners, rather than a grid or rectangle. **Box Select** and **Lasso Select** use thin foreground outlines (0.85 px) rather than heavy strokes. These are presentation-only changes and do not affect geometry.

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
3. Choose **Pen** in the viewport toolbar. Click to add anchors; click-drag to create symmetric incoming/outgoing Bezier handles. The highlighted **Pen** mode is active until you choose **Select** or press **Esc**. To start with no points, choose **Edit > Create and Path > Clear All Points**, confirm, and draw a new path. Clear is a single Undo step.
4. In Pen mode, click an existing segment to add an anchor at the clicked location using a shape-preserving cubic split. Click an endpoint to select the side to extend, then click empty space to continue from that end. With three or more points in an open path, clicking the opposite endpoint joins the ends and exits Pen mode. The other end can be selected and extended similarly. **Snap: Off / 1 / 10 / 100 cm** controls Pen placement and handles.
5. Choose **Select**. Click an anchor to select it, **Shift-click** an anchor to extend an index range, or **Shift-click the curve to insert a point**, or **Ctrl-click** to toggle individual anchors. Use **Box Select** to draw a rectangle around projected anchors; **Shift+drag** adds and **Ctrl+drag** toggles those in the box. A simple click in Box Select behaves like Select. **Esc** exits Box Select, and Ctrl+A selects all. The selection strip and inspector All/None controls offer the same actions; there is no duplicate selection dropdown.
6. In **Select**, choose **Move**, **Rotate**, or **Scale** from the toolbar, or press **Spacebar** inside the Strand viewport to cycle Move → Rotate → Scale → Move. The preview uses a dedicated editor-mode manager to render **Unreal's native editor transform widget**, anchored at the selected point or center of selected points. It uses native widget hit-testing and drag deltas but edits the Strand asset via transactions; it does not select or move actors in the Level Editor. **W/E/R are not Strand shortcuts** so right-mouse **WASD flight navigation** remains with Unreal. Move translates point positions; Rotate/Scale transform a group about its centroid or, for one anchor, rotate its tangents/orientation and scale its tangent lengths in place. Scale is **uniform** even when an engine axis is dragged. For precise placement the inspector offers **Absolute** target positions or **Relative** additive offsets in asset centimeters, toggled only from the Spline Details inspector. The top-center **World/Local** buttons change the native widget axis orientation: World is the preview/asset basis, while Local aligns X with the primary selected anchor's **outgoing spline tangent**, including evaluated Auto tangents. The anchor's saved **LocalRotation** supplies the preferred up/roll reference rather than redefining forward. Pitch/Yaw/Roll remain editable through **Anchor Orientation** in the inspector. **Reset Rotation** sets selected authored rotations to zero, and **Align to Curve** is an explicit opt-in to align their X axes to native spline direction. Neither action changes positions or tangents. These settings affect different things: numeric values remain in asset axes, even when the widget axes are Local. **F** frames the curve, **P** activates Pen, **B** activates Box Select, **V** returns to Select and **Delete** removes selected anchors when at least two remain. Keyboard commands require viewport focus; Space does not cycle during Pen or Box Select.
7. For direct editing, move the pointer over an anchor (hand cursor), a Bézier handle (grab cursor), or empty canvas (arrow cursor). **Shift-hover a curve segment** to display a cyan segment and a dot at the actual insertion location; **Shift-click** to insert there. A *stationary* right-click on an anchor opens contextual point actions (insert, delete, Bézier mode, interpolation and orientation); a stationary right-click on a curve segment opens **Insert Anchor Here** and **Select Segment Endpoints**. Right-drag and RMB+WASD remain native Unreal camera navigation. For precision, hold **Ctrl+Shift** and use J/L for asset -X/+X, I/K for +Y/-Y, and U/O for +Z/-Z (1 cm each). These inputs create separate Undo steps and respect the active edit plane. Arrow/Page keys remain native Unreal camera navigation.
8. Select one or more anchors and choose a **Bézier Handles** mode in the right inspector: **Free** (separate handles), **Aligned** (shared direction, independent lengths), **Mirrored** (shared direction and length), **Auto (Unreal)** (native smoothing), or **Corner** (zero-length handles). On a single anchor, click the **Incoming Handle** or **Outgoing Handle** or pick its viewport marker to edit numerically or via the Strand axis handles. Linked constraints persist across subsequent drags and asset saves. The old one-shot Align/Mirror operations are still available as distinct commands; they switch that point back to Free.
9. Use **Insert After** for a single selected point or **Delete Selected** for a set that leaves at least two points. Whole-spline tools remain separate from selection tools. The left-side modeling strip begins with Pen and includes **Replace Current Spline** beside the modeling dropdowns: Circle, Rectangle and Helix replace the points in the open asset (Undo/Redo supported). **Asset toolbar > Primitives** creates a separate Strand asset instead. The left strip also includes a state-sensitive Open/Close Path action. In More > Resample by Arc Length you can enter an exact point count (2 to 4096, default 16) and apply an approximately equal-distance reconstruction. The last applied count is remembered per-user (Undo restores the original curve). Save via Unreal's native asset toolbar.

### Split a spline into two independent assets

For an **open** spline with at least three anchors, select exactly one internal anchor (not the first or last). Right-click the anchor and choose **Split into Two Strand Assets**, or use the left modeling **More > Create and Path > Split at Selected Anchor into Two Assets**. Strand creates two new assets, named with `_Start` and `_End` suffixes, in the original asset folder and highlights both in the Content Browser. Both contain the split anchor. Strand bakes evaluated native Auto tangents before splitting so curve segments stay geometrically consistent. The **original asset and all linked actors remain unchanged**. This operation creates assets rather than editing the original; use Content Browser asset management to discard unwanted results. Source import/reimport links are not carried over. Closed-loop paths must first be explicitly opened.

### Capture an existing native Unreal spline

Select a level actor with exactly one native `USplineComponent`, right-click the actor and choose **Strand: Save Native Spline as Asset...**. Alternatively, use **Tools > Strand: Save Selected Actor Spline as Asset...**. Pick an asset path/name in the dialog. Strand captures its local positions, tangents, rotations, scales, interpolation and loop state without changing the component or Actor transform, and associates the created asset with the native component. For actors with multiple spline components, use Strand's explicit component capture in the actor Details/Strand links instead of relying on ambiguous actor selection. Canceling the dialog leaves native content unchanged.

World/Local does **not** turn an unplaced spline asset into level-space geometry. Strand assets have no actor transform: World uses the preview origin and Local follows the selected point's outgoing tangent, using its stored orientation as an up/roll reference. Group transformations pivot about the selected points' average, using the primary selected point as the Local-axis reference. A 2D edit plane still constrains geometry regardless of widget coordinate mode.

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
| **Select / Box Select / Pen** | Select supports single-click, Shift-click anchor ranges, Shift-click shape-preserving insertion on the curve, Ctrl-click toggles, Ctrl+A all, and Esc clear. Box Select drag selects projected anchors in a screen rectangle, Shift adds, Ctrl toggles, Esc returns to Select; click without drag also selects a single point. Pen adds, extends, closes and inserts anchors; Esc exits Pen. |
| **Move / Rotate / Scale** | Unreal's standard transform widget, located at the selected anchor/handle or the selected group's centroid. **Spacebar** cycles modes while Select is active. **W/E/R no longer switch Strand modes**, preserving RMB+WASD camera flight. With one anchor Rotate changes orientation/tangent directions and Scale adjusts tangent lengths in place; with multiple anchors Rotate/Scale use the group center. Scale always applies a uniform multiplier to Strand geometry. |
| **Pen Snap** | Optional placement/handle snapping to 1, 10, or 100 cm, or Off. Available while Pen is active. |
| **Camera** | Perspective, Top (XY), Front (XZ), Side (YZ). Controls the view, not saved geometry. |
| **Edit: 3D** | No movement-axis restriction. |
| **Edit: XY / XZ / YZ** | Constrains translation and numeric edits to the chosen two axes. Switching modes does **not** flatten existing positions or tangents; a locked numeric axis is disabled. Choosing a 2D mode also changes to the matching orthographic view. |
| **Frame All / Frame Primary** | Focuses the camera on the full curve or primary selected anchor. |
| **Points / Grid / Tangents** | Toggles overlays. Tangents default on in Select mode for one selected point and hide automatically during Pen drawing. |
| **Selection / Point Editor** | Navigate anchors, select All/None, edit point position or selection center, choose tangent handles and interpolation. With multiple points, use relative Rotate Z degrees and Scale multiplier fields. |
| **Authoring workspace** | Asset operations (New, Primitives, Export, Reimport, Actors) are appended to Unreal's existing Save/Browse toolkit toolbar. The upper-left icon strip holds Select/Box Select and selection actions, while Pen is first in the left-side modeling strip, the upper-center icon strip holds transform and World/Local axes (numeric Absolute/Relative is only in Spline Details), and the upper-right strip holds view settings, and the left-side vertical icon strip exposes Insert, Delete, Subdivide, Smooth, Linearize, Reverse, Loop, persistent handle/interpolation modes and advanced modeling. Tooltips provide names and shortcuts; tool icons use Strand-registered SVG brushes with valid 24 x 24 aspect ratios and consistent 28 px square icon content inside the 48 x 36 controls. Thin separators distinguish selection actions, transform coordinate modes and viewport display commands. |
| **Bézier Handles** | Persistent Free, Aligned, Mirrored, Auto and Corner per-point authoring behavior, editable on one or multiple selected anchors. Selected mode or Mixed is displayed in the inspector. Auto uses native Unreal spline smoothing; dragging an Auto or Corner handle converts it to Free. |
| **Selected Tangent** | Incoming (orange) / Outgoing (blue) Handle buttons and clickable handles select the evaluated tangent for numeric or widget edits on one anchor. Linked handle modes apply to both widget and numeric changes. |
| **Asset Properties** | The native property view is in its own dockable **Source & Import** tab beside **Spline Details**. Selecting anchors no longer requires scrolling through source metadata. |

**Transform widget:** The asset viewport owns a dedicated `FEditorModeTools` manager and enables Unreal's standard editor widget only when an anchor is selected in Select mode with Points visible. `GetWidgetLocation`, `GetWidgetMode`, and `InputWidgetDelta` route the engine widget into Strand's selection and edit transactions. Strand no longer draws its own imitation XYZ arrows, colored rings, or white scale square. The preview has its own selection and its own mode manager; it does not take over the Level Editor's actor selection or mode manager, or replace native spline component gizmos. In Pen and Box Select modes, or with no selected anchor, transform controls are disabled and lose their highlighted state; this distinguishes the stored transform mode from an available transform action. Rotation and scaling of auto-smoothed anchors may require changing the point to custom tangents before a visible handle shape change, because Unreal evaluates Auto tangents dynamically.

**Marker colors:** Start is green, end of an open spline is red, selected anchors are yellow, incoming tangent handles are orange and outgoing handles are blue with a larger primary marker. Preview point markers and sampled segments are capped for very dense curves; the primary point remains visible. Pen segment hit targets are disabled above 4,096 points to prevent large hit-proxy allocations; endpoint extension remains available.

**Point types:** Curve (Auto), Curve (Clamped), Curve (Custom Tangents), Linear, and Constant are offered as native Unreal interpolation types. **Constant** interpolation is not supported by Strand's SVG or Alembic exporters. Point **Pitch/Yaw/Roll** are editable directly in the contextual Anchor Orientation inspector; rotations and scales also remain stored in native asset point data. The Local gizmo follows the point's evaluated outgoing tangent. Saved point rotation remains independent asset data and guides the frame's up/roll reference; it does not need to be edited for tangent alignment.

### Precision nudge shortcuts

Select at least one anchor, focus the Strand viewport, and hold **Ctrl+Shift**. Press **J/L** for negative/positive asset X, **I/K** for positive/negative Y, or **U/O** for positive/negative Z. Each press moves 1 cm and creates an Undo step. Constrained 2D edit planes still lock their excluded axis. Native Unreal Arrow/Page camera controls are unchanged, even when an anchor is selected. Space cycles Move/Rotate/Scale; RMB+WASD still flies the camera.

### Spline Tools

| Tool | Behavior and important limitation |
| --- | --- |
| **Clear All Points** | Confirm and remove all points from the asset, allowing Pen to start from an empty curve. Undoable; assets with fewer than two points should not be placed or exported as usable geometry. |
| **Activate Spline Pen** | Enter viewport Pen mode directly from the Edit menu. |
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

The Edit menu has three concise groups: **Selected Points**, **Create and Path**, and **Refine and Shape**. This leaves primary Select/Box Select, left-strip Pen, and transformation controls visible instead of crowding a single long dropdown. The Bézier mode property is read-only in the raw asset point array; change it using the selection-aware inspector to keep geometry and stored behavior synchronized.

The authoring toolbar uses Slate brushes registered at 20 px with corrected four-component SVG view boxes. This avoids the distorted/narrow 16 px SVG rendering present in earlier packages. The editor uses a transient `USplineComponent` for preview. It does not spawn a persistent actor in the current level. An imported asset's **Reimport** does not push changes to existing native spline components. Save the edited Strand asset and apply it to level instances explicitly.

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

- Persistent Free/Aligned/Mirrored/Auto/Corner handle editing is implemented in v0.6.8 source, but requires manual Unreal verification for drag behavior, exact tangent conversion and Undo/Redo. Segment dragging, custom pivots and procedural primitives remain unimplemented. Point alignment and spatial-axis distribution are under Edit > Selected Points.
- Pen free-3D placement uses a **camera-facing construction plane** passing through the current endpoint, or through the local origin when the path is empty. It is NOT surface snapping or scene geometry picking. Constrained XY/XZ/YZ modes use a fixed coordinate plane at the selected endpoint's locked-axis depth, rather than flattening the curve.
- Pen click-drag creates symmetric Mirrored handles; click-only creation starts in Corner mode. Select mode can change either to Free, Aligned, Mirrored, Auto or Corner. Inserting on an existing cubic segment must preserve shape; the affected split controls are reset to Free when necessary so stored handle constraints cannot silently reshape the subdivided segment.
- The editor supports multiple selected anchors with one primary point. Pen point placement and group viewport input still need actual UE 5.8.3 testing with camera navigation, press/release capture, viewport resizing, axis selection and Undo/Redo.
- An empty or single-point Strand asset may exist while authoring; supply at least two anchors before exporting or placing for a meaningful spline.
- SVG projection of 3D curves discards a dimension. Geometry and interpolation metadata are not identical interchange concepts.
- The v0.6.16 toolbar, input isolation, segment-target stability and Source & Import tab require a fresh UE 5.8.3 build and live input QA, particularly tangent-aligned Local axes and stable axes during dragging, numeric rotation, native widget dragging, Spacebar cycling, RMB+WASD flight navigation, linked-handle mouse editing, Undo/Redo, save/reopen, Auto/Corner transitions, native spline compatibility, and SVG/Alembic round-trips. The v0.6.7 widget-mode warning fix and the v0.6.9 C4456 test compilation fix are retained.

The implementation roadmap and release criteria are in [FEATURE_ROADMAP.md](FEATURE_ROADMAP.md). The interaction research is recorded in [Docs/SPLINE_AUTHORING_TOOL_SURVEY.md](Docs/SPLINE_AUTHORING_TOOL_SURVEY.md). Previous changes and historical development notes are preserved in [CHANGELOG.md](CHANGELOG.md) and the versioned changelogs; they are not duplicated in this current-version README.

### Viewport targeting and point colors

The native Move/Rotate/Scale widget should highlight the axis under the pointer before dragging. Strand also outlines hovered points and Bézier handles, and previews a cyan segment with a precise insertion dot when Shift is held. Open splines use green for the start and red for the end; selected anchors remain yellow. Shift-click an unselected curve segment in Select mode to insert a point at the clicked position, with Undo/Redo support. The new insertion dot and cursor states require live Unreal QA.

## Spline Primitives

Use **Asset toolbar > Primitives** (alongside Save/Browse) to create an independent Circle, Rectangle or Helix spline asset in the current Content Browser folder. The open asset is never replaced by this command. Use the **Edit strip > Replace Current Spline** dropdown to instead replace the active asset’s geometry in place with one of those primitives. This keeps the asset identity and linked actors, preserves placement defaults, clears its prior source curve association to prevent unexpected Reimport, and is a single Undo/Redo transaction. The preview automatically refreshes and prior point selection is cleared. Undo restores the original geometry and source curve association. Replacement does not automatically push updates to already placed native spline components.

Shift-hover over a spline segment previews an insertion dot using a stable screen-space curve target. Shift-click inserts that point while preserving the cubic shape; native controls take priority over the segment target. Source/import properties are in the separate **Source & Import** tab.


### 0.6.30 Hotfix 1

Fixes exact 3D/closed-loop subdivision tangent math, makes the Cleanup preview modeless so the orange candidate is visible while settings change, adds Refresh Preview and no-candidate status, and removes the redundant Cleanup submenu entry. See `CHANGELOG_0.6.30_HOTFIX1.md` and `TEST_PLAN_0.6.30_HOTFIX1.md`.


### Precision editing (v0.6.41 development)

In the **Strand Spline Asset Editor**:

- **More → Selected Points → Snap to Existing Spline Geometry**: set a custom maximum distance and snap selected anchors to nearest unselected anchors or to other native evaluated curve segments. Snapping acts in asset-local space. It does not target level actors.
- **Edit Plane → Working Plane from 3 Selected Anchors**: select exactly three non-collinear anchors to define a fixed custom construction plane. Move/handle adjustments and Pen creation respect the plane. Select **Edit Plane → Free 3D** to restore unconstrained editing.

These editing operations do not replace Unreal's own viewport transform widgets. The editor user preference **Enable Screen Rotate** is still required for the native white camera-rotation ring.


### Interactive snap-on-drag (v0.6.43 development)
In the Strand Spline Asset Editor, enable **Edit Plane → Snap to Other Anchors While Moving**. The neighboring **Snap During Drag: Target Priority** submenu selects anchor targets, eligible evaluated curve segments, or both. During a Move gesture, the magenta target indicator previews where the primary selected anchor will land. The final group offset is committed on mouse release as part of one Undo operation. Hold **Ctrl** to temporarily suppress snapping; snapping is also disabled while soft selection is active. The saved distance tolerance is set in **More → Selected Points → Snap to Existing Spline Geometry**. These targets belong to the currently edited spline asset; they do not represent level-world surface snapping.

The selected anchor's soft-selection marker can be enlarged from **Editor Preferences → Plugins → Strand Editor → Selection Visualization**.
