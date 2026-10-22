#include "StrandSplineRefresh.h"
#include "Components/SplineComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"

namespace
{
    UBlueprint* FindOwningBlueprint(const USplineComponent* Source)
    {
        if (!Source) { return nullptr; }
        if (UBlueprint* Blueprint = Source->GetTypedOuter<UBlueprint>()) { return Blueprint; }
        if (const UBlueprintGeneratedClass* BPClass = Source->GetTypedOuter<UBlueprintGeneratedClass>())
        { return Cast<UBlueprint>(BPClass->ClassGeneratedBy); }
        if (AActor* Owner = Source->GetOwner())
        { return Cast<UBlueprint>(Owner->GetClass()->ClassGeneratedBy); }
        return nullptr;
    }

    bool HasTemplateInArchetypeChain(const USplineComponent* Candidate, const USplineComponent* Template)
    {
        const UObject* Current = Candidate;
        for (int32 Depth = 0; Current && Depth < 20; ++Depth)
        {
            if (Current == Template) { return true; }
            const UObject* Next = Current->GetArchetype();
            if (Next == Current) { break; }
            Current = Next;
        }
        return false;
    }

    void CopySplineGeometry(const USplineComponent* Source, USplineComponent* Target)
    {
        Target->ClearSplinePoints(false);
        for (int32 Index = 0; Index < Source->GetNumberOfSplinePoints(); ++Index)
        {
            Target->AddPoint(Source->GetSplinePointAt(Index, ESplineCoordinateSpace::Local), false);
        }
        Target->SetClosedLoop(Source->IsClosedLoop(), false);
        Target->UpdateSpline();
        Target->UpdateBounds();
        Target->MarkRenderStateDirty();
        Target->MarkRenderDynamicDataDirty();
    }
}

void StrandSplineRefresh::NotifySplineChanged(USplineComponent* Spline)
{
    if (!IsValid(Spline)) { return; }
    Spline->UpdateSpline();
    Spline->UpdateBounds();
    Spline->MarkRenderStateDirty();
    Spline->MarkRenderDynamicDataDirty();

    if (Spline->IsTemplate())
    {
        UBlueprint* Blueprint = FindOwningBlueprint(Spline);
        if (IsValid(Blueprint) && IsValid(Blueprint->GeneratedClass))
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            // Blueprint viewports use EditorPreview worlds that aren't always returned
            // by GEngine->GetWorldContexts(). Iterate only live preview spline instances
            // and synchronize a matching archetype/name in the owning Blueprint class.
            for (TObjectIterator<USplineComponent> It; It; ++It)
            {
                USplineComponent* Preview = *It;
                if (!IsValid(Preview) || Preview == Spline || Preview->IsTemplate()) { continue; }
                AActor* Owner = Preview->GetOwner();
                if (!IsValid(Owner) || !Owner->IsA(Blueprint->GeneratedClass)) { continue; }
                UWorld* World = Preview->GetWorld();
                if (!IsValid(World) || World->WorldType != EWorldType::EditorPreview) { continue; }
                if (!HasTemplateInArchetypeChain(Preview, Spline) && Preview->GetFName() != Spline->GetFName()) { continue; }
                // Do not touch transforms or asset metadata on the preview component.
                CopySplineGeometry(Spline, Preview);
            }
        }
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
}
