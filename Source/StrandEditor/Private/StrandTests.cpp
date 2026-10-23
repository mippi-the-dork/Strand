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
    TestTrue(TEXT("<use> translation is applied"), Curves[0].Points[0].LocalPosition.X > 40.0);

    const FSplineImportedCurve& Mixed = Curves[1];
    TestTrue(TEXT("Implicit Z makes a closed loop"), Mixed.bClosedLoop);
    TestEqual(TEXT("No duplicated Z endpoint"), Mixed.Points.Num(), 3);
    if (Mixed.Points.Num() == 3)
    {
        TestTrue(TEXT("Straight segment has exact chord derivative"),
            Mixed.Points[0].LeaveTangent.Equals(FVector(100, 0, 0), 0.0001));
        TestTrue(TEXT("Incoming and outgoing handles are independent at mixed corner"),
            Mixed.Points[1].ArriveTangent.Equals(FVector(100, 0, 0), 0.0001) &&
            Mixed.Points[1].LeaveTangent.Equals(FVector(90, 0, 0), 0.0001));
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
        TEXT("<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\">")
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
        TestTrue(FString::Printf(TEXT("Contour %d source/native geometry exact"), Index),
            Asset->MatchesSplineComponent(Component, 0.001f));
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
            // Strand intentionally maps SVG +Y (down) to Unreal -Y (up).
            return FVector(FCString::Atod(*Cells[Offset]), -FCString::Atod(*Cells[Offset + 1]), 0.0);
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
