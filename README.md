# Strand

**Spline data assets for Unreal Engine 5.5+**

Strand stores native Unreal spline point configurations as reusable Content Browser assets. It is **not** a new spline primitive, editor mode, rendering system or spline mesh generator. Existing `USplineComponent` actors, PCG and Landscape spline systems remain responsible for editing and using curves.

**Version:** 0.2.0 source candidate, editor build not yet validated. **Platform:** UE 5.5+ source candidate; UE 5.8.3 should be verified first. **Dependencies:** PCG and Unreal's Alembic Importer plugins, existing native engine FBX SDK (optional legacy importer). **Modules:** Strand (runtime), StrandPCG (PCG runtime), StrandEditor (editor).

## Install

Copy the whole `Strand` folder to `<Project>/Plugins/Strand`, enable the plugin and PCG and Alembic Importer dependencies, regenerate project files and build your Editor target. The Alembic C++ SDK is accessed through Unreal's existing `AlembicLib` and `AlembicLibrary` modules; **do not bundle a second Alembic SDK**.

The plugin package does not include prebuilt binaries. An Unreal Engine SDK/compiler is not present in this authoring environment, so the source is not yet compile-verified. See [TEST_PLAN.md](TEST_PLAN.md).

## Key workflows

### Create an asset from a native spline

1. Select an Actor with a `USplineComponent`.
2. Right-click the actor in the Level Editor, then choose **Convert Spline to Spline Data Asset**.
3. Select the Content Browser asset path in Unreal's standard save dialog.
4. Strand records points and closed-loop state and adds a **Spline Asset Tracker** to the actor, referencing the asset and source component.
5. Keep editing the original spline in the standard Unreal viewport.

For actors with multiple spline components, add a **Spline Asset Tracker** and select the intended **Tracked Spline** component explicitly. Never infer which of multiple splines was intended. Existing actor/component transforms are untouched.

### Apply an asset to an existing spline

1. Select a Strand **Spline Data Asset** in the Content Browser.
2. Select a level actor with an unambiguous spline component, or a tracker with an explicitly configured target.
3. Right-click the actor > **Apply Selected Spline Data Asset**. Alternatively right-click the asset > **Apply to Selected Level Spline Actor(s)**.
4. Confirm the overwrite. The asset is assigned to the tracker so subsequent changes can be compared, reset and saved.

### Edit, compare, reset, update

Select an actor's **Spline Asset Tracker** component in Details. It shows **Assigned Spline Asset** and **Tracked Spline** references and a read-only sync status:

* **Compare Local vs Asset:** point counts, number of differing points, first difference, maximum positional delta and loop mismatch. No mutation.
* **Browse to Data Asset:** locate the associated asset in the Content Browser.
* **Reset to Spline Data Asset:** confirm and replace the local component's points with the asset's points.
* **Update Spline Data Asset:** confirm and replace the asset's points with the component's local points, saving the `.uasset`.

Reset and Update use `FScopedTransaction` for Undo/Redo. The current Update implementation explicitly saves the package after modification; after Undo, **save again** to persist the in-memory Undo state to disk. Project source control permissions are still respected by Unreal.

No automatic synchronization occurs when an artist moves a spline handle. There is no hidden live-binding that could unexpectedly modify other actors or shared assets.

### Runtime and PCG

The runtime C++/Blueprint library, `UStrandLibrary::ApplySplineDataAsset`, explicitly applies a Strand asset to an existing `USplineComponent` and preserves target transforms. `UStrandPCGLibrary::ApplySplineDataAssetToPcg` creates a transient `UPCGSplineData` using the stored native `FSplinePoint` values and a **caller-provided** transform. A PCG graph integration can consume that object; Strand does not override native graph behavior. Both libraries operate on native spline data.

### Landscape Splines

The Landscape context actions import an existing Landscape connected path into a data asset or apply an asset to a **topology-compatible** path, preserving existing segment references, meshes, widths and terrain settings. This is not identical to `USplineComponent`: Landscape curves expose scalar tangent handles and topological connections. Multi-branching or mismatched paths are deliberately refused rather than destroyed/recreated. Save a backup before converting important road/terrain networks.

## Import files

Use **Tools > Strand: Import Alembic Curves...**, **Import SVG Curves...**, or **Import Legacy FBX Curves...**. Strand uses a dedicated factory for each format with a low default priority to avoid hijacking the engine's regular `.abc` Groom/mesh and `.fbx` mesh importers. Selected curves are imported to the active single selected Content Browser folder, or `/Game` if no valid folder is selected.

Each **disconnected curve** becomes its own `USplineDataAsset`. The first is returned to Unreal's import system, while sibling assets are separately registered; save all newly imported siblings with **Save All**.

### SVG 2D

Supports `<path>`, `<line>`, `<polyline>` and `<polygon>`; `M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z`, nested transforms, Bezier handles and ellipse arcs. Projects to local XY with Z=0 and inverts the downward-positive SVG Y axis. One SVG unit is treated as one centimeter. The importer does not interpret CSS, strokes, `<use>` references or `viewBox` presentation sizing.

### Alembic ABC 3D

Traverses native `Alembic::AbcGeom::ICurves` objects recursively, using the sample at `ImportSampleSeconds` (default zero). Recognizes:

* **Linear** curves: exact native spline point chains.
* **Cubic Bezier**: exact cubic control handles converted to Unreal Hermite derivatives (`3 * (handle - anchor)`).
* **Catmull-Rom**: smoothed Hermite interpretation (not guaranteed parameterization-identical across DCC applications).
* **Cubic uniform B-spline**: evaluated into bounded Hermite samples. This is an approximation.
* **Variable order NURBS:** rational de Boor evaluator for consistent knot/weight layouts, then approximated as Hermite samples.

Unsupported basis functions, malformed knot layouts and missing curves produce Output Log warnings; Strand never deliberately replaces them with unrelated geometry silently.

Alembic has **no standard scene unit**. The default import is **one Alembic coordinate unit equals one Unreal centimeter**, and `SourceUnitsToCentimeters` on the asset can be changed before Reimport (for example, `100` if exported in meters). `bAlembicYUp` defaults to true and remaps source Y-up to Unreal Z-up. Parent Alembic `IXform` transforms are **not baked** into local curve points, avoiding scene placement leaking into reusable spline data. If a DCC stores shape-defining scale/rotation in `IXform`, bake/apply those transforms to the source curves before export. Frame animation is not stored: one configured static time sample is imported.

**Collision note:** Unreal's native Alembic importer is intended for other content (mesh caches and Grooms). Use Strand's named Tools command to explicitly select its curve importer instead of relying on generic `.abc` import auto-selection. Unreal's Groom importer itself uses the `ICurves` schema, but Strand does not create Groom assets.

### FBX legacy compatibility

An explicit **Tools > Strand: Import Legacy FBX Curves...** command and low-priority **Strand Legacy Curves (FBX)** factory remain for exported `FbxNurbsCurve` and `FbxLine` data. This is a fallback for DCCs that actually emit those FBX curve node types, **not the recommended primary source format**. Standard FBX mesh/skeletal workflows should remain with Unreal's default factory. FBX assets imported through this legacy path do not currently participate in the SVG/Alembic reimport handler.

### Reimport behavior

SVG and Alembic assets record a source filename and deterministic `SourceCurveId`. Using the Content Browser's standard **Reimport** feature loads that file, finds the matching source curve and replaces **only that asset's stored points**. Reimport:

* Shows confirmation before overwriting the shared asset.
* Does **not** automatically overwrite level actors or active Blueprint spline components.
* Preserves the asset and fails if the expected curve is missing, renamed or reordered.
* Retains edited Alembic conversion settings in the Strand asset (unit scale, Y-axis mapping and static sample time).
* Records the import source in `UAssetImportData`.

Review any tracked spline instances and intentionally Reset/Apply when you want them updated. Reimport does not perform a three-way merge of local viewport edits with external DCC edits.

## Representation and compatibility

`USplineDataAsset` derives from `UPrimaryDataAsset` and stores `Points: TArray<FSplinePointData>` plus `bIsClosedLoop`. Each point holds `LocalPosition`, `ArriveTangent`, `LeaveTangent`, `LocalRotation`, `LocalScale` and `PointType`. Actor and component placement are **never stored**. Stored spline input keys are normalized to point indices (0, 1, 2, ...).

| Existing Unreal system | Strand handling | Limitations |
|---|---|---|
| `USplineComponent` | Native read/write, exact stored fields | Additional component settings and custom spline metadata not captured |
| Blueprint classes containing `USplineComponent` | Same native component adapter | Construction Scripts can regenerate and overwrite points; integrate explicitly |
| `UPCGSplineData` | Runtime adapter with explicit transform | Not an automatically installed PCG graph node |
| Landscape Splines | Editor path export/apply | Only validated compatible nonbranching paths; tangent handle approximation |
| Specialized `USplineComponent` subclasses, including Water | Inherits standard geometry operations | Does not capture system-specific width/depth/metadata fields |
| `USplineMeshComponent` | Can continue consuming positions/tangents from a parent spline | Spline Mesh stores endpoints, not a full spline; native mesh rebuild remains its owner's job |
| Native spline editing tools | Unchanged | Strand adds asset tracking rather than overriding spline interaction |

The architecture is intentionally built around adapters and the standard engine spline data representation, not a custom spline runtime. The proposed next-stage adapter API, status overlays, mass operations and metadata schema are specified in [FEATURE_ROADMAP.md](FEATURE_ROADMAP.md).

## Source migration from the original prototype

Previous prototype package name: `SplineAssetPipeline`; new plugin and module names: `Strand`, `StrandPCG`, `StrandEditor`. Unreal class and struct references may require Core Redirects when loading assets created using an old compiled plugin. A suggested migration checklist and redirects are in [MIGRATION.md](MIGRATION.md). Keep backups before migration.

## Current verification status

The ZIP has been checked for expected file structure and descriptor integrity, but **it has not been compiled against real Unreal Engine 5.5 through 5.8 headers or run in-editor**. The engine-version API boundaries most likely to require a local correction pass are Alembic's engine-bundled libraries, PCG, Landscape and Content Browser ToolMenus. Do not use in a shipping project before running [TEST_PLAN.md](TEST_PLAN.md) on an actual engine install.
