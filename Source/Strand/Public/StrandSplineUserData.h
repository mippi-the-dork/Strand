#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "StrandSplinePlacement.h"
#include "StrandSplineUserData.generated.h"

class USplineDataAsset;
class USplineComponent;
class USplineMeshComponent;

/** Serialized, per-component Strand link. No additional actor component or spline class is needed. */
UCLASS(BlueprintType, EditInlineNew)
class STRAND_API UStrandSplineUserData : public UAssetUserData
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strand")
    TObjectPtr<USplineDataAsset> AssignedSplineAsset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strand|Placement")
    EStrandCoordinateMode CoordinateMode = EStrandCoordinateMode::Absolute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strand|Placement")
    EStrandAnchorMode AnchorMode = EStrandAnchorMode::PointIndex;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strand|Placement")
    int32 AnchorPointIndex = 0;

    /** Spline meshes store only a single Hermite segment, using the same metadata class. */
    static UStrandSplineUserData* GetOnSplineMesh(const USplineMeshComponent* Mesh);
    static USplineDataAsset* GetAssignedAssetOnSplineMesh(const USplineMeshComponent* Mesh);
    static void AssignAssetToSplineMesh(USplineMeshComponent* Mesh, USplineDataAsset* Asset);

    UFUNCTION(BlueprintPure, Category="Strand|Placement")
    bool IsInSync(const USplineComponent* Spline) const;


    /** Retrieve the Strand metadata already stored on a native spline component. */
    UFUNCTION(BlueprintPure, Category="Strand", meta=(DisplayName="Get Strand Spline Data"))
    static UStrandSplineUserData* Get(const USplineComponent* Spline);

    /** Retrieve an assigned asset without requiring an additional component. */
    UFUNCTION(BlueprintPure, Category="Strand")
    static USplineDataAsset* GetAssignedAsset(const USplineComponent* Spline);

    /** Transaction-friendly association. This changes the reference, never spline geometry. */
    UFUNCTION(BlueprintCallable, Category="Strand")
    static void AssignAsset(USplineComponent* Spline, USplineDataAsset* Asset);
};
