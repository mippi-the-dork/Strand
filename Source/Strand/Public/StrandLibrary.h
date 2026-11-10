#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StrandLibrary.generated.h"

class USplineDataAsset;
class USplineComponent;

UCLASS()
class STRAND_API UStrandLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Strand", meta=(DefaultToSelf="TargetComponent"))
    static bool ApplySplineDataAsset(const USplineDataAsset* Asset, USplineComponent* TargetComponent);
};
