#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SplineAssetTrackerComponent.generated.h"

class USplineDataAsset;
class USplineComponent;

/** Attach to an actor with a spline. The reference and spline association are instance-editable. */
UCLASS(ClassGroup=(Spline), meta=(BlueprintSpawnableComponent))
class STRAND_API USplineAssetTrackerComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USplineAssetTrackerComponent();

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Strand")
    TObjectPtr<USplineDataAsset> AssignedSplineAsset;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Strand")
    TObjectPtr<USplineComponent> TrackedSpline;

    UFUNCTION(BlueprintPure, Category="Strand")
    USplineComponent* ResolveSpline() const;

    UFUNCTION(BlueprintPure, Category="Strand")
    bool HasLocalChanges() const;
};
