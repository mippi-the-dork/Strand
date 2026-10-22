# Strand

**Reusable spline assets for Unreal Engine 5.5+**

Strand stores **native `USplineComponent` point data** as Content Browser assets (`USplineDataAsset`). It does not replace Unreal spline editing, introduce a competing spline component, or generate meshes. A Strand asset stores local positions, arrive/leave tangents, rotations, scales, point types and the closed-loop flag, **never the actor or component transform**.

**Version:** 0.4.3 source candidate. **Validated against a live UE Editor build:** Not yet; first build target UE 5.8.3. **Modules:** Strand (runtime), StrandPCG (PCG runtime), StrandEditor (editor). Dependencies: Unreal PCG and Alembic Importer plugins.

## Quick start: drag directly into the Level Editor

1. Import an SVG/ABC using **Tools > Strand: Import SVG Curves...** or **Import Alembic Curves...**. Each disconnected curve receives its own **Strand Spline** asset.
2. Drag one **Strand Spline** asset from the Content Browser into the **Level viewport**, and drop it where you want it.
3. Strand's native `UActorFactory` integration creates an `AStrandSplineActor` (a lightweight normal Actor containing a stock `USplineComponent` as its root) and assigns the Strand asset to that component's Asset User Data.
4. Select the new actor's **Spline Component**. Edit its curve normally with Unreal's viewport spline controls. No additional tracking component appears.
5. In the **Spline Component Details > Strand** category, use the asset picker and the **Apply Asset**, **Update Asset**, **Save as New** or **Compare** buttons.

**Quick Add:** In a Level Editor viewport choose **Quickly Add > Strand Spline Actor** (also available under Place Actors > Basic). This creates the exact same `AStrandSplineActor` class as a dropped Strand asset, but with an unlinked native spline to edit or assign later. The existing **Spline Test** entry is not part of Strand and remains untouched.

The actor factory is explicitly registered with UE 5.8's `UPlacementSubsystem` as well as the legacy editor factory registry. It accepts only `USplineDataAsset` and does not intercept other Content Browser asset types. It uses Unreal's normal asset placement transform, so the actor should appear at the drop position. The right-click **Create Native Spline Actor at World Origin** command remains available as a fallback.

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

**Coordinate behavior:** All stored Strand point positions remain local to the source spline, even when "Absolute" is selected. "Absolute" means *preserve the asset's local coordinates*, not world-space coordinates. "Relative" subtracts either an existing point's location or the average of all point locations; it never writes any Actor or Component transform. The anchor options live on each native spline, and asset-level defaults live on the asset itself. Imported SVG/ABC/FBX curves and manually created new assets default to **Relative + Center** for intuitive viewport drops, including assets made before placement options existed. Native spline assets captured from Unreal are explicitly set to **Absolute**, preserving original shape offsets. Updating an asset from an anchored instance reverses the placement offset, so the source coordinate frame isn't unintentionally erased.

**Blueprint viewport:** Strand now invalidates the native spline render state and refreshes matching Blueprint preview spline components directly, even when the preview world is not in the normal engine world-context list after Apply/placement adjustments, without requiring a manual Compile to see open/closed curve changes. Construction scripts that independently overwrite spline points may still control their final shape.

Asset assignment is serialized as a tiny **`UStrandSplineUserData : UAssetUserData`** instance owned by each native spline. It is *not* an actor component and does not require changing Unreal engine source or subclassing `USplineComponent`. Assignment alone does not silently replace local geometry. Apply and Update are explicit and confirmation-gated. Operations use `FScopedTransaction` for editor Undo/Redo. `Update Asset` also saves the `.uasset`; after undoing it, save again to persist that reversal.

**Spline Mesh Components:** Strand also adds a Details category on native `USplineMeshComponent` (including loaded derived types). A spline mesh is one deformable two-endpoint section, *not* a multi-point USplineComponent. It can save its endpoint positions/tangents as a two-point Strand asset and apply a compatible two-point open asset. Multi-point and closed assets are explicitly rejected rather than truncated. Existing Static Mesh, materials, collision, mesh-specific roll/scale and other properties remain native. Update from a spline mesh replaces properties that the 2-point Strand representation does not carry (the confirmation warns).

**Compatibility detail:** Strand registers Details customization for currently loaded native spline subclasses, plus native Spline Mesh classes. This can interact with other plugins' own custom class layouts; test the relevant specialized components in the installed editor. Strand does not change Water/Cine/PCG-specific metadata.

## Existing actors and multiple splines

- Select a Strand asset, right-click an actor with one spline and choose **Apply Selected Spline Data Asset**. It overwrites points after confirmation and assigns the asset to that component.
- Right-click an actor with no spline and choose **Add Native Spline Component from Selected Strand**. Strand adds an ordinary `USplineComponent`, respecting the existing actor root.
- Actors containing several splines offer individually named **Save [Component] as Strand Spline** and **Apply Selected Strand to [Component]** actions. Strand never arbitrarily targets one of several splines.
- Right-click a Strand asset and choose **Apply to Selected Level Spline Actor(s)** or **Select Loaded Actors Using This Strand**. The latter scans native spline Asset User Data, but does not include unloaded World Partition cells or Blueprint class defaults.
- Select an existing actor with a spline and choose **Convert Spline to Spline Data Asset** to create a new linked Strand asset.

## Import and reimport

- **SVG (.svg):** 2D paths/lines/polylines/polygons, Bezier curves, quadratic curves, smooth commands, elliptical arcs, closed paths and nested transforms. Stored in local XY with Z=0.
- **Alembic (.abc):** `ICurves` geometry with supported linear, Bezier, Catmull-Rom, B-spline and NURBS forms; nested curve identities preserved for source-aware reimport. Alembic units must be configured for the source application.
- **Legacy FBX (.fbx):** optional explicit importer for native FBX curve nodes. Strand does not replace normal FBX mesh import.
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

## Version 0.4.3 verification priorities

1. **Quickly Add > Strand Spline Actor** creates the native-root Actor class. Drag-drop of a Strand asset creates the **same class**, prepopulated and linked.
2. **Spline Mesh Actor** / any `USplineMeshComponent`: Strand panel visible, two-point asset works. Multi-point and closed assets cannot be applied to the mesh segment.
3. **Strand Actions** are in the right Details column, with four **white** SVG icons; Unreal's usual disabled-state styling applies.
4. Blueprint editor: change Coordinate Mode, Center/Point Index, Apply, closed-loop state. Spline visuals update immediately in the Blueprint preview without Compile. Construction scripts may still override values.
5. Test existing subclass property customization; manually report if native specialized controls disappear.

This is a source candidate only. Build and viewport checks against UE 5.8.3 are still required.
