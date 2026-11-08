// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/SplineComponent.h"
#include "SplineDataAsset.h"
#include "StrandSplineCleanupOperations.h"

namespace StrandCleanupTestUtils
{
    static USplineComponent* Native(const TArray<FSplinePointData>& Points, bool bClosed = false)
    {
        USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
        Spline->ClearSplinePoints(false);
        for (int32 I = 0; I < Points.Num(); ++I)
        { Spline->AddPoint(Points[I].ToSplinePoint(I), false); }
        Spline->SetClosedLoop(bClosed, false);
        Spline->UpdateSpline();
        return Spline;
    }
    static TArray<FSplinePointData> Line(int32 Count = 7)
    {
        TArray<FSplinePointData> Points;
        for (int32 I = 0; I < Count; ++I)
        {
            FSplinePointData Point;
            Point.LocalPosition = FVector(I*100., I*20., I*10.);
            Point.ArriveTangent = Point.LeaveTangent = FVector(100, 20, 10);
            Point.PointType = ESplinePointType::CurveCustomTangent;
            Points.Add(Point);
        }
        return Points;
    }
    static TArray<FSplinePointData> Curved3D()
    {
        TArray<FSplinePointData> Points = Line(3);
        Points[0].LeaveTangent = FVector(20, 120, 35);
        Points[1].ArriveTangent = FVector(115, 40, -50);
        Points[1].LeaveTangent = FVector(95, -70, 110);
        Points[2].ArriveTangent = FVector(35, 150, -35);
        Points[1].LocalRotation = FRotator(12, 22, 32);
        Points[1].LocalScale = FVector(1, 2, 3);
        return Points;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandCleanupSimplifyTest,
    "Strand.Editor.Authoring.Cleanup.SimplifyTolerance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandCleanupSimplifyTest::RunTest(const FString& Parameters)
{
    using namespace StrandCleanupTestUtils;
    TArray<FSplinePointData> Source = Line();
    USplineComponent* Spline = Native(Source);
    StrandSplineCleanup::FResult Result;
    TestTrue(TEXT("Straight 3D spline simplifies within 0.01 cm"),
        StrandSplineCleanup::Simplify(Source, Spline, 0.01, false, nullptr, Result));
    TestEqual(TEXT("Redundant anchors removed"), Result.Points.Num(), 2);
    if (Result.Points.Num() != 2) { return false; }
    TestEqual(TEXT("Source count unchanged"), Source.Num(), 7);
    TestTrue(TEXT("Sampled error meets tolerance"), Result.SampledMaxDeviationCm <= 0.01);
    TestTrue(TEXT("Start preserved"), Result.Points[0].LocalPosition.Equals(Source[0].LocalPosition, 1.e-5));
    TestTrue(TEXT("End preserved"), Result.Points.Last().LocalPosition.Equals(Source.Last().LocalPosition, 1.e-5));
    StrandSplineCleanup::FResult Invalid;
    Invalid.Points = Source;
    TestFalse(TEXT("Negative tolerance rejected"), StrandSplineCleanup::Simplify(Source, Spline, -1.0, false, nullptr, Invalid));
    TestEqual(TEXT("Invalid request leaves output intact"), Invalid.Points.Num(), 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandCleanupDissolveTest,
    "Strand.Editor.Authoring.Cleanup.DissolveSelected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandCleanupDissolveTest::RunTest(const FString& Parameters)
{
    using namespace StrandCleanupTestUtils;
    const TArray<FSplinePointData> Source = Line(5);
    USplineComponent* Spline = Native(Source);
    StrandSplineCleanup::FResult Result;
    const TArray<int32> Selected = {1, 3};
    TestTrue(TEXT("Selected interior anchors dissolved"),
        StrandSplineCleanup::Dissolve(Source, Spline, Selected, 0.01, Result));
    TestEqual(TEXT("Exactly selected anchors removed"), Result.Points.Num(), 3);
    if (Result.Points.Num() != 3) { return false; }
    TestTrue(TEXT("Middle point preserved"), Result.Points[1].LocalPosition.Equals(Source[2].LocalPosition));
    StrandSplineCleanup::FResult Rejected;
    TestFalse(TEXT("Endpoint dissolve rejected atomically"),
        StrandSplineCleanup::Dissolve(Source, Spline, TArray<int32>{0, 2}, 10., Rejected));
    TestEqual(TEXT("No output on rejected dissolve"), Rejected.Points.Num(), 0);
    TArray<FSplinePointData> Corner = Line(3);
    Corner[1].LocalPosition = FVector(100, 240, -30);
    for (FSplinePointData& Point : Corner) { Point.PointType = ESplinePointType::Linear; }
    TestFalse(TEXT("Sharp 3D corner cannot dissolve at microscopic tolerance"),
        StrandSplineCleanup::Dissolve(Corner, Native(Corner), TArray<int32>{1}, 0.001, Rejected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandExactSubdivideTest,
    "Strand.Editor.Authoring.Cleanup.ExactSubdivision3D",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandExactSubdivideTest::RunTest(const FString& Parameters)
{
    using namespace StrandCleanupTestUtils;
    TArray<FSplinePointData> Original = Curved3D();
    USplineComponent* Before = Native(Original);
    TArray<FSplinePointData> Result;
    constexpr int32 Cuts = 3;
    TestTrue(TEXT("De Casteljau parameter subdivision succeeds"),
        StrandSplineCleanup::SubdivideExact(Original, Before, Cuts, false, Result));
    TestEqual(TEXT("Exact control point count"), Result.Num(), 3+2*Cuts);
    if (Result.Num() != 3+2*Cuts) { return false; }
    USplineComponent* After = Native(Result);
    double MaxError = 0.0;
    for (int32 Span = 0; Span < 2; ++Span)
    {
        for (int32 K = 0; K < Cuts+1; ++K)
        {
            for (int32 S = 0; S <= 12; ++S)
            {
                const double T = static_cast<double>(S)/12;
                const FVector Reference = StrandSplineCleanup::EvalNativeSegment(Before, Span, (K+T)/(Cuts+1));
                const FVector Actual = StrandSplineCleanup::EvalNativeSegment(After, Span*(Cuts+1)+K, T);
                MaxError = FMath::Max(MaxError, FVector::Distance(Reference, Actual));
            }
        }
    }
    TestTrue(TEXT("Original 3D cubic is unchanged at dense parameter samples"), MaxError < 0.001);
    TestTrue(TEXT("First segment derivative is scaled correctly"),
        Result[0].LeaveTangent.Equals(Before->GetSplinePointAt(0, ESplineCoordinateSpace::Local).LeaveTangent / (Cuts + 1.0), 0.001));
    TestTrue(TEXT("Last segment derivative is scaled correctly"),
        Result.Last().ArriveTangent.Equals(Before->GetSplinePointAt(2, ESplineCoordinateSpace::Local).ArriveTangent / (Cuts + 1.0), 0.001));
    TestTrue(TEXT("Generated anchor preserves interpolated rotation"), !Result[1].LocalRotation.ContainsNaN());
    TestTrue(TEXT("Generated anchor preserves interpolated scale"), !Result[1].LocalScale.ContainsNaN());
    TArray<FSplinePointData> ArcResult;
    TestTrue(TEXT("Equal-distance cut placement succeeds"),
        StrandSplineCleanup::SubdivideExact(Original, Before, 2, true, ArcResult));
    TestEqual(TEXT("Equal-distance count"), ArcResult.Num(), 7);
    if (ArcResult.Num() != 7) { return false; }
    TArray<FSplinePointData> Closed = Curved3D();
    USplineComponent* ClosedNative = Native(Closed, true);
    TArray<FSplinePointData> ClosedResult;
    TestTrue(TEXT("Closed 3D spline exact subdivision succeeds"),
        StrandSplineCleanup::SubdivideExact(Closed, ClosedNative, 2, false, ClosedResult));
    TestEqual(TEXT("Closed loop creates cuts without duplicate seam"), ClosedResult.Num(), 9);
    if (ClosedResult.Num() == 9)
    {
        USplineComponent* ClosedAfter = Native(ClosedResult, true);
        double MaxLoopError = 0.0;
        for (int32 Span = 0; Span < 3; ++Span)
        {
            for (int32 Piece = 0; Piece < 3; ++Piece)
            {
                for (int32 Sample = 0; Sample <= 8; ++Sample)
                {
                    const double T = static_cast<double>(Sample) / 8.0;
                    const FVector Reference = StrandSplineCleanup::EvalNativeSegment(ClosedNative, Span, (Piece + T) / 3.0);
                    const FVector Actual = StrandSplineCleanup::EvalNativeSegment(ClosedAfter, Span * 3 + Piece, T);
                    MaxLoopError = FMath::Max(MaxLoopError, FVector::Distance(Reference, Actual));
                }
            }
        }
        TestTrue(TEXT("Closed loop retains cubic geometry across seam"), MaxLoopError < 0.001);
        TestTrue(TEXT("Closed seam incoming derivative is scaled correctly"),
            ClosedResult[0].ArriveTangent.Equals(
                ClosedNative->GetSplinePointAt(0, ESplineCoordinateSpace::Local).ArriveTangent / 3.0, 0.001));
    }
    TArray<FSplinePointData> Constant = Line(3);
    Constant[0].PointType = ESplinePointType::Constant;
    TArray<FSplinePointData> ConstantResult;
    TestTrue(TEXT("Constant interpolation subdivisions accepted"),
        StrandSplineCleanup::SubdivideExact(Constant, Native(Constant), 2, false, ConstantResult));
    TestEqual(TEXT("Constant source endpoint is preserved"), ConstantResult.Num(), 7);
    if (ConstantResult.Num() == 7)
    {
        TestTrue(TEXT("Step remains at the first anchor until segment end"),
            ConstantResult[1].LocalPosition.Equals(Constant[0].LocalPosition));
        TestTrue(TEXT("Original second anchor is not displaced by constant span"),
            ConstantResult[3].LocalPosition.Equals(Constant[1].LocalPosition));
    }
    TestFalse(TEXT("Excessive cut count rejected"),
        StrandSplineCleanup::SubdivideExact(Original, Before, 100, false, ArcResult));
    TestEqual(TEXT("Invalid cut request does not change output"), ArcResult.Num(), 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandContinuityRepairTest,
    "Strand.Editor.Authoring.Cleanup.ContinuityAndRepair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandContinuityRepairTest::RunTest(const FString& Parameters)
{
    using namespace StrandCleanupTestUtils;
    TArray<FSplinePointData> Source = Curved3D();
    USplineComponent* Spline = Native(Source);
    const TArray<int32> Selected = {1};
    const FVector OriginalPosition = Source[1].LocalPosition;
    TArray<FSplinePointData> G1 = Source;
    TestTrue(TEXT("G1 continuity succeeds"),
        StrandSplineCleanup::SetContinuity(G1, Spline, Selected, false));
    TestTrue(TEXT("G1 vectors aligned"),
        FVector::DotProduct(G1[1].ArriveTangent.GetSafeNormal(), G1[1].LeaveTangent.GetSafeNormal()) > 0.9999);
    TestTrue(TEXT("G1 keeps original point"), G1[1].LocalPosition.Equals(OriginalPosition));
    TArray<FSplinePointData> C1 = Source;
    TestTrue(TEXT("C1 continuity succeeds"),
        StrandSplineCleanup::SetContinuity(C1, Spline, Selected, true));
    TestTrue(TEXT("C1 tangents have matching vectors"),
        C1[1].ArriveTangent.Equals(C1[1].LeaveTangent, 0.0001));
    TArray<FSplinePointData> Normalized = Source;
    TestTrue(TEXT("Normalize tangent lengths"),
        StrandSplineCleanup::NormalizeSelectedTangents(Normalized, Spline, Selected, 75.0));
    TestTrue(TEXT("Incoming magnitude 75cm"), FMath::IsNearlyEqual(Normalized[1].ArriveTangent.Size(), 75.0, 0.001));
    TestTrue(TEXT("Outgoing magnitude 75cm"), FMath::IsNearlyEqual(Normalized[1].LeaveTangent.Size(), 75.0, 0.001));
    TArray<FSplinePointData> Recalculated = Source;
    TestTrue(TEXT("Recalculate neighbor-informed tangents"),
        StrandSplineCleanup::RecalculateSelectedTangents(Recalculated, Spline, Selected));
    TestTrue(TEXT("Recalculate preserves point"), Recalculated[1].LocalPosition.Equals(OriginalPosition));
    TArray<FSplinePointData> Duplicates = Line(5);
    Duplicates[2].LocalPosition = Duplicates[1].LocalPosition;
    for (FSplinePointData& Point : Duplicates) { Point.PointType = ESplinePointType::Linear; }
    const TArray<int32> Found = StrandSplineCleanup::FindDuplicateAnchors(Duplicates, false, 0.01);
    TestTrue(TEXT("Detects duplicate neighbor"), Found.Contains(2));
    TestFalse(TEXT("Does not mistake other anchors for duplicates"), Found.Contains(3));
    StrandSplineCleanup::FResult Repaired;
    TestTrue(TEXT("Degenerate repair runs"), StrandSplineCleanup::RemoveDegenerate(Duplicates, Native(Duplicates), 0.1, 1.0, Repaired));
    TestTrue(TEXT("Repair removes duplicate point"), Repaired.Removed >= 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandCleanupWeldSelectionTest,
    "Strand.Editor.Authoring.Cleanup.WeldAndLengthSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandCleanupWeldSelectionTest::RunTest(const FString& Parameters)
{
    using namespace StrandCleanupTestUtils;
    TArray<FSplinePointData> Loop = Line(4);
    Loop.Last().LocalPosition = Loop[0].LocalPosition + FVector(0.05, 0, 0);
    USplineComponent* NativeLoop = Native(Loop);
    bool bClosed = false;
    TArray<FSplinePointData> Rejected = Loop;
    TestFalse(TEXT("Reject gap larger than welding threshold"),
        StrandSplineCleanup::WeldLoopEndpoints(Rejected, bClosed, NativeLoop, 0.01));
    TestFalse(TEXT("Rejected weld keeps loop open"), bClosed);
    TestTrue(TEXT("Weld adjacent loop endpoints"),
        StrandSplineCleanup::WeldLoopEndpoints(Loop, bClosed, NativeLoop, 0.1));
    TestTrue(TEXT("Weld closes loop"), bClosed);
    TestEqual(TEXT("One redundant seam anchor removed"), Loop.Num(), 3);
    TestTrue(TEXT("First anchor retained"), Loop[0].LocalPosition.Equals(FVector::ZeroVector));
    const TArray<FSplinePointData> Points = Line(5);
    const TArray<int32> Short = StrandSplineCleanup::SelectBySegmentLength(Native(Points), 150, true);
    TestEqual(TEXT("All short segments yield all anchors"), Short.Num(), 5);
    const TArray<int32> Long = StrandSplineCleanup::SelectBySegmentLength(Native(Points), 150, false);
    TestTrue(TEXT("No segment above threshold"), Long.IsEmpty());
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
