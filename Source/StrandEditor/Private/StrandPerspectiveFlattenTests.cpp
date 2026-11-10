#include "StrandPerspectiveFlatten.h"
#include <cfloat>
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPerspectiveFlattenSilhouetteTest,
    "Strand.Editor.Authoring.CameraSpace.PerspectiveFlattenSilhouette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPerspectiveFlattenSilhouetteTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Original;
    Original.SetNum(3);
    Original[0].LocalPosition = FVector(300, -190, 30);
    Original[1].LocalPosition = FVector(550, 145, 170);
    Original[2].LocalPosition = FVector(750, -30, -125);
    for (int32 I = 0; I < Original.Num(); ++I)
    { Original[I].PointType = ESplinePointType::CurveCustomTangent; }
    Original[0].LeaveTangent = FVector(150, 350, 450);
    Original[1].ArriveTangent = FVector(100, -20, 350);
    Original[1].LeaveTangent = FVector(250, -180, -150);
    Original[2].ArriveTangent = FVector(130, -400, -210);
    Original[1].LocalRotation = FRotator(10, 20, 35);
    const TArray<FSplinePointData> Baseline = Original;
    USplineComponent* Source = NewObject<USplineComponent>(GetTransientPackage());
    Source->ClearSplinePoints(false);
    for (int32 I = 0; I < Original.Num(); ++I) { Source->AddPoint(Original[I].ToSplinePoint(I), false); }
    Source->SetClosedLoop(false, false);
    Source->UpdateSpline();
    StrandPerspectiveFlatten::FCamera Camera;
    Camera.Location = FVector::ZeroVector;
    Camera.Forward = FVector(1, 0, 0);
    Camera.HorizontalFOVDegrees = 90;
    Camera.ViewportWidth = 1400;
    TArray<FSplinePointData> Candidate;
    FString Error;
    TestTrue(TEXT("Perspective candidate produced"), StrandPerspectiveFlatten::Build(Source, Original, false, Camera,
        0.5, Candidate, Error));
    if (Candidate.Num() < 2) { AddError(Error); return false; }
    TestTrue(TEXT("Original points unchanged"), Original[1].LocalPosition.Equals(Baseline[1].LocalPosition));
    TestTrue(TEXT("Original authored rotation preserved at original anchor"),
        Candidate.ContainsByPredicate([&](const FSplinePointData& P)
        { return P.LocalRotation.Equals(Original[1].LocalRotation) && P.LocalPosition.X > 300; }));
    for (const FSplinePointData& P : Candidate)
    {
        TestTrue(TEXT("Candidate on camera-facing plane"), FMath::IsNearlyEqual(P.LocalPosition.X, Candidate[0].LocalPosition.X, 1.e-4));
        TestTrue(TEXT("Candidate tangent is planar"), FMath::Abs(P.ArriveTangent.X) < 1.e-3 && FMath::Abs(P.LeaveTangent.X) < 1.e-3);
    }
    USplineComponent* Result = NewObject<USplineComponent>(GetTransientPackage());
    Result->ClearSplinePoints(false);
    for (int32 I = 0; I < Candidate.Num(); ++I) { Result->AddPoint(Candidate[I].ToSplinePoint(I), false); }
    Result->UpdateSpline();
    // Compare source's camera-ray-projected silhouette against the nearest point
    // on the dense reconstructed curve. This tests visual outcome, not only planarity.
    double MaximumError = 0.0;
    const double Depth = Candidate[0].LocalPosition.X;
    for (int32 I = 0; I <= 100; ++I)
    {
        FVector Desired;
        TestTrue(TEXT("Source projects"), StrandPerspectiveFlatten::Project(
            Source->GetLocationAtSplineInputKey(2.0 * I / 100.0, ESplineCoordinateSpace::Local), Camera, Depth, Desired));
        double Nearest = DBL_MAX;
        for (int32 J = 0; J <= 1200; ++J)
        {
            const FVector P = Result->GetLocationAtSplineInputKey(
                (Candidate.Num() - 1.0) * J / 1200.0, ESplineCoordinateSpace::Local);
            Nearest = FMath::Min(Nearest, FVector::Dist(Desired, P));
        }
        MaximumError = FMath::Max(MaximumError, Nearest);
    }
    const double UnitsPerPixel = 2.0 * Depth / Camera.ViewportWidth;
    TestTrue(FString::Printf(TEXT("Reprojected silhouette stays within 2.5 sampled pixels; worst %.3f px"),
        MaximumError / UnitsPerPixel), MaximumError <= UnitsPerPixel * 2.5);
    TArray<FSplinePointData> Rejected = Candidate;
    const int32 BeforeCount = Rejected.Num();
    StrandPerspectiveFlatten::FCamera Invalid = Camera;
    Invalid.Forward = -FVector::XAxisVector;
    TestFalse(TEXT("Behind-camera curve rejected"), StrandPerspectiveFlatten::Build(Source, Original,
        false, Invalid, 1.0, Rejected, Error));
    TestEqual(TEXT("Failed projection does not overwrite candidate"), Rejected.Num(), BeforeCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandPerspectiveFlattenClosedLoopTest,
    "Strand.Editor.Authoring.CameraSpace.PerspectiveFlattenClosedLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandPerspectiveFlattenClosedLoopTest::RunTest(const FString& Parameters)
{
    TArray<FSplinePointData> Original;
    Original.SetNum(4);
    for (int32 I = 0; I < 4; ++I)
    {
        const double Theta = 2.0 * PI * I / 4.0;
        Original[I].LocalPosition = FVector(420.0 + 50.0 * FMath::Sin(Theta),
            120.0 * FMath::Cos(Theta), 120.0 * FMath::Sin(Theta));
        Original[I].PointType = ESplinePointType::CurveCustomTangent;
        Original[I].ArriveTangent = FVector(0.0, -130.0 * FMath::Sin(Theta), 130.0 * FMath::Cos(Theta));
        Original[I].LeaveTangent = Original[I].ArriveTangent;
    }
    USplineComponent* Source = NewObject<USplineComponent>(GetTransientPackage());
    Source->ClearSplinePoints(false);
    for (int32 I = 0; I < Original.Num(); ++I)
    { Source->AddPoint(Original[I].ToSplinePoint(I), false); }
    Source->SetClosedLoop(true, false);
    Source->UpdateSpline();
    StrandPerspectiveFlatten::FCamera Camera;
    Camera.Location = FVector::ZeroVector;
    Camera.Forward = FVector::XAxisVector;
    Camera.ViewportWidth = 1200;
    TArray<FSplinePointData> Result;
    FString Error;
    TestTrue(TEXT("Closed curve projected"), StrandPerspectiveFlatten::Build(
        Source, Original, true, Camera, 1.0, Result, Error));
    if (Result.Num() < 4) { AddError(Error); return false; }
    TestTrue(TEXT("Closed seam retains nonzero arrival tangent"), !Result[0].ArriveTangent.IsNearlyZero());
    TestTrue(TEXT("No duplicate seam anchor"), !Result.Last().LocalPosition.Equals(Result[0].LocalPosition, 0.00001));
    for (const FSplinePointData& P : Result)
    { TestTrue(TEXT("Closed projected anchor lies on plane"), FMath::IsNearlyEqual(P.LocalPosition.X, Result[0].LocalPosition.X, 1.e-4)); }
    return true;
}
#endif
