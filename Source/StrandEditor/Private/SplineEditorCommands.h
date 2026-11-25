#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineDataAsset;
class USplineComponent;
class SWindow;
enum class EStrandCoordinateMode : uint8;
enum class EStrandAnchorMode : uint8;

enum class EStrandProjectionTargetScope : uint8;
enum class EStrandProjectionRayMode : uint8;

struct FStrandProjectionOptions
{
    EStrandProjectionTargetScope TargetScope;
    EStrandProjectionRayMode RayMode;
    TWeakObjectPtr<AActor> Target;
    TArray<TWeakObjectPtr<AActor>> Targets;
    FVector WorldDirection = FVector(0, 0, -1);
    float MaxDistance = 10000.0f;
    float Clearance = 0.0f;
    float Strength = 1.0f;
    int32 FirstAnchor = 0;
    int32 LastAnchor = -1;
    bool bAlignNormals = false;
    // Continuous conform adaptively inserts spline points along eligible spans.
    bool bContinuousConform = false;
    float ConformMaxSpacing = 100.0f; // cm along original spline
    float ConformTolerance = 2.0f;  // cm maximum midpoint deviation
    int32 ConformMaxNewPoints = 1024; // maximum inserted points; preview becomes best-effort when reached
    bool bConformSmoothTangents = true;
    // Asset editor only: bake the temporary actor's world transform into saved
    // point geometry, rather than returning points in the temporary local frame.
    bool bBakeWorldPlacementToAsset = false;
    // Editor-only snapshot of selection/soft-selection influence. Empty means every eligible anchor.
    TArray<double> AnchorWeights;
    bool bUseAnchorWeights = false;
};

namespace SplineEditorCommands
{
    TArray<AActor*> SelectedActors();
    USplineComponent* FindUnambiguousSpline(AActor* Actor);
    USplineDataAsset* SelectedSplineAsset();
    bool SaveAsset(USplineDataAsset* Asset);
    void ImportCurves(bool bAlembic);
    void ConvertSelectedActors();
    void ApplySelectedAssetToActors();
    void ApplyAssetToSelectedActors(TWeakObjectPtr<USplineDataAsset> Asset);
    void ApplySelectedAssetToLandscape();
    void ConvertSelectedLandscape();
    void CompareComponent(USplineComponent* Spline);
    void SetComponentPlacement(USplineComponent* Spline,
        EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorPointIndex);
    void ResetComponent(USplineComponent* Spline);
    void UpdateComponentAsset(USplineComponent* Spline);
    void ProjectComponentOntoScene(USplineComponent* Spline, const FStrandProjectionOptions& Options,
        TFunction<void(USplineComponent*, const TArray<int32>&)> OnApplied = {}, TSharedPtr<SWindow> ParentWindow = nullptr);
    void AssignAssetToComponent(TWeakObjectPtr<USplineComponent> Component, TWeakObjectPtr<USplineDataAsset> Asset);

    // Native, editor-only creation and assignment workflows.
    void CreateNativeActorFromAsset(TWeakObjectPtr<USplineDataAsset> Asset);
    void SelectActorsUsingAsset(TWeakObjectPtr<USplineDataAsset> Asset);
    // Read-only overview of loaded level users and divergence from the source asset.
    void InspectAssetUsage(TWeakObjectPtr<USplineDataAsset> Asset);
    void ApplyAssetToComponent(TWeakObjectPtr<USplineDataAsset> Asset, TWeakObjectPtr<USplineComponent> Component);
    void CaptureComponent(TWeakObjectPtr<USplineComponent> Component);
    void AddNativeSplineAndApplyToSelected();
    TArray<USplineDataAsset*> SelectedSplineAssets();
    void AddSplineComponentsAndAssign(const TArray<USplineDataAsset*>& Assets);
    void CreateMultiActorFromAssets(const TArray<USplineDataAsset*>& Assets);
    void ApplyPlacementToComponents(const TArray<TWeakObjectPtr<USplineComponent>>& Splines,
        EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorPointIndex);
    void AssignAssetToComponents(const TArray<TWeakObjectPtr<USplineComponent>>& Splines, USplineDataAsset* Asset);

}
