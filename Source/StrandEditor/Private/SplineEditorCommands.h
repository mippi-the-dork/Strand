#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineDataAsset;
class USplineComponent;
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
    void ProjectComponentOntoScene(USplineComponent* Spline, const FStrandProjectionOptions& Options);
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
