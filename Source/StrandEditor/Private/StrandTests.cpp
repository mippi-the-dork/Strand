#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "StrandSplineUserData.h"
#include "StrandSplineActor.h"
#include "StrandSplineMeshAdapter.h"
#include "Components/SplineMeshComponent.h"

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
    UStrandSplineUserData::AssignAsset(Target, nullptr);
    TestNull(TEXT("Clearing assignment leaves no reference"), UStrandSplineUserData::GetAssignedAsset(Target));
    TestTrue(TEXT("Clearing assignment preserves points"), Asset->MatchesSplineComponent(Target));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandMeshAdapterTest,
    "Strand.Editor.SplineMeshTwoPointAdapter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandMeshAdapterTest::RunTest(const FString& Parameters)
{
    const AStrandSplineActor* DefaultActor = AStrandSplineActor::StaticClass()->GetDefaultObject<AStrandSplineActor>();
    TestNotNull(TEXT("Quick Add native actor has stock spline"), DefaultActor->Spline.Get());
    TestTrue(TEXT("Stock spline is the actor root"), DefaultActor->GetRootComponent() == DefaultActor->Spline.Get());

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
