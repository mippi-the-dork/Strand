#include "StrandPrecisionOperations.h"
#include "StrandPrecisionProjection.h"
#include "StrandCameraSpaceMath.h"
#include "StrandSplineEditOperations.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionPlaneCaptureTest,
    "Strand.Editor.Authoring.Precision.ThreeAnchorWorkingPlane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPrecisionPlaneCaptureTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    Points[0].LocalPosition = FVector(0, 0, 5);
    Points[1].LocalPosition = FVector(100, 0, 5);
    Points[2].LocalPosition = FVector(0, 100, 5);
    Points[3].LocalPosition = FVector(15, 15, 30);
    FVector Origin, Normal;
    TestTrue(TEXT("Three valid anchors define a plane"),
        StrandPrecision::PlaneFromThreePoints(Points, TArray<int32>{0, 1, 2}, Origin, Normal));
    TestTrue(TEXT("Plane normal is correct"), Normal.Equals(FVector::ZAxisVector, 1.e-6));
    TestTrue(TEXT("Projected drag has zero normal component"),
        FMath::Abs(FVector::DotProduct(StrandCameraSpace::ProjectDelta(FVector(25, 12, 90), Normal), Normal)) < 1.e-6);
    TestFalse(TEXT("Two anchors cannot define a plane"),
        StrandPrecision::PlaneFromThreePoints(Points, TArray<int32>{0, 1}, Origin, Normal));
    TestFalse(TEXT("Repeated index is invalid"),
        StrandPrecision::PlaneFromThreePoints(Points, TArray<int32>{0, 1, 1}, Origin, Normal));
    Points[2].LocalPosition = FVector(200, 0, 5);
    TestFalse(TEXT("Collinear anchors reject plane capture"),
        StrandPrecision::PlaneFromThreePoints(Points, TArray<int32>{0, 1, 2}, Origin, Normal));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionAnchorSnapTest,
    "Strand.Editor.Authoring.Precision.SnapToOtherAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPrecisionAnchorSnapTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    Points[0].LocalPosition = FVector(0, 0, 0);
    Points[1].LocalPosition = FVector(10, 0, 0);
    Points[2].LocalPosition = FVector(15, 0, 0);
    Points[3].LocalPosition = FVector(25, 0, 0);
    FVector Target;
    int32 Index = INDEX_NONE;
    TestTrue(TEXT("Find nearest unselected anchor"),
        StrandPrecision::FindNearestOtherAnchor(Points, TArray<int32>{0, 1}, 1, 6, Target, Index));
    TestEqual(TEXT("Excluded selected anchor 0"), Index, 2);
    TestTrue(TEXT("Nearest position"), Target.Equals(Points[2].LocalPosition));
    TestFalse(TEXT("Reject target outside tolerance"),
        StrandPrecision::FindNearestOtherAnchor(Points, TArray<int32>{0, 1}, 1, 4, Target, Index));
    TestFalse(TEXT("Reject source not selected"),
        StrandPrecision::FindNearestOtherAnchor(Points, TArray<int32>{0, 1}, 2, 10, Target, Index));
    TestFalse(TEXT("Reject invalid maximum"),
        StrandPrecision::FindNearestOtherAnchor(Points, TArray<int32>{0, 1}, 1, -1, Target, Index));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionSegmentSnapTest,
    "Strand.Editor.Authoring.Precision.SnapToOtherCurveSegment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPrecisionSegmentSnapTest::RunTest(const FString& Parameters)
{
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    for (const FVector& P : {FVector(50, 5, 0), FVector(150, 5, 0),
                             FVector(0, 0, 0), FVector(100, 0, 0), FVector(200, 0, 0)})
    {
        Spline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
    }
    for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
    { Spline->SetSplinePointType(I, ESplinePointType::Linear, false); }
    Spline->UpdateSpline();
    FVector Target;
    int32 Segment = INDEX_NONE;
    TestTrue(TEXT("Find a non-adjacent evaluated segment"),
        StrandPrecision::FindNearestOtherSegment(Spline, TArray<int32>{0, 1}, 0, 20.0, Target, Segment));
    TestEqual(TEXT("Choose eligible segment 2"), Segment, 2);
    TestTrue(TEXT("Project to other segment"), Target.Equals(FVector(50, 0, 0), 0.1));
    TestFalse(TEXT("Reject outside range"),
        StrandPrecision::FindNearestOtherSegment(Spline, TArray<int32>{0, 1}, 0, 1.0, Target, Segment));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionSnapPreviewPlanTest,
    "Strand.Editor.Authoring.Precision.SnapPreviewPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPrecisionSnapPreviewPlanTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    Points[0].LocalPosition = FVector(0, 0, 0);
    Points[1].LocalPosition = FVector(10, 0, 0);
    Points[2].LocalPosition = FVector(15, 0, 0);
    Points[3].LocalPosition = FVector(100, 0, 0);
    const TArray<int32> Selected{0, 1};
    TArray<TPair<int32, FVector>> Changes;
    StrandPrecision::BuildSnapPlan(Points, nullptr, Selected, false, 6.0, Changes);
    TestEqual(TEXT("Preview contains only eligible actual moves"), Changes.Num(), 1);
    if (Changes.Num() == 1)
    {
        TestEqual(TEXT("Source 1 is moved"), Changes[0].Key, 1);
        TestTrue(TEXT("Target 2 selected from original geometry"),
            Changes[0].Value.Equals(FVector(15, 0, 0), 1.e-6));
    }
    TestTrue(TEXT("Preview does not mutate source anchor 1"),
        Points[1].LocalPosition.Equals(FVector(10, 0, 0), 1.e-6));
    StrandPrecision::BuildSnapPlan(Points, nullptr, Selected, false, 4.0, Changes);
    TestEqual(TEXT("No targets within tighter tolerance"), Changes.Num(), 0);
    StrandPrecision::BuildSnapPlan(Points, nullptr, Selected, false, -5.0, Changes);
    TestEqual(TEXT("Invalid tolerance produces no proposed edits"), Changes.Num(), 0);
    StrandPrecision::BuildSnapPlan(Points, nullptr, Selected, true, 20.0, Changes);
    TestEqual(TEXT("Missing native segment evaluator produces no proposed edits"), Changes.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPrecisionInteractiveSnapTest,
    "Strand.Editor.Authoring.Precision.InteractiveDragTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPrecisionInteractiveSnapTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    Points[0].LocalPosition = FVector(0, 0, 0);
    Points[1].LocalPosition = FVector(100, 0, 0);
    Points[2].LocalPosition = FVector(107, 0, 0);
    Points[3].LocalPosition = FVector(130, 0, 0);
    const TArray<int32> Selected{0, 1};
    FVector Target;
    int32 Index = INDEX_NONE;
    TestTrue(TEXT("Primary position finds closest unselected anchor"),
        StrandPrecision::FindLiveAnchorTarget(Points, Selected, Points[1].LocalPosition,
            10.0, Target, Index));
    TestEqual(TEXT("Target is index 2, not selected index 0"), Index, 2);
    TestTrue(TEXT("Target matches exact anchor position"), Target.Equals(Points[2].LocalPosition));
    TestTrue(TEXT("Live lookup leaves original geometry unchanged"),
        Points[1].LocalPosition.Equals(FVector(100, 0, 0)));
    TestFalse(TEXT("Outside tolerance gives no target"),
        StrandPrecision::FindLiveAnchorTarget(Points, Selected, FVector(100, 0, 0), 6.0, Target, Index));
    TestFalse(TEXT("NaN source rejected"),
        StrandPrecision::FindLiveAnchorTarget(Points, Selected,
            FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 10.0, Target, Index));
    TestFalse(TEXT("No selection rejected"),
        StrandPrecision::FindLiveAnchorTarget(Points, {}, Points[1].LocalPosition, 10.0, Target, Index));

    // A primary-anchor snap applies one rigid offset to the selection; it
    // must never collapse all selected points onto the same target.
    const FVector OriginalOffset = Points[1].LocalPosition - Points[0].LocalPosition;
    TestTrue(TEXT("Final snap translates the selected group"),
        StrandSplineEditOperations::TranslateSelected(Points, Selected,
            Points[2].LocalPosition - Points[1].LocalPosition));
    TestTrue(TEXT("Primary reaches previewed target"), Points[1].LocalPosition.Equals(Points[2].LocalPosition));
    TestTrue(TEXT("Group spacing stays fixed"),
        (Points[1].LocalPosition - Points[0].LocalPosition).Equals(OriginalOffset));
    TestTrue(TEXT("Unselected source target stays put"),
        Points[2].LocalPosition.Equals(FVector(107, 0, 0)));
    return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandConstraintFrameTest,
    "Strand.Editor.Authoring.Precision.ConstraintFrames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandConstraintFrameTest::RunTest(const FString& Parameters)
{
    for (const FVector& Normal : {FVector::XAxisVector, FVector::YAxisVector,
        FVector::ZAxisVector, FVector(1, 2, 3).GetSafeNormal()})
    {
        FVector U, V, N;
        TestTrue(TEXT("Plane frame exists"), StrandPrecisionProjection::MakePlaneFrame(Normal,
            FVector::XAxisVector, U, V, N));
        TestTrue(TEXT("U in plane"), FMath::Abs(FVector::DotProduct(U, N)) < 1.e-6);
        TestTrue(TEXT("V in plane"), FMath::Abs(FVector::DotProduct(V, N)) < 1.e-6);
        TestTrue(TEXT("Basis orthogonal"), FMath::Abs(FVector::DotProduct(U, V)) < 1.e-6);
        TestTrue(TEXT("Basis oriented"), FVector::CrossProduct(U,V).Equals(N,1.e-6));
    }
    FVector U,V,N;
    TestFalse(TEXT("Zero normal rejected"), StrandPrecisionProjection::MakePlaneFrame(
        FVector::ZeroVector, FVector::XAxisVector, U,V,N));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSurfaceProjectionKernelTest,
    "Strand.Editor.Authoring.Precision.SurfaceProjectionKernel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandSurfaceProjectionKernelTest::RunTest(const FString& Parameters)
{
    const FVector A(0,0,0), B(100,0,0), C(0,100,0);
    FVector Hit(7,7,7), Normal(0,0,0);
    double Distance = -1;
    TestTrue(TEXT("Ray hits triangle"), StrandPrecisionProjection::ProjectRayToTriangle(
        FVector(25,25,100), FVector(0,0,-1), A,B,C,200,Hit,Normal,Distance));
    TestTrue(TEXT("Hit matches face"), Hit.Equals(FVector(25,25,0),1.e-6));
    TestTrue(TEXT("Normal follows winding"), Normal.Equals(FVector::ZAxisVector,1.e-6));
    TestTrue(TEXT("Hit distance"), FMath::IsNearlyEqual(Distance,100.0));
    TestTrue(TEXT("Clearance follows face normal"), StrandPrecisionProjection::ApplyClearance(
        Hit,Normal,12).Equals(FVector(25,25,12),1.e-6));
    TestFalse(TEXT("Out-of-range miss"), StrandPrecisionProjection::ProjectRayToTriangle(
        FVector(25,25,100), FVector(0,0,-1), A,B,C,50,Hit,Normal,Distance));
    TestFalse(TEXT("Off triangle miss"), StrandPrecisionProjection::ProjectRayToTriangle(
        FVector(150,150,100), FVector(0,0,-1), A,B,C,200,Hit,Normal,Distance));
    TestFalse(TEXT("Parallel miss"), StrandPrecisionProjection::ProjectRayToTriangle(
        FVector(25,25,100), FVector(1,0,0), A,B,C,200,Hit,Normal,Distance));
    TestFalse(TEXT("Degenerate triangle rejected"), StrandPrecisionProjection::ProjectRayToTriangle(
        FVector(25,25,100), FVector(0,0,-1), A,A,A,200,Hit,Normal,Distance));
    return true;
}
#endif
