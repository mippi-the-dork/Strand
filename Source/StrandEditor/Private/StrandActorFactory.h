#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactory.h"
#include "StrandActorFactory.generated.h"

/** Enables dragging a Strand Spline asset straight from Content Browser to the Level viewport. */
UCLASS()
class UStrandActorFactory : public UActorFactory
{
    GENERATED_BODY()
public:
    UStrandActorFactory(const FObjectInitializer& ObjectInitializer);

    virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
    virtual bool CanPlaceElementsFromAssetData(const FAssetData& AssetData) override;
    virtual UClass* GetDefaultActorClass(const FAssetData& AssetData) override;
    virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;

protected:
    virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
    virtual FString GetDefaultActorLabel(UObject* Asset) const override;
};
