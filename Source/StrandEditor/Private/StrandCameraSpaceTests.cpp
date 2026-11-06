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
