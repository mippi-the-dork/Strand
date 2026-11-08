// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "StrandProjectSettings.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "StrandSplineUserData.h"
#include "StrandSplineActor.h"
#include "StrandSplineMeshAdapter.h"
#include "Components/SplineMeshComponent.h"
#include "Components/BillboardComponent.h"
#include "StrandPlacement.h"
#include "StrandSplineViewport.h"
#include "Math/RotationMatrix.h"
#include "StrandSplineEditOperations.h"
#include "StrandSplineInputUtils.h"
#include "StrandSplineSelectionUtils.h"
#include "StrandSplineFraming.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplineAssetRoundTripTest,
    "Strand.Data.RoundTripPointData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSplineAssetRoundTripTest::RunTest(const FString& Parameters)
{
    USplineComponent* Source = NewObject<USplineComponent>(GetTransientPackage());
    Source->ClearSplinePoints(false);
    Source->AddPoint(FSplinePoint(0, FVector(15, 25, 40), FVector(4, -7, 3),
        FVector(20, 0, 7), FRotator(2, 7, 0), FVector(1.3, 0.8, 1),
        ESplinePointType::CurveCustomTangent), false);
    Source->AddPoint(FSplinePoint(1, FVector(45, 50, 80), FVector(12, 14, -8),
        FVector(-9, 9, 2), FRotator(0, 90, 20), FVector(1, 2, 3),
        ESplinePointType::CurveCustomTangent), false);
    Source->AddPoint(FSplinePoint(2, FVector(80, 85, 1), FVector(-7, -4, 2),
        FVector(5, -11, 10), FRotator(15, 12, 20), FVector::OneVector,
        ESplinePointType::CurveCustomTangent), false);
    Source->SetClosedLoop(true, false);
    Source->UpdateSpline();

    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    Asset->SerializeFromSplineComponent(Source);
    TestEqual(TEXT("Number of saved points"), Asset->Points.Num(), 3);
    TestTrue(TEXT("Loop flag"), Asset->bIsClosedLoop);

    USplineComponent* Target = NewObject<USplineComponent>(GetTransientPackage());
    Target->SetRelativeLocation(FVector(100, 400, 600));
    Target->SetRelativeRotation(FRotator(0, 50, 0));
    Target->SetRelativeScale3D(FVector(4, 4, 4));
    const FTransform Before = Target->GetRelativeTransform();
    Asset->ApplyToSplineComponent(Target);
    TestTrue(TEXT("Round trip preserves tangent/point/rotation/scale"), Asset->MatchesSplineComponent(Target, 0.0001f));
    TestTrue(TEXT("Target component transform left intact"), Target->GetRelativeTransform().Equals(Before));

    // A Strand reference lives in native component Asset User Data, not a separate actor component.
    UStrandSplineUserData::AssignAsset(Target, Asset);
    TestEqual(TEXT("Assigned Strand asset is stored on native spline"),
        UStrandSplineUserData::GetAssignedAsset(Target), Asset);
    TestTrue(TEXT("Assigning an asset does not touch spline geometry"), Asset->MatchesSplineComponent(Target));
    // Absolute retains authored local coordinates, Relative translates only positions.
    USplineComponent* Anchored = NewObject<USplineComponent>(GetTransientPackage());
    Anchored->SetRelativeLocation(FVector(31, 41, 59));
    const FTransform AnchorTransform = Anchored->GetRelativeTransform();
    Asset->ApplyToSplineComponentWithPlacement(Anchored,
        EStrandCoordinateMode::Relative, EStrandAnchorMode::PointIndex, 1);
    TestTrue(TEXT("Relative point 1 is component origin"),
        Anchored->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::Local).IsNearlyZero());
    TestTrue(TEXT("Relative anchor preserves closed loop"), Anchored->IsClosedLoop());
    TestTrue(TEXT("Relative anchor preserves component transform"),
        Anchored->GetRelativeTransform().Equals(AnchorTransform));
    TestTrue(TEXT("Relative mode comparison"), Asset->MatchesSplineComponentWithPlacement(Anchored,
        EStrandCoordinateMode::Relative, EStrandAnchorMode::PointIndex, 1));
    Anchored->SetLocationAtSplinePoint(2, FVector(100, 50, 0), ESplineCoordinateSpace::Local);
    Asset->SerializeFromSplineComponentWithPlacement(Anchored,
        EStrandCoordinateMode::Relative, EStrandAnchorMode::PointIndex, 1);
    TestTrue(TEXT("Relative update restores authored coordinate frame"),
        Asset->MatchesSplineComponentWithPlacement(Anchored,
            EStrandCoordinateMode::Relative, EStrandAnchorMode::PointIndex, 1));
    USplineComponent* Centered = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponentWithPlacement(Centered,
        EStrandCoordinateMode::Relative, EStrandAnchorMode::PointCenter, 0);
    FVector Centroid = FVector::ZeroVector;
    for (int32 Index = 0; Index < Centered->GetNumberOfSplinePoints(); ++Index)
    {
        Centroid += Centered->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
    }
    Centroid /= static_cast<double>(Centered->GetNumberOfSplinePoints());
    TestTrue(TEXT("Center anchor is at spline local origin"), Centroid.IsNearlyZero(0.001));
    TestTrue(TEXT("Point center placement comparison"), Asset->MatchesSplineComponentWithPlacement(Centered,
        EStrandCoordinateMode::Relative, EStrandAnchorMode::PointCenter, 0));

    USplineComponent* Independent = NewObject<USplineComponent>(GetTransientPackage());
    TestNull(TEXT("Second native spline starts unassigned"), UStrandSplineUserData::GetAssignedAsset(Independent));
    // The asset was intentionally reserialized from Anchored above, so its
    // contents no longer match Target. Test the actual contract instead:
    // unlinking metadata must not modify the native component's geometry.
    TArray<FSplinePoint> GeometryBeforeClear;
    const int32 PointCountBeforeClear = Target->GetNumberOfSplinePoints();
    const bool bClosedBeforeClear = Target->IsClosedLoop();
    for (int32 Index = 0; Index < PointCountBeforeClear; ++Index)
    {
        GeometryBeforeClear.Add(Target->GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
    }
    UStrandSplineUserData::AssignAsset(Target, nullptr);
    TestNull(TEXT("Clearing assignment leaves no reference"), UStrandSplineUserData::GetAssignedAsset(Target));
    bool bGeometryPreserved = Target->GetNumberOfSplinePoints() == PointCountBeforeClear &&
        Target->IsClosedLoop() == bClosedBeforeClear;
    if (bGeometryPreserved)
    {
        for (int32 Index = 0; Index < PointCountBeforeClear; ++Index)
        {
            const FSplinePoint After = Target->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
            const FSplinePoint& PointBeforeClear = GeometryBeforeClear[Index];
            bGeometryPreserved &= After.Position.Equals(PointBeforeClear.Position, 0.0001) &&
                After.ArriveTangent.Equals(PointBeforeClear.ArriveTangent, 0.0001) &&
                After.LeaveTangent.Equals(PointBeforeClear.LeaveTangent, 0.0001) &&
                After.Rotation.Equals(PointBeforeClear.Rotation, 0.0001) &&
                After.Scale.Equals(PointBeforeClear.Scale, 0.0001) && After.Type == PointBeforeClear.Type;
        }
    }
    TestTrue(TEXT("Clearing assignment preserves points"), bGeometryPreserved);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandAuthoredAnchorOrientationTest,
    "Strand.Editor.Authoring.AuthoredAnchorOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandAuthoredAnchorOrientationTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData Point;
    Point.LocalPosition = FVector(11, 42, -7);
    Point.ArriveTangent = FVector(40, 0, 0);
    Point.LeaveTangent = FVector(0, 120, 30);
    Point.LocalRotation = FRotator(10, 55, 20);
    Point.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points.Add(Point);
    const FSplinePointData Before = Point;
    const TArray<int32> Selection{0};
    TestTrue(TEXT("Orientation edit succeeds"), StrandSplineEditOperations::SetSelectedOrientation(
        Asset->Points, Selection, FRotator(20, 100, 5)));
    TestTrue(TEXT("Authored rotation updates"), Asset->Points[0].LocalRotation.Equals(FRotator(20, 100, 5), 0.001));
    TestTrue(TEXT("Anchor position preserved"), Asset->Points[0].LocalPosition.Equals(Before.LocalPosition));
    TestTrue(TEXT("Independent arrive tangent preserved"), Asset->Points[0].ArriveTangent.Equals(Before.ArriveTangent));
    TestTrue(TEXT("Independent leave tangent preserved"), Asset->Points[0].LeaveTangent.Equals(Before.LeaveTangent));
    TestEqual(TEXT("Point type remains custom"), Asset->Points[0].PointType.GetValue(), Before.PointType.GetValue());
    TestFalse(TEXT("Identical orientation does not dirty"), StrandSplineEditOperations::SetSelectedOrientation(
        Asset->Points, Selection, FRotator(20, 100, 5)));
    TestTrue(TEXT("Reset rotation"), StrandSplineEditOperations::SetSelectedOrientation(
        Asset->Points, Selection, FRotator::ZeroRotator));

    TSharedRef<SStrandSplineViewport> Viewport = SNew(SStrandSplineViewport);
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    Spline->AddPoint(Before.ToSplinePoint(0), false);
    FSplinePointData Second = Before;
    Second.LocalPosition = Before.LocalPosition + FVector(0, 200, 20);
    Spline->AddPoint(Second.ToSplinePoint(1), true);
    Viewport->SetSpline(Spline);
    Viewport->SetSelectedPointIndex(0);
    Viewport->SetAuthoredAnchorRotation(FRotator(0, 0, 0));
    TestTrue(TEXT("Explicit zero authored frame does not inherit native point rotation"),
        Viewport->GetAuthoredAnchorRotation().IsNearlyZero());
    Viewport->SetAuthoredAnchorRotation(FRotator(20, 100, 5));
    TestTrue(TEXT("Viewport receives Strand's authored orientation as up/roll reference"),
        Viewport->GetAuthoredAnchorRotation().Equals(FRotator(20, 100, 5), 0.001));
    Viewport->SetWidgetLocal(false);
    TestTrue(TEXT("World widget basis is asset X axis"),
        Viewport->GetCurrentWidgetAxisX().Equals(FVector::XAxisVector, 0.0001));
    Viewport->SetWidgetLocal(true);
    const FVector Outgoing = Before.LeaveTangent.GetSafeNormal();
    TestTrue(TEXT("Local X follows outgoing tangent even with authored nonzero yaw"),
        Viewport->GetCurrentWidgetAxisX().Equals(Outgoing, 0.0001));
    Viewport->SetAuthoredAnchorRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("Unrotated imported anchor also uses outgoing tangent"),
        Viewport->GetCurrentWidgetAxisX().Equals(Outgoing, 0.0001));
    Viewport->SetWidgetLocal(false);
    TestTrue(TEXT("World X remains fixed even on curved anchor"),
        Viewport->GetCurrentWidgetAxisX().Equals(FVector::XAxisVector, 0.0001));
    Viewport->SetWidgetLocal(true);
    // A linear endpoint with no evaluated handle still needs a defined axis.
    USplineComponent* LinearSpline = NewObject<USplineComponent>(GetTransientPackage());
    LinearSpline->ClearSplinePoints(false);
    LinearSpline->AddPoint(FSplinePoint(0, FVector::ZeroVector, FVector::ZeroVector,
        FVector::ZeroVector, FRotator::ZeroRotator, FVector::OneVector, ESplinePointType::Linear), false);
    LinearSpline->AddPoint(FSplinePoint(1, FVector(0, 150, 0), FVector::ZeroVector,
        FVector::ZeroVector, FRotator::ZeroRotator, FVector::OneVector, ESplinePointType::Linear), true);
    Viewport->SetSpline(LinearSpline);
    Viewport->SetSelectedPointIndex(0);
    TestTrue(TEXT("Linear point without tangent uses next-segment direction"),
        Viewport->GetCurrentWidgetAxisX().Equals(FVector::YAxisVector, 0.0001));
    return true;
}

// This specifically exercises the standalone preview's native-widget wiring.
// The prior v0.6.11 code returned a widget mode but supplied no editor-mode
// manager, so all geometry tests passed while the widget remained invisible.
// A separate live render and mouse-drag check is still required in UE.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandNativeViewportWidgetLifecycleTest,
    "Strand.Editor.Authoring.NativeWidgetLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandNativeViewportWidgetLifecycleTest::RunTest(const FString& Parameters)
{
    TSharedRef<SStrandSplineViewport> Viewport = SNew(SStrandSplineViewport);
    TestFalse(TEXT("No selection has no transform widget"), Viewport->IsNativeWidgetReady());

    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
    Spline->AddSplinePoint(FVector(100.0, 0.0, 0.0), ESplineCoordinateSpace::Local, true);
    Viewport->SetSpline(Spline);
    TestFalse(TEXT("A spline without selection has no widget"), Viewport->IsNativeWidgetReady());

    Viewport->SetSelectedPointIndex(0);
    TestTrue(TEXT("Selected anchor enables Strand's owned native widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetWidgetMode(1);
    TestTrue(TEXT("Rotation mode retains the native widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetWidgetMode(2);
    TestTrue(TEXT("Scale mode retains the native widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetShowControlPoints(false);
    TestFalse(TEXT("Hiding selectable anchors hides widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetShowControlPoints(true);
    TestTrue(TEXT("Restoring anchors restores widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetMarqueeMode(true);
    TestFalse(TEXT("Box Select suppresses the transform widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetMarqueeMode(false);
    TestTrue(TEXT("Exiting Box Select restores native widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetPenMode(true);
    TestFalse(TEXT("Pen suppresses the transform widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetPenMode(false);
    TestTrue(TEXT("Exiting Pen restores native widget"), Viewport->IsNativeWidgetReady());
    Viewport->SetSelectedPointIndex(INDEX_NONE);
    TestFalse(TEXT("Clearing selection hides transform widget"), Viewport->IsNativeWidgetReady());
    return true;
}

// Regression for OFPA saving a Blueprint that has a Strand-linked native spline
// component. Blueprint instancing can inherit either the private template
// user-data pointer or a local copy with a private template ARCHETYPE.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandBlueprintUserDataOwnershipTest,
    "Strand.Editor.Placement.BlueprintUserDataOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandBlueprintUserDataOwnershipTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Template = NewObject<USplineComponent>(GetTransientPackage());
    UStrandSplineUserData::AssignAsset(Template, Asset);
    UStrandSplineUserData* Source = UStrandSplineUserData::Get(Template);
    if (!TestNotNull(TEXT("Blueprint component template owns Strand data"), Source)) { return false; }
    Source->CoordinateMode = EStrandCoordinateMode::Relative;
    Source->AnchorMode = EStrandAnchorMode::PointCenter;
    Source->AnchorPointIndex = 3;

    USplineComponent* Instance = NewObject<USplineComponent>(GetTransientPackage());
    // Simulate the inherited pointer on a Blueprint-created level instance.
    Instance->AddAssetUserData(Source);
    TestTrue(TEXT("Inherited user data is visible on the instance"),
        UStrandSplineUserData::GetAssignedAsset(Instance) == Asset);
    const bool bRepaired = UStrandSplineUserData::EnsureComponentOwned(Instance);
    UStrandSplineUserData* Local = UStrandSplineUserData::Get(Instance);
    TestTrue(TEXT("Foreign user-data ownership repaired"), bRepaired);
    if (!TestNotNull(TEXT("Repaired metadata exists"), Local)) { return false; }
    TestTrue(TEXT("Metadata outer is exactly the receiving instance"), Local->GetOuter() == Instance);
    TestTrue(TEXT("Instance has independent Strand metadata"), Local != Source);
    TestTrue(TEXT("Repair avoids private Blueprint-template archetypes"),
        Local->GetArchetype() == UStrandSplineUserData::StaticClass()->GetDefaultObject());
    TestEqual(TEXT("Asset reference retained"), Local->AssignedSplineAsset.Get(), Asset);
    TestEqual(TEXT("Coordinate mode retained"), Local->CoordinateMode, EStrandCoordinateMode::Relative);
    TestEqual(TEXT("Anchor mode retained"), Local->AnchorMode, EStrandAnchorMode::PointCenter);
    TestEqual(TEXT("Anchor index retained"), Local->AnchorPointIndex, 3);
    TestFalse(TEXT("Repeated repair is a no-op"), UStrandSplineUserData::EnsureComponentOwned(Instance));
    Local->AnchorPointIndex = 1;
    TestEqual(TEXT("Editing placed instance does not change Blueprint template"), Source->AnchorPointIndex, 3);

    USplineComponent* SameAssetInstance = NewObject<USplineComponent>(GetTransientPackage());
    SameAssetInstance->AddAssetUserData(Source);
    UStrandSplineUserData::AssignAsset(SameAssetInstance, Asset);
    TestTrue(TEXT("Assigning the same asset still fixes foreign ownership"),
        UStrandSplineUserData::Get(SameAssetInstance)->GetOuter() == SameAssetInstance);

    // NewObject with a non-null Template COPIES property values; it does not
    // promise that GetArchetype() will become that template. The previous test
    // asserted exactly that undocumented behavior, so it failed on UE 5.8.3
    // before actually testing Strand. Verify the copy independently, and only
    // exercise the foreign-archetype repair branch if the engine constructed one.
    USplineComponent* LocalArchetypeInstance = NewObject<USplineComponent>(GetTransientPackage());
    UStrandSplineUserData* TemplateCopy = NewObject<UStrandSplineUserData>(
        LocalArchetypeInstance, UStrandSplineUserData::StaticClass(), NAME_None,
        RF_Transactional, Source);
    LocalArchetypeInstance->AddAssetUserData(TemplateCopy);
    TestEqual(TEXT("Template initialization retained spline link"),
        TemplateCopy->AssignedSplineAsset.Get(), Asset);
    TestEqual(TEXT("Template initialization retained anchor index"),
        TemplateCopy->AnchorPointIndex, 3);
    const bool bForeignArchetype =
        TemplateCopy->GetArchetype() != UStrandSplineUserData::StaticClass()->GetDefaultObject();
    if (bForeignArchetype)
    {
        TestTrue(TEXT("Locally owned metadata with foreign archetype is repaired"),
            UStrandSplineUserData::EnsureComponentOwned(LocalArchetypeInstance));
    }
    else
    {
        TestFalse(TEXT("Already-safe local template copy needs no repair"),
            UStrandSplineUserData::EnsureComponentOwned(LocalArchetypeInstance));
    }
    UStrandSplineUserData* CheckedCopy = UStrandSplineUserData::Get(LocalArchetypeInstance);
    TestTrue(TEXT("Local metadata has component ownership and safe archetype"),
        CheckedCopy && CheckedCopy->GetOuter() == LocalArchetypeInstance &&
        CheckedCopy->GetArchetype() == UStrandSplineUserData::StaticClass()->GetDefaultObject());
    TestEqual(TEXT("Local metadata retains its asset after verification or repair"),
        CheckedCopy ? CheckedCopy->AssignedSplineAsset.Get() : nullptr, Asset);
    TestFalse(TEXT("Verified safe local metadata needs no repeated repair"),
        UStrandSplineUserData::EnsureComponentOwned(LocalArchetypeInstance));

    // Clearing a null-valued assignment must remove the user-data OBJECT,
    // not leave an empty subobject whose archetype can poison level saving.
    USplineComponent* EmptyInheritedInstance = NewObject<USplineComponent>(GetTransientPackage());
    UStrandSplineUserData* EmptyTemplateData = NewObject<UStrandSplineUserData>(Template);
    EmptyInheritedInstance->AddAssetUserData(EmptyTemplateData);
    TestNull(TEXT("Empty inherited Strand metadata contains no linked asset"),
        UStrandSplineUserData::GetAssignedAsset(EmptyInheritedInstance));
    UStrandSplineUserData::AssignAsset(EmptyInheritedInstance, nullptr);
    TestNull(TEXT("Clearing an already-empty field removes inherited user-data subobject"),
        UStrandSplineUserData::Get(EmptyInheritedInstance));
    USplineComponent* OldEmptyActorInstance = NewObject<USplineComponent>(GetTransientPackage());
    OldEmptyActorInstance->AddAssetUserData(EmptyTemplateData);
    TestTrue(TEXT("Automatic repair drops empty foreign metadata on previously placed instances"),
        UStrandSplineUserData::EnsureComponentOwned(OldEmptyActorInstance));
    TestNull(TEXT("No orphaned Strand metadata remains after empty-link repair"),
        UStrandSplineUserData::Get(OldEmptyActorInstance));

    USplineMeshComponent* MeshTemplate = NewObject<USplineMeshComponent>(GetTransientPackage());
    UStrandSplineUserData::AssignAssetToSplineMesh(MeshTemplate, Asset);
    UStrandSplineUserData* MeshSource = UStrandSplineUserData::GetOnSplineMesh(MeshTemplate);
    USplineMeshComponent* MeshInstance = NewObject<USplineMeshComponent>(GetTransientPackage());
    MeshInstance->AddAssetUserData(MeshSource);
    UStrandSplineUserData::AssignAssetToSplineMesh(MeshInstance, Asset);
    const UStrandSplineUserData* MeshLocal = UStrandSplineUserData::GetOnSplineMesh(MeshInstance);
    TestNotNull(TEXT("Spline mesh retains Strand metadata"), MeshLocal);
    TestTrue(TEXT("Spline mesh instance owns independent metadata"),
        MeshLocal && MeshLocal->GetOuter() == MeshInstance && MeshLocal != MeshSource);
    USplineMeshComponent* EmptyMesh = NewObject<USplineMeshComponent>(GetTransientPackage());
    EmptyMesh->AddAssetUserData(NewObject<UStrandSplineUserData>(MeshTemplate));
    UStrandSplineUserData::AssignAssetToSplineMesh(EmptyMesh, nullptr);
    TestNull(TEXT("Clearing an empty spline mesh link removes inherited metadata"),
        UStrandSplineUserData::GetOnSplineMesh(EmptyMesh));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandMeshAdapterTest,
    "Strand.Editor.SplineMeshTwoPointAdapter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandMeshAdapterTest::RunTest(const FString& Parameters)
{
    const AStrandSplineActor* DefaultActor = AStrandSplineActor::StaticClass()->GetDefaultObject<AStrandSplineActor>();
    TestNotNull(TEXT("Quick Add native actor has stock spline"), DefaultActor->Spline.Get());
    TestTrue(TEXT("Stock spline is the actor root"), DefaultActor->GetRootComponent() == DefaultActor->Spline.Get());
    {
        AStrandSplineActor* Actor = NewObject<AStrandSplineActor>(GetTransientPackage());
        TArray<USplineComponent*> Splines;
        Actor->GetComponents<USplineComponent>(Splines);
        TestEqual(TEXT("Placed Strand actor has exactly one native spline"), Splines.Num(), 1);
#if WITH_EDITORONLY_DATA
        TestNotNull(TEXT("Strand actor has an editor-only billboard"), Actor->StrandBillboard.Get());
        if (Actor->StrandBillboard)
        {
            TestTrue(TEXT("Billboard attaches to native spline root"),
                Actor->StrandBillboard->GetAttachParent() == Actor->Spline.Get());
            TestTrue(TEXT("Billboard uses editor screen-size scaling"),
                Actor->StrandBillboard->bIsScreenSizeScaled != 0);
            TestTrue(TEXT("Billboard screen-size is positive"),
                Actor->StrandBillboard->ScreenSize > 0.0f);
            TestFalse(TEXT("Billboard is not editable in derived Blueprints"),
                Actor->StrandBillboard->bEditableWhenInherited);
        }
#endif
    }

    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    Asset->Points.SetNum(2);
    Asset->Points[0].LocalPosition = FVector(10, 20, 30);
    Asset->Points[0].LeaveTangent = FVector(50, 0, 0);
    Asset->Points[1].LocalPosition = FVector(210, 40, 30);
    Asset->Points[1].ArriveTangent = FVector(40, 10, 0);
    Asset->bIsClosedLoop = false;
    Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
    USplineMeshComponent* Mesh = NewObject<USplineMeshComponent>(GetTransientPackage());
    UStrandSplineUserData::AssignAssetToSplineMesh(Mesh, Asset);
    TestTrue(TEXT("Two point open asset accepted"), StrandSplineMeshAdapter::CanApply(Asset));
    TestTrue(TEXT("Apply succeeds"), StrandSplineMeshAdapter::Apply(Mesh, Asset));
    TestTrue(TEXT("Applied mesh endpoints and tangents match"), StrandSplineMeshAdapter::Matches(Mesh, Asset));
    TestEqual(TEXT("Mesh links to source asset"), UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh), Asset);
    Mesh->SetEndPosition(FVector(240, 70, 30), true);
    TestFalse(TEXT("Mesh modification detected"), StrandSplineMeshAdapter::Matches(Mesh, Asset));
    StrandSplineMeshAdapter::Capture(Mesh, Asset, true);
    TestTrue(TEXT("Mesh to asset captures exact edited endpoints"), StrandSplineMeshAdapter::Matches(Mesh, Asset));
    Asset->bIsClosedLoop = true;
    TestFalse(TEXT("Closed asset rejected on spline mesh"), StrandSplineMeshAdapter::CanApply(Asset));
    Asset->bIsClosedLoop = false;
    Asset->Points.AddDefaulted();
    TestFalse(TEXT("Multipoint asset rejected on spline mesh"), StrandSplineMeshAdapter::CanApply(Asset));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "SplineImportHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgGeometryTest,
    "Strand.Editor.Svg.ExactSegmentsAndClosures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgGeometryTest::RunTest(const FString& Parameters)
{
    const FString Fixture =
        TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\">")
        TEXT("<defs><rect id=\"template\" x=\"1\" y=\"2\" width=\"20\" height=\"10\" rx=\"3\" ry=\"2\"/></defs>")
        TEXT("<use href=\"#template\" x=\"40\" y=\"0\"/>")
        TEXT("<path id=\"mixed\" d=\"M0 0 L100 0 C130 0 130 60 100 60 Z\"/>")
        TEXT("<path id=\"explicit-close\" d=\"M0 0 C10 0 10 10 0 10 C-10 10 -20 0 0 0 Z\"/>")
        TEXT("<path id=\"compact-arc\" d=\"M0 0 A5 5 0 0110 10\"/>")
        TEXT("<circle id=\"disk\" cx=\"30\" cy=\"30\" r=\"10\"/>")
        TEXT("</svg>");
    const FString Dir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("StrandTests"));
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString File = FPaths::Combine(Dir, TEXT("StrandSvgGeometry.svg"));
    if (!TestTrue(TEXT("Write temporary SVG test fixture"), FFileHelper::SaveStringToFile(Fixture, *File)))
    {
        return false;
    }
    TArray<FSplineImportedCurve> Curves;
    const bool bLoaded = SplineAssetImport::LoadSvgCurves(File, Curves);
    IFileManager::Get().Delete(*File);
    if (!TestTrue(TEXT("Import SVG fixture"), bLoaded) ||
        !TestEqual(TEXT("Exactly five visible geometries; defs alone are not imported"), Curves.Num(), 5))
    {
        return false;
    }
    // The <use> of a rounded rect is the first visible geometry.
    TestTrue(TEXT("Referenced rounded rect forms a closed loop"), Curves[0].bClosedLoop);
    TestTrue(TEXT("Rounded rect retains multiple corner tangent pairs"), Curves[0].Points.Num() >= 8);
    TestTrue(TEXT("<use> translation is applied with SVG X mirror"), Curves[0].Points[0].LocalPosition.X < -40.0);

    const FSplineImportedCurve& Mixed = Curves[1];
    TestTrue(TEXT("Implicit Z makes a closed loop"), Mixed.bClosedLoop);
    TestEqual(TEXT("No duplicated Z endpoint"), Mixed.Points.Num(), 3);
    if (Mixed.Points.Num() == 3)
    {
        TestTrue(TEXT("Straight segment has exact chord derivative"),
            Mixed.Points[0].LeaveTangent.Equals(FVector(-100, 0, 0), 0.0001));
        TestTrue(TEXT("Incoming and outgoing handles are independent at mixed corner"),
            Mixed.Points[1].ArriveTangent.Equals(FVector(-100, 0, 0), 0.0001) &&
            Mixed.Points[1].LeaveTangent.Equals(FVector(-90, 0, 0), 0.0001));
        const FVector ClosingChord = Mixed.Points[0].LocalPosition - Mixed.Points.Last().LocalPosition;
        TestTrue(TEXT("Implicit closing edge has exact start/end derivatives"),
            Mixed.Points.Last().LeaveTangent.Equals(ClosingChord, 0.0001) &&
            Mixed.Points[0].ArriveTangent.Equals(ClosingChord, 0.0001));
    }

    const FSplineImportedCurve& Explicit = Curves[2];
    TestTrue(TEXT("Explicit final cubic followed by Z is closed"), Explicit.bClosedLoop);
    TestEqual(TEXT("Explicit repeated endpoint is merged"), Explicit.Points.Num(), 2);
    if (Explicit.Points.Num() == 2)
    {
        TestTrue(TEXT("Explicit closing Bezier preserves nonzero first arrive tangent"),
            !Explicit.Points[0].ArriveTangent.IsNearlyZero());
        TestTrue(TEXT("Start outgoing and incoming handles are independent"),
            !Explicit.Points[0].LeaveTangent.Equals(Explicit.Points[0].ArriveTangent));
    }
    TestTrue(TEXT("SVG compact arc flags parse into an open curve"),
        !Curves[3].bClosedLoop && Curves[3].Points.Num() >= 2);
    TestTrue(TEXT("Native SVG circle imports as a closed spline"),
        Curves[4].bClosedLoop && Curves[4].Points.Num() == 4);

    for (const FSplineImportedCurve& Curve : Curves)
    {
        for (const FSplinePointData& P : Curve.Points)
        {
            TestEqual(TEXT("Explicit custom tangents prevent auto smoothing"),
                      P.PointType.GetValue(), ESplinePointType::CurveCustomTangent);
            TestTrue(TEXT("Imported point stays on the XY plane"), FMath::IsNearlyZero(P.LocalPosition.Z));
        }
    }
    return true;
}
#endif

// Exercise the actual XML ingestion path, not just the SVG path command
// parser: several DCC exporters wrap a single quoted d= attribute over
// multiple physical lines. Losing text after a newline drops curve segments
// and Z/z closures while leaving a superficially valid partial spline.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgWrappedAttributeTest,
    "Strand.Editor.Svg.MultilineAttributesPreserveSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgWrappedAttributeTest::RunTest(const FString& Parameters)
{
    const FString Xml =
        TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\">\n")
        TEXT("<path id=\"wrapped\" d=\"M 0 0 C 10 0 10 10 0 10\r\n")
        TEXT(" C -10 10 -10 0 0 0\t Z\"/>")
        TEXT("<path id=\"open\" d=\"M 0 0 L 10 0\n L 10 10\"/>")
        TEXT("</svg>");
    const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("StrandTests"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = FPaths::Combine(Directory, TEXT("StrandSvgWrappedAttributes.svg"));
    if (!TestTrue(TEXT("Write multiline SVG fixture"), FFileHelper::SaveStringToFile(Xml, *Filename)))
    {
        return false;
    }
    TArray<FSplineImportedCurve> Curves;
    if (!TestTrue(TEXT("Import paths with multiline quoted attributes"),
            SplineAssetImport::LoadSvgCurves(Filename, Curves)) ||
        !TestEqual(TEXT("Two independent multiline paths"), Curves.Num(), 2))
    {
        return false;
    }
    TestTrue(TEXT("Wrapped cubic path remains closed"), Curves[0].bClosedLoop);
    TestEqual(TEXT("Both cubic segments retained across CRLF"), Curves[0].Points.Num(), 2);
    TestFalse(TEXT("Second path remains open"), Curves[1].bClosedLoop);
    TestEqual(TEXT("Second open path keeps line after LF"), Curves[1].Points.Num(), 3);
    return true;
}
#endif

// Regression using the supplied Illustrator-authored Strand logo. This SVG is
// intentionally FIVE independent contours. Only the rounded-square paths are
// closed; the center stroke and two outer arcs are open in the source file.
#if WITH_DEV_AUTOMATION_TESTS
#include "Interfaces/IPluginManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandIllustratorLogoTest,
    "Strand.Editor.Svg.IllustratorLogoClosureRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandIllustratorLogoTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin was found"), Plugin.IsValid())) { return false; }
    const FString Fixture = FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Tests/Fixtures/Strand_Illustrator_Logo.svg"));
    TArray<FSplineImportedCurve> Curves;
    if (!TestTrue(TEXT("Imported Illustrator source SVG"),
        SplineAssetImport::LoadSvgCurves(Fixture, Curves))) { return false; }
    if (!TestEqual(TEXT("Five independent Illustrator contours"), Curves.Num(), 5)) { return false; }
    const bool ExpectedClosed[5] = {false, true, true, false, false};
    for (int32 Index = 0; Index < Curves.Num(); ++Index)
    {
        const FSplineImportedCurve& Curve = Curves[Index];
        TestEqual(FString::Printf(TEXT("Contour %d source closure"), Index),
            Curve.bClosedLoop, ExpectedClosed[Index]);
        USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
        Asset->Points = Curve.Points;
        Asset->bIsClosedLoop = Curve.bClosedLoop;
        USplineComponent* Component = NewObject<USplineComponent>(GetTransientPackage());
        // Simulate replacing a spline that used an unrelated overridden loop key.
        Component->SetClosedLoopAtPosition(true, 999.0f, false);
        Asset->ApplyToSplineComponent(Component);
        TestEqual(FString::Printf(TEXT("Contour %d native closure"), Index),
            Component->IsClosedLoop(), ExpectedClosed[Index]);
        TestEqual(FString::Printf(TEXT("Contour %d point count"), Index),
            Component->GetNumberOfSplinePoints(), Curve.Points.Num());
        const bool bExact = Asset->MatchesSplineComponent(Component, 0.001f);
        if (!bExact)
        {
            // Keep this strict, even when the analytic curve matches. Print the
            // first differing field so the engine's representation can be
            // diagnosed without weakening point/tangent/rotation/scale tests.
            for (int32 PointIndex = 0; PointIndex < Curve.Points.Num(); ++PointIndex)
            {
                const FSplinePoint Actual = Component->GetSplinePointAt(
                    PointIndex, ESplineCoordinateSpace::Local);
                const FSplinePointData& Wanted = Curve.Points[PointIndex];
                const bool bPosition = Actual.Position.Equals(Wanted.LocalPosition, 0.001);
                const bool bArrive = Actual.ArriveTangent.Equals(Wanted.ArriveTangent, 0.001);
                const bool bLeave = Actual.LeaveTangent.Equals(Wanted.LeaveTangent, 0.001);
                const bool bRotation = Actual.Rotation.Equals(Wanted.LocalRotation, 0.001);
                const bool bScale = Actual.Scale.Equals(Wanted.LocalScale, 0.001);
                const bool bType = Component->GetSplinePointType(PointIndex) == Wanted.PointType.GetValue();
                if (!(bPosition && bArrive && bLeave && bRotation && bScale && bType))
                {
                    AddWarning(FString::Printf(
                        TEXT("Contour %d point %d: Position=%d (delta=%.8g), Arrive=%d (delta=%.8g), ")
                        TEXT("Leave=%d (delta=%.8g), Rotation=%d, Scale=%d (delta=%.8g), Type=%d (native=%d asset=%d)."),
                        Index, PointIndex, bPosition ? 1 : 0,
                        (Actual.Position - Wanted.LocalPosition).Size(), bArrive ? 1 : 0,
                        (Actual.ArriveTangent - Wanted.ArriveTangent).Size(), bLeave ? 1 : 0,
                        (Actual.LeaveTangent - Wanted.LeaveTangent).Size(), bRotation ? 1 : 0,
                        bScale ? 1 : 0, (Actual.Scale - Wanted.LocalScale).Size(), bType ? 1 : 0,
                        static_cast<int32>(Component->GetSplinePointType(PointIndex)),
                        static_cast<int32>(Wanted.PointType.GetValue())));
                    break;
                }
            }
        }
        TestTrue(FString::Printf(TEXT("Contour %d source/native point data exact"), Index), bExact);
#if WITH_EDITORONLY_DATA
        // Both Illustrator squares contain hard corners with split handles.
        if (Index == 1 || Index == 2)
        {
            TestTrue(FString::Printf(TEXT("Contour %d enables native split-handle editing"), Index),
                Component->bAllowDiscontinuousSpline);
        }
#endif
        if (ExpectedClosed[Index])
        {
            TestTrue(FString::Printf(TEXT("Contour %d loop length"), Index),
                Component->GetSplineLength() > 0.0f);
            // The closing key after N-1 is N: the loop endpoint must match
            // the source's first point, without storing a redundant endpoint.
            const FVector LastLoopLocation = Component->GetLocationAtSplineInputKey(
                static_cast<float>(Curve.Points.Num()), ESplineCoordinateSpace::Local);
            TestTrue(FString::Printf(TEXT("Contour %d seam returns to first point"), Index),
                LastLoopLocation.Equals(Curve.Points[0].LocalPosition, 0.005));
        }
    }
    return true;
}
#endif

// The Illustrator logo has an independently decoded cubic reference file.
// Unlike a point/tangent round-trip, this evaluates the native Unreal spline
// INSIDE every SVG segment, including the two final closing curves. It can
// detect missing segments, incorrect tangent sign/length, and loop seams.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandIllustratorAnalyticGeometryTest,
    "Strand.Editor.Svg.IllustratorAnalyticGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandIllustratorAnalyticGeometryTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin found"), Plugin.IsValid())) { return false; }

    const FString FixtureDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests/Fixtures"));
    TArray<FSplineImportedCurve> Curves;
    if (!TestTrue(TEXT("Load original Illustrator SVG"), SplineAssetImport::LoadSvgCurves(
        FPaths::Combine(FixtureDirectory, TEXT("Strand_Illustrator_Logo.svg")), Curves)) ||
        !TestEqual(TEXT("Five Illustrator paths"), Curves.Num(), 5))
    {
        return false;
    }

    FString GoldenText;
    if (!TestTrue(TEXT("Load independent source Bézier reference"), FFileHelper::LoadFileToString(
        GoldenText, *FPaths::Combine(FixtureDirectory, TEXT("Strand_Illustrator_Logo_CubicReference.csv")))))
    {
        return false;
    }

    struct FReferenceSegment
    {
        FVector P0, C1, C2, P3;
    };
    TArray<FReferenceSegment> Expected[5];
    TArray<FString> Lines;
    GoldenText.ParseIntoArrayLines(Lines, true);
    for (const FString& Line : Lines)
    {
        if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) { continue; }
        TArray<FString> Cells;
        Line.ParseIntoArray(Cells, TEXT(","), false);
        if (!TestEqual(TEXT("Reference row has 10 columns"), Cells.Num(), 10)) { return false; }
        const int32 ContourIndex = FCString::Atoi(*Cells[0]);
        const int32 SegmentIndex = FCString::Atoi(*Cells[1]);
        if (!TestTrue(TEXT("Valid contour index"), ContourIndex >= 0 && ContourIndex < 5)) { return false; }
        if (!TestEqual(TEXT("Reference rows are in original SVG segment order"),
            SegmentIndex, Expected[ContourIndex].Num())) { return false; }

        const auto SvgPoint = [&Cells](int32 Offset) -> FVector
        {
            // Default Strand SVG reorientation maps (-X, -Y) on XY.
            return FVector(-FCString::Atod(*Cells[Offset]), -FCString::Atod(*Cells[Offset + 1]), 0.0);
        };
        FReferenceSegment& Segment = Expected[ContourIndex].AddDefaulted_GetRef();
        Segment.P0 = SvgPoint(2);
        Segment.C1 = SvgPoint(4);
        Segment.C2 = SvgPoint(6);
        Segment.P3 = SvgPoint(8);
    }

    constexpr int32 SourceSegmentCounts[5] = {9, 8, 8, 2, 2};
    constexpr bool bSourceClosed[5] = {false, true, true, false, false};
    constexpr double Samples[] = {0.0, 0.125, 0.25, 0.5, 0.75, 0.875, 1.0};
    for (int32 I = 0; I < 5; ++I)
    {
        const FSplineImportedCurve& Curve = Curves[I];
        if (!TestEqual(FString::Printf(TEXT("Path %d source segment count"), I),
                Expected[I].Num(), SourceSegmentCounts[I]) ||
            !TestEqual(FString::Printf(TEXT("Path %d imported closed flag"), I),
                Curve.bClosedLoop, bSourceClosed[I]))
        {
            return false;
        }
        const int32 ExpectedSplinePoints = bSourceClosed[I]
            ? SourceSegmentCounts[I] : SourceSegmentCounts[I] + 1;
        if (!TestEqual(FString::Printf(TEXT("Path %d native control points"), I),
            Curve.Points.Num(), ExpectedSplinePoints))
        {
            return false;
        }

        USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
        Asset->Points = Curve.Points;
        Asset->bIsClosedLoop = Curve.bClosedLoop;
        USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
        Asset->ApplyToSplineComponent(Native);
        if (!TestEqual(FString::Printf(TEXT("Path %d native closed flag"), I),
            Native->IsClosedLoop(), bSourceClosed[I]))
        {
            return false;
        }
#if WITH_EDITORONLY_DATA
        if (I == 1 || I == 2)
        {
            TestTrue(FString::Printf(TEXT("Path %d permits native independent Bézier handles"), I),
                Native->bAllowDiscontinuousSpline);
        }
#endif
        for (int32 J = 0; J < Expected[I].Num(); ++J)
        {
            const FReferenceSegment& S = Expected[I][J];
            for (const double T : Samples)
            {
                const double U = 1.0 - T;
                const FVector ExpectedPosition = U*U*U*S.P0 + 3*U*U*T*S.C1 +
                    3*U*T*T*S.C2 + T*T*T*S.P3;
                const FVector NativePosition = Native->GetLocationAtSplineInputKey(
                    static_cast<float>(J + T), ESplineCoordinateSpace::Local);
                TestTrue(FString::Printf(TEXT("Path %d segment %d at %.3f matches source SVG"), I, J, T),
                    NativePosition.Equals(ExpectedPosition, 0.005));
            }
        }

        // Relative placement must translate positions only, not change shape.
        USplineComponent* Relative = NewObject<USplineComponent>(GetTransientPackage());
        const FVector Anchor = Asset->GetPlacementAnchor(EStrandAnchorMode::PointCenter, 0);
        Asset->ApplyToSplineComponentWithPlacement(Relative,
            EStrandCoordinateMode::Relative, EStrandAnchorMode::PointCenter, 0);
        TestEqual(FString::Printf(TEXT("Path %d relative loop state"), I),
            Relative->IsClosedLoop(), bSourceClosed[I]);
        for (int32 J = 0; J < Expected[I].Num(); ++J)
        {
            const FReferenceSegment& S = Expected[I][J];
            const double T = 0.5;
            const double U = 1.0 - T;
            const FVector ExpectedMidpoint = U*U*U*S.P0 + 3*U*U*T*S.C1 +
                3*U*T*T*S.C2 + T*T*T*S.P3;
            const FVector PlacedMidpoint = Relative->GetLocationAtSplineInputKey(
                static_cast<float>(J + T), ESplineCoordinateSpace::Local) + Anchor;
            TestTrue(FString::Printf(TEXT("Path %d relative segment %d retains shape"), I, J),
                PlacedMidpoint.Equals(ExpectedMidpoint, 0.005));
        }
    }
    return true;
}
#endif

// Regression for viewport left-to-right reading order, using the original
// Illustrator Digit icon supplied by the author. Its three glyph paths are
// ordered 1, 2, 3 from left to right in the SVG source.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgOrientationTest,
    "Strand.Editor.Svg.DigitIconOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgOrientationTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin found"), Plugin.IsValid())) { return false; }
    const FString Fixture = FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Tests/Fixtures/Strand_Digit_Icon.svg"));
    TArray<FSplineImportedCurve> Mirrored;
    TArray<FSplineImportedCurve> Legacy;
    if (!TestTrue(TEXT("Import Digit SVG using default top-view orientation"),
            SplineAssetImport::LoadSvgCurves(Fixture, Mirrored, true)) ||
        !TestTrue(TEXT("Import Digit SVG using legacy orientation"),
            SplineAssetImport::LoadSvgCurves(Fixture, Legacy, false)) ||
        !TestEqual(TEXT("Digit icon has five independent contours"), Mirrored.Num(), 5) ||
        !TestEqual(TEXT("Legacy orientation contains matching contour count"),
            Legacy.Num(), Mirrored.Num()))
    {
        return false;
    }
    const auto MeanX = [](const FSplineImportedCurve& Curve) -> double
    {
        double X = 0.0;
        for (const FSplinePointData& Pt : Curve.Points) { X += Pt.LocalPosition.X; }
        return Curve.Points.IsEmpty() ? 0.0 : X / Curve.Points.Num();
    };
    // Digit glyphs are paths 2..4; positive SVG X places 1 left of 2 left of 3.
    TestTrue(TEXT("Legacy imports glyphs with increasing X"),
        MeanX(Legacy[2]) < MeanX(Legacy[3]) && MeanX(Legacy[3]) < MeanX(Legacy[4]));
    TestTrue(TEXT("Default native orientation mirrors glyph X"),
        MeanX(Mirrored[2]) > MeanX(Mirrored[3]) && MeanX(Mirrored[3]) > MeanX(Mirrored[4]));
    for (int32 I = 0; I < Mirrored.Num(); ++I)
    {
        if (!TestEqual(FString::Printf(TEXT("Contour %d point count"), I),
                Mirrored[I].Points.Num(), Legacy[I].Points.Num())) { return false; }
        TestEqual(FString::Printf(TEXT("Contour %d closure unchanged"), I),
            Mirrored[I].bClosedLoop, Legacy[I].bClosedLoop);
        for (int32 P = 0; P < Mirrored[I].Points.Num(); ++P)
        {
            const FSplinePointData& A = Mirrored[I].Points[P];
            const FSplinePointData& B = Legacy[I].Points[P];
            TestTrue(TEXT("X mirror preserves local positions with sign reversal"),
                FMath::IsNearlyEqual(A.LocalPosition.X, -B.LocalPosition.X, 0.0001));
            TestTrue(TEXT("X mirror leaves Y/Z unchanged"),
                FMath::IsNearlyEqual(A.LocalPosition.Y, B.LocalPosition.Y, 0.0001) &&
                FMath::IsNearlyEqual(A.LocalPosition.Z, B.LocalPosition.Z, 0.0001));
            TestTrue(TEXT("X mirror reflects incoming and outgoing handles"),
                FMath::IsNearlyEqual(A.ArriveTangent.X, -B.ArriveTangent.X, 0.0001) &&
                FMath::IsNearlyEqual(A.LeaveTangent.X, -B.LeaveTangent.X, 0.0001) &&
                FMath::IsNearlyEqual(A.ArriveTangent.Y, B.ArriveTangent.Y, 0.0001) &&
                FMath::IsNearlyEqual(A.LeaveTangent.Y, B.LeaveTangent.Y, 0.0001));
        }
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
// The five project-level SVG origins must move all subpaths within the same
// source canvas by the same translation, while preserving their geometry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgCanvasOriginTest,
    "Strand.Editor.Svg.ProjectCanvasOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgCanvasOriginTest::RunTest(const FString& Parameters)
{
    const FString File = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("StrandTests"), TEXT("CanvasOrigin.svg"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!FFileHelper::SaveStringToFile(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 50\"><path d=\"M0 0 L100 50\" /></svg>"), *File))
    { AddError(TEXT("Unable to write temporary SVG origin test fixture.")); return false; }
    UStrandProjectSettings* Settings = GetMutableDefault<UStrandProjectSettings>();
    const EStrandSvgCanvasOrigin Before = Settings->SvgCanvasOrigin;
    struct FCase { EStrandSvgCanvasOrigin Origin; FVector First; FVector Last; };
    const FCase Cases[] = {
        {EStrandSvgCanvasOrigin::TopLeft, FVector(0,0,0), FVector(-100,-50,0)},
        {EStrandSvgCanvasOrigin::TopRight, FVector(100,0,0), FVector(0,-50,0)},
        {EStrandSvgCanvasOrigin::BottomLeft, FVector(0,50,0), FVector(-100,0,0)},
        {EStrandSvgCanvasOrigin::BottomRight, FVector(100,50,0), FVector(0,0,0)},
        {EStrandSvgCanvasOrigin::Center, FVector(50,25,0), FVector(-50,-25,0)}
    };
    for (const FCase& Case : Cases)
    {
        Settings->SvgCanvasOrigin = Case.Origin;
        TArray<FSplineImportedCurve> Curves;
        const bool bLoaded = SplineAssetImport::LoadSvgCurves(File, Curves, true);
        TestTrue(TEXT("SVG load with custom canvas origin"), bLoaded);
        if (bLoaded && Curves.Num() == 1 && Curves[0].Points.Num() == 2)
        {
            TestTrue(TEXT("First point uses source canvas origin"), Curves[0].Points[0].LocalPosition.Equals(Case.First, 0.001));
            TestTrue(TEXT("Second point uses source canvas origin"), Curves[0].Points[1].LocalPosition.Equals(Case.Last, 0.001));
        }
        else { AddError(TEXT("Origin test: expected one two-point curve.")); }
    }
    Settings->SvgCanvasOrigin = Before;
    IFileManager::Get().Delete(*File);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "SplineDataAssetFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandAlembicFactoryRoutingTest,
    "Strand.Editor.Import.AlembicFactoryRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandAlembicFactoryRoutingTest::RunTest(const FString& Parameters)
{
    UStrandAlembicImportRouterFactory* Router = NewObject<UStrandAlembicImportRouterFactory>();
    UStrandAlembicFactory* Direct = NewObject<UStrandAlembicFactory>();
    TestNotNull(TEXT("Interactive Alembic router factory exists"), Router);
    TestNotNull(TEXT("Explicit Strand Alembic factory exists"), Direct);
    if (!Router || !Direct) { return false; }
    // The automation runner can mark factories as non-interactive. Testing
    // FactoryCanImport("Curve.abc") against an unconditional 'true' would then
    // reject the exact safeguard we need for unattended asset imports.
    TestTrue(TEXT("Interactive router recognizes Alembic"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc"), false));
    TestTrue(TEXT("Interactive router recognizes uppercase Alembic extension"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.ABC"), false));
    TestFalse(TEXT("Automated import bypasses interactive Alembic chooser"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc"), true));
    TestFalse(TEXT("Router does not intercept FBX"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.fbx"), false));
    TestFalse(TEXT("Router does not intercept SVG"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.svg"), false));
    TestFalse(TEXT("Router does not intercept another extension after .abc"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc.bak"), false));

    const bool bAutomatedImport = Router->IsAutomatedImport();
    AddInfo(FString::Printf(TEXT("Alembic router test environment: %s"),
        bAutomatedImport ? TEXT("automated import") : TEXT("interactive import")));
    TestEqual(TEXT("Live router respects its current import context"),
        Router->FactoryCanImport(TEXT("Curve.abc")), !bAutomatedImport);
    TestFalse(TEXT("Live router rejects FBX"), Router->FactoryCanImport(TEXT("Curve.fbx")));
    TestTrue(TEXT("Direct Strand importer accepts Alembic"), Direct->FactoryCanImport(TEXT("Curve.abc")));
    TestTrue(TEXT("Router has priority over explicit curve factory"), Router->ImportPriority > Direct->ImportPriority);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandDropLogic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandMultiAssetDropEligibilityTest,
    "Strand.Editor.Placement.MultiAssetDropEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandMultiAssetDropEligibilityTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* A = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineDataAsset* B = NewObject<USplineDataAsset>(GetTransientPackage());
    UObject* Unrelated = NewObject<UObject>(GetTransientPackage());

    TArray<UObject*> Assets;
    Assets.Add(A);
    TestFalse(TEXT("One Strand asset cannot trigger a multi-spline dialog"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(A);
    TestFalse(TEXT("Repeated previews of the same asset never trigger grouping"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(B);
    TestTrue(TEXT("A completed drop with two distinct Strand assets can offer grouping"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(Unrelated);
    TestFalse(TEXT("Mixed types must use normal editor placement"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Interfaces/IPluginManager.h"
#include "SplineImportHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandBlenderNurbsMissingKnotsTest,
    "Strand.Editor.Alembic.BlenderPeriodicNurbsWithoutKnots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandBlenderNurbsMissingKnotsTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin is installed"), Plugin.IsValid())) { return false; }
    const FString Filename = FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Tests/Fixtures/Alembic/Blender_5.2.3_BezierAndPeriodicNurbs.abc"));
    if (!TestTrue(TEXT("Blender Alembic regression fixture is packaged"), FPaths::FileExists(Filename)))
    { return false; }
    TArray<FSplineImportedCurve> Curves;
    if (!TestTrue(TEXT("Blender Bézier and knotless periodic NURBS both import"),
        SplineAssetImport::LoadAlembicCurves(Filename, nullptr, Curves)))
    { return false; }
    TestEqual(TEXT("Both Blender curve objects are represented"), Curves.Num(), 2);
    bool bFoundBezier = false;
    bool bFoundNurbs = false;
    for (const FSplineImportedCurve& Curve : Curves)
    {
        if (Curve.SourceId.Contains(TEXT("NurbsCircle")))
        {
            bFoundNurbs = true;
            TestTrue(TEXT("Periodic NURBS is closed"), Curve.bClosedLoop);
            TestTrue(TEXT("Periodic NURBS yields editable samples"), Curve.Points.Num() >= 8);
        }
        else
        {
            bFoundBezier = true;
            TestTrue(TEXT("Blender Bézier source has native spline anchors"), Curve.Points.Num() >= 2);
        }
        USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
        Asset->Points = Curve.Points;
        Asset->bIsClosedLoop = Curve.bClosedLoop;
        USplineComponent* Preview = NewObject<USplineComponent>(GetTransientPackage());
        Asset->ApplyToSplineComponent(Preview);
        TestTrue(TEXT("Curve can be evaluated by a native spline component"),
            Preview->GetSplineLength() > 0.0f);
    }
    TestTrue(TEXT("NURBS exists in output"), bFoundNurbs);
    TestTrue(TEXT("Bézier exists in output"), bFoundBezier);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandSplineExport.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplineAuthoringExportTest,
    "Strand.Editor.Export.SvgAndAlembicRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplineAuthoringExportTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector::ZeroVector;
    A.LeaveTangent = FVector(100, 40, 0);
    A.ArriveTangent = FVector(100, 0, 0);
    A.PointType = ESplinePointType::CurveCustomTangent;
    B.LocalPosition = FVector(200, 100, 0);
    B.ArriveTangent = FVector(90, 50, 0);
    B.LeaveTangent = FVector(90, 50, 0);
    B.PointType = ESplinePointType::CurveCustomTangent;
    C.LocalPosition = FVector(300, -20, 0);
    C.ArriveTangent = FVector(40, -120, 0);
    C.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points = {A, B, C};
    TestTrue(TEXT("XY spline recognized as planar"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LocalPosition.Z = 15.0;
    TestFalse(TEXT("3D spline requires explicit SVG projection"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LocalPosition.Z = 0.0;
    Asset->Points[1].LeaveTangent.Z = 25.0;
    TestFalse(TEXT("Off-plane tangent handle prevents silent projection"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LeaveTangent.Z = 0.0;
    FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Strand"), TEXT("ExportTests"));
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString SvgPath = FPaths::Combine(Dir, TEXT("StrandSplineTest.svg"));
    const FString AbcPath = FPaths::Combine(Dir, TEXT("StrandSplineTest.abc"));
    FString Error;
    if (!TestTrue(TEXT("SVG export succeeds"),
        StrandSplineExport::WriteSvg(Asset, SvgPath, StrandSplineExport::EProjection::XY, Error)))
    { AddError(Error); return false; }
    FString Xml;
    TestTrue(TEXT("Exported SVG is UTF-8 text"), FFileHelper::LoadFileToString(Xml, *SvgPath));
    TestTrue(TEXT("SVG uses cubic paths"), Xml.Contains(TEXT(" C ")));
    TestTrue(TEXT("SVG uses declared coordinate bounds"), Xml.Contains(TEXT("viewBox=")));
    TArray<FSplineImportedCurve> SvgCurves;
    TestTrue(TEXT("SVG is understood by Strand's importer"), SplineAssetImport::LoadSvgCurves(SvgPath, SvgCurves));
    TestTrue(TEXT("SVG preserves anchors"), SvgCurves.Num() == 1 && SvgCurves[0].Points.Num() == 3);
    Error.Reset();
    if (!TestTrue(TEXT("Alembic export succeeds"), StrandSplineExport::WriteAlembic(Asset, AbcPath, Error)))
    { AddError(Error); return false; }
    TArray<FSplineImportedCurve> AbcCurves;
    TestTrue(TEXT("Alembic can be imported again"), SplineAssetImport::LoadAlembicCurves(AbcPath, nullptr, AbcCurves));
    if (TestTrue(TEXT("Alembic preserves the point count"), AbcCurves.Num() == 1 && AbcCurves[0].Points.Num() == 3))
    {
        TestTrue(TEXT("Alembic preserves 3D local position orientation"),
            AbcCurves[0].Points[1].LocalPosition.Equals(Asset->Points[1].LocalPosition, 0.005));
        TestTrue(TEXT("Alembic preserves cubic leave tangent"),
            AbcCurves[0].Points[0].LeaveTangent.Equals(Asset->Points[0].LeaveTangent, 0.005));
    }
    // Exercise closed-loop Bézier topology (3 anchors -> 9 exported control vertices).
    Asset->bIsClosedLoop = true;
    Error.Reset();
    TestTrue(TEXT("Closed Alembic export succeeds"), StrandSplineExport::WriteAlembic(Asset, AbcPath, Error));
    AbcCurves.Reset();
    TestTrue(TEXT("Closed Alembic re-imports"), SplineAssetImport::LoadAlembicCurves(AbcPath, nullptr, AbcCurves));
    TestTrue(TEXT("Closed topology preserved"),
        AbcCurves.Num() == 1 && AbcCurves[0].bClosedLoop && AbcCurves[0].Points.Num() == 3);
    IFileManager::Get().Delete(*SvgPath);
    IFileManager::Get().Delete(*AbcPath);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplineEditingOperationsTest,
    "Strand.Editor.Authoring.SplineEditingOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplineEditingOperationsTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(5.0, -7.0, 15.0);
    B.LocalPosition = FVector(120.0, 75.0, 45.0);
    C.LocalPosition = FVector(270.0, -30.0, -20.0);
    A.LeaveTangent = FVector(110.0, 180.0, 15.0);
    B.ArriveTangent = FVector(150.0, 80.0, 20.0);
    B.LeaveTangent = FVector(100.0, -45.0, 50.0);
    C.ArriveTangent = FVector(160.0, -70.0, -20.0);
    A.PointType = B.PointType = C.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points = {A, B, C};
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Native);
    TArray<FVector> OriginalSamples;
    for (int32 I = 0; I <= 48; ++I)
    {
        OriginalSamples.Add(Native->GetLocationAtSplineInputKey(I * 2.0 / 48.0, ESplineCoordinateSpace::Local));
    }
    TestTrue(TEXT("Subdivide succeeds"), StrandSplineEditOperations::Subdivide(Asset->Points, false, Native));
    TestEqual(TEXT("Subdivide inserts one anchor per segment"), Asset->Points.Num(), 5);
    Asset->ApplyToSplineComponent(Native);
    double LargestDeviation = 0.0;
    for (int32 I = 0; I <= 48; ++I)
    {
        const FVector After = Native->GetLocationAtSplineInputKey(I * 4.0 / 48.0, ESplineCoordinateSpace::Local);
        LargestDeviation = FMath::Max(LargestDeviation, FVector::Distance(OriginalSamples[I], After));
    }
    TestTrue(TEXT("de Casteljau subdivision retains cubic shape to 0.01 cm"), LargestDeviation < 0.01);
    const TArray<FSplinePointData> BeforeReverse = Asset->Points;
    StrandSplineEditOperations::Reverse(Asset->Points, false);
    TestTrue(TEXT("Reverse flips endpoint order"), Asset->Points[0].LocalPosition.Equals(BeforeReverse.Last().LocalPosition));
    TestTrue(TEXT("Reverse swaps and negates arriving tangent"), Asset->Points[0].ArriveTangent.Equals(-BeforeReverse.Last().LeaveTangent));
    StrandSplineEditOperations::Mirror(Asset->Points, 0);
    StrandSplineEditOperations::Mirror(Asset->Points, 0);
    for (int32 I = 0; I < Asset->Points.Num(); ++I)
    {
        TestTrue(TEXT("Double mirror restores point coordinates"), Asset->Points[I].LocalPosition.Equals(BeforeReverse[BeforeReverse.Num() - I - 1].LocalPosition, 0.001));
    }
    StrandSplineEditOperations::Flatten(Asset->Points, 2);
    for (const FSplinePointData& Point : Asset->Points)
    {
        TestTrue(TEXT("Flatten XY zeros depth and both handles"),
            FMath::IsNearlyZero(Point.LocalPosition.Z) && FMath::IsNearlyZero(Point.ArriveTangent.Z) && FMath::IsNearlyZero(Point.LeaveTangent.Z));
    }
    StrandSplineEditOperations::MakeAllLinear(Asset->Points);
    for (const FSplinePointData& Point : Asset->Points) { TestTrue(TEXT("Linear mode"), Point.PointType == ESplinePointType::Linear); }
    StrandSplineEditOperations::MakeAllAuto(Asset->Points);
    for (const FSplinePointData& Point : Asset->Points) { TestTrue(TEXT("Auto mode"), Point.PointType == ESplinePointType::Curve); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplinePenGeometryTest,
    "Strand.Editor.Authoring.SplinePenGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplinePenGeometryTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(0, 10, 30);
    B.LocalPosition = FVector(120, 40, -20);
    C.LocalPosition = FVector(250, -20, 90);
    A.PointType = B.PointType = C.PointType = ESplinePointType::CurveCustomTangent;
    A.LeaveTangent = FVector(100, 160, 30);
    B.ArriveTangent = FVector(60, 90, 20);
    B.LeaveTangent = FVector(75, -80, 10);
    C.ArriveTangent = FVector(120, -70, 45);
    Asset->Points = { A, B, C };
    Asset->ApplyToSplineComponent(Native);
    // Inserting at a non-midpoint parameter must not reshape either segment.
    TArray<FVector> Before;
    for (int32 I = 0; I <= 80; ++I)
    {
        Before.Add(Native->GetLocationAtSplineInputKey(I / 40.0, ESplineCoordinateSpace::Local));
    }
    TestEqual(TEXT("Pen split index"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, 0.25), 1);
    TestEqual(TEXT("Pen split adds one point"), Asset->Points.Num(), 4);
    Asset->ApplyToSplineComponent(Native);
    double MaxDeviation = 0.0;
    for (int32 I = 0; I <= 80; ++I)
    {
        const double OldKey = I / 40.0;
        const double NewKey = OldKey <= 0.25 ? OldKey / 0.25
            : OldKey <= 1.0 ? 1.0 + (OldKey - 0.25) / 0.75 : OldKey + 1.0;
        MaxDeviation = FMath::Max(MaxDeviation, FVector::Distance(Before[I],
            Native->GetLocationAtSplineInputKey(NewKey, ESplineCoordinateSpace::Local)));
    }
    TestTrue(TEXT("Quarter-segment split preserves cubic to 0.01 cm"), MaxDeviation < 0.01);
    const int32 CountBeforeInvalid = Asset->Points.Num();
    TestEqual(TEXT("Cannot split open final endpoint"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, CountBeforeInvalid - 1, 0.5), INDEX_NONE);
    TestEqual(TEXT("Cannot split t=0 endpoint"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, 0.0), INDEX_NONE);
    TestEqual(TEXT("Invalid inserts never mutate"), Asset->Points.Num(), CountBeforeInvalid);
    TestEqual(TEXT("Pen prepend index"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(-50, 0, 0), true, false), 0);
    TestTrue(TEXT("Prepended anchor stays first"), Asset->Points[0].LocalPosition.Equals(FVector(-50, 0, 0)));
    TestEqual(TEXT("Pen append index"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(350, 0, 0), false, false), 5);
    TestTrue(TEXT("Pen handle edit applies"), StrandSplineEditOperations::SetPenHandles(
        Asset->Points, 5, FVector(10, 20, -5)));
    TestTrue(TEXT("Outgoing handle is 3x drag"), Asset->Points[5].LeaveTangent.Equals(FVector(30, 60, -15)));
    TestTrue(TEXT("Incoming handle mirrors outgoing"), Asset->Points[5].ArriveTangent.Equals(FVector(30, 60, -15)));
    TestEqual(TEXT("Closed splines cannot be extended"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(400, 0, 0), false, true), INDEX_NONE);
    TestEqual(TEXT("Count unchanged after closed extension"), Asset->Points.Num(), 6);
    // The last segment of a closed spline wraps back to point zero. Inserting
    // on this span must preserve closure and the two cubic subsegments.
    Asset->Points = { A, B, C };
    Asset->bIsClosedLoop = true;
    Asset->ApplyToSplineComponent(Native);
    const FVector ClosedBeforeA = Native->GetLocationAtSplineInputKey(2.73f, ESplineCoordinateSpace::Local);
    const FVector ClosedBeforeB = Native->GetLocationAtSplineInputKey(2.90f, ESplineCoordinateSpace::Local);
    TestEqual(TEXT("Closed wrap insert appends before closure"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, true, Native, 2, 0.73), 3);
    Asset->ApplyToSplineComponent(Native);
    TestTrue(TEXT("Closed loop remains closed"), Native->IsClosedLoop());
    TestTrue(TEXT("Closed wrap insertion point stays on original curve"),
        ClosedBeforeA.Equals(Native->GetLocationAtSplineInputKey(3.0, ESplineCoordinateSpace::Local), 0.01));
    TestTrue(TEXT("Closed wrap second half remains on original curve"),
        ClosedBeforeB.Equals(Native->GetLocationAtSplineInputKey(3.0f + (0.90f - 0.73f) / 0.27f,
            ESplineCoordinateSpace::Local), 0.01));
    // Empty assets are valid while drawing a new path from scratch.
    Asset->Points.Empty();
    TestEqual(TEXT("First anchor in empty path"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(4, 5, 6), false, false), 0);
    TestEqual(TEXT("Second anchor in empty path"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(44, 5, 6), false, false), 1);
    return true;
}

// The authored relationship between cubic handles is stored per Strand point,
// not on USplineComponent. Native component geometry must remain compatible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandBezierHandleModesTest,
    "Strand.Editor.Authoring.BezierHandleModes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandBezierHandleModesTest::RunTest(const FString& Parameters)
{
    namespace Ops = StrandSplineEditOperations;
    FSplinePointData Point;
    Point.PointType = ESplinePointType::CurveCustomTangent;
    Point.ArriveTangent = FVector(30.0, 0.0, 0.0);
    Point.LeaveTangent = FVector(0.0, 60.0, 0.0);
    TestEqual(TEXT("Legacy custom tangents default to independently editable Free mode"),
        Ops::EffectiveHandleMode(Point), EStrandBezierHandleMode::Free);
    TestTrue(TEXT("Aligned mode can be applied"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Aligned, Point.ArriveTangent, Point.LeaveTangent));
    TestTrue(TEXT("Aligned initial handles point in a common direction"),
        Point.ArriveTangent.GetSafeNormal().Equals(Point.LeaveTangent.GetSafeNormal(), 0.000001));
    const double IncomingLength = Point.ArriveTangent.Size();
    TestTrue(TEXT("Aligned incoming edit changes both directions"),
        Ops::EditHandle(Point, 1, FVector(0.0, 40.0, 0.0)));
    TestTrue(TEXT("Aligned incoming edit retains outgoing handle length"),
        FMath::IsNearlyEqual(Point.LeaveTangent.Size(), 60.0) &&
        FMath::IsNearlyEqual(Point.ArriveTangent.Size(), 40.0));
    TestTrue(TEXT("Aligned mode persists after drag"),
        Ops::EffectiveHandleMode(Point) == EStrandBezierHandleMode::Aligned);
    TestTrue(TEXT("Aligned outgoing edit changes both directions"),
        Ops::EditHandle(Point, 2, FVector(-40.0, 0.0, 0.0)) &&
        Point.ArriveTangent.Equals(FVector(-40.0, 0.0, 0.0), 0.000001));
    TestTrue(TEXT("Aligned editing preserves the initial incoming length on mode conversion"),
        FMath::IsNearlyEqual(IncomingLength, 30.0));

    TestTrue(TEXT("Mirrored mode can be applied"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Mirrored, Point.ArriveTangent, Point.LeaveTangent));
    TestTrue(TEXT("Mirrored mode equalizes handle lengths"),
        Point.ArriveTangent.Equals(Point.LeaveTangent, 0.000001));
    TestTrue(TEXT("Mirrored outgoing drag follows on incoming side"),
        Ops::EditHandle(Point, 2, FVector(2.0, 3.0, 4.0)) &&
        Point.ArriveTangent.Equals(FVector(2.0, 3.0, 4.0), 0.000001));
    TestTrue(TEXT("Mirrored incoming drag follows on outgoing side"),
        Ops::EditHandle(Point, 1, FVector(-7.0, 12.0, 0.0)) &&
        Point.LeaveTangent.Equals(FVector(-7.0, 12.0, 0.0), 0.000001));
    TestTrue(TEXT("Mirrored zero handle collapses its counterpart"),
        Ops::EditHandle(Point, 1, FVector::ZeroVector) &&
        Point.ArriveTangent.IsNearlyZero() && Point.LeaveTangent.IsNearlyZero());

    TestTrue(TEXT("Corner mode collapses both handles"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Corner, Point.ArriveTangent, Point.LeaveTangent));
    TestTrue(TEXT("Corner uses native custom tangent data"),
        Point.PointType.GetValue() == ESplinePointType::CurveCustomTangent &&
        Point.ArriveTangent.IsNearlyZero() && Point.LeaveTangent.IsNearlyZero());
    TestTrue(TEXT("Dragging a corner promotes it to Free"),
        Ops::EditHandle(Point, 2, FVector(7.0, 0.0, 0.0)) &&
        Ops::EffectiveHandleMode(Point) == EStrandBezierHandleMode::Free);
    TestTrue(TEXT("Free mode leaves other handle alone"),
        Ops::EditHandle(Point, 1, FVector(0.0, 11.0, 0.0)) &&
        Point.LeaveTangent.Equals(FVector(7.0, 0.0, 0.0), 0.000001));

    TestTrue(TEXT("Auto mode restores native Unreal spline smoothing"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Auto, FVector(0.0, 11.0, 0.0), FVector(7.0, 0.0, 0.0)));
    TestEqual(TEXT("Auto mode maps to native Curve interpolation"),
        Point.PointType.GetValue(), ESplinePointType::Curve);
    TestEqual(TEXT("Auto mode reports automatically even for legacy Curve anchors"),
        Ops::EffectiveHandleMode(Point), EStrandBezierHandleMode::Auto);
    TestTrue(TEXT("Breaking auto preserves evaluated native tangents"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Free, FVector(12.0, 24.0, 0.0), FVector(12.0, 24.0, 0.0)) &&
        Point.ArriveTangent.Equals(FVector(12.0, 24.0, 0.0)) &&
        Point.LeaveTangent.Equals(FVector(12.0, 24.0, 0.0)) &&
        Point.PointType.GetValue() == ESplinePointType::CurveCustomTangent);
    TestFalse(TEXT("Repeating unchanged Free mode is a no-op"), Ops::SetHandleMode(Point,
        EStrandBezierHandleMode::Free, Point.ArriveTangent, Point.LeaveTangent));
    TestFalse(TEXT("Invalid handle index cannot mutate data"), Ops::EditHandle(Point, 3, FVector(1.0, 0.0, 0.0)));

    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData Second;
    Second.LocalPosition = FVector(100.0, 0.0, 0.0);
    Asset->Points = { Point, Second };
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Native);
    TestEqual(TEXT("Native spline still receives exactly the authored point count"),
        Native->GetNumberOfSplinePoints(), 2);
    TestTrue(TEXT("Native geometry retains authored custom tangents"),
        Native->GetSplinePointAt(0, ESplineCoordinateSpace::Local).LeaveTangent.Equals(Point.LeaveTangent));
    TArray<FSplinePointData> Drawn;
    TestEqual(TEXT("Pen click creates an anchor"),
        Ops::AppendPenPoint(Drawn, FVector(20.0, 15.0, 0.0), false, false), 0);
    TestTrue(TEXT("Pen click-only anchors are corners"),
        Ops::EffectiveHandleMode(Drawn[0]) == EStrandBezierHandleMode::Corner);
    TestTrue(TEXT("Pen dragging creates symmetrical mirrored handles"),
        Ops::SetPenHandles(Drawn, 0, FVector(10.0, 20.0, 0.0)) &&
        Ops::EffectiveHandleMode(Drawn[0]) == EStrandBezierHandleMode::Mirrored &&
        Drawn[0].ArriveTangent.Equals(Drawn[0].LeaveTangent));
    TestTrue(TEXT("Strand authoring mode survives a UObject duplicate"),
        DuplicateObject<USplineDataAsset>(Asset, GetTransientPackage())->Points[0].HandleMode ==
        EStrandBezierHandleMode::Free);
    return true;
}


// Direct Select-mode insertion uses the same shape-preserving core as Pen,
// but targets a specific segment/parameter from the viewport hit proxy.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandDirectSegmentInsertionTest,
    "Strand.Editor.Authoring.DirectSegmentInsertion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandDirectSegmentInsertionTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B;
    A.LocalPosition = FVector(0, 0, 0);
    B.LocalPosition = FVector(100, 0, 0);
    A.PointType = B.PointType = ESplinePointType::Linear;
    Asset->Points = { A, B };
    Asset->ApplyToSplineComponent(Native);
    const int32 NewIndex = StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, 0.35);
    TestEqual(TEXT("Direct segment insertion selects the inserted anchor"), NewIndex, 1);
    TestEqual(TEXT("Direct insertion creates exactly one point"), Asset->Points.Num(), 3);
    TestTrue(TEXT("Inserted anchor follows picked parameter"),
        Asset->Points.IsValidIndex(1) &&
        Asset->Points[1].LocalPosition.Equals(FVector(35, 0, 0), 0.0001));
    TestTrue(TEXT("Open spline endpoints remain unchanged"),
        Asset->Points[0].LocalPosition.Equals(A.LocalPosition) &&
        Asset->Points.Last().LocalPosition.Equals(B.LocalPosition));
    Asset->ApplyToSplineComponent(Native);
    TestTrue(TEXT("Inserted native curve remains a straight segment"),
        Native->GetLocationAtSplineInputKey(0.5f, ESplineCoordinateSpace::Local).Equals(FVector(17.5, 0, 0), 0.001) &&
        Native->GetLocationAtSplineInputKey(1.5f, ESplineCoordinateSpace::Local).Equals(FVector(67.5, 0, 0), 0.001));
    const int32 Count = Asset->Points.Num();
    TestEqual(TEXT("Reject invalid segment"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, Count - 1, 0.5), INDEX_NONE);
    TestEqual(TEXT("Invalid direct insertion is non-destructive"), Asset->Points.Num(), Count);
    return true;
}

// v0.6.23 precision insertion actions reuse native cubic geometry splitting.
// Test several fractions independently so the 25/50/75 menu cannot silently
// regress into equally spaced straight-line placement or destructive resampling.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionSegmentFractionsTest,
    "Strand.Editor.Authoring.PrecisionSegmentFractions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandPrecisionSegmentFractionsTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B;
    A.LocalPosition = FVector(0.0, 40.0, 0.0);
    B.LocalPosition = FVector(180.0, 110.0, 65.0);
    A.PointType = B.PointType = ESplinePointType::CurveCustomTangent;
    A.LeaveTangent = FVector(250.0, -110.0, 170.0);
    B.ArriveTangent = FVector(40.0, 230.0, -75.0);
    for (const double Fraction : { 0.25, 0.5, 0.75 })
    {
        Asset->Points = { A, B };
        Asset->bIsClosedLoop = false;
        Asset->ApplyToSplineComponent(Native);
        TArray<FVector> Before;
        for (int32 Index = 0; Index <= 100; ++Index)
        {
            Before.Add(Native->GetLocationAtSplineInputKey(Index / 100.0, ESplineCoordinateSpace::Local));
        }
        const int32 Inserted = StrandSplineEditOperations::InsertOnSegment(Asset->Points,
            false, Native, 0, Fraction);
        TestEqual(TEXT("Precision fraction inserts the middle anchor"), Inserted, 1);
        TestEqual(TEXT("Precision fraction makes exactly three anchors"), Asset->Points.Num(), 3);
        if (!Asset->Points.IsValidIndex(1)) { return false; }
        const FVector ExpectedPosition = Before[FMath::RoundToInt(Fraction * 100.0)];
        TestTrue(TEXT("Inserted point matches evaluated 3D cubic"),
            Asset->Points[1].LocalPosition.Equals(ExpectedPosition, 0.01));
        Asset->ApplyToSplineComponent(Native);
        double MaximumError = 0.0;
        for (int32 Index = 0; Index <= 100; ++Index)
        {
            const double T = Index / 100.0;
            const double NewT = T <= Fraction ? T / Fraction : 1.0 + (T - Fraction) / (1.0 - Fraction);
            MaximumError = FMath::Max(MaximumError, FVector::Distance(Before[Index],
                Native->GetLocationAtSplineInputKey(NewT, ESplineCoordinateSpace::Local)));
        }
        TestTrue(TEXT("Precision insertion preserves entire cubic within 0.01 cm"), MaximumError < 0.01);
    }
    return true;
}

// Direct Ctrl-drag reshaping uses a least-norm Hermite derivative update.
// Point positions stay fixed, the cursor-picked interior point follows the
// intended 3D delta, and the unaffected neighboring segment does not jump.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandDirectSegmentBendTest,
    "Strand.Editor.Authoring.DirectSegmentBend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandDirectSegmentBendTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(-150.0, 10.0, 20.0);
    B.LocalPosition = FVector(100.0, 55.0, -35.0);
    C.LocalPosition = FVector(250.0, 190.0, 100.0);
    A.PointType = B.PointType = C.PointType = ESplinePointType::Curve;
    Asset->Points = { A, B, C };
    Asset->ApplyToSplineComponent(Native);
    const FSplinePoint P0 = Native->GetSplinePointAt(0, ESplineCoordinateSpace::Local);
    const FSplinePoint P1 = Native->GetSplinePointAt(1, ESplineCoordinateSpace::Local);
    const FVector OriginalMiddle = Native->GetLocationAtSplineInputKey(0.4f, ESplineCoordinateSpace::Local);
    const FVector OriginalNextSpan = Native->GetLocationAtSplineInputKey(1.35f, ESplineCoordinateSpace::Local);
    const FVector Delta(0.0, 80.0, -25.0);
    TestTrue(TEXT("Direct bend accepts an interior 3D curve segment"),
        StrandSplineEditOperations::BendSegment(Asset->Points, false, 0, 0.4, Delta, P0, P1));
    TestEqual(TEXT("Direct bend retains original point count"), Asset->Points.Num(), 3);
    TestTrue(TEXT("Direct bend keeps first anchor position"), Asset->Points[0].LocalPosition.Equals(A.LocalPosition));
    TestTrue(TEXT("Direct bend keeps second anchor position"), Asset->Points[1].LocalPosition.Equals(B.LocalPosition));
    TestTrue(TEXT("Direct bend keeps unrelated anchor position"), Asset->Points[2].LocalPosition.Equals(C.LocalPosition));
    TestTrue(TEXT("Direct bend switches to independent custom handles"),
        Asset->Points[0].PointType == ESplinePointType::CurveCustomTangent &&
        Asset->Points[1].PointType == ESplinePointType::CurveCustomTangent &&
        Asset->Points[0].HandleMode == EStrandBezierHandleMode::Free);
    Asset->ApplyToSplineComponent(Native);
    TestTrue(TEXT("Dragged location follows precise 3D cursor delta"),
        Native->GetLocationAtSplineInputKey(0.4f, ESplineCoordinateSpace::Local).Equals(OriginalMiddle + Delta, 0.01));
    TestTrue(TEXT("Neighbor segment keeps its original geometry"),
        Native->GetLocationAtSplineInputKey(1.35f, ESplineCoordinateSpace::Local).Equals(OriginalNextSpan, 0.01));

    TArray<FSplinePointData> Invalid = { A, B };
    const FSplinePointData Before = Invalid[0];
    TestFalse(TEXT("Reject near-endpoint drag to avoid extreme tangent amplification"),
        StrandSplineEditOperations::BendSegment(Invalid, false, 0, 0.01, Delta, P0, P1));
    TestTrue(TEXT("Rejected edit preserves original tangent"),
        Invalid[0].LeaveTangent.Equals(Before.LeaveTangent));
    TestFalse(TEXT("Reject invalid segment index"),
        StrandSplineEditOperations::BendSegment(Invalid, false, 1, 0.5, Delta, P0, P1));
    TestFalse(TEXT("Reject non-finite delta"),
        StrandSplineEditOperations::BendSegment(Invalid, false, 0, 0.5,
            FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), P0, P1));

    // Linear segments must transition to cubic smoothly, without a jump in
    // the baseline straight line before the actual curvature edit is applied.
    A.PointType = B.PointType = ESplinePointType::Linear;
    Asset->Points = { A, B };
    Asset->ApplyToSplineComponent(Native);
    const FSplinePoint L0 = Native->GetSplinePointAt(0, ESplineCoordinateSpace::Local);
    const FSplinePoint L1 = Native->GetSplinePointAt(1, ESplineCoordinateSpace::Local);
    const FVector LinearOriginal = Native->GetLocationAtSplineInputKey(0.5f, ESplineCoordinateSpace::Local);
    TestTrue(TEXT("Linear segment can be bent into an editable cubic"),
        StrandSplineEditOperations::BendSegment(Asset->Points, false, 0, 0.5, Delta, L0, L1));
    Asset->ApplyToSplineComponent(Native);
    TestTrue(TEXT("Linear-to-curve bending follows the requested offset"),
        Native->GetLocationAtSplineInputKey(0.5f, ESplineCoordinateSpace::Local).Equals(LinearOriginal + Delta, 0.01));
    return true;
}


// Camera fit is tested in isolation: no editor window is required to prove
// every evaluated curve sample projects within the requested safe viewport.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplineComfortFrameTest,
    "Strand.Editor.Viewport.ComfortFrameSpline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplineComfortFrameTest::RunTest(const FString& Parameters)
{
    TArray<FVector> Points;
    for (int32 I = 0; I <= 128; ++I)
    {
        const double T = static_cast<double>(I) / 128.0;
        Points.Add(FVector(1000.0 * (T - 0.5), 280.0 * FMath::Sin(T * 9.0),
            360.0 * FMath::Cos(T * 5.0) + 250.0 * T));
    }
    const FRotator View(-20.0, 45.0, 0.0);
    for (const double Aspect : {0.65, 1.0, 1.78, 2.5})
    {
        StrandSplineFraming::FPerspectiveFit Fit;
        if (!TestTrue(TEXT("Camera fit exists for valid perspective samples"),
            StrandSplineFraming::FitPerspective(Points, View, 90.0, Aspect, 0.74, Fit)))
        {
            return false;
        }
        const FQuat Camera = View.Quaternion();
        const double HTan = FMath::Tan(FMath::DegreesToRadians(45.0));
        const double VTan = HTan / Aspect;
        double MinX = DBL_MAX, MaxX = -DBL_MAX;
        double MinY = DBL_MAX, MaxY = -DBL_MAX;
        for (const FVector& P : Points)
        {
            const FVector Relative = P - Fit.Location;
            const double Depth = FVector::DotProduct(Relative, Camera.GetForwardVector());
            if (!TestTrue(TEXT("All framed samples in front of camera"), Depth > 10.0)) { return false; }
            const double X = FVector::DotProduct(Relative, Camera.GetRightVector()) / (Depth * HTan);
            const double Y = FVector::DotProduct(Relative, Camera.GetUpVector()) / (Depth * VTan);
            MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
            MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
            if (!TestTrue(TEXT("Every point is comfortably within the perspective viewport"),
                FMath::Abs(X) <= 0.741 && FMath::Abs(Y) <= 0.741)) { return false; }
        }
        TestTrue(TEXT("Projected horizontal extents are optically centered"),
            FMath::Abs(MinX + MaxX) < 0.025);
        TestTrue(TEXT("Projected vertical extents are optically centered"),
            FMath::Abs(MinY + MaxY) < 0.025);
    }
    StrandSplineFraming::FPerspectiveFit Invalid;
    TestFalse(TEXT("Invalid aspect rejected"),
        StrandSplineFraming::FitPerspective(Points, View, 90.0, 0.0, 0.74, Invalid));
    TestFalse(TEXT("Empty points rejected"),
        StrandSplineFraming::FitPerspective(TArray<FVector>(), View, 90.0, 1.2, 0.74, Invalid));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSelectSharpCornersTest,
    "Strand.Editor.Authoring.SelectSharpCorners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSelectSharpCornersTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(0.0, 0.0, 0.0);
    B.LocalPosition = FVector(100.0, 0.0, 0.0);
    C.LocalPosition = FVector(100.0, 100.0, 0.0);
    A.PointType = B.PointType = C.PointType = ESplinePointType::Linear;
    Asset->Points = {A, B, C};
    Asset->ApplyToSplineComponent(Native);
    const TArray<int32> Corners = StrandSplineEditOperations::FindSharpCorners(Native, 45.0);
    TestEqual(TEXT("Linear right angle has one interior corner"), Corners.Num(), 1);
    if (Corners.Num() == 1) { TestEqual(TEXT("90-degree corner is middle point"), Corners[0], 1); }
    TestTrue(TEXT("120-degree threshold excludes a right angle"),
        StrandSplineEditOperations::FindSharpCorners(Native, 120.0).IsEmpty());
    Asset->bIsClosedLoop = true;
    Asset->ApplyToSplineComponent(Native);
    TestEqual(TEXT("Closed right triangle includes seam corners"),
        StrandSplineEditOperations::FindSharpCorners(Native, 45.0).Num(), 3);
    Asset->bIsClosedLoop = false;
    B.PointType = ESplinePointType::CurveCustomTangent;
    B.ArriveTangent = FVector(100.0, 0.0, 0.0);
    B.LeaveTangent = FVector(-100.0, 0.0, 0.0);
    Asset->Points[1] = B;
    Asset->ApplyToSplineComponent(Native);
    TestEqual(TEXT("Custom-tangent reversal creates a cusp"),
        StrandSplineEditOperations::FindSharpCorners(Native, 170.0).Num(), 1);
    TestTrue(TEXT("Invalid threshold does not select points"),
        StrandSplineEditOperations::FindSharpCorners(Native, 0.0).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandLassoSelectionGeometryTest,
    "Strand.Editor.Authoring.LassoSelectionGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandLassoSelectionGeometryTest::RunTest(const FString& Parameters)
{
    using namespace StrandSplineSelectionUtils;
    TArray<FIntPoint> Polygon = { FIntPoint(10, 10), FIntPoint(90, 10),
        FIntPoint(90, 70), FIntPoint(50, 45), FIntPoint(10, 70) };
    TestTrue(TEXT("Closed freehand stroke recognized"), IsSelectionStroke(Polygon));
    TestTrue(TEXT("Point inside outline selected"), IsInsideLasso(FVector2D(50, 20), Polygon));
    TestTrue(TEXT("Edge hit included"), IsInsideLasso(FVector2D(10, 30), Polygon));
    TestFalse(TEXT("Concave indentation excluded"), IsInsideLasso(FVector2D(50, 65), Polygon));
    TestFalse(TEXT("Outside anchor excluded"), IsInsideLasso(FVector2D(120, 40), Polygon));
    TArray<FIntPoint> Reverse = Polygon;
    for (int32 I = 0, J = Reverse.Num() - 1; I < J; ++I, --J) { Reverse.Swap(I, J); }
    TestTrue(TEXT("Opposite drawing direction works"), IsInsideLasso(FVector2D(50, 20), Reverse));
    TestFalse(TEXT("Opposite drawing direction excludes indentation"), IsInsideLasso(FVector2D(50, 65), Reverse));
    const TArray<FIntPoint> TinyStroke = { FIntPoint(1, 1), FIntPoint(2, 2), FIntPoint(3, 2) };
    const TArray<FIntPoint> ShortStroke = { FIntPoint(1, 1), FIntPoint(2, 2) };
    TestFalse(TEXT("Tiny stroke treated as click"), IsSelectionStroke(TinyStroke));
    TestFalse(TEXT("Unclosed short stroke is not a polygon"), IsInsideLasso(FVector2D(50, 20), ShortStroke));
    TSet<int32> InitialSelection;
    InitialSelection.Add(2);
    const TSet<int32> Grown = GrowAnchorSelection(InitialSelection, 5, false);
    TestTrue(TEXT("Selection grows to previous neighbor"), Grown.Contains(1));
    TestTrue(TEXT("Selection grows to next neighbor"), Grown.Contains(3));
    TestEqual(TEXT("Grow selects three anchors"), Grown.Num(), 3);
    const TSet<int32> Shrunk = ShrinkAnchorSelection(Grown, 5, false);
    TestTrue(TEXT("Shrink returns center anchor"), Shrunk.Contains(2));
    TestEqual(TEXT("Shrink removes both boundaries"), Shrunk.Num(), 1);
    TSet<int32> Seam;
    Seam.Add(0);
    const TSet<int32> Wrapped = GrowAnchorSelection(Seam, 5, true);
    TestTrue(TEXT("Closed-loop grow wraps across seam"), Wrapped.Contains(4));
    TestFalse(TEXT("Open-loop grow never wraps"), GrowAnchorSelection(Seam, 5, false).Contains(4));
        TArray<FIntPoint> Path = {FIntPoint(0,0)};
    TestFalse(TEXT("Subpixel motion does not spam lasso samples"), ShouldAppendStrokePoint(Path, FIntPoint(1,1)));
    TestTrue(TEXT("Significant motion appends a lasso sample"), ShouldAppendStrokePoint(Path, FIntPoint(5,5)));
    return true;
}

// Complement to split: join from two selected open assets, preserving both
// original 3D curves and auto-evaluated tangent geometry at the shared seam.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandJoinTwoAssetsTest,
    "Strand.Editor.Authoring.JoinTwoOpenSplineAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandJoinTwoAssetsTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Source = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FSplinePointData Point;
        Point.LocalPosition = FVector(Index * 140.0,
            FMath::Sin(Index * 0.6) * 115.0, FMath::Cos(Index * 0.47) * 72.0);
        Point.PointType = ESplinePointType::Curve;
        Source->Points.Add(Point);
    }
    Source->ApplyToSplineComponent(Native);
    TArray<FSplinePointData> Left, Right;
    if (!TestTrue(TEXT("Can split the source"),
        StrandSplineEditOperations::SplitOpenSplineAtAnchor(Source->Points, false,
            Native, 2, Left, Right))) { return false; }
    USplineDataAsset* LeftAsset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineDataAsset* RightAsset = NewObject<USplineDataAsset>(GetTransientPackage());
    LeftAsset->Points = Left;
    RightAsset->Points = Right;
    USplineComponent* LeftNative = NewObject<USplineComponent>(GetTransientPackage());
    USplineComponent* RightNative = NewObject<USplineComponent>(GetTransientPackage());
    LeftAsset->ApplyToSplineComponent(LeftNative);
    RightAsset->ApplyToSplineComponent(RightNative);

    TArray<FSplinePointData> Joined;
    double Gap = -1.0;
    bool bWelded = false;
    TestTrue(TEXT("Join split halves"), StrandSplineEditOperations::JoinOpenSplineAssets(
        Left, false, LeftNative, Right, false, RightNative, Joined, Gap, bWelded));
    TestTrue(TEXT("Shared endpoint welded"), bWelded);
    TestTrue(TEXT("Weld has zero gap"), FMath::IsNearlyZero(Gap, 0.001));
    TestEqual(TEXT("Weld restores exact original anchor count"), Joined.Num(), 5);
    TestEqual(TEXT("Source remains untouched"), Source->Points.Num(), 5);
    TestEqual(TEXT("Original auto interpolation unchanged"),
        Source->Points[2].PointType.GetValue(), ESplinePointType::Curve);
    USplineDataAsset* Result = NewObject<USplineDataAsset>(GetTransientPackage());
    Result->Points = Joined;
    USplineComponent* JoinedNative = NewObject<USplineComponent>(GetTransientPackage());
    Result->ApplyToSplineComponent(JoinedNative);
    for (int32 Edge = 0; Edge < 4; ++Edge)
    {
        for (int32 Step = 0; Step <= 12; ++Step)
        {
            const double Key = Edge + Step / 12.0;
            TestTrue(TEXT("Joined curve preserves original sampled 3D geometry"),
                JoinedNative->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::Local).Equals(
                    Native->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::Local), 0.01));
        }
    }

    // Noncoincident ends: preserve both geometries with an explicit linear bridge.
    FSplinePointData A0, A1, B0, B1;
    A0.LocalPosition = FVector(0, 0, 0);
    A1.LocalPosition = FVector(100, 0, 0);
    B0.LocalPosition = FVector(220, 20, 10);
    B1.LocalPosition = FVector(340, 20, 10);
    A0.PointType = A1.PointType = B0.PointType = B1.PointType = ESplinePointType::Linear;
    USplineDataAsset* First = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineDataAsset* Second = NewObject<USplineDataAsset>(GetTransientPackage());
    First->Points = {A0, A1};
    Second->Points = {B0, B1};
    First->ApplyToSplineComponent(LeftNative);
    Second->ApplyToSplineComponent(RightNative);
    TArray<FSplinePointData> Bridge;
    TestTrue(TEXT("Separated assets can join with explicit bridge"),
        StrandSplineEditOperations::JoinOpenSplineAssets(First->Points, false, LeftNative,
            Second->Points, false, RightNative, Bridge, Gap, bWelded));
    TestFalse(TEXT("Separate ends are not welded"), bWelded);
    TestEqual(TEXT("Separate join keeps all anchors"), Bridge.Num(), 4);
    TestEqual(TEXT("Bridge is linear"), Bridge[1].PointType.GetValue(), ESplinePointType::Linear);

    // The closest pair may require reversing the second asset.
    Second->Points[0].LocalPosition = FVector(340, 0, 0);
    Second->Points[1].LocalPosition = FVector(100, 0, 0);
    Second->ApplyToSplineComponent(RightNative);
    TArray<FSplinePointData> ReverseJoin;
    TestTrue(TEXT("Join automatically orients nearest endpoints"),
        StrandSplineEditOperations::JoinOpenSplineAssets(First->Points, false, LeftNative,
            Second->Points, false, RightNative, ReverseJoin, Gap, bWelded));
    TestTrue(TEXT("Nearest ends are welded after reversing second curve"), bWelded);
    TestEqual(TEXT("Reverse join has three anchors"), ReverseJoin.Num(), 3);
    TestTrue(TEXT("Reversed asset retains its far endpoint"),
        ReverseJoin.Last().LocalPosition.Equals(FVector(340, 0, 0)));

    const int32 UnmodifiedCount = ReverseJoin.Num();
    First->bIsClosedLoop = true;
    TestFalse(TEXT("Closed spline is rejected"),
        StrandSplineEditOperations::JoinOpenSplineAssets(First->Points, true, LeftNative,
            Second->Points, false, RightNative, ReverseJoin, Gap, bWelded));
    TestEqual(TEXT("Invalid request does not alter caller output"), ReverseJoin.Num(), UnmodifiedCount);
    return true;
}

// Non-destructive source split: both outputs share one internal boundary anchor
// and preserve the native evaluated cubic, including automatic interpolation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplitAtAnchorTest,
    "Strand.Editor.Authoring.SplitOpenSplineAtAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplitAtAnchorTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Source = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FSplinePointData Point;
        Point.LocalPosition = FVector(Index * 100.0, FMath::Sin(Index * 0.7) * 90.0,
            FMath::Cos(Index * 0.8) * 50.0);
        Point.PointType = ESplinePointType::Curve;
        Source->Points.Add(Point);
    }
    Source->ApplyToSplineComponent(Native);
    TArray<FVector> Before;
    for (int32 Edge = 0; Edge < 4; ++Edge)
    {
        for (int32 Step = 0; Step <= 10; ++Step)
        {
            Before.Add(Native->GetLocationAtSplineInputKey(Edge + Step / 10.0f,
                ESplineCoordinateSpace::Local));
        }
    }
    TArray<FSplinePointData> Start, End;
    TestTrue(TEXT("Split valid internal anchor"), StrandSplineEditOperations::SplitOpenSplineAtAnchor(
        Source->Points, false, Native, 2, Start, End));
    TestEqual(TEXT("First half has three anchors"), Start.Num(), 3);
    TestEqual(TEXT("Second half has three anchors"), End.Num(), 3);
    TestTrue(TEXT("Both halves share the exact boundary anchor"),
        Start.Last().LocalPosition.Equals(End[0].LocalPosition));
    TestEqual(TEXT("Original source stays unchanged"), Source->Points.Num(), 5);
    TestEqual(TEXT("Original automatic interpolation stays unchanged"),
        Source->Points[2].PointType.GetValue(), ESplinePointType::Curve);
    USplineDataAsset* SegmentAsset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Half = NewObject<USplineComponent>(GetTransientPackage());
    for (int32 Side = 0; Side < 2; ++Side)
    {
        SegmentAsset->Points = Side == 0 ? Start : End;
        SegmentAsset->bIsClosedLoop = false;
        SegmentAsset->ApplyToSplineComponent(Half);
        for (int32 Edge = 0; Edge < 2; ++Edge)
        {
            for (int32 Step = 0; Step <= 10; ++Step)
            {
                const FVector Actual = Half->GetLocationAtSplineInputKey(Edge + Step / 10.0f,
                    ESplineCoordinateSpace::Local);
                const FVector Expected = Before[(Edge + (Side == 0 ? 0 : 2)) * 11 + Step];
                TestTrue(TEXT("Split half matches original evaluated curve"),
                    Actual.Equals(Expected, 0.01));
            }
        }
    }
    TArray<FSplinePointData> InvalidStart, InvalidEnd;
    TestFalse(TEXT("Cannot split a closed loop"),
        StrandSplineEditOperations::SplitOpenSplineAtAnchor(Source->Points, true, Native,
            2, InvalidStart, InvalidEnd));
    TestFalse(TEXT("Cannot split at endpoint"),
        StrandSplineEditOperations::SplitOpenSplineAtAnchor(Source->Points, false, Native,
            0, InvalidStart, InvalidEnd));
    TestTrue(TEXT("Invalid split leaves output untouched"),
        InvalidStart.IsEmpty() && InvalidEnd.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandWorkspaceInputIsolationTest,
    "Strand.Editor.Authoring.WorkspaceInputIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandWorkspaceInputIsolationTest::RunTest(const FString& Parameters)
{
    using namespace StrandSplineInputUtils;
    TestTrue(TEXT("Shift+Control+J nudges negative X"),
        NudgeDirection(EKeys::J, true, true, false).Equals(FVector(-1, 0, 0)));
    TestTrue(TEXT("Shift+Control+L nudges positive X"),
        NudgeDirection(EKeys::L, true, true, false).Equals(FVector(1, 0, 0)));
    TestTrue(TEXT("Shift+Control+I nudges positive Y"),
        NudgeDirection(EKeys::I, true, true, false).Equals(FVector(0, 1, 0)));
    TestTrue(TEXT("Shift+Control+K nudges negative Y"),
        NudgeDirection(EKeys::K, true, true, false).Equals(FVector(0, -1, 0)));
    TestTrue(TEXT("Shift+Control+U nudges positive Z"),
        NudgeDirection(EKeys::U, true, true, false).Equals(FVector(0, 0, 1)));
    TestTrue(TEXT("Shift+Control+O nudges negative Z"),
        NudgeDirection(EKeys::O, true, true, false).Equals(FVector(0, 0, -1)));
    TestTrue(TEXT("Native arrow navigation is not consumed as a nudge"),
        NudgeDirection(EKeys::Left, true, true, false).IsNearlyZero() &&
        NudgeDirection(EKeys::Right, true, true, false).IsNearlyZero() &&
        NudgeDirection(EKeys::Up, true, true, false).IsNearlyZero() &&
        NudgeDirection(EKeys::Down, true, true, false).IsNearlyZero());
    TestTrue(TEXT("Native PageUp/PageDown navigation is not consumed as a nudge"),
        NudgeDirection(EKeys::PageUp, true, true, false).IsNearlyZero() &&
        NudgeDirection(EKeys::PageDown, true, true, false).IsNearlyZero());
    TestTrue(TEXT("Unmodified keys never nudge"),
        NudgeDirection(EKeys::J, false, false, false).IsNearlyZero() &&
        NudgeDirection(EKeys::J, false, true, false).IsNearlyZero());
    TestTrue(TEXT("Alt camera modifier suppresses nudge"),
        NudgeDirection(EKeys::J, true, true, true).IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandContextualPrecisionEditingTest,
    "Strand.Editor.Authoring.ContextualPrecisionEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandContextualPrecisionEditingTest::RunTest(const FString& Parameters)
{
    // The context menu, Shift-click insertion preview, and Pen use the same
    // shape-preserving segment splitter. Exercise a real spatial cubic rather
    // than just a linear test fixture, and verify precision movements.
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B;
    A.LocalPosition = FVector(10.0, -30.0, 20.0);
    B.LocalPosition = FVector(140.0, 50.0, 80.0);
    A.LeaveTangent = FVector(150.0, 90.0, -40.0);
    B.ArriveTangent = FVector(25.0, -100.0, 70.0);
    A.PointType = B.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points = {A, B};
    Asset->ApplyToSplineComponent(Native);
    constexpr double Split = 0.4;
    const double Samples[] = {0.05, 0.21, 0.36, 0.52, 0.76, 0.93};
    TArray<FVector> OriginalCurve;
    for (double T : Samples)
    {
        OriginalCurve.Add(Native->GetLocationAtSplineInputKey(static_cast<float>(T), ESplineCoordinateSpace::Local));
    }
    const int32 InsertIndex = StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, Split);
    TestEqual(TEXT("Contextual insertion returns the new anchor index"), InsertIndex, 1);
    TestEqual(TEXT("Contextual cubic insert creates one anchor"), Asset->Points.Num(), 3);
    Asset->ApplyToSplineComponent(Native);
    for (int32 I = 0; I < UE_ARRAY_COUNT(Samples); ++I)
    {
        const double T = Samples[I];
        const double Mapped = T < Split ? T / Split : 1.0 + (T - Split) / (1.0 - Split);
        TestTrue(*FString::Printf(TEXT("Cubic geometry preserved at sample %d"), I),
            Native->GetLocationAtSplineInputKey(static_cast<float>(Mapped), ESplineCoordinateSpace::Local).Equals(OriginalCurve[I], 0.05));
    }
    const FVector FixedStart = Asset->Points[0].LocalPosition;
    const FVector FixedEnd = Asset->Points[2].LocalPosition;
    const FVector PreviousHandle = Asset->Points[1].LeaveTangent;
    TestTrue(TEXT("Precision nudge applies to the selected anchor"),
        StrandSplineEditOperations::TranslateSelected(Asset->Points, TArray<int32>{1}, FVector(1.0, -10.0, 0.0)));
    TestTrue(TEXT("Nudge changes position by exact centimeters"),
        Asset->Points[1].LocalPosition.Equals(Native->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::Local) + FVector(1, -10, 0), 0.0001));
    TestTrue(TEXT("Nudge leaves other anchors and tangent vectors intact"),
        Asset->Points[0].LocalPosition.Equals(FixedStart) &&
        Asset->Points[2].LocalPosition.Equals(FixedEnd) &&
        Asset->Points[1].LeaveTangent.Equals(PreviousHandle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandGroupEditingTest,
    "Strand.Editor.Authoring.GroupEditingOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandGroupEditingTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(5);
    Points[0].LocalPosition = FVector(-100, 5, 0);
    Points[1].LocalPosition = FVector(0, 7, 20);
    Points[2].LocalPosition = FVector(30, 12, 10);
    Points[3].LocalPosition = FVector(90, 16, 40);
    Points[4].LocalPosition = FVector(300, -5, 0);
    const TArray<int32> Selection = { 1, 2, 3 };
    const FVector Center = StrandSplineEditOperations::SelectionCenter(Points, Selection);
    TestTrue(TEXT("Selection center averages only chosen control points"),
        Center.Equals(FVector(40, 35.0 / 3.0, 70.0 / 3.0), 0.000001));
    const FVector UnselectedBefore = Points[0].LocalPosition;
    const FVector UnselectedAfter = Points[4].LocalPosition;
    TestTrue(TEXT("Group translate changes selected points"), StrandSplineEditOperations::TranslateSelected(
        Points, Selection, FVector(100, 0, -10)));
    TestTrue(TEXT("Primary point maintains correct offset"),
        Points[2].LocalPosition.Equals(FVector(130, 12, 0)));
    TestTrue(TEXT("Group translate leaves unselected endpoints"),
        Points[0].LocalPosition.Equals(UnselectedBefore) && Points[4].LocalPosition.Equals(UnselectedAfter));
    TestTrue(TEXT("Align selection along Y"), StrandSplineEditOperations::AlignSelected(Points, Selection, 1));
    TestTrue(TEXT("Aligned points share Y"),
        FMath::IsNearlyEqual(Points[1].LocalPosition.Y, Points[2].LocalPosition.Y) &&
        FMath::IsNearlyEqual(Points[2].LocalPosition.Y, Points[3].LocalPosition.Y));
    TestTrue(TEXT("Distribute selection along X"), StrandSplineEditOperations::DistributeSelected(Points, Selection, 0));
    TestTrue(TEXT("Intermediate anchor is midpoint of selection range"),
        FMath::IsNearlyEqual(Points[2].LocalPosition.X, 145.0));
    TestTrue(TEXT("Unselected endpoints remain untouched after align/distribute"),
        Points[0].LocalPosition.Equals(UnselectedBefore) && Points[4].LocalPosition.Equals(UnselectedAfter));
    const FVector FirstTangent(30, 0, 0);
    const FVector SecondTangent(0, 60, 0);
    TestTrue(TEXT("One-shot aligned tangents accepts nonzero driver"),
        StrandSplineEditOperations::MatchTangents(Points[2], FirstTangent, SecondTangent, false, true));
    TestTrue(TEXT("Aligned handles preserve separate lengths"),
        Points[2].ArriveTangent.Equals(FVector(30, 0, 0)) &&
        Points[2].LeaveTangent.Equals(FVector(60, 0, 0)));
    TestTrue(TEXT("One-shot mirrored tangents accept outgoing driver"),
        StrandSplineEditOperations::MatchTangents(Points[2], FirstTangent, SecondTangent, true, false));
    TestTrue(TEXT("Mirrored handles share equal vector"),
        Points[2].ArriveTangent.Equals(SecondTangent) &&
        Points[2].LeaveTangent.Equals(SecondTangent));
    // Group rotation/scaling also updates tangent vectors (but not position
    // offsets of unrelated points) and leaves the selection center unchanged.
    const FVector GroupPivot = StrandSplineEditOperations::SelectionCenter(Points, Selection);
    const FVector BeforeRotation = Points[1].LocalPosition;
    Points[1].ArriveTangent = FVector(12, 0, 0);
    TestTrue(TEXT("Rotate selected around centroid"),
        StrandSplineEditOperations::RotateSelected(Points, Selection, 2, 90.0));
    TestTrue(TEXT("Rotation preserves centroid"),
        StrandSplineEditOperations::SelectionCenter(Points, Selection).Equals(GroupPivot, 0.00001));
    TestTrue(TEXT("Rotation rotates tangent vectors, not their origins"),
        Points[1].ArriveTangent.Equals(FVector(0, 12, 0), 0.00001));
    TestTrue(TEXT("Rotation preserves radial distance"),
        FMath::IsNearlyEqual((BeforeRotation - GroupPivot).Size(),
            (Points[1].LocalPosition - GroupPivot).Size(), 0.00001));
    TestTrue(TEXT("Uniform scale modifies selected geometry"),
        StrandSplineEditOperations::ScaleSelected(Points, Selection, 2.0));
    TestTrue(TEXT("Uniform scale doubles tangents"),
        Points[1].ArriveTangent.Equals(FVector(0, 24, 0), 0.00001));
    TestTrue(TEXT("Uniform scale retains group centroid"),
        StrandSplineEditOperations::SelectionCenter(Points, Selection).Equals(GroupPivot, 0.00001));
    TestTrue(TEXT("Transforms leave unselected endpoints intact"),
        Points[0].LocalPosition.Equals(UnselectedBefore) && Points[4].LocalPosition.Equals(UnselectedAfter));
    // Selection math used by screen-space Box Select: replace, extend,
    // toggle and out-of-range hits must not corrupt the selected point set.
    TSet<int32> BoxSelection;
    StrandSplineEditOperations::ApplySelectionHits(BoxSelection, 5, { 1, 2, 2, 99 }, false, false);
    TestTrue(TEXT("Box replace removes invalid and duplicate hits"),
        BoxSelection.Num() == 2 && BoxSelection.Contains(1) && BoxSelection.Contains(2));
    StrandSplineEditOperations::ApplySelectionHits(BoxSelection, 5, { 3 }, true, false);
    TestTrue(TEXT("Shift box adds without discarding the original selection"),
        BoxSelection.Num() == 3 && BoxSelection.Contains(1) && BoxSelection.Contains(3));
    StrandSplineEditOperations::ApplySelectionHits(BoxSelection, 5, { 2, 4 }, false, true);
    TestTrue(TEXT("Ctrl box toggles hits and retains other anchors"),
        BoxSelection.Num() == 3 && !BoxSelection.Contains(2) && BoxSelection.Contains(4));
    StrandSplineEditOperations::ApplySelectionHits(BoxSelection, 5, {}, false, false);
    TestTrue(TEXT("Empty unmodified box clears selection"), BoxSelection.IsEmpty());
    // Native transform widget also supports single-point shaping. Rotation
    // affects tangents and point orientation; scaling affects tangent lengths,
    // but must not move the anchor itself or unrelated points.
    {
        TArray<FSplinePointData> OnePoint = Points;
        const TArray<int32> One = { 2 };
        const FVector AnchorBefore = OnePoint[2].LocalPosition;
        const FVector OtherBefore = OnePoint[1].LocalPosition;
        OnePoint[2].ArriveTangent = FVector(10.0, 0.0, 0.0);
        TestTrue(TEXT("Single anchor rotation allowed"),
            StrandSplineEditOperations::RotateSelected(OnePoint, One, 2, 90.0));
        TestTrue(TEXT("Single anchor rotates its tangent but stays in place"),
            OnePoint[2].LocalPosition.Equals(AnchorBefore, 0.00001) &&
            OnePoint[2].ArriveTangent.Equals(FVector(0, 10, 0), 0.00001));
        TestTrue(TEXT("Single anchor scale allowed"),
            StrandSplineEditOperations::ScaleSelected(OnePoint, One, 2.0));
        TestTrue(TEXT("Single anchor scale affects tangents only"),
            OnePoint[2].LocalPosition.Equals(AnchorBefore, 0.00001) &&
            OnePoint[2].ArriveTangent.Equals(FVector(0, 20, 0), 0.00001) &&
            OnePoint[1].LocalPosition.Equals(OtherBefore, 0.00001));
    }
    // Arbitrary 3D rotations are required for Unreal's local-coordinate widget.
    {
        TArray<FSplinePointData> LocalPoints = Points;
        const TArray<int32> One = { 2 };
        const FVector AnchorBefore = LocalPoints[2].LocalPosition;
        LocalPoints[2].ArriveTangent = FVector(0.0, 10.0, 0.0);
        const FVector Axis = FVector(1.0, 1.0, 1.0).GetSafeNormal();
        const FQuat Delta(Axis, UE_PI / 3.0);
        TestTrue(TEXT("Arbitrary local-axis quaternion rotation accepted"),
            StrandSplineEditOperations::RotateSelectedQuat(LocalPoints, One, Delta));
        TestTrue(TEXT("Local-axis rotation preserves anchor position"),
            LocalPoints[2].LocalPosition.Equals(AnchorBefore, 0.00001));
        TestTrue(TEXT("Local-axis rotation transforms tangent direction using quaternion"),
            LocalPoints[2].ArriveTangent.Equals(Delta.RotateVector(FVector(0.0, 10.0, 0.0)), 0.00001));
        TestFalse(TEXT("Identity rotation rejected"),
            StrandSplineEditOperations::RotateSelectedQuat(LocalPoints, One, FQuat::Identity));
    }
    TestTrue(TEXT("Absolute numeric position resolves to target-minus-current"),
        FMath::IsNearlyEqual(StrandSplineEditOperations::PositionEntryDelta(-50.0, 125.0, false), 175.0));
    TestTrue(TEXT("Relative numeric position preserves signed offset"),
        FMath::IsNearlyEqual(StrandSplineEditOperations::PositionEntryDelta(-50.0, -25.0, true), -25.0));
    {
        const FQuat LocalFrame(FVector::ZAxisVector, UE_PI / 2.0);
        const FQuat InputDelta(FVector::XAxisVector, UE_PI / 4.0);
        const FQuat LocalDelta = StrandSplineEditOperations::WidgetRotationToAsset(InputDelta, LocalFrame, true);
        const FQuat WorldDelta = StrandSplineEditOperations::WidgetRotationToAsset(InputDelta, LocalFrame, false);
        TestTrue(TEXT("Local widget rotation follows point orientation"),
            LocalDelta.RotateVector(FVector::ZAxisVector).Equals(FVector(FMath::Sqrt(0.5), 0.0, FMath::Sqrt(0.5)), 0.00001));
        TestTrue(TEXT("World widget rotation ignores point orientation"),
            WorldDelta.RotateVector(FVector::YAxisVector).Equals(InputDelta.RotateVector(FVector::YAxisVector), 0.00001));
    }
    TestFalse(TEXT("Zero scale rejected"),
        StrandSplineEditOperations::ScaleSelected(Points, Selection, 0.0));
    TestFalse(TEXT("Invalid rotation axis rejected"),
        StrandSplineEditOperations::RotateSelected(Points, Selection, 5, 15.0));
    const TArray<FSplinePointData> Unchanged = Points;
    TestFalse(TEXT("Cannot distribute on invalid axis"),
        StrandSplineEditOperations::DistributeSelected(Points, Selection, 3));
    TestFalse(TEXT("Cannot translate with zero delta"),
        StrandSplineEditOperations::TranslateSelected(Points, Selection, FVector::ZeroVector));
    TestTrue(TEXT("Invalid operations do not touch unrelated point data"),
        Points[0].LocalPosition.Equals(Unchanged[0].LocalPosition));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplinePrimitiveAuthoringTest,
    "Strand.Editor.Authoring.SplinePrimitiveAuthoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplinePrimitiveAuthoringTest::RunTest(const FString& Parameters)
{
    for (int32 Kind = 0; Kind < 3; ++Kind)
    {
        TArray<FSplinePointData> Points;
        bool bClosed = false;
        TestTrue(TEXT("Primitive creation succeeds"),
            StrandSplineEditOperations::MakePrimitive(Kind, Points, bClosed));
        TestEqual(TEXT("Expected anchor count"), Points.Num(), Kind == 2 ? 25 : 4);
        TestEqual(TEXT("Expected closure"), bClosed, Kind != 2);
        USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
        Asset->Points = Points;
        Asset->bIsClosedLoop = bClosed;
        USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
        Asset->ApplyToSplineComponent(Native);
        TestEqual(TEXT("Native spline anchor count"), Native->GetNumberOfSplinePoints(), Points.Num());
        TestEqual(TEXT("Native spline closure"), Native->IsClosedLoop(), bClosed);
        TestTrue(TEXT("Primitive round-trip matches native component"), Asset->MatchesSplineComponent(Native, 0.001f));
        if (Kind == 0)
        {
            TestTrue(TEXT("Circle starts at +X"), Points[0].LocalPosition.Equals(FVector(100, 0, 0), 0.001));
            TestTrue(TEXT("Circle tangent points to +Y"), Points[0].LeaveTangent.Y > 160.0);
            TestTrue(TEXT("Circle circumference near expected"),
                FMath::Abs(Native->GetSplineLength() - 2.0 * UE_PI * 100.0) < 1.0);
        }
        if (Kind == 1)
        {
            TestTrue(TEXT("Rectangle uses linear interpolation"),
                Points[0].PointType == ESplinePointType::Linear);
            TestTrue(TEXT("Rectangle has expected perimeter"),
                FMath::Abs(Native->GetSplineLength() - 700.0) < 0.01);
        }
        if (Kind == 2)
        {
            TestTrue(TEXT("Helix rises along Z"), Points.Last().LocalPosition.Z > Points[0].LocalPosition.Z);
            TestTrue(TEXT("Helix preserves finite tangents"), !Points[5].LeaveTangent.ContainsNaN());
        }
    }
    TArray<FSplinePointData> Points;
    bool bClosed = false;
    TestFalse(TEXT("Invalid primitive type rejected"),
        StrandSplineEditOperations::MakePrimitive(-1, Points, bClosed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandInPlacePrimitiveReplacementTest,
    "Strand.Editor.Authoring.InPlacePrimitiveReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandInPlacePrimitiveReplacementTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Current = NewObject<USplineDataAsset>(GetTransientPackage());
    TestTrue(TEXT("Initial asset is valid"), Current != nullptr);
    if (!Current) { return false; }
    Current->DefaultAnchorPointIndex = 2;
    FSplinePointData Before;
    Before.LocalPosition = FVector(41, 17, -8);
    Current->Points.Add(Before);
    Current->bIsClosedLoop = false;
    USplineDataAsset* Identity = Current;

    for (int32 Kind : {0, 1, 2})
    {
        TestTrue(TEXT("In-place geometry replacement succeeds"),
            StrandSplineEditOperations::ReplacePrimitiveGeometry(*Current, Kind));
        TestTrue(TEXT("Same spline asset remains open"), Current == Identity);
        TestEqual(TEXT("Expected point count"), Current->Points.Num(), Kind == 2 ? 25 : 4);
        TestEqual(TEXT("Expected loop state"), Current->bIsClosedLoop, Kind != 2);
        TestEqual(TEXT("Placement settings are preserved"), Current->DefaultAnchorPointIndex, 2);
        USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
        Current->ApplyToSplineComponent(Native);
        TestTrue(TEXT("Replaced geometry retains native spline compatibility"),
            Current->MatchesSplineComponent(Native, 0.001f));
    }
    const TArray<FSplinePointData> Saved = Current->Points;
    const bool bSavedLoop = Current->bIsClosedLoop;
    TestFalse(TEXT("Unknown primitive cannot overwrite existing geometry"),
        StrandSplineEditOperations::ReplacePrimitiveGeometry(*Current, -1));
    TestEqual(TEXT("Invalid action preserves point count"), Current->Points.Num(), Saved.Num());
    TestEqual(TEXT("Invalid action preserves closed state"), Current->bIsClosedLoop, bSavedLoop);
    TestTrue(TEXT("Invalid action preserves first anchor"),
        Current->Points[0].LocalPosition.Equals(Saved[0].LocalPosition));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandArcLengthResamplingTest,
    "Strand.Editor.Authoring.ArcLengthResampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandArcLengthResamplingTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> SourcePoints;
    bool bClosed = false;
    TestTrue(TEXT("Circle primitive generated"), StrandSplineEditOperations::MakePrimitive(0, SourcePoints, bClosed));
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Native->ClearSplinePoints(false);
    for (int32 Index = 0; Index < SourcePoints.Num(); ++Index)
    {
        Native->AddPoint(SourcePoints[Index].ToSplinePoint(Index), false);
    }
    Native->SetClosedLoop(bClosed, true);
    TArray<FSplinePointData> NewPoints;
    TestTrue(TEXT("Closed curve resampled"), StrandSplineEditOperations::ResampleByArcLength(
        Native, SourcePoints, true, 16, NewPoints));
    TestEqual(TEXT("Exact resampled anchor count"), NewPoints.Num(), 16);
    if (NewPoints.Num() != 16) { return false; }
    // Technical-art workflows must support arbitrary exact counts, not just
    // the old 8/16/32/64 presets.
    TestTrue(TEXT("Arbitrary 37-point request succeeds"), StrandSplineEditOperations::ResampleByArcLength(
        Native, SourcePoints, true, 37, NewPoints));
    TestEqual(TEXT("Exact arbitrary anchor count"), NewPoints.Num(), 37);
    TestFalse(TEXT("Above hard cap rejected"), StrandSplineEditOperations::ResampleByArcLength(
        Native, SourcePoints, true, 4097, NewPoints));
    TestEqual(TEXT("Rejected count does not replace output"), NewPoints.Num(), 37);
    TestTrue(TEXT("First position preserved"), NewPoints[0].LocalPosition.Equals(SourcePoints[0].LocalPosition, 0.001));
    for (const FSplinePointData& Point : NewPoints)
    {
        TestFalse(TEXT("No NaN in resampled positions"), Point.LocalPosition.ContainsNaN());
        TestFalse(TEXT("No NaN in resampled tangents"), Point.LeaveTangent.ContainsNaN());
    }
    TArray<FSplinePointData> Unchanged = NewPoints;
    TestFalse(TEXT("Invalid zero samples rejected"), StrandSplineEditOperations::ResampleByArcLength(
        Native, SourcePoints, true, 0, NewPoints));
    TestEqual(TEXT("Invalid request does not modify existing output"), NewPoints.Num(), Unchanged.Num());
    Native->SetClosedLoop(false, true);
    TestTrue(TEXT("Open curve resampled"), StrandSplineEditOperations::ResampleByArcLength(
        Native, SourcePoints, false, 8, NewPoints));
    TestEqual(TEXT("Open curve count"), NewPoints.Num(), 8);
    TestTrue(TEXT("Open curve final point matches endpoint"),
        NewPoints.Last().LocalPosition.Equals(SourcePoints.Last().LocalPosition, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandNativeSplineConversionContractTest,
    "Strand.Editor.Authoring.NativeSplineCaptureContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandNativeSplineConversionContractTest::RunTest(const FString& Parameters)
{
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Native->ClearSplinePoints(false);
    Native->AddPoint(FSplinePoint(0, FVector(25, -15, 40), FVector(10, -20, 5),
        FVector(25, 30, 10), FRotator(5, 30, 10), FVector(1, 2, 1),
        ESplinePointType::CurveCustomTangent), false);
    Native->AddPoint(FSplinePoint(1, FVector(-120, 80, 65), FVector(-12, 10, 5),
        FVector(20, 0, -4), FRotator(0, -40, 5), FVector(1, 1, 2),
        ESplinePointType::CurveCustomTangent), false);
    Native->AddPoint(FSplinePoint(2, FVector(-170, 90, 140), FVector(-11, -5, 9),
        FVector(8, 6, 0), FRotator(10, 0, -5), FVector(2, 1, 1),
        ESplinePointType::CurveCustomTangent), false);
    Native->SetClosedLoop(true, false);
    Native->SetRelativeTransform(FTransform(FRotator(0, 20, 0), FVector(100, 500, 30)));
    Native->UpdateSpline();
    const FTransform NativeTransform = Native->GetRelativeTransform();
    USplineDataAsset* Captured = NewObject<USplineDataAsset>(GetTransientPackage());
    Captured->SerializeFromSplineComponent(Native);
    TestEqual(TEXT("Capture preserves point count"), Captured->Points.Num(), 3);
    TestTrue(TEXT("Capture preserves closed flag"), Captured->bIsClosedLoop);
    TestTrue(TEXT("Capture preserves exact native point geometry"), Captured->MatchesSplineComponent(Native, 0.0001f));
    TestTrue(TEXT("Capture leaves component transform intact"), Native->GetRelativeTransform().Equals(NativeTransform));
    UStrandSplineUserData::AssignAsset(Native, Captured);
    TestEqual(TEXT("Captured native spline linked to new asset"),
        UStrandSplineUserData::GetAssignedAsset(Native), Captured);
    TestTrue(TEXT("Linking captured asset does not alter native geometry"), Captured->MatchesSplineComponent(Native, 0.0001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandTangentEndpointExtensionTest,
    "Strand.Editor.Authoring.TangentEndpointExtension",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandTangentEndpointExtensionTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Source;
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(0, 0, 0);
    B.LocalPosition = FVector(100, 80, 20);
    C.LocalPosition = FVector(210, 200, 75);
    A.PointType = B.PointType = C.PointType = ESplinePointType::CurveCustomTangent;
    A.ArriveTangent = A.LeaveTangent = FVector(60, 120, 15);
    B.ArriveTangent = B.LeaveTangent = FVector(110, 95, 50);
    C.ArriveTangent = C.LeaveTangent = FVector(120, -30, 45);
    Source = {A, B, C};
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Native->ClearSplinePoints(false);
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        Native->AddPoint(Source[Index].ToSplinePoint(Index), false);
    }
    Native->UpdateSpline();
    const FVector StartDirection = -Native->GetSplinePointAt(0, ESplineCoordinateSpace::Local).ArriveTangent.GetSafeNormal();
    const FVector EndDirection = Native->GetSplinePointAt(2, ESplineCoordinateSpace::Local).LeaveTangent.GetSafeNormal();
    TArray<FSplinePointData> ExtendedStart = Source;
    TestTrue(TEXT("Extend start succeeds"), StrandSplineEditOperations::ExtendOpenEndpoint(ExtendedStart, Native, true));
    TestEqual(TEXT("Start adds exactly one point"), ExtendedStart.Num(), 4);
    TestTrue(TEXT("Start extension follows negative incoming tangent"),
        (ExtendedStart[0].LocalPosition - Source[0].LocalPosition).GetSafeNormal().Equals(StartDirection, 0.0001));
    TestTrue(TEXT("Original start location preserved"), ExtendedStart[1].LocalPosition.Equals(Source[0].LocalPosition));
    TArray<FSplinePointData> ExtendedEnd = Source;
    TestTrue(TEXT("Extend end succeeds"), StrandSplineEditOperations::ExtendOpenEndpoint(ExtendedEnd, Native, false));
    TestEqual(TEXT("End adds exactly one point"), ExtendedEnd.Num(), 4);
    TestTrue(TEXT("End extension follows outgoing tangent"),
        (ExtendedEnd.Last().LocalPosition - Source.Last().LocalPosition).GetSafeNormal().Equals(EndDirection, 0.0001));
    TestTrue(TEXT("Original end position preserved"), ExtendedEnd[2].LocalPosition.Equals(Source[2].LocalPosition));
    TestFalse(TEXT("Null preview rejected"), StrandSplineEditOperations::ExtendOpenEndpoint(ExtendedEnd, nullptr, false));
    // Imported/auto points can serialize zero tangents; evaluate the native
    // curve rather than using those raw zero vectors to place new endpoints.
    TArray<FSplinePointData> AutoPoints = Source;
    for (FSplinePointData& Point : AutoPoints)
    {
        Point.PointType = ESplinePointType::Curve;
        Point.ArriveTangent = Point.LeaveTangent = FVector::ZeroVector;
    }
    USplineComponent* AutoNative = NewObject<USplineComponent>(GetTransientPackage());
    AutoNative->ClearSplinePoints(false);
    for (int32 Index = 0; Index < AutoPoints.Num(); ++Index)
    {
        AutoNative->AddPoint(AutoPoints[Index].ToSplinePoint(Index), false);
    }
    AutoNative->UpdateSpline();
    const FVector EvaluatedAutoDirection = -AutoNative->GetSplinePointAt(0,
        ESplineCoordinateSpace::Local).ArriveTangent.GetSafeNormal();
    TestTrue(TEXT("Auto start extends"), StrandSplineEditOperations::ExtendOpenEndpoint(
        AutoPoints, AutoNative, true));
    TestTrue(TEXT("Auto spline extends along evaluated tangent"),
        (AutoPoints[0].LocalPosition - Source[0].LocalPosition).GetSafeNormal().Equals(EvaluatedAutoDirection, 0.0001));
    TestEqual(TEXT("Original Auto endpoint converted for tangent preservation"),
        AutoPoints[1].PointType.GetValue(), ESplinePointType::CurveCustomTangent);
    return true;
}

#endif
