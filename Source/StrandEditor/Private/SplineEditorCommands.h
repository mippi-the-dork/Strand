#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineDataAsset;
class USplineComponent;
class USplineAssetTrackerComponent;

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
    void CompareTracker(USplineAssetTrackerComponent* Tracker);
    void BrowseTrackerAsset(USplineAssetTrackerComponent* Tracker);
    void ResetTracker(USplineAssetTrackerComponent* Tracker);
    void UpdateTracker(USplineAssetTrackerComponent* Tracker);
}
