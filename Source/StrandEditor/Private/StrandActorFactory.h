#pragma once

#include "CoreMinimal.h"
#include "ActorFactories/ActorFactory.h"
#include "StrandActorFactory.generated.h"

class USplineDataAsset;

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
    // No BeginPlacement / EndPlacement overrides: the engine uses this factory
    // to spawn temporary drag previews as well as finalized actors.
    // Post-drop grouping is owned by StrandEditor via OnNewActorsDropped.
protected:
    virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
    virtual FString GetDefaultActorLabel(UObject* Asset) const override;
};

/** Quick Add entry for the same native multi-spline actor used by grouped asset placement. */
UCLASS()
class UStrandMultiActorFactory : public UActorFactory
{
    GENERATED_BODY()
public:
    UStrandMultiActorFactory(const FObjectInitializer& ObjectInitializer);
    virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
    virtual bool CanPlaceElementsFromAssetData(const FAssetData& AssetData) override;
    virtual UClass* GetDefaultActorClass(const FAssetData& AssetData) override;
};
