## Strand Spline Editor (0.6.2)

Double-click a **Strand Spline** asset in the Content Browser to open its dedicated native 3D editor. The viewport evaluates a transient Unreal `USplineComponent` and includes Perspective/Top/Front/Side cameras, Frame All and Frame Point, a bounded local XY floor below the curve, point markers, and optional selected-point tangent guides.

**Select a point:** Click its marker in the viewport, or use the previous/next buttons in the right-hand Point Inspector. Selected points are amber-yellow; green identifies the start and orange identifies the end of an open curve. Very dense splines limit displayed markers for performance; an already selected point is always drawn.

**Edit points:** Click a point, then use Unreal's native translation widget to drag it in 3D. A complete drag is grouped into one Undo/Redo transaction. Alternatively, commit X, Y or Z (in Unreal centimeters) in the Selected Point inspector; compact display formatting does not round the stored double values. The preview and stored Strand data asset update, not any placed actors. Advanced per-point properties (arrive/leave tangents, rotation, scale, point type), source information, placement defaults, and import settings remain available in the native Details panel. The editor preview does not spawn a level actor. Tangent handles are currently a visualization, not direct-manipulation handles.

**Actions:** Use **New Spline** in the editor to create another native asset (two editable starting points). **Export > Alembic (.abc)** writes 3D cubic curve geometry; **Export > SVG (.svg)** writes a 2D path. A spline with depth outside XY requires you to select XY, XZ, or YZ projection, and the original asset is not changed. Exports are also available from the Strand Spline Content Browser right-click menu. The Selected Point panel supports Insert Point (exact cubic subdivision), Delete Point, Point Type, and Toggle Closed Loop with Undo/Redo. The editor's native toolbar handles Save. The viewport's asset action row offers Reimport Source, Create Actor, Select Users, and Inspect Usage. Reimport asks for confirmation because it replaces the source-derived stored points and can overwrite manual point edits. Import settings are source-format aware in Details: SVG-only options are hidden for Alembic assets and Alembic-only options are hidden for SVG assets. Changes to import options take effect when you reimport; placed instances are not updated automatically.

Periodic Alembic NURBS exported with no knots now import under a documented **uniform periodic rational** fallback. The missing original knot values cannot be inferred exactly.

# Strand

**Reusable spline assets for Unreal Engine 5.5+**

Strand stores **native `USplineComponent` point data** as Content Browser assets (`USplineDataAsset`). It does not replace Unreal spline editing, introduce a competing spline component, or generate meshes. A Strand asset stores local positions, arrive/leave tangents, rotations, scales, point types and the closed-loop flag, **never the actor or component transform**.

**Version:** 0.6.2 development source candidate. **Validated against a live UE Editor build:** v0.6.1 built successfully in UE 5.8.3 (user test); v0.6.2 still requires compilation. **Modules:** Strand (runtime), StrandPCG (PCG runtime), StrandEditor (editor). Dependencies: Unreal PCG and Alembic Importer plugins.

## Quick start: drag directly into the Level Editor

1. Import an SVG/ABC using **Tools > Strand: Import SVG Curves...** or **Import Alembic Curves...**. Each disconnected curve receives its own **Strand Spline** asset.
2. Drag one **Strand Spline** asset from the Content Browser into the **Level viewport**, and drop it where you want it.
3. Strand's native `UActorFactory` integration creates an `AStrandSplineActor` (a lightweight normal Actor containing a stock `USplineComponent` as its root) and assigns the Strand asset to that component's Asset User Data.
4. Select the new actor's **Spline Component**. Edit its curve normally with Unreal's viewport spline controls. No additional tracking component appears.
5. In the **Spline Component Details > Strand** category, use the asset picker and the **Apply Asset**, **Update Asset**, **Save as New** or **Compare** buttons.

**Icon roles:** The Strand Spline Content Browser asset keeps its existing SVG and teal asset color `#00B4A7`. The placeable **Strand Spline Actor** uses the new white `StrandSplineActor_16.svg` icon for its Outliner/class and placement presentation. An editor-only billboard attached to the actor's native spline root displays a PNG rendering of the ORIGINAL Strand asset SVG in the viewport. This icon does not render in gameplay and does not create a mesh.

**Billboard controls:** Select a Strand Spline Actor itself (rather than its Spline Component) and look under **Strand > Viewport**. `Show Strand Billboard` toggles the icon and `Billboard Scale` adjusts its size. These settings never modify spline point data or actor transforms. The billboard is a native `UBillboardComponent` marked editor-only.

**Quick Add:** In a Level Editor viewport choose **Quickly Add > Strand Spline Actor** (also available under Place Actors > Basic). This creates the exact same `AStrandSplineActor` class as a dropped Strand asset, but with an unlinked native spline to edit or assign later. The existing **Spline Test** entry is not part of Strand and remains untouched.

The actor factory is explicitly registered with UE 5.8's `UPlacementSubsystem` as well as the legacy editor factory registry. It accepts only `USplineDataAsset` and does not intercept other Content Browser asset types. It uses Unreal's normal asset placement transform, so the actor should appear at the drop position. The right-click **Create Strand Spline Actor** command offers a fallback and reuses the same native root spline rather than creating an extra spline component.

**Note:** Native spline components do not generate visible rendered meshes at runtime on their own. The spline is still a native component edited with Unreal's standard spline toolset.

## Native Spline Component Details

Strand adds a **Strand** category to existing `USplineComponent` Details with **Strand Actions** on the left and **four white, native-looking icon buttons** on the right (Apply, Update, Save as New, Compare), each with a descriptive tooltip:

| Control | Behavior |
| --- | --- |
| Strand Spline Asset | Assign, browse, or clear an asset reference. Assigning a different shape asks whether to apply the asset or preserve the local points. |
| Sync Status | **In Sync**, **Modified Locally**, or **No Asset Assigned**, with point counts. |
| Apply Asset | Confirm and restore the assigned asset's point data into the native spline. |
| Update Asset | Confirm and save the spline's current points into the assigned `.uasset`; warns about other loaded users and imported source data. |
| Save as New | Save current spline points to a new Strand asset through the standard Content Browser asset dialog and link it. |
| Compare | Non-destructive point/count/tangent/rotation/scale/loop comparison. |
| Coordinate Mode | **Absolute** preserves the asset's original spline-local coordinates. **Relative** translates spline-local positions around a selected anchor while preserving the actor/component transform. |
| Use Center of Spline Points | In Relative mode, anchor the arithmetic average of all spline points at component local origin. |
| Anchor Point Index | In Relative mode, select a zero-based spline point to position at component local origin instead. |

**Coordinate behavior:** All stored Strand point positions remain local to the source spline, even when "Absolute" is selected. "Absolute" means *preserve the asset's local coordinates*, not world-space coordinates. "Relative" subtracts either an existing point's location or the average of all point locations; it never writes any Actor or Component transform. The anchor options live on each native spline, and asset-level defaults live on the asset itself. Imported SVG/Alembic curves and manually created new assets default to **Relative + Center** for intuitive viewport drops, including assets made before placement options existed. Native spline assets captured from Unreal are explicitly set to **Absolute**, preserving original shape offsets. Updating an asset from an anchored instance reverses the placement offset, so the source coordinate frame isn't unintentionally erased.

**Blueprint viewport:** Strand now invalidates the native spline render state and refreshes matching Blueprint preview spline components directly, even when the preview world is not in the normal engine world-context list after Apply/placement adjustments, without requiring a manual Compile to see open/closed curve changes. Construction scripts that independently overwrite spline points may still control their final shape.

Asset assignment is serialized as a tiny **`UStrandSplineUserData : UAssetUserData`** instance owned by each native spline. It is *not* an actor component and does not require changing Unreal engine source or subclassing `USplineComponent`. Assignment alone does not silently replace local geometry. Apply and Update are explicit and confirmation-gated. Operations use `FScopedTransaction` for editor Undo/Redo. `Update Asset` also saves the `.uasset`; after undoing it, save again to persist that reversal.

**Spline Mesh Components:** Strand also adds a Details category on native `USplineMeshComponent` (including loaded derived types). A spline mesh is one deformable two-endpoint section, *not* a multi-point USplineComponent. It can save its endpoint positions/tangents as a two-point Strand asset and apply a compatible two-point open asset. Multi-point and closed assets are explicitly rejected rather than truncated. Existing Static Mesh, materials, collision, mesh-specific roll/scale and other properties remain native. Update from a spline mesh replaces properties that the 2-point Strand representation does not carry (the confirmation warns).

**Compatibility detail:** Strand registers Details customization for currently loaded native spline subclasses, plus native Spline Mesh classes. This can interact with other plugins' own custom class layouts; test the relevant specialized components in the installed editor. Strand does not change Water/Cine/PCG-specific metadata.

## Existing actors and multiple splines

- Select a Strand asset, right-click an actor with one spline and choose **Apply Selected Spline Data Asset**. It overwrites points after confirmation and assigns the asset to that component.
- Right-click an actor with no spline and choose **Add Native Spline Component from Selected Strand**. Strand adds an ordinary `USplineComponent`, respecting the existing actor root.
- Actors containing several splines offer individually named **Save [Component] as Strand Spline** and **Apply Selected Strand to [Component]** actions. Strand never arbitrarily targets one of several splines.
- Right-click a Strand asset and choose **Create Strand Spline Actor**, **Apply to Selected Splines**, **Select Actors Using This Asset**, or **Inspect Strand Usage...**. Create uses a point in front of the current level viewport camera, while direct viewport drag-and-drop uses the mouse drop location. Apply is disabled when no unambiguous native spline target is selected. The two usage actions scan native spline Asset User Data for loaded level instances, not unloaded World Partition cells or Blueprint class defaults. Inspect reports per-component sync and mismatch status without modifying selections.
- Select an existing actor with a spline and choose **Convert Spline to Spline Data Asset** to create a new linked Strand asset.


## Interactive Alembic import selection (v0.5.1)

Drag an `.abc` file from Windows Explorer into a Content Browser folder, or use the Content Browser **Import** command. Strand's interactive `.abc` factory presents three choices:

- **Strand Spline Importer:** reads native Alembic `ICurves` and creates independent Strand Spline assets. It does not generate mesh or Groom assets.
- **Unreal Alembic Importer:** delegates this file to Unreal's installed `AlembicImporter` factory and its usual import-options workflow, typically for Static Mesh / Geometry Cache / Skeletal Mesh-style Alembic content.
- **Cancel:** imports nothing.

The explicit **Tools > Strand: Import Alembic Curves** command bypasses this choice dialog and always uses Strand. Automated asset imports are not interrupted by the chooser; scripts that want Strand should pass `UStrandAlembicFactory` explicitly. Dragging multiple files may show one prompt per file.

**Engine integration status:** The chooser relies on Unreal's `UFactory::ImportPriority` ordering and delegates the engine option through Unreal's existing Alembic factory. Test both branches in UE 5.8.3 before release, especially native Geometry Cache/animated imports. This plugin has not been engine-compiled in this environment.

**FBX removal:** Strand no longer registers any `.fbx` importer or exports FBX test curves. Unreal's native FBX importers are unaffected. Strand assets previously created from FBX remain normal `.uasset` spline data, but Strand does not provide FBX reimport.

## Import and reimport

- **SVG (.svg):** native 2D paths/lines/polylines/polygons/rectangles/circles/ellipses, in-file `<use>` references, cubic and quadratic Bezier curves, smooth commands, elliptical arcs, closed paths and nested transforms. Mixed straight/curved segments retain independent incoming and outgoing tangents with geometrically straight Hermite segments. Stored in local XY with Z=0.
- **Alembic (.abc):** `ICurves` geometry with supported linear, Bezier, Catmull-Rom, B-spline and NURBS forms; nested curve identities preserved for source-aware reimport. Alembic units must be configured for the source application.
- Import each distinct curve into its own Strand asset. Reimport updates the *asset*, **not every placed instance automatically**; that choice remains with the designer.

All format readers and their behavior are inherited from Strand v0.3.0 and have not been engine-tested in this pass.

## Runtime, PCG, Landscape

- `UStrandLibrary::ApplySplineDataAsset` applies point data to any existing native `USplineComponent`, preserving its transform.
- `UStrandPCGLibrary::ApplySplineDataAssetToPcg` produces transient PCG spline spatial data from asset points plus a caller-supplied transform.
- Landscape context commands convert or apply topology-compatible connected spline paths; branched networks and mismatched paths are rejected rather than rebuilt arbitrarily. Landscape mesh/terrain settings remain native. In UE 5.8 binary builds, `ULandscapeSplinesComponent::ModifySplines` and `RequestSplineLayerUpdate` aren't linkable; automatic Landscape Edit Layer deformation refresh is not guaranteed. Test Landscape workflows in a disposable level.

## Appearance

Content Browser type name: **Strand Spline**. Type color: **`#00B4A7`**. Original supplied `Resources/StrandSplineAsset_64.svg` is used for the small icon and asset thumbnail.

## Install and testing

Copy `Strand/` to `<Project>/Plugins/Strand/`, enable PCG and Alembic Importer, regenerate the Editor solution and compile. **If updating over Strand 0.3.x or an earlier installation, run `RemoveLegacyTracker.bat` once to delete obsolete tracker source left behind by a folder overlay.** Otherwise UnrealBuildTool will still compile those removed files. No engine source modifications are required. The zip contains complete C++ source, not binaries. Run `Automation RunTests Strand.Data.RoundTripPointData` in Unreal's console after loading the Editor.

**Build validation is pending against UE 5.8.3.** In particular, verify native viewport drag/drop actor factory registration, full Spline Component Details, and asset-user-data persistence after saving/reopening maps or Blueprint instances. See [TEST_PLAN.md](TEST_PLAN.md).

## Version 0.4.5 changes and verification priorities

- Editor billboard uses `UBillboardComponent::bIsScreenSizeScaled` and `ScreenSize` so it behaves like stock editor sprites while moving the camera. Actor Details exposes only **Show Strand Billboard** and **Billboard Size** (a view-size multiplier); the internal `StrandBillboard` component pointer is no longer an editable/visible actor property. Billboard remains editor-only, not gameplay geometry.
- Right-click Strand asset menu has four actions in a dedicated **Strand** section: **Create Strand Spline Actor**, **Apply to Selected Splines**, **Select Actors Using This Asset**, **Inspect Strand Usage...**. Applying is disabled when no valid level target is selected. Exactly one Strand asset must be selected. The engine-owned `Convert to Different Data Asset Type` remains available in general **Asset Actions**, separate from the **Strand** section. Strand does not override the conversion command.
- Create from right-click positions the actor in front of the current level viewport camera, falling back to world origin only when no editor viewport camera is available; viewport drag-and-drop still respects the actual drop point.
- Confirm that viewport orbit/zoom keeps the billboard readable, its Size slider changes screen presence immediately, and edits to the actor's spline or transform have no unexpected effect on the sprite's screen size.
- Verify that actor Details no longer exposes the raw `StrandBillboard` property/texture and that Blueprint inheritance doesn't expose an editable sprite component. The two intended Strand > Viewport controls remain.
- Confirm the asset SVG, actor SVG, `Strand Spline` Content Browser label, and `#00B4A7` color have not changed.

## Version 0.4.4 verification priorities

1. Confirm the Strand Spline asset keeps its original Content Browser artwork, teal type color and `Strand Spline` label.
2. Confirm Strand Spline Actor Outliner and Quick Add use the NEW white actor SVG, not the asset icon.
3. Confirm Quick Add, asset viewport drag/drop, and the asset right-click Create Strand Spline Actor action all create `AStrandSplineActor` with exactly one native `USplineComponent`, used as root.
4. Confirm the original Strand asset artwork appears as an editor viewport billboard at the spline origin, with `Show Strand Billboard` and `Billboard Scale` working immediately.
5. Save/reopen a level, duplicate an actor, derive a Blueprint from the actor and reload it. Verify the billboard returns correctly and never renders in PIE or a packaged game.
6. Confirm Strand operations on the root spline, including assigned asset, Undo/Redo, placement modes, and closed-loop behavior remain intact.

## Version 0.4.3 verification priorities

1. **Quickly Add > Strand Spline Actor** creates the native-root Actor class. Drag-drop of a Strand asset creates the **same class**, prepopulated and linked.
2. **Spline Mesh Actor** / any `USplineMeshComponent`: Strand panel visible, two-point asset works. Multi-point and closed assets cannot be applied to the mesh segment.
3. **Strand Actions** are in the right Details column, with four **white** SVG icons; Unreal's usual disabled-state styling applies.
4. Blueprint editor: change Coordinate Mode, Center/Point Index, Apply, closed-loop state. Spline visuals update immediately in the Blueprint preview without Compile. Construction scripts may still override values.
5. Test existing subclass property customization; manually report if native specialized controls disappear.

This is a source candidate only. Build and viewport checks against UE 5.8.3 are still required.

## Shared-asset review (v0.4.6)

A Strand asset is reusable across many native spline components. Each reference is stored on the component as `UStrandSplineUserData`; editing the shape on one component never changes other instances unless you explicitly choose **Update Asset** and subsequently apply that asset to those instances.

- **Inspect Strand Usage...** reports each linked component in loaded level actors and whether its current point data matches the asset (after per-instance anchoring). It does not alter selection or geometry. Lists are capped at 25 entries; the totals remain complete.
- **Select Actors Using This Asset** selects loaded level actors containing linked native spline components. It is separate from the read-only usage report.
- **Update Asset** now shows linked actor/component counts and how many other loaded components currently differ from the asset. Other instances are never automatically overwritten.
- The Spline Component Details status is **In Sync** or **Different from asset**. A mismatch does not prove which side was edited most recently, so Strand does not label it as definitely a local modification.
- Unreal's own **Convert to Different Data Asset Type** is still available under general asset operations, not Strand-specific actions.

The Content Browser menu builds from asset metadata and loads the selected Strand asset only when an action is executed, reducing work caused by opening menus in large projects.

## Version 0.4.7 SVG fidelity update

The SVG importer now preserves split tangents on mixed straight/curved segments, explicit and implicit closing handles, compact arc flags, rounded rectangles, SVG ellipses/circles and internal `<use>` references. The original source file must be reimported to update old imported Strand assets, and actors must explicitly Apply the updated asset. **Absolute** places the original source coordinates in the spline component local space; to align multiple assets from one SVG, all actors must share the same actor/component transforms. Run `Automation RunTests Strand.Editor.Svg.ExactSegmentsAndClosures` and see [TEST_SVG_0.4.7.md](TEST_SVG_0.4.7.md).

## Version 0.4.11: SVG orientation, strict round-trip diagnostics

The supplied Illustrator **Digit-Icon.svg** reveals a horizontal mirror when SVG splines are viewed from above in Unreal: the original `1 2 3` reads `3 2 1`. New SVG imports now use a mirrored X coordinate by default, producing XY points `(-SVG X, -SVG Y, 0)` after the SVG's own nested transforms. **All Bézier handles are transformed through the same mapping**, preserving the curve and closure; the asset and actor transforms are unchanged. This mapping is a viewport-friendly convention, not world-space or viewport-camera tracking.

Each Strand asset has an editor-only **Mirror SVG X on Import** setting under **Strand > Import**. This is enabled by default; disable it and Reimport to keep v0.4.10's mapping `(SVG X, -SVG Y, 0)`. Reimport updates saved asset geometry only. Existing placed actors require **Apply Asset**. Set all actors to the same world transform, component transform, and **Absolute** coordinate mode when comparing disconnected paths from one SVG.

The multiline XML regression now uses an XML declaration followed by a normal line break, like real SVG exports. The Illustrator round-trip test remains strict and reports the first position, tangent, rotation, scale, or point-type mismatch rather than interpreting accurate evaluated shape as identical serialized component state. Native spline application also resets any inherited loop state before reconstructing point keys.

**New test:** `Automation RunTests Strand.Editor.Svg.DigitIconOrientation`. See [TEST_SVG_0.4.11.md](TEST_SVG_0.4.11.md). The reported UE 5.8.3 results for 0.4.10 were: analytic geometry **PASS**, multiline synthetic XML **FAIL**, and strict Illustrator point round-trip **FAIL**. The last two changes require another engine run; no success is assumed.

## Version 0.4.10: Complete wrapped SVG paths

SVG exporters often line-wrap a long `d="..."` attribute. In UE 5.8.3 testing, Strand v0.4.9 imported only the commands on the first physical line of the original Illustrator SVG, causing missing portions of its S-shaped curve and open rounded-square paths. v0.4.10 normalizes XML attribute whitespace before passing the SVG to Unreal's XML parser, allowing all commands, including `Z` closures, to be read.

The supplied original Illustrator fixture should import as **5 independent curves** with **9, 8, 8, 2, 2 segments**, and closed flags **open, closed, closed, open, open**. Follow [TEST_SVG_0.4.10.md](TEST_SVG_0.4.10.md). After installing the new plugin, reimport the SVG source and **Apply Asset** to existing level splines, or create fresh actors from fresh imports. Previously imported `.uasset` files retain their incomplete geometry until reimported.

## Version 0.4.9: SVG shape preservation

Strand's SVG importer stores exact per-segment cubic control derivatives. To allow native spline editing without forcing sharp joins to share one handle, Strand now enables the built-in **Allow Discontinuous Spline** property when an applied asset has differing custom arrive and leave tangents. Blueprint preview components inherit this editor setting. No custom spline type is created.

The importer logs the name, point count, segment count, and open/closed state of the first 32 curves. The supplied Illustrator logo has **five curves, 29 segments, two closed rounded squares and three intentionally open strokes**. Do not automatically close or join its center and outer arc strokes.

The independent `Tests/Fixtures/Strand_Illustrator_Logo_CubicReference.csv` file records the original SVG's 29 authored Bézier or line segments. Run `Automation RunTests Strand.Editor.Svg.IllustratorAnalyticGeometry` to sample the native Unreal spline at seven parametric positions along **every segment**, including the two closing curves, and to check the shapes under center-relative placement. This detects missing or distorted segments even when all native control points and loop flags appear correct.

See [TEST_SVG_0.4.9.md](TEST_SVG_0.4.9.md) for steps. Reimport after upgrading; existing placed actors do not update themselves automatically and require **Apply Asset**.


## Strand 0.5.0 additions (development candidate)

- **Multi-selection:** Select Strand actors in the Outliner to find a shared Strand section with counts and batch placement and assignment controls. Mixed values remain visibly mixed.
- **Multi Spline Actor:** Quickly Add > Strand Multi Spline Actor, or select multiple assets in Content Browser and choose **Create Strand Multi Spline Actor**. Uses one native spline root plus independent native Spline Components. Add spline components through the actor Details button.
- **Viewport multi-drop:** Dragging one Strand Spline asset places exactly one regular actor with no dialog; native Unreal preview placement determines its drop location. When two or more **different** Strand assets are dropped together, the editor asks only **after mouse release** whether to leave the separate actors or combine them into one multi-spline actor. This uses Unreal's completed-drop event rather than actor-factory preview spawns. No keeps all placed actors intact. Yes replaces only newly dropped, verified actors after successful multi-spline creation. The explicit asset context command remains available.
- **Add Spline Component and Assign:** Right-click one or more selected Strand assets to add one native spline per asset to each selected actor. This does not overwrite existing components.
- **SVG Canvas Origin:** In Project Settings > Plugins > Strand choose Top Left, Top Right, Bottom Left, Bottom Right, or Center. Applies the same source viewBox offset to all contours during import/reimport and does not modify existing level transforms.
- **Test curve generation:** Tools > Strand: Generate Helix and Braid Test Curves creates two Alembic files in `Saved/Strand/CurveImportTests`. The generator now outputs only native linear Alembic `OCurves`, not meshes. On Alembic assets generated here, disable Y-up import conversion because these fixtures are authored Z-up.
- **Asset editor proposal:** See `ASSET_EDITOR_DESIGN.md` for a native spline preview toolkit and safe source/instance editing workflow. This is not shipped functionality in v0.5.0.

See `TEST_PLAN_0.5.0.md` for required acceptance tests. The development source has not been compiled in Unreal Engine here.

## Strand 0.5.2 installation notes

Replace the **entire** `Plugins/Strand` directory with the packaged `Strand` folder; do not simply copy over it. UnrealBuildTool compiles every `.cpp` remaining under `Source`, including files removed from newer releases. In particular, the former `Source/StrandEditor/Private/SplineFbxImporter.cpp` must not remain. If you prefer an in-place upgrade, run `RemoveRetiredStrandSources.bat` after extracting the new source. This script touches only that obsolete Strand source file.

The **Add Spline** button on a Strand Multi Spline Actor is implemented in the `StrandEditor` Details customization and owns the Undo transaction. The runtime actor remains independent of `UnrealEd`.

## v0.5.4 placement regression

Viewport asset placement now leaves Unreal's drag preview and native drop lifecycle in control. A one-asset drag must never prompt about multiple spline assets, create hidden-Outliner actors, delete an existing actor, or duplicate a native spline. The post-drop grouping handler verifies distinct dropped assets and the resulting actors before any replacement. See [TEST_PLAN_0.5.4.md](TEST_PLAN_0.5.4.md).

When an Alembic curve is rejected because its basis or knot data cannot be interpreted, the import warning includes the actual Alembic schema details rather than attempting a speculative conversion.
