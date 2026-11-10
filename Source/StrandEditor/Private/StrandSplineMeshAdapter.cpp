#include "StrandSplineMeshAdapter.h"
#include "SplineDataAsset.h"
#include "StrandSplineUserData.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"

namespace
{
    bool CloseEnough(const FVector& A, const FVector& B) { return A.Equals(B, 0.01f); }
    FVector AnchorOffset(const USplineDataAsset* Asset, const UStrandSplineUserData* Data)
    {
        if (!Asset) { return FVector::ZeroVector; }
        const EStrandCoordinateMode Mode = Data ? Data->CoordinateMode : Asset->DefaultCoordinateMode;
        return Mode == EStrandCoordinateMode::Relative
            ? Asset->GetPlacementAnchor(Data ? Data->AnchorMode : Asset->DefaultAnchorMode,
                                       Data ? Data->AnchorPointIndex : Asset->DefaultAnchorPointIndex)
            : FVector::ZeroVector;
    }
}

bool StrandSplineMeshAdapter::CanApply(const USplineDataAsset* Asset)
{
    // A USplineMeshComponent has exactly two Hermite endpoint controls, not arbitrary spline points.
    return IsValid(Asset) && Asset->Points.Num() == 2 && !Asset->bIsClosedLoop;
}

bool StrandSplineMeshAdapter::Matches(const USplineMeshComponent* Mesh, const USplineDataAsset* Asset)
{
    if (!IsValid(Mesh) || !CanApply(Asset)) { return false; }
    const FVector Offset = AnchorOffset(Asset, UStrandSplineUserData::GetOnSplineMesh(Mesh));
    return CloseEnough(Mesh->GetStartPosition(), Asset->Points[0].LocalPosition - Offset)
        && CloseEnough(Mesh->GetEndPosition(), Asset->Points[1].LocalPosition - Offset)
        && CloseEnough(Mesh->GetStartTangent(), Asset->Points[0].LeaveTangent)
        && CloseEnough(Mesh->GetEndTangent(), Asset->Points[1].ArriveTangent);
}

bool StrandSplineMeshAdapter::Apply(USplineMeshComponent* Mesh, USplineDataAsset* Asset)
{
    if (!IsValid(Mesh) || !CanApply(Asset)) { return false; }
    Mesh->Modify();
    const FVector Offset = AnchorOffset(Asset, UStrandSplineUserData::GetOnSplineMesh(Mesh));
    Mesh->SetStartAndEnd(Asset->Points[0].LocalPosition - Offset, Asset->Points[0].LeaveTangent,
                         Asset->Points[1].LocalPosition - Offset, Asset->Points[1].ArriveTangent, true);
    NotifyChanged(Mesh);
    Mesh->MarkPackageDirty();
    return true;
}

void StrandSplineMeshAdapter::Capture(USplineMeshComponent* Mesh, USplineDataAsset* Asset, bool bRestoreSourceFrame)
{
    if (!IsValid(Mesh) || !IsValid(Asset)) { return; }
    const FVector Offset = bRestoreSourceFrame
        ? AnchorOffset(Asset, UStrandSplineUserData::GetOnSplineMesh(Mesh)) : FVector::ZeroVector;
    Asset->Modify();
    Asset->Points.SetNum(2);
    Asset->Points[0].LocalPosition = Mesh->GetStartPosition() + Offset;
    Asset->Points[0].LeaveTangent = Mesh->GetStartTangent();
    Asset->Points[0].ArriveTangent = Mesh->GetStartTangent();
    Asset->Points[0].LocalRotation = FRotator::ZeroRotator;
    Asset->Points[0].LocalScale = FVector::OneVector;
    Asset->Points[0].PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points[1].LocalPosition = Mesh->GetEndPosition() + Offset;
    Asset->Points[1].ArriveTangent = Mesh->GetEndTangent();
    Asset->Points[1].LeaveTangent = Mesh->GetEndTangent();
    Asset->Points[1].LocalRotation = FRotator::ZeroRotator;
    Asset->Points[1].LocalScale = FVector::OneVector;
    Asset->Points[1].PointType = ESplinePointType::CurveCustomTangent;
    Asset->bIsClosedLoop = false;
    Asset->MarkPackageDirty();
}

void StrandSplineMeshAdapter::NotifyChanged(USplineMeshComponent* Mesh)
{
    if (!IsValid(Mesh)) { return; }
    Mesh->UpdateMesh();
    Mesh->MarkRenderStateDirty();
    if (Mesh->IsTemplate())
    {
        UBlueprint* Blueprint = Mesh->GetTypedOuter<UBlueprint>();
        if (!Blueprint)
        {
            if (UBlueprintGeneratedClass* Class = Mesh->GetTypedOuter<UBlueprintGeneratedClass>())
            { Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy); }
        }
        if (Blueprint && Blueprint->GeneratedClass && GEngine)
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            for (TObjectIterator<USplineMeshComponent> It; It; ++It)
            {
                USplineMeshComponent* Preview = *It;
                if (!IsValid(Preview) || Preview->IsTemplate() || Preview == Mesh) { continue; }
                AActor* Owner = Preview->GetOwner();
                UWorld* World = Preview->GetWorld();
                if (!IsValid(Owner) || !Owner->IsA(Blueprint->GeneratedClass) ||
                    !IsValid(World) || World->WorldType != EWorldType::EditorPreview) { continue; }
                if (Preview->GetFName() != Mesh->GetFName() && Preview->GetArchetype() != Mesh) { continue; }
                Preview->SetStartAndEnd(Mesh->GetStartPosition(), Mesh->GetStartTangent(),
                    Mesh->GetEndPosition(), Mesh->GetEndTangent(), true);
                Preview->MarkRenderStateDirty();
            }
        }
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
}
