#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "StrandProjectionWorkbench.h"
#include "StrandProjectionPlacement.h"
#include "SplineEditorCommands.h"
#include "StrandSplineActor.h"
#include "StrandSurfaceProjectionMath.h"
#include "StrandProjectionAssetCommit.h"
#include "StrandProjectionReviewSnapshot.h"
#include "StrandProjectionAssetBaseline.h"
#include "StrandContinuousProjection.h"



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionReviewSourceStaleTest,
    "Strand.Editor.SurfaceProjection.ReviewRejectsStaleSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionReviewSourceStaleTest::RunTest(const FString& Parameters)
{
    USplineComponent* Source = NewObject<USplineComponent>(GetTransientPackage());
    Source->ClearSplinePoints(false);
    Source->AddSplinePoint(FVector(0, 0, 0), ESplineCoordinateSpace::Local, false);
    Source->AddSplinePoint(FVector(100, 0, 0), ESplineCoordinateSpace::Local, false);
    Source->AddSplinePoint(FVector(200, 0, 0), ESplineCoordinateSpace::Local, false);
    Source->UpdateSpline();
    const StrandProjectionReviewSnapshot::FSnapshot Saved =
        StrandProjectionReviewSnapshot::FSnapshot::Capture(Source);
    TestTrue(TEXT("Unmodified native source passes review verification"), Saved.Matches(Source));
    Source->SetLocationAtSplinePoint(2, FVector(200, 0, 90), ESplineCoordinateSpace::Local);
    TestFalse(TEXT("Changes to an unprojected anchor invalidate the review"), Saved.Matches(Source));
    Saved.Restore(Source);
    TestTrue(TEXT("Source snapshot restores original point geometry"), Saved.Matches(Source));
    Source->SetTangentAtSplinePoint(1, FVector(0, 150, 0), ESplineCoordinateSpace::Local);
    TestFalse(TEXT("Tangent-only edits invalidate the review"), Saved.Matches(Source));
    Saved.Restore(Source);
    Source->SetClosedLoop(true);
    TestFalse(TEXT("Closed-loop changes invalidate the review"), Saved.Matches(Source));
    Saved.Restore(Source);
    TestTrue(TEXT("Restoring the original closed-loop state succeeds"), Saved.Matches(Source));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionAssetStaleTest,
    "Strand.Editor.SurfaceProjection.ReviewRejectsStaleAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionAssetStaleTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A;
    A.LocalPosition = FVector(10, 20, 30);
    A.HandleMode = EStrandBezierHandleMode::Mirrored;
    FSplinePointData B = A;
    B.LocalPosition = FVector(100, 20, 30);
    Asset->Points = {A, B};
    const StrandProjectionAssetBaseline::FSnapshot Saved =
        StrandProjectionAssetBaseline::FSnapshot::Capture(Asset);
    TestTrue(TEXT("Unmodified asset passes stale-asset check"), Saved.Matches(Asset));
    Asset->Points[1].LocalPosition.Z += 10;
    TestFalse(TEXT("Unprojected asset anchor changes reject Apply"), Saved.Matches(Asset));
    Asset->Points[1] = B;
    Asset->Points[0].HandleMode = EStrandBezierHandleMode::Aligned;
    TestFalse(TEXT("Handle-mode-only edits reject stale Apply"), Saved.Matches(Asset));
    Asset->Points[0] = A;
    Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
    TestFalse(TEXT("Placement-mode changes reject stale Apply"), Saved.Matches(Asset));
    Asset->DefaultCoordinateMode = EStrandCoordinateMode::Relative;
    TestTrue(TEXT("Restored asset allows Apply"), Saved.Matches(Asset));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionInvalidCommitTest,
    "Strand.Editor.SurfaceProjection.InvalidCommitIsNonDestructive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionInvalidCommitTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A;
    A.LocalPosition = FVector(12, 34, 56);
    A.HandleMode = EStrandBezierHandleMode::Aligned;
    FSplinePointData B = A;
    B.LocalPosition = FVector(98, 76, 54);
    Asset->Points = {A, B};
    USplineComponent* Source = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Source);
    const TArray<int32> InvalidMapping = {0, 0};
    TestFalse(TEXT("Duplicate original index mapping rejects Apply"),
        StrandProjectionAssetCommit::Apply(Asset, Source, &InvalidMapping));
    TestEqual(TEXT("Rejected apply retains original anchor count"), Asset->Points.Num(), 2);
    TestTrue(TEXT("Rejected apply retains original positions"),
        Asset->Points[0].LocalPosition.Equals(A.LocalPosition) &&
        Asset->Points[1].LocalPosition.Equals(B.LocalPosition));
    TestTrue(TEXT("Rejected apply retains authored handle mode"),
        Asset->Points[0].HandleMode == EStrandBezierHandleMode::Aligned);
    TestTrue(TEXT("Rejected apply does not change placement defaults"),
        Asset->DefaultCoordinateMode == EStrandCoordinateMode::Relative);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionCameraPlacementTest,
    "Strand.Editor.SurfaceProjection.CameraPlacementTransform",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionCameraPlacementTest::RunTest(const FString& Parameters)
{
    const FVector Camera(100, 200, 300);
    const FRotator CameraRotation(0, 90, 0);
    const FVector Center(50, 20, 0);
    const FVector Scale(2, 1, 1);
    TestFalse(TEXT("Rejects zero placement distance"), StrandProjectionPlacement::ValidCameraDistance(0));
    TestFalse(TEXT("Rejects negative placement distance"),
        StrandProjectionPlacement::ValidCameraDistance(-500.0));
    TestTrue(TEXT("Accepts positive placement distance"), StrandProjectionPlacement::ValidCameraDistance(500));
    const FTransform Unrotated = StrandProjectionPlacement::CameraPlacement(Camera, CameraRotation,
        500.0, Center, FQuat::Identity, Scale);
    TestTrue(TEXT("Unrotated placement preserves actor orientation"), Unrotated.GetRotation().Equals(FQuat::Identity));
    TestTrue(TEXT("Unrotated placement positions the anchor center at camera distance"),
        Unrotated.TransformPosition(Center).Equals(Camera + CameraRotation.Vector() * 500.0, 0.01));
    const FQuat Facing = CameraRotation.Quaternion();
    const FTransform Rotated = StrandProjectionPlacement::CameraPlacement(Camera, CameraRotation,
        1500.0, Center, Facing, Scale);
    TestTrue(TEXT("Optional camera orientation matches rotation"), Rotated.GetRotation().Equals(Facing));
    TestTrue(TEXT("Rotated placement preserves world anchor center and distance"),
        Rotated.TransformPosition(Center).Equals(Camera + CameraRotation.Vector() * 1500.0, 0.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionWorkbenchOptionsTest,
    "Strand.Editor.SurfaceProjection.WorkbenchOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionWorkbenchOptionsTest::RunTest(const FString& Parameters)
{
    FStrandProjectionOptions Options;
    Options.TargetScope = EStrandProjectionTargetScope::EntireLevel;
    Options.RayMode = EStrandProjectionRayMode::WorldDirection;
    Options.WorldDirection = FVector(0, 0, -1);
    FString Error;
    TestTrue(TEXT("All scene needs no manually chosen target"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.LastAnchor = 2;
    Options.FirstAnchor = 3;
    TestFalse(TEXT("Reversed anchor ranges rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.FirstAnchor = 0;
    Options.LastAnchor = -1;
    TestTrue(TEXT("Negative last anchor denotes all"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.WorldDirection = FVector::ZeroVector;
    TestFalse(TEXT("Zero world direction rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    TestTrue(TEXT("World direction controls enabled for world mode"),
        StrandProjectionWorkbench::UsesWorldDirection(Options));
    TestFalse(TEXT("Continuous controls disabled when feature is off"),
        StrandProjectionWorkbench::UsesContinuousSettings(Options));
    Options.RayMode = EStrandProjectionRayMode::CameraProjection;
    TestFalse(TEXT("World direction controls disabled for perspective camera mode"),
        StrandProjectionWorkbench::UsesWorldDirection(Options));
    Options.RayMode = EStrandProjectionRayMode::CameraDirection;
    TestFalse(TEXT("World direction controls disabled for parallel camera mode"),
        StrandProjectionWorkbench::UsesWorldDirection(Options));
    Options.RayMode = EStrandProjectionRayMode::CameraProjection;
    TestTrue(TEXT("Camera projection does not need a world vector"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.TargetScope = EStrandProjectionTargetScope::SingleActor;
    TestFalse(TEXT("Single actor requires a target"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.TargetScope = EStrandProjectionTargetScope::ActorSet;
    TestFalse(TEXT("Actor set requires targets"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.TargetScope = EStrandProjectionTargetScope::EntireLevel;
    Options.Strength = 1.5f;
    TestFalse(TEXT("Out-of-range strength rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.Strength = 0.5f;
    Options.MaxDistance = 0.0f;
    TestFalse(TEXT("Nonpositive distance rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.MaxDistance = 200.0f;
    TestFalse(TEXT("Empty splines cannot be projected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 0, Error));
    TestTrue(TEXT("Valid camera projection settings accepted"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.bContinuousConform = true;
    TestTrue(TEXT("Continuous controls enabled when feature is on"),
        StrandProjectionWorkbench::UsesContinuousSettings(Options));
    Options.ConformMaxSpacing = 0.0f;
    TestFalse(TEXT("Continuous spacing must be positive"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.ConformMaxSpacing = 100.0f;
    Options.ConformTolerance = 0.0f;
    TestFalse(TEXT("Continuous error tolerance must be positive"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.ConformTolerance = 2.0f;
    Options.ConformMaxNewPoints = 8193;
    TestFalse(TEXT("Continuous new-point budget is capped"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    Options.ConformMaxNewPoints = 1024;
    TestTrue(TEXT("Valid continuous settings accepted"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionSelectionWeightsTest,
    "Strand.Editor.SurfaceProjection.SelectedAnchorFalloff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionSelectionWeightsTest::RunTest(const FString& Parameters)
{
    const TArray<double> Weights = {0.0, 0.4, 1.0, 0.0};
    TestEqual(TEXT("All-anchors mode ignores a saved selection"),
        StrandSurfaceProjectionMath::EffectiveStrength(0.5, Weights, false, 0), 0.5);
    TestEqual(TEXT("Unselected points do not project"),
        StrandSurfaceProjectionMath::EffectiveStrength(0.5, Weights, true, 0), 0.0);
    TestTrue(TEXT("Soft selection scales projection strength"),
        FMath::IsNearlyEqual(StrandSurfaceProjectionMath::EffectiveStrength(0.5, Weights, true, 1), 0.2));
    TestEqual(TEXT("Selected anchor receives full requested strength"),
        StrandSurfaceProjectionMath::EffectiveStrength(0.5, Weights, true, 2), 0.5);
    FStrandProjectionOptions Options;
    Options.TargetScope = EStrandProjectionTargetScope::EntireLevel;
    Options.RayMode = EStrandProjectionRayMode::WorldDirection;
    Options.bUseAnchorWeights = true;
    Options.AnchorWeights = Weights;
    FString Error;
    TestTrue(TEXT("Selection mask matches current spline"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    TestFalse(TEXT("Selection mask mismatch is rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 5, Error));
    Options.AnchorWeights = {0.0, 0.0, 0.0, 0.0};
    TestFalse(TEXT("Empty effective selection is rejected"),
        StrandProjectionWorkbench::ValidateOptions(Options, 4, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionAssetSessionTest,
    "Strand.Editor.SurfaceProjection.TemporaryAssetCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionAssetSessionTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData P0;
    P0.LocalPosition = FVector(0, 0, 0);
    P0.HandleMode = EStrandBezierHandleMode::Mirrored;
    P0.PointType = ESplinePointType::CurveCustomTangent;
    P0.ArriveTangent = FVector(-30, 0, 0);
    P0.LeaveTangent = FVector(30, 0, 0);
    FSplinePointData P1 = P0;
    P1.LocalPosition = FVector(100, 0, 0);
    P1.HandleMode = EStrandBezierHandleMode::Aligned;
    Asset->Points = {P0, P1};
    USplineComponent* Temporary = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Temporary);
    Temporary->SetLocationAtSplinePoint(0, FVector(0, 0, 70), ESplineCoordinateSpace::Local);
    TestTrue(TEXT("Accepting a temporary native spline copies geometry to the asset"),
        StrandProjectionAssetCommit::Apply(Asset, Temporary));
    TestTrue(TEXT("Projected position returned to asset"),
        Asset->Points[0].LocalPosition.Equals(FVector(0, 0, 70), KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Mirrored handle authoring rule preserved"),
        Asset->Points[0].HandleMode == EStrandBezierHandleMode::Mirrored);
    TestTrue(TEXT("Aligned handle authoring rule preserved"),
        Asset->Points[1].HandleMode == EStrandBezierHandleMode::Aligned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandConformBudgetDistributionTest,
    "Strand.Editor.SurfaceProjection.ErrorAwareBudgetDistribution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandConformBudgetDistributionTest::RunTest(const FString& Parameters)
{
    const FVector A(0, 0, 0), B(200, 0, 0);
    const double Flat = StrandContinuousProjection::EstimateSpanDemand(A, B, 200, 50, 2,
        FVector(50, 0, 0), FVector(100, 0, 0), FVector(150, 0, 0));
    const double Uneven = StrandContinuousProjection::EstimateSpanDemand(A, B, 200, 50, 2,
        FVector(50, 0, 30), FVector(100, 0, -30), FVector(150, 0, 30));
    TestTrue(TEXT("Uneven surface has greater point demand at equal length"), Uneven > Flat * 5.0);
    const double QuarterPeak = StrandContinuousProjection::EstimateSpanDemand(A, B, 200, 50, 2,
        FVector(50, 0, 30), FVector(100, 0, 0), FVector(150, 0, -30));
    TestTrue(TEXT("Quarter samples detect deviations hidden at midpoint"), QuarterPeak > Flat);
    TArray<double> Demands = {Flat, Uneven, Flat};
    TArray<int32> Allocations;
    StrandContinuousProjection::AllocateSpanBudgets(Demands, 20, Allocations);
    TestEqual(TEXT("Allocator returns one allocation per span"), Allocations.Num(), 3);
    TestEqual(TEXT("Entire point budget assigned"), Allocations[0] + Allocations[1] + Allocations[2], 20);
    TestTrue(TEXT("High-error span receives most of the remaining budget"),
        Allocations[1] > Allocations[0] + Allocations[2]);
    TestTrue(TEXT("Flat spans are not starved where budget permits"),
        Allocations[0] >= 1 && Allocations[2] >= 1);
    StrandContinuousProjection::AllocateSpanBudgets(Demands, 2, Allocations);
    TestEqual(TEXT("Tiny budget still respects hard cap"),
        Allocations[0] + Allocations[1] + Allocations[2], 2);
    TestTrue(TEXT("Tiny budget prioritizes high-error span"), Allocations[1] >= 1);
    StrandContinuousProjection::AllocateSpanBudgets(Demands, 0, Allocations);
    TestEqual(TEXT("Zero budget inserts nothing"),
        Allocations[0] + Allocations[1] + Allocations[2], 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandConformErrorPriorityTest,
    "Strand.Editor.SurfaceProjection.ErrorBeforeSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandConformErrorPriorityTest::RunTest(const FString& Parameters)
{
    // Start with a long, flat half and a short but sharp error feature.
    // Only two points can be added, so the sharp section should receive one.
    const auto Sample = [](double T) -> FVector
    {
        const double Height = T > 0.5 ? 50.0 * FMath::Sin((T - 0.5) * UE_PI * 2.0) : 0.0;
        return FVector(1000.0 * T, 0.0, Height);
    };
    TArray<StrandContinuousProjection::FSample> Samples;
    bool bLimited = false;
    TestTrue(TEXT("Priority planner completes under fixed budget"),
        StrandContinuousProjection::BuildSegment(Sample(0), Sample(1), 1000,
            80, 1, 2, Sample, Samples, &bLimited));
    TestEqual(TEXT("Planner respects two-point budget"), Samples.Num(), 2);
    TestTrue(TEXT("Sharp half receives refinement before flat spacing"),
        Samples.ContainsByPredicate([](const StrandContinuousProjection::FSample& S)
        { return S.T > 0.5; }));
    TestTrue(TEXT("Budget limitation reported"), bLimited);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandContinuousConformAdaptiveTest,
    "Strand.Editor.SurfaceProjection.ContinuousAdaptiveSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandContinuousConformAdaptiveTest::RunTest(const FString& Parameters)
{
    auto Surface = [](double T) -> FVector
    {
        return FVector(T * 400.0, 0.0, 35.0 * FMath::Sin(T * UE_PI * 2.0));
    };
    TArray<StrandContinuousProjection::FSample> Samples;
    TestTrue(TEXT("Curve subdivided against projected surface"),
        StrandContinuousProjection::BuildSegment(Surface(0), Surface(1), 400.0,
            75.0, 1.0, 256, Surface, Samples));
    TestTrue(TEXT("Generated interior points"), Samples.Num() >= 5);
    double PreviousT = 0.0;
    for (const auto& Sample : Samples)
    {
        TestTrue(TEXT("Subdivision order strictly increasing"), Sample.T > PreviousT && Sample.T < 1.0);
        TestTrue(TEXT("Every generated point lies on projected surface"),
            Sample.Position.Equals(Surface(Sample.T), 0.001));
        PreviousT = Sample.T;
    }
    bool bLimited = false;
    TestTrue(TEXT("Small budget returns best-effort conform rather than failing"),
        StrandContinuousProjection::BuildSegment(Surface(0), Surface(1), 400.0,
            5.0, 0.1, 2, Surface, Samples, &bLimited));
    TestTrue(TEXT("Budget saturation is reported"), bLimited);
    TestEqual(TEXT("Small budget inserts exactly two samples"), Samples.Num(), 2);
    TestTrue(TEXT("Best-effort points sorted"), Samples[0].T < Samples[1].T);
    TestTrue(TEXT("Both best-effort points lie on the projected surface"),
        Samples[0].Position.Equals(Surface(Samples[0].T), 0.001) &&
        Samples[1].Position.Equals(Surface(Samples[1].T), 0.001));
    TestTrue(TEXT("Zero budget keeps a valid original span"),
        StrandContinuousProjection::BuildSegment(Surface(0), Surface(1), 400.0,
            5.0, 0.1, 0, Surface, Samples, &bLimited));
    TestTrue(TEXT("Zero-point limit reports under-resolution"), bLimited);
    TestEqual(TEXT("Zero budget generates no anchors"), Samples.Num(), 0);
    // Each candidate interval should perform at most one trace. A naive
    // best-first implementation repeatedly traces all pending intervals,
    // making the point limit prohibitively expensive on complex surfaces.
    int32 Evaluations = 0;
    TestTrue(TEXT("Planner supports substantial point budgets"),
        StrandContinuousProjection::BuildSegment(Surface(0), Surface(1), 400.0,
            1.0, 0.02, 128,
            [&Evaluations, &Surface](double T) { ++Evaluations; return Surface(T); },
            Samples, &bLimited));
    TestTrue(TEXT("Sampler caches midpoint traces rather than recomputing"),
        Evaluations <= 2 * Samples.Num() + 1);
    TestTrue(TEXT("Generated points always respect maximum"), Samples.Num() <= 128);
    for (int32 I = 1; I < Samples.Num(); ++I)
    {
        TestTrue(TEXT("Dense sample output remains sorted"), Samples[I - 1].T < Samples[I].T);
    }
    TestTrue(TEXT("Straight short spans do not need added points"),
        StrandContinuousProjection::BuildSegment(FVector::ZeroVector, FVector(25,0,0),
            25.0, 100.0, 1.0, 0,
            [](double T) { return FVector(25.0 * T, 0, 0); }, Samples));
    TestEqual(TEXT("Straight short spans stay unchanged"), Samples.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandContinuousConformCommitTest,
    "Strand.Editor.SurfaceProjection.ContinuousAssetPointMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandContinuousConformCommitTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A;
    A.PointType = ESplinePointType::CurveCustomTangent;
    A.HandleMode = EStrandBezierHandleMode::Mirrored;
    FSplinePointData B = A;
    B.LocalPosition = FVector(200, 0, 0);
    B.HandleMode = EStrandBezierHandleMode::Aligned;
    Asset->Points = {A,B};
    USplineComponent* Spline = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Spline);
    FSplinePoint Inserted = Spline->GetSplinePointAt(0, ESplineCoordinateSpace::Local);
    Inserted.Position = FVector(100,0,25);
    Inserted.InputKey = 1.0f;
    Inserted.Type = ESplinePointType::CurveCustomTangent;
    // Rebuild ordered native component points explicitly.
    const FSplinePoint End = Spline->GetSplinePointAt(1, ESplineCoordinateSpace::Local);
    const FSplinePoint Start = Spline->GetSplinePointAt(0, ESplineCoordinateSpace::Local);
    Spline->ClearSplinePoints(false);
    Spline->AddPoint(Start, false);
    Spline->AddPoint(Inserted, false);
    FSplinePoint Last = End;
    Last.InputKey = 2.0f;
    Spline->AddPoint(Last, false);
    Spline->UpdateSpline();
    const TArray<int32> Mapping = {0, INDEX_NONE, 1};
    TestTrue(TEXT("Inserted point can be committed with source-anchor mapping"),
        StrandProjectionAssetCommit::Apply(Asset, Spline, &Mapping));
    TestEqual(TEXT("Asset receives added sample"), Asset->Points.Num(), 3);
    TestTrue(TEXT("Original first handle policy stays mirrored"),
        Asset->Points[0].HandleMode == EStrandBezierHandleMode::Mirrored);
    TestTrue(TEXT("New point gets free handles"),
        Asset->Points[1].HandleMode == EStrandBezierHandleMode::Free);
    TestTrue(TEXT("Original second handle policy stays aligned"),
        Asset->Points[2].HandleMode == EStrandBezierHandleMode::Aligned);
    TestTrue(TEXT("Relative commit retains original asset placement mode"),
        Asset->DefaultCoordinateMode == EStrandCoordinateMode::Relative);
    USplineDataAsset* WorldAsset = NewObject<USplineDataAsset>(GetTransientPackage());
    WorldAsset->Points = {A, B};
    TestTrue(TEXT("Absolute commit accepts mapped points"),
        StrandProjectionAssetCommit::Apply(WorldAsset, Spline, &Mapping, true));
    TestTrue(TEXT("World-baked asset defaults to absolute placement"),
        WorldAsset->DefaultCoordinateMode == EStrandCoordinateMode::Absolute);
    TestTrue(TEXT("Absolute commit retains original first handle policy"),
        WorldAsset->Points[0].HandleMode == EStrandBezierHandleMode::Mirrored);
    const TArray<int32> DuplicateMapping = {0, 0, 1};
    TestFalse(TEXT("Duplicate original-anchor mapping must be rejected"),
        StrandProjectionAssetCommit::Apply(Asset, Spline, &DuplicateMapping));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionReturnSpaceTest,
    "Strand.Editor.SurfaceProjection.ReturnSpaceTransform",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandProjectionReturnSpaceTest::RunTest(const FString& Parameters)
{
    FSplinePointData Local;
    Local.LocalPosition = FVector(25, 30, -15);
    Local.ArriveTangent = FVector(10, -5, 3);
    Local.LeaveTangent = FVector(8, 11, -2);
    Local.LocalRotation = FRotator(5, 20, 35);
    Local.LocalScale = FVector(0.5, 1.25, 2.0);
    const FTransform World(FRotator(10, 90, -20), FVector(500, -200, 90), FVector(2, 3, 4));
    FSplinePointData Baked = Local;
    StrandProjectionAssetCommit::BakePointToWorld(Baked, World);
    TestTrue(TEXT("Absolute position matches native world placement"),
        Baked.LocalPosition.Equals(World.TransformPosition(Local.LocalPosition), 0.001));
    TestTrue(TEXT("Arrival tangent includes rotation and scale"),
        Baked.ArriveTangent.Equals(World.TransformVector(Local.ArriveTangent), 0.001));
    TestTrue(TEXT("Departure tangent includes rotation and scale"),
        Baked.LeaveTangent.Equals(World.TransformVector(Local.LeaveTangent), 0.001));
    TestTrue(TEXT("World rotation is baked into each authored point"),
        Baked.LocalRotation.Quaternion().Equals((World.GetRotation() * Local.LocalRotation.Quaternion()), 0.001));
    TestTrue(TEXT("World scaling is baked into point scale"),
        Baked.LocalScale.Equals(Local.LocalScale * World.GetScale3D(), 0.001));
    TestTrue(TEXT("Local source point remains unchanged"),
        Local.LocalPosition.Equals(FVector(25,30,-15)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandContinuousConformTangentTest,
    "Strand.Editor.SurfaceProjection.ContinuousTangentBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandContinuousConformTangentTest::RunTest(const FString& Parameters)
{
    const FVector Tangent = StrandContinuousProjection::TangentAt(
        FVector(0,0,0), FVector(10,0,0), FVector(10,1000,0));
    TestTrue(TEXT("Tangent bounded by shortest neighboring edge"), Tangent.Size() <= 15.001);
    TestTrue(TEXT("Degenerate edge produces safe zero tangent"),
        StrandContinuousProjection::TangentAt(FVector::ZeroVector, FVector::ZeroVector,
            FVector(100,0,0)).IsNearlyZero());
    return true;
}
#endif
