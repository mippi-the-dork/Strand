#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"
#include "Engine/DataAsset.h"

#include "SplineDataAsset.generated.h"

class UAssetImportData;

/** One spline-local point. No actor/component transform or scene placement is stored. */
USTRUCT(BlueprintType)
struct STRAND_API FSplinePointData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") FVector LocalPosition = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") FVector ArriveTangent = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") FVector LeaveTangent = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") FRotator LocalRotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") FVector LocalScale = FVector::OneVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spline") TEnumAsByte<ESplinePointType::Type> PointType = ESplinePointType::Curve;

    FSplinePoint ToSplinePoint(int32 Index) const;
};

UCLASS(BlueprintType)
class STRAND_API USplineDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spline") TArray<FSplinePointData> Points;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spline") bool bIsClosedLoop = false;

#if WITH_EDITORONLY_DATA
    /** Original external file and source curve identity, for deterministic per-curve reimport. */
    UPROPERTY(VisibleAnywhere, Instanced, Category="Strand|Source")
    TObjectPtr<UAssetImportData> AssetImportData;

    UPROPERTY(VisibleAnywhere, Category="Strand|Source")
    FString SourceCurveId;

    UPROPERTY(VisibleAnywhere, Category="Strand|Source")
    FString SourceImportFormat;

    /** Source coordinate units to Unreal centimeters; Alembic does not standardize units. */
    UPROPERTY(EditAnywhere, Category="Strand|Import", meta=(ClampMin="0.000001"))
    float SourceUnitsToCentimeters = 1.0f;

    /** Convert conventional right-handed Y-up Alembic curves into Unreal's Z-up space. */
    UPROPERTY(EditAnywhere, Category="Strand|Import")
    bool bAlembicYUp = true;

    /** Alembic sample time; a Strand spline asset holds one static sampled curve. */
    UPROPERTY(EditAnywhere, Category="Strand|Import", meta=(ClampMin="0.0"))
    double ImportSampleSeconds = 0.0;
#endif

#if WITH_EDITOR && WITH_EDITORONLY_DATA
    virtual void PostInitProperties() override;
#endif

    /** Reads points in spline-local coordinates, including independent incoming/outgoing tangents. */
    UFUNCTION(BlueprintCallable, Category="Strand")
    void SerializeFromSplineComponent(const USplineComponent* SourceComponent);

    /** Replaces target points but never changes the actor or component transform. */
    UFUNCTION(BlueprintCallable, Category="Strand")
    void ApplyToSplineComponent(USplineComponent* TargetComponent) const;

    UFUNCTION(BlueprintPure, Category="Strand")
    bool MatchesSplineComponent(const USplineComponent* Component, float Tolerance = 0.01f) const;
};
