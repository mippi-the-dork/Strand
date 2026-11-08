// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "StrandPrecisionOperations.h"
#include "StrandCameraSpaceMath.h"

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
#endif
