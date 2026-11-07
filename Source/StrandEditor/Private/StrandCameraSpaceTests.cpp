#include "StrandCameraSpaceMath.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandCameraPlaneMathTest,
    "Strand.Editor.Authoring.CameraSpace.PlaneProjectionAndFlatten",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandCameraPlaneMathTest::RunTest(const FString& Parameters)
{
    const FVector Normal = FVector(1.0, 2.0, 3.0).GetSafeNormal();
    const FVector Origin(20, -12, 8);
    const FVector Delta(13, 7, -4);
    const FVector Projected = StrandCameraSpace::ProjectDelta(Delta, Normal);
    TestTrue(TEXT("Constrained movement is perpendicular to camera forward"), FMath::Abs(FVector::DotProduct(Projected, Normal)) < 1.e-6);
    TestTrue(TEXT("Constrained movement preserves planar component"),
        StrandCameraSpace::ProjectDelta(Projected, Normal).Equals(Projected, 1.e-6));
    TArray<FSplinePointData> Points;
    Points.SetNum(3);
    Points[0].LocalPosition = FVector(50, -40, 29);
    Points[1].LocalPosition = FVector(-25, 8, 90);
    Points[2].LocalPosition = FVector(10, 15, 31);
    Points[0].ArriveTangent = FVector(14, 10, -3);
    Points[0].LeaveTangent = FVector(-8, 11, 12);
    Points[0].LocalRotation = FRotator(12, 18, 35);
    const FRotator OriginalRotation = Points[0].LocalRotation;
    const FVector Unselected = Points[2].LocalPosition;
    TestTrue(TEXT("Flatten selected anchors"), StrandCameraSpace::Flatten(Points, TArray<int32>{0, 1}, Origin, Normal));
    TestTrue(TEXT("Flatten puts first anchor on reference plane"), FMath::Abs(FVector::DotProduct(Points[0].LocalPosition - Origin, Normal)) < 1.e-5);
    TestTrue(TEXT("Flatten puts second anchor on reference plane"), FMath::Abs(FVector::DotProduct(Points[1].LocalPosition - Origin, Normal)) < 1.e-5);
    TestTrue(TEXT("Flatten projects derivative vectors"), FMath::Abs(FVector::DotProduct(Points[0].LeaveTangent, Normal)) < 1.e-5);
    TestTrue(TEXT("Flatten preserves authored rotation"), Points[0].LocalRotation.Equals(OriginalRotation));
    TestTrue(TEXT("Flatten leaves unselected anchors untouched"), Points[2].LocalPosition.Equals(Unselected));
    TestFalse(TEXT("Flatten is idempotent"), StrandCameraSpace::Flatten(Points, TArray<int32>{0, 1}, Origin, Normal));
    TArray<FSplinePointData> AlreadyOnPlane;
    AlreadyOnPlane.SetNum(1);
    AlreadyOnPlane[0].LocalPosition = Origin;
    AlreadyOnPlane[0].ArriveTangent = Normal * 30.0;
    AlreadyOnPlane[0].LeaveTangent = Normal * -25.0;
    TestTrue(TEXT("Coplanar anchor still needs tangent flattening"), StrandCameraSpace::Flatten(AlreadyOnPlane, TArray<int32>{0}, Origin, Normal));
    TestTrue(TEXT("Coplanar incoming handle projected"), AlreadyOnPlane[0].ArriveTangent.IsNearlyZero());
    TestTrue(TEXT("Coplanar outgoing handle projected"), AlreadyOnPlane[0].LeaveTangent.IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSoftSelectionInfluenceTest,
    "Strand.Editor.Authoring.CameraSpace.SoftSelectionDistances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandSoftSelectionInfluenceTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Points;
    Points.SetNum(4);
    const FVector Positions[] = {FVector(0, 0, 0), FVector(100, 0, 0), FVector(100, 100, 0), FVector(0, 100, 0)};
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Spline->ClearSplinePoints(false);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Points[Index].LocalPosition = Positions[Index];
        Points[Index].PointType = ESplinePointType::Linear;
        Spline->AddPoint(Points[Index].ToSplinePoint(Index), false);
    }
    Spline->SetClosedLoop(true, false);
    Spline->UpdateSpline();
    using namespace StrandCameraSpace;
    const TArray<int32> Selected{0};
    TestEqual(TEXT("Selected always has full weight"), InfluenceAt(Points, Selected, 0, Spline, EInfluence::Spatial3D, 150, EFalloff::Linear), 1.0);
    TestTrue(TEXT("3D weight uses Euclidean distance"), FMath::IsNearlyEqual(InfluenceAt(Points, Selected, 2, Spline, EInfluence::Spatial3D, 150, EFalloff::Linear), 1.0 - FMath::Sqrt(20000.0) / 150, 1.e-5));
    TestTrue(TEXT("Spline arc distance differs from Euclidean at opposite corner"), InfluenceAt(Points, Selected, 2, Spline, EInfluence::AlongSpline, 150, EFalloff::Linear) < 1.e-5);
    TestTrue(TEXT("Closed loop wraps seam to previous point"), FMath::IsNearlyEqual(InfluenceAt(Points, Selected, 3, Spline, EInfluence::AlongSpline, 150, EFalloff::Linear), 1.0 / 3.0, 1.e-4));
    TestTrue(TEXT("Smooth falloff ends at zero"), FMath::IsNearlyZero(Falloff(150, 150, EFalloff::Smooth)));
    TestEqual(TEXT("Smooth falloff midpoint"), Falloff(75, 150, EFalloff::Smooth), 0.5);
    TestFalse(TEXT("Nonfinite influence rejected"), Falloff(std::numeric_limits<double>::quiet_NaN(), 100, EFalloff::Linear) > 0.0);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSoftSelectionDragConsistency,
    "Strand.Editor.Authoring.CameraSpace.DragChunkIndependence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandSoftSelectionDragConsistency::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Whole;
    Whole.SetNum(3);
    Whole[0].LocalPosition = FVector::ZeroVector;
    Whole[1].LocalPosition = FVector(100, 0, 0);
    Whole[2].LocalPosition = FVector(200, 0, 0);
    TArray<FSplinePointData> Steps = Whole;
    const TArray<double> FrozenWeights{1.0, 0.65, 0.2};
    const FVector Delta(60.0, 15.0, -9.0);
    TestTrue(TEXT("One-step weighted drag changed geometry"), StrandCameraSpace::ApplyWeightedDelta(Whole, FrozenWeights, Delta));
    for (int32 I = 0; I < 20; ++I)
    {
        StrandCameraSpace::ApplyWeightedDelta(Steps, FrozenWeights, Delta / 20.0);
    }
    for (int32 I = 0; I < Whole.Num(); ++I)
    {
        TestTrue(FString::Printf(TEXT("Point %d: twenty substeps equal one drag"), I),
            Whole[I].LocalPosition.Equals(Steps[I].LocalPosition, 1.e-5));
    }
    TestFalse(TEXT("Mismatched weight array rejected"), StrandCameraSpace::ApplyWeightedDelta(Steps, TArray<double>{1.0}, Delta));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSoftTransformConsistency,
    "Strand.Editor.Authoring.CameraSpace.WeightedRotateScaleChunkIndependence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandSoftTransformConsistency::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Initial;
    Initial.SetNum(3);
    Initial[0].LocalPosition = FVector(0, 0, 0);
    Initial[1].LocalPosition = FVector(100, 10, 0);
    Initial[2].LocalPosition = FVector(200, 40, 20);
    for (FSplinePointData& P : Initial)
    {
        P.ArriveTangent = FVector(12, 3, 1);
        P.LeaveTangent = FVector(9, -4, 2);
    }
    const TArray<double> Weights{1.0, 0.5, 0.0};
    const FVector Pivot(0, 0, 0);
    TArray<FSplinePointData> Whole = Initial;
    TArray<FSplinePointData> Chunks = Initial;
    const FVector Axis = FVector(1, 2, 3).GetSafeNormal();
    const double TotalAngle = PI / 3.0;
    TestTrue(TEXT("Weighted rotation changes influenced points"),
        StrandCameraSpace::ApplyWeightedRotation(Whole, Weights, Pivot, FQuat(Axis, TotalAngle)));
    for (int32 I = 0; I < 24; ++I)
    {
        StrandCameraSpace::ApplyWeightedRotation(Chunks, Weights, Pivot, FQuat(Axis, TotalAngle / 24.0));
    }
    for (int32 I = 0; I < Whole.Num(); ++I)
    {
        TestTrue(FString::Printf(TEXT("Rotation point %d position independent of drag chunking"), I),
            Whole[I].LocalPosition.Equals(Chunks[I].LocalPosition, 1.e-5));
        TestTrue(FString::Printf(TEXT("Rotation point %d incoming tangent independent of chunking"), I),
            Whole[I].ArriveTangent.Equals(Chunks[I].ArriveTangent, 1.e-5));
        TestTrue(FString::Printf(TEXT("Rotation point %d outgoing tangent independent of chunking"), I),
            Whole[I].LeaveTangent.Equals(Chunks[I].LeaveTangent, 1.e-5));
    }
    TestTrue(TEXT("Zero influence does not affect point position"),
        Whole[2].LocalPosition.Equals(Initial[2].LocalPosition));
    TestFalse(TEXT("Reject mismatched weights on rotation"),
        StrandCameraSpace::ApplyWeightedRotation(Chunks, TArray<double>{1.0}, Pivot, FQuat(Axis, TotalAngle)));
    Whole = Initial;
    Chunks = Initial;
    const double Factor = 2.0;
    TestTrue(TEXT("Weighted scale changes influenced points"),
        StrandCameraSpace::ApplyWeightedScale(Whole, Weights, Pivot, Factor));
    for (int32 I = 0; I < 20; ++I)
    {
        StrandCameraSpace::ApplyWeightedScale(Chunks, Weights, Pivot, FMath::Pow(Factor, 1.0 / 20.0));
    }
    for (int32 I = 0; I < Whole.Num(); ++I)
    {
        TestTrue(FString::Printf(TEXT("Scale point %d position independent of drag chunking"), I),
            Whole[I].LocalPosition.Equals(Chunks[I].LocalPosition, 1.e-5));
        TestTrue(FString::Printf(TEXT("Scale point %d incoming tangent independent of chunking"), I),
            Whole[I].ArriveTangent.Equals(Chunks[I].ArriveTangent, 1.e-5));
    }
    TestFalse(TEXT("Reject negative scale"),
        StrandCameraSpace::ApplyWeightedScale(Chunks, Weights, Pivot, -1.0));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandCameraScreenGestureTest,
    "Strand.Editor.Authoring.CameraSpace.ScreenGizmoMath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandCameraScreenGestureTest::RunTest(const FString& Parameters)
{
    const FVector Right = FVector::YAxisVector;
    const FVector Up = FVector::ZAxisVector;
    const FVector Delta = StrandCameraSpace::ScreenPixelsToPlaneDelta(
        FVector2D(100, -50), Right, Up, 1000.0, 90.0, 1000.0);
    TestTrue(TEXT("100 pixels right at given depth becomes 200 world units right"),
        Delta.Equals(FVector(0, 200, 100), 1.e-6));
    TestTrue(TEXT("Planar drag never changes camera-forward depth"),
        FMath::Abs(FVector::DotProduct(Delta, FVector::XAxisVector)) < 1.e-6);
    const FVector Invalid = StrandCameraSpace::ScreenPixelsToPlaneDelta(
        FVector2D(10, 20), Right, Up, 1000.0, 90.0, 0.0);
    TestTrue(TEXT("Invalid viewport dimensions rejected"), Invalid.IsNearlyZero());
    const double Seam = StrandCameraSpace::ScreenRotationDelta(
        FMath::DegreesToRadians(179.0), FMath::DegreesToRadians(-179.0));
    TestTrue(TEXT("Screen ring angle is wrapped across 180-degree seam"),
        FMath::IsNearlyEqual(FMath::RadiansToDegrees(Seam), -2.0, 1.e-6));
    TestTrue(TEXT("No movement yields zero screen rotation"),
        FMath::IsNearlyZero(StrandCameraSpace::ScreenRotationDelta(0.5, 0.5)));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSoftTangentGestureTest,
    "Strand.Editor.Authoring.CameraSpace.SoftTangentDragConsistency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandSoftTangentGestureTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Whole;
    Whole.SetNum(3);
    TArray<FSplinePoint> Evaluated;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Whole[Index].LocalPosition = FVector(Index * 100.0, 0, 0);
        Whole[Index].ArriveTangent = FVector(30, 0, 0);
        Whole[Index].LeaveTangent = FVector(30, 0, 0);
        Whole[Index].PointType = ESplinePointType::CurveCustomTangent;
        Whole[Index].HandleMode = EStrandBezierHandleMode::Free;
        Evaluated.Add(Whole[Index].ToSplinePoint(Index));
    }
    const TArray<FSplinePointData> Before = Whole;
    TArray<FSplinePointData> Chunked = Whole;
    const TArray<double> Weights{1.0, 0.5, 0.0};
    const FVector Delta(0, 18, 6);
    TestTrue(TEXT("Whole handle gesture changes the selected handle"),
        StrandCameraSpace::ApplyWeightedTangentDelta(Whole, Evaluated, Weights, 2, Delta));
    for (int32 Step = 0; Step < 12; ++Step)
    {
        StrandCameraSpace::ApplyWeightedTangentDelta(Chunked, Evaluated, Weights, 2, Delta / 12.0);
    }
    for (int32 Index = 0; Index < 3; ++Index)
    {
        TestTrue(FString::Printf(TEXT("Anchor %d stays fixed"), Index),
            Whole[Index].LocalPosition.Equals(Before[Index].LocalPosition));
        TestTrue(FString::Printf(TEXT("Handle %d is chunk-independent"), Index),
            Whole[Index].LeaveTangent.Equals(Chunked[Index].LeaveTangent, 0.0001));
        TestTrue(FString::Printf(TEXT("Incoming handle %d unchanged"), Index),
            Whole[Index].ArriveTangent.Equals(Before[Index].ArriveTangent));
    }
    TestTrue(TEXT("Nearest unselected tangent moves at half influence"),
        Whole[1].LeaveTangent.Equals(Before[1].LeaveTangent + Delta * 0.5, 0.0001));
    TestTrue(TEXT("Outside influence remains untouched"),
        Whole[2].LeaveTangent.Equals(Before[2].LeaveTangent));
    TestFalse(TEXT("Invalid handle rejected"),
        StrandCameraSpace::ApplyWeightedTangentDelta(Whole, Evaluated, Weights, 0, Delta));
    TestFalse(TEXT("Mismatched weights rejected"),
        StrandCameraSpace::ApplyWeightedTangentDelta(Whole, Evaluated, TArray<double>{1.0}, 2, Delta));
    return true;
}
#endif
