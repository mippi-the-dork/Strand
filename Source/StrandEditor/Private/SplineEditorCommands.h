#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineDataAsset;
class USplineComponent;
enum class EStrandCoordinateMode : uint8;
enum class EStrandAnchorMode : uint8;

namespace SplineEditorCommands
{
    TArray<AActor*> SelectedActors();
    USplineComponent* FindUnambiguousSpline(AActor* Actor);
    USplineDataAsset* SelectedSplineAsset();
    bool SaveAsset(USplineDataAsset* Asset);
    void ImportCurves(bool bAlembic, bool bLegacyFbx = false);
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
    void AssignAssetToComponent(TWeakObjectPtr<USplineComponent> Component, TWeakObjectPtr<USplineDataAsset> Asset);

    // Native, editor-only creation and assignment workflows.
    void CreateNativeActorFromAsset(TWeakObjectPtr<USplineDataAsset> Asset);
    void SelectActorsUsingAsset(TWeakObjectPtr<USplineDataAsset> Asset);
    // Read-only overview of loaded level users and divergence from the source asset.
    void InspectAssetUsage(TWeakObjectPtr<USplineDataAsset> Asset);
    void ApplyAssetToComponent(TWeakObjectPtr<USplineDataAsset> Asset, TWeakObjectPtr<USplineComponent> Component);
    void CaptureComponent(TWeakObjectPtr<USplineComponent> Component);
    void AddNativeSplineAndApplyToSelected();

}
