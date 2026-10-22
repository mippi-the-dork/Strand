#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"

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
    if (!IsValid(Spline) || GetAssignedAsset(Spline) == Asset) { return; }
    Spline->Modify();
    UStrandSplineUserData* Data = Get(Spline);
    if (!IsValid(Asset))
    {
        // Remove only Strand's metadata; leave every other AssetUserData entry intact.
        if (Data) { Spline->RemoveUserDataOfClass(StaticClass()); }
    }
    else
    {
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
    }
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
    if (!IsValid(Mesh) || GetAssignedAssetOnSplineMesh(Mesh) == Asset) { return; }
    Mesh->Modify();
    UStrandSplineUserData* Data = GetOnSplineMesh(Mesh);
    if (!IsValid(Asset))
    {
        if (Data) { Mesh->RemoveUserDataOfClass(StaticClass()); }
    }
    else
    {
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
    }
    Mesh->MarkPackageDirty();
}
