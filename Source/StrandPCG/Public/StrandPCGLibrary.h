#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StrandPCGLibrary.generated.h"

class UPCGSplineData;
class USplineDataAsset;

/** PCG adapter. Output is transient spatial data that can be supplied to PCG graph consumers. */
UCLASS()
class SPLINEASSETPIPELINEPCG_API UStrandPCGLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Strand|PCG", )
    static UPCGSplineData* ApplySplineDataAssetToPcg(UObject* Outer, const USplineDataAsset* Asset, FTransform Transform);
};
