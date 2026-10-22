#include "StrandActorFactory.h"
#include "StrandPlacement.h"
#include "StrandSplineActor.h"
#include "StrandSplineRefresh.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "AssetRegistry/AssetData.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "StrandActorFactory"

UStrandActorFactory::UStrandActorFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DisplayName = LOCTEXT("DisplayName", "Strand Spline Actor");
    NewActorClass = AStrandSplineActor::StaticClass();
    bUseSurfaceOrientation = false;
    bShowInEditorQuickMenu = true;
    MenuPriority = 1;
    // We add this factory to the editor's registry explicitly at PostEngineInit.
    bShouldAutoRegister = false;
}

bool UStrandActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
    if (!AssetData.IsValid() || AssetData.AssetClassPath == USplineDataAsset::StaticClass()->GetClassPathName())
    {
        return true;
    }
    OutErrorMsg = LOCTEXT("WrongAsset", "Select a Strand Spline asset.");
    return false;
}

bool UStrandActorFactory::CanPlaceElementsFromAssetData(const FAssetData& AssetData)
{
    FText Error;
    // Empty data is a valid native Strand actor placement through Quick Add.
    return CanCreateActorFrom(AssetData, Error);
}

UClass* UStrandActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
    // The editor may ask for factories that can create arbitrary AActors using empty
    // asset data. Decline those requests rather than claiming every generic actor.
    FText Error;
    return CanCreateActorFrom(AssetData, Error) ? AStrandSplineActor::StaticClass() : nullptr;
}

UObject* UStrandActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
    if (!IsValid(ActorInstance)) { return nullptr; }
    TArray<USplineComponent*> Components;
    ActorInstance->GetComponents<USplineComponent>(Components);
    USplineDataAsset* OnlyAsset = nullptr;
    for (USplineComponent* Component : Components)
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Component);
        if (!Asset) { continue; }
        if (OnlyAsset && OnlyAsset != Asset) { return nullptr; }
        OnlyAsset = Asset;
    }
    return OnlyAsset;
}

void UStrandActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
    USplineDataAsset* Strand = Cast<USplineDataAsset>(Asset);
    if (!IsValid(NewActor)) { return; }
    // Quick Add produces the same actor class, but starts with an unlinked native spline.
    if (!IsValid(Strand)) { return; }
    // No custom spline representation: this is a plain actor and native spline component.
    TArray<USplineComponent*> Splines;
    NewActor->GetComponents<USplineComponent>(Splines);
    USplineComponent* Spline = Splines.IsEmpty() ? StrandPlacement::AddNativeSpline(NewActor) : Splines[0];
    if (!Spline) { return; }
    Strand->ApplyToSplineComponentWithPlacement(Spline,
        Strand->DefaultCoordinateMode, Strand->DefaultAnchorMode, Strand->DefaultAnchorPointIndex);
    UStrandSplineUserData::AssignAsset(Spline, Strand);
    StrandSplineRefresh::NotifySplineChanged(Spline);
    NewActor->MarkPackageDirty();
}

FString UStrandActorFactory::GetDefaultActorLabel(UObject* Asset) const
{
    return IsValid(Asset) ? FString::Printf(TEXT("Strand_%s"), *Asset->GetName()) : TEXT("Strand Spline Actor");
}

#undef LOCTEXT_NAMESPACE
