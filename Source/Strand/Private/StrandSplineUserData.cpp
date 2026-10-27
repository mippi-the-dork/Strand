#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "Components/ActorComponent.h"
#include "Components/SplineMeshComponent.h"


bool UStrandSplineUserData::EnsureComponentOwned(UActorComponent* Component)
{
    if (!IsValid(Component)) { return false; }
    UStrandSplineUserData* Source = Cast<UStrandSplineUserData>(
        Component->GetAssetUserDataOfClass(StaticClass()));
    if (!IsValid(Source)) { return false; }

    // There are TWO cases that can cause a private Blueprint reference:
    //  (1) A placed component retains the template's user-data POINTER.
    //  (2) The instance owns a duplicated user-data object, but that object's
    //      ARCHETYPE still points to the private Blueprint template user data.
    // The second case produces the save error with "Unknown property" because
    // the illegal reference is the UObject archetype, not a reflected property.
    // In both cases create a fresh instance whose archetype is the Strand CDO.
    const bool bOwnedHere = (Source->GetOuter() == Component);
    const bool bSafeArchetype = (Source->GetArchetype() == StaticClass()->GetDefaultObject());
    if (bOwnedHere && bSafeArchetype) { return false; }

    // A cleared Blueprint assignment can leave an empty metadata subobject on
    // an already-placed instance. An empty link has no placement semantics;
    // drop that Strand-only entry instead of preserving a dangling subobject.
    if (!IsValid(Source->AssignedSplineAsset.Get()))
    {
        Component->Modify();
        Component->RemoveUserDataOfClass(StaticClass());
        Component->MarkPackageDirty();
        return true;
    }

    // Do NOT DuplicateObject or NewObject with Source as the template:
    // either operation may preserve the private Blueprint archetype.
    // Construct from the Strand class CDO, and copy only the actual payload.
    USplineDataAsset* Asset = Source->AssignedSplineAsset.Get();
    const EStrandCoordinateMode Coordinate = Source->CoordinateMode;
    const EStrandAnchorMode Anchor = Source->AnchorMode;
    const int32 AnchorIndex = Source->AnchorPointIndex;

    Component->Modify();
    UStrandSplineUserData* Local = NewObject<UStrandSplineUserData>(
        Component, StaticClass(), NAME_None, RF_Transactional);
    Local->AssignedSplineAsset = Asset;
    Local->CoordinateMode = Coordinate;
    Local->AnchorMode = Anchor;
    Local->AnchorPointIndex = AnchorIndex;

    // Only Strand's entry is replaced. Other tools' user data remains intact.
    Component->RemoveUserDataOfClass(StaticClass());
    Component->AddAssetUserData(Local);
    Component->MarkPackageDirty();
    return true;
}

UStrandSplineUserData* UStrandSplineUserData::Get(const USplineComponent* Spline)
{
    if (!IsValid(Spline)) { return nullptr; }
    // IInterface_AssetUserData's getter is non-const; this lookup does not mutate the component.
    return Cast<UStrandSplineUserData>(const_cast<USplineComponent*>(Spline)->GetAssetUserDataOfClass(StaticClass()));
}

USplineDataAsset* UStrandSplineUserData::GetAssignedAsset(const USplineComponent* Spline)
{
    const UStrandSplineUserData* Data = Get(Spline);
    return IsValid(Data) ? Data->AssignedSplineAsset.Get() : nullptr;
}

void UStrandSplineUserData::AssignAsset(USplineComponent* Spline, USplineDataAsset* Asset)
{
    if (!IsValid(Spline)) { return; }
    // Clearing the Strand asset must remove Strand metadata itself, even if its
    // AssignedSplineAsset field is ALREADY null. Keeping the empty, inherited
    // UAssetUserData is enough to break OFPA level saves.
    if (!IsValid(Asset))
    {
        if (Get(Spline))
        {
            Spline->Modify();
            Spline->RemoveUserDataOfClass(StaticClass());
            Spline->MarkPackageDirty();
        }
        return;
    }

    // Same-asset assignment is NOT necessarily a no-op: it can repair a
    // Blueprint-instanced subobject with a private foreign archetype.
    EnsureComponentOwned(Spline);
    if (GetAssignedAsset(Spline) == Asset) { return; }
    Spline->Modify();
    UStrandSplineUserData* Data = Get(Spline);
    if (!Data)
    {
        Data = NewObject<UStrandSplineUserData>(Spline, StaticClass(), NAME_None, RF_Transactional);
        Spline->AddAssetUserData(Data);
    }
    Data->Modify();
    Data->AssignedSplineAsset = Asset;
    Data->CoordinateMode = Asset->DefaultCoordinateMode;
    Data->AnchorMode = Asset->DefaultAnchorMode;
    Data->AnchorPointIndex = Asset->DefaultAnchorPointIndex;
    Spline->MarkPackageDirty();
}

bool UStrandSplineUserData::IsInSync(const USplineComponent* Spline) const
{
    return IsValid(AssignedSplineAsset.Get()) && AssignedSplineAsset->MatchesSplineComponentWithPlacement(
        Spline, CoordinateMode, AnchorMode, AnchorPointIndex);
}

UStrandSplineUserData* UStrandSplineUserData::GetOnSplineMesh(const USplineMeshComponent* Mesh)
{
    return IsValid(Mesh) ? Cast<UStrandSplineUserData>(const_cast<USplineMeshComponent*>(Mesh)->GetAssetUserDataOfClass(StaticClass())) : nullptr;
}
USplineDataAsset* UStrandSplineUserData::GetAssignedAssetOnSplineMesh(const USplineMeshComponent* Mesh)
{
    const UStrandSplineUserData* Data = GetOnSplineMesh(Mesh);
    return Data ? Data->AssignedSplineAsset.Get() : nullptr;
}
void UStrandSplineUserData::AssignAssetToSplineMesh(USplineMeshComponent* Mesh, USplineDataAsset* Asset)
{
    if (!IsValid(Mesh)) { return; }
    if (!IsValid(Asset))
    {
        if (GetOnSplineMesh(Mesh))
        {
            Mesh->Modify();
            Mesh->RemoveUserDataOfClass(StaticClass());
            Mesh->MarkPackageDirty();
        }
        return;
    }
    EnsureComponentOwned(Mesh);
    if (GetAssignedAssetOnSplineMesh(Mesh) == Asset) { return; }
    Mesh->Modify();
    UStrandSplineUserData* Data = GetOnSplineMesh(Mesh);
    if (!Data)
    {
        Data = NewObject<UStrandSplineUserData>(Mesh, StaticClass(), NAME_None, RF_Transactional);
        Mesh->AddAssetUserData(Data);
    }
    Data->Modify();
    Data->AssignedSplineAsset = Asset;
    Data->CoordinateMode = Asset->DefaultCoordinateMode;
    Data->AnchorMode = Asset->DefaultAnchorMode;
    Data->AnchorPointIndex = Asset->DefaultAnchorPointIndex;
    Mesh->MarkPackageDirty();
}
