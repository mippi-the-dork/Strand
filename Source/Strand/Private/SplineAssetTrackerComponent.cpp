#include "SplineAssetTrackerComponent.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

USplineAssetTrackerComponent::USplineAssetTrackerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
}

USplineComponent* USplineAssetTrackerComponent::ResolveSpline() const
{
    if (IsValid(TrackedSpline) && TrackedSpline->GetOwner() == GetOwner()) { return TrackedSpline; }
    if (!GetOwner()) { return nullptr; }
    // Never silently pick the wrong native spline on multi-component actors.
    TArray<USplineComponent*> Splines;
    GetOwner()->GetComponents<USplineComponent>(Splines);
    return Splines.Num() == 1 ? Splines[0] : nullptr;
}

bool USplineAssetTrackerComponent::HasLocalChanges() const
{
    return !AssignedSplineAsset || !AssignedSplineAsset->MatchesSplineComponent(ResolveSpline());
}
