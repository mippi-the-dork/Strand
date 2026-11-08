// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "StrandSplineOrientationOperations.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/SplineComponent.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandRollEditsTest,
    "Strand.Editor.Authoring.Orientation.RollEditsPreserveGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandRollEditsTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        Points[Index].LocalPosition = FVector(Index * 100.0, Index * Index * 13.0, Index * 27.0);
        Points[Index].ArriveTangent = FVector(32.0, 8.0, 4.0);
        Points[Index].LeaveTangent = FVector(32.0, 8.0, 4.0);
    }
    const TArray<FSplinePointData> Before = Points;
    TestFalse(TEXT("Reject nonfinite roll"), StrandSplineOrientationOperations::SetRoll(
        Points, TArray<int32>{0, 1}, std::numeric_limits<double>::quiet_NaN()));
    TestTrue(TEXT("Set selected roll"), StrandSplineOrientationOperations::SetRoll(
        Points, TArray<int32>{0, 2}, 45.0));
    TestEqual(TEXT("First roll set"), Points[0].LocalRotation.Roll, 45.0);
    TestEqual(TEXT("Second selected roll set"), Points[2].LocalRotation.Roll, 45.0);
    TestEqual(TEXT("Unselected roll unchanged"), Points[1].LocalRotation.Roll, 0.0);
    TestFalse(TEXT("Identical set is no-op"), StrandSplineOrientationOperations::SetRoll(
        Points, TArray<int32>{0, 2}, 45.0));
    TestTrue(TEXT("Increment selected roll"), StrandSplineOrientationOperations::AddRoll(
        Points, TArray<int32>{2}, -15.0));
    TestEqual(TEXT("Increment yields 30 degrees"), Points[2].LocalRotation.Roll, 30.0);
    TestFalse(TEXT("Invalid index cannot mutate"), StrandSplineOrientationOperations::SetRoll(
        Points, TArray<int32>{-1, 999}, 19.0));
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        TestTrue(TEXT("Positions preserved"), Points[Index].LocalPosition.Equals(Before[Index].LocalPosition));
        TestTrue(TEXT("Arrival tangents preserved"), Points[Index].ArriveTangent.Equals(Before[Index].ArriveTangent));
        TestTrue(TEXT("Departure tangents preserved"), Points[Index].LeaveTangent.Equals(Before[Index].LeaveTangent));
        TestTrue(TEXT("Scale preserved"), Points[Index].LocalScale.Equals(Before[Index].LocalScale));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandRollDistributionTest,
    "Strand.Editor.Authoring.Orientation.DistributeRollByDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandRollDistributionTest::RunTest(const FString& Parameters)
{
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    TArray<FSplinePointData> Points;
    const double Positions[] = {0.0, 50.0, 200.0, 500.0, 700.0};
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FSplinePointData Point;
        Point.LocalPosition = FVector(Positions[Index], 0.0, 0.0);
        Point.PointType = ESplinePointType::Linear;
        Points.Add(Point);
        Spline->AddPoint(Point.ToSplinePoint(Index), false);
    }
    Spline->UpdateSpline();
    Points[0].LocalRotation.Roll = 10.0;
    Points[4].LocalRotation.Roll = 100.0;
    Points[2].LocalRotation.Roll = -12.0;
    const TArray<int32> Selection{0, 1, 2, 4};
    TestTrue(TEXT("Distance-based roll distribution changes interior"),
        StrandSplineOrientationOperations::DistributeRoll(Points, Selection, Spline));
    TestTrue(TEXT("First interpolation at 50/700"), FMath::IsNearlyEqual(
        Points[1].LocalRotation.Roll, 10.0 + 90.0 * 50.0 / 700.0, 0.001));
    TestTrue(TEXT("Second interpolation at 200/700"), FMath::IsNearlyEqual(
        Points[2].LocalRotation.Roll, 10.0 + 90.0 * 200.0 / 700.0, 0.001));
    TestEqual(TEXT("First endpoint untouched"), Points[0].LocalRotation.Roll, 10.0);
    TestEqual(TEXT("Last endpoint untouched"), Points[4].LocalRotation.Roll, 100.0);
    TestEqual(TEXT("Unselected anchor untouched"), Points[3].LocalRotation.Roll, 0.0);
    TestFalse(TEXT("Repeat distribution is idempotent"),
        StrandSplineOrientationOperations::DistributeRoll(Points, Selection, Spline));
    TestFalse(TEXT("Less than three selections rejects"),
        StrandSplineOrientationOperations::DistributeRoll(Points, TArray<int32>{0, 4}, Spline));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandNativeOrientationFrameTest,
    "Strand.Editor.Authoring.Orientation.NativeSplineFrames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandNativeOrientationFrameTest::RunTest(const FString& Parameters)
{
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    Spline->AddPoint(FSplinePoint(0, FVector(0, 0, 0), FVector::ZeroVector,
        FVector::ZeroVector, FRotator(0, 0, 0), FVector::OneVector, ESplinePointType::Linear), false);
    Spline->AddPoint(FSplinePoint(1, FVector(200, 0, 0), FVector::ZeroVector,
        FVector::ZeroVector, FRotator(0, 0, 90), FVector::OneVector, ESplinePointType::Linear), false);
    Spline->UpdateSpline();
    const double Length = Spline->GetSplineLength();
    TestTrue(TEXT("Nonzero spline length"), Length > 1.0);
    for (int32 Index = 0; Index <= 16; ++Index)
    {
        const FQuat Frame = Spline->GetQuaternionAtDistanceAlongSpline(
            Length * Index / 16.0, ESplineCoordinateSpace::Local);
        const FVector Forward = Frame.GetAxisX();
        const FVector Right = Frame.GetAxisY();
        const FVector Up = Frame.GetAxisZ();
        TestTrue(TEXT("Native frame is finite"), !Frame.ContainsNaN());
        TestTrue(TEXT("Forward axis unit length"), FMath::IsNearlyEqual(Forward.Size(), 1.0, 0.001));
        TestTrue(TEXT("Up axis unit length"), FMath::IsNearlyEqual(Up.Size(), 1.0, 0.001));
        TestTrue(TEXT("Up and forward orthogonal"), FMath::Abs(FVector::DotProduct(Forward, Up)) < 0.001);
        TestTrue(TEXT("Right and up orthogonal"), FMath::Abs(FVector::DotProduct(Right, Up)) < 0.001);
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandSplineEditOperations.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandLocalTangentRollTest,
    "Strand.Editor.Authoring.Orientation.LocalTangentRollPreservesShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandLocalTangentRollTest::RunTest(const FString& Parameters)
{
    FSplinePointData Point;
    Point.LocalPosition = FVector(15, -25, 90);
    Point.ArriveTangent = FVector(8, 12, -3);
    Point.LeaveTangent = FVector(45, 15, 30);
    Point.LocalRotation = FRotator(10, 20, 30);
    const FSplinePointData Before = Point;
    const FVector Forward = Point.LeaveTangent.GetSafeNormal();
    const FVector OriginalUp = Point.LocalRotation.Quaternion().RotateVector(FVector::ZAxisVector);
    TestTrue(TEXT("Local X roll modifies orientation"),
        StrandSplineEditOperations::RollAnchorAroundForward(Point, Forward, 42.0));
    const FVector ActualUp = Point.LocalRotation.Quaternion().RotateVector(FVector::ZAxisVector);
    const FVector ExpectedUp = FQuat(Forward, FMath::DegreesToRadians(42.0)).RotateVector(OriginalUp);
    TestTrue(TEXT("Orientation rotates around forward tangent"), ActualUp.Equals(ExpectedUp, 0.0001));
    TestTrue(TEXT("Anchor position unchanged"), Point.LocalPosition.Equals(Before.LocalPosition));
    TestTrue(TEXT("Incoming tangent unchanged"), Point.ArriveTangent.Equals(Before.ArriveTangent));
    TestTrue(TEXT("Outgoing tangent unchanged"), Point.LeaveTangent.Equals(Before.LeaveTangent));
    TestFalse(TEXT("Zero roll is a no-op"), StrandSplineEditOperations::RollAnchorAroundForward(Point, Forward, 0.0));
    TestFalse(TEXT("Degenerate tangent rejected"), StrandSplineEditOperations::RollAnchorAroundForward(Point, FVector::ZeroVector, 35.0));
    return true;
}
#endif
