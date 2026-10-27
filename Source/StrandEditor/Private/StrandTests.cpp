#include "StrandProjectSettings.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
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
    TestTrue(TEXT("<use> translation is applied with SVG X mirror"), Curves[0].Points[0].LocalPosition.X < -40.0);

    const FSplineImportedCurve& Mixed = Curves[1];
    TestTrue(TEXT("Implicit Z makes a closed loop"), Mixed.bClosedLoop);
    TestEqual(TEXT("No duplicated Z endpoint"), Mixed.Points.Num(), 3);
    if (Mixed.Points.Num() == 3)
    {
        TestTrue(TEXT("Straight segment has exact chord derivative"),
            Mixed.Points[0].LeaveTangent.Equals(FVector(-100, 0, 0), 0.0001));
        TestTrue(TEXT("Incoming and outgoing handles are independent at mixed corner"),
            Mixed.Points[1].ArriveTangent.Equals(FVector(-100, 0, 0), 0.0001) &&
            Mixed.Points[1].LeaveTangent.Equals(FVector(-90, 0, 0), 0.0001));
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
        TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\">\n")
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
        const bool bExact = Asset->MatchesSplineComponent(Component, 0.001f);
        if (!bExact)
        {
            // Keep this strict, even when the analytic curve matches. Print the
            // first differing field so the engine's representation can be
            // diagnosed without weakening point/tangent/rotation/scale tests.
            for (int32 PointIndex = 0; PointIndex < Curve.Points.Num(); ++PointIndex)
            {
                const FSplinePoint Actual = Component->GetSplinePointAt(
                    PointIndex, ESplineCoordinateSpace::Local);
                const FSplinePointData& Wanted = Curve.Points[PointIndex];
                const bool bPosition = Actual.Position.Equals(Wanted.LocalPosition, 0.001);
                const bool bArrive = Actual.ArriveTangent.Equals(Wanted.ArriveTangent, 0.001);
                const bool bLeave = Actual.LeaveTangent.Equals(Wanted.LeaveTangent, 0.001);
                const bool bRotation = Actual.Rotation.Equals(Wanted.LocalRotation, 0.001);
                const bool bScale = Actual.Scale.Equals(Wanted.LocalScale, 0.001);
                const bool bType = Component->GetSplinePointType(PointIndex) == Wanted.PointType.GetValue();
                if (!(bPosition && bArrive && bLeave && bRotation && bScale && bType))
                {
                    AddWarning(FString::Printf(
                        TEXT("Contour %d point %d: Position=%d (delta=%.8g), Arrive=%d (delta=%.8g), ")
                        TEXT("Leave=%d (delta=%.8g), Rotation=%d, Scale=%d (delta=%.8g), Type=%d (native=%d asset=%d)."),
                        Index, PointIndex, bPosition ? 1 : 0,
                        (Actual.Position - Wanted.LocalPosition).Size(), bArrive ? 1 : 0,
                        (Actual.ArriveTangent - Wanted.ArriveTangent).Size(), bLeave ? 1 : 0,
                        (Actual.LeaveTangent - Wanted.LeaveTangent).Size(), bRotation ? 1 : 0,
                        bScale ? 1 : 0, (Actual.Scale - Wanted.LocalScale).Size(), bType ? 1 : 0,
                        static_cast<int32>(Component->GetSplinePointType(PointIndex)),
                        static_cast<int32>(Wanted.PointType.GetValue())));
                    break;
                }
            }
        }
        TestTrue(FString::Printf(TEXT("Contour %d source/native point data exact"), Index), bExact);
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
            // Default Strand SVG reorientation maps (-X, -Y) on XY.
            return FVector(-FCString::Atod(*Cells[Offset]), -FCString::Atod(*Cells[Offset + 1]), 0.0);
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

// Regression for viewport left-to-right reading order, using the original
// Illustrator Digit icon supplied by the author. Its three glyph paths are
// ordered 1, 2, 3 from left to right in the SVG source.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgOrientationTest,
    "Strand.Editor.Svg.DigitIconOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgOrientationTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin found"), Plugin.IsValid())) { return false; }
    const FString Fixture = FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Tests/Fixtures/Strand_Digit_Icon.svg"));
    TArray<FSplineImportedCurve> Mirrored;
    TArray<FSplineImportedCurve> Legacy;
    if (!TestTrue(TEXT("Import Digit SVG using default top-view orientation"),
            SplineAssetImport::LoadSvgCurves(Fixture, Mirrored, true)) ||
        !TestTrue(TEXT("Import Digit SVG using legacy orientation"),
            SplineAssetImport::LoadSvgCurves(Fixture, Legacy, false)) ||
        !TestEqual(TEXT("Digit icon has five independent contours"), Mirrored.Num(), 5) ||
        !TestEqual(TEXT("Legacy orientation contains matching contour count"),
            Legacy.Num(), Mirrored.Num()))
    {
        return false;
    }
    const auto MeanX = [](const FSplineImportedCurve& Curve) -> double
    {
        double X = 0.0;
        for (const FSplinePointData& Pt : Curve.Points) { X += Pt.LocalPosition.X; }
        return Curve.Points.IsEmpty() ? 0.0 : X / Curve.Points.Num();
    };
    // Digit glyphs are paths 2..4; positive SVG X places 1 left of 2 left of 3.
    TestTrue(TEXT("Legacy imports glyphs with increasing X"),
        MeanX(Legacy[2]) < MeanX(Legacy[3]) && MeanX(Legacy[3]) < MeanX(Legacy[4]));
    TestTrue(TEXT("Default native orientation mirrors glyph X"),
        MeanX(Mirrored[2]) > MeanX(Mirrored[3]) && MeanX(Mirrored[3]) > MeanX(Mirrored[4]));
    for (int32 I = 0; I < Mirrored.Num(); ++I)
    {
        if (!TestEqual(FString::Printf(TEXT("Contour %d point count"), I),
                Mirrored[I].Points.Num(), Legacy[I].Points.Num())) { return false; }
        TestEqual(FString::Printf(TEXT("Contour %d closure unchanged"), I),
            Mirrored[I].bClosedLoop, Legacy[I].bClosedLoop);
        for (int32 P = 0; P < Mirrored[I].Points.Num(); ++P)
        {
            const FSplinePointData& A = Mirrored[I].Points[P];
            const FSplinePointData& B = Legacy[I].Points[P];
            TestTrue(TEXT("X mirror preserves local positions with sign reversal"),
                FMath::IsNearlyEqual(A.LocalPosition.X, -B.LocalPosition.X, 0.0001));
            TestTrue(TEXT("X mirror leaves Y/Z unchanged"),
                FMath::IsNearlyEqual(A.LocalPosition.Y, B.LocalPosition.Y, 0.0001) &&
                FMath::IsNearlyEqual(A.LocalPosition.Z, B.LocalPosition.Z, 0.0001));
            TestTrue(TEXT("X mirror reflects incoming and outgoing handles"),
                FMath::IsNearlyEqual(A.ArriveTangent.X, -B.ArriveTangent.X, 0.0001) &&
                FMath::IsNearlyEqual(A.LeaveTangent.X, -B.LeaveTangent.X, 0.0001) &&
                FMath::IsNearlyEqual(A.ArriveTangent.Y, B.ArriveTangent.Y, 0.0001) &&
                FMath::IsNearlyEqual(A.LeaveTangent.Y, B.LeaveTangent.Y, 0.0001));
        }
    }
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
// The five project-level SVG origins must move all subpaths within the same
// source canvas by the same translation, while preserving their geometry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSvgCanvasOriginTest,
    "Strand.Editor.Svg.ProjectCanvasOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSvgCanvasOriginTest::RunTest(const FString& Parameters)
{
    const FString File = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("StrandTests"), TEXT("CanvasOrigin.svg"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!FFileHelper::SaveStringToFile(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 50\"><path d=\"M0 0 L100 50\" /></svg>"), *File))
    { AddError(TEXT("Unable to write temporary SVG origin test fixture.")); return false; }
    UStrandProjectSettings* Settings = GetMutableDefault<UStrandProjectSettings>();
    const EStrandSvgCanvasOrigin Before = Settings->SvgCanvasOrigin;
    struct FCase { EStrandSvgCanvasOrigin Origin; FVector First; FVector Last; };
    const FCase Cases[] = {
        {EStrandSvgCanvasOrigin::TopLeft, FVector(0,0,0), FVector(-100,-50,0)},
        {EStrandSvgCanvasOrigin::TopRight, FVector(100,0,0), FVector(0,-50,0)},
        {EStrandSvgCanvasOrigin::BottomLeft, FVector(0,50,0), FVector(-100,0,0)},
        {EStrandSvgCanvasOrigin::BottomRight, FVector(100,50,0), FVector(0,0,0)},
        {EStrandSvgCanvasOrigin::Center, FVector(50,25,0), FVector(-50,-25,0)}
    };
    for (const FCase& Case : Cases)
    {
        Settings->SvgCanvasOrigin = Case.Origin;
        TArray<FSplineImportedCurve> Curves;
        const bool bLoaded = SplineAssetImport::LoadSvgCurves(File, Curves, true);
        TestTrue(TEXT("SVG load with custom canvas origin"), bLoaded);
        if (bLoaded && Curves.Num() == 1 && Curves[0].Points.Num() == 2)
        {
            TestTrue(TEXT("First point uses source canvas origin"), Curves[0].Points[0].LocalPosition.Equals(Case.First, 0.001));
            TestTrue(TEXT("Second point uses source canvas origin"), Curves[0].Points[1].LocalPosition.Equals(Case.Last, 0.001));
        }
        else { AddError(TEXT("Origin test: expected one two-point curve.")); }
    }
    Settings->SvgCanvasOrigin = Before;
    IFileManager::Get().Delete(*File);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "SplineDataAssetFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandAlembicFactoryRoutingTest,
    "Strand.Editor.Import.AlembicFactoryRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandAlembicFactoryRoutingTest::RunTest(const FString& Parameters)
{
    UStrandAlembicImportRouterFactory* Router = NewObject<UStrandAlembicImportRouterFactory>();
    UStrandAlembicFactory* Direct = NewObject<UStrandAlembicFactory>();
    TestNotNull(TEXT("Interactive Alembic router factory exists"), Router);
    TestNotNull(TEXT("Explicit Strand Alembic factory exists"), Direct);
    if (!Router || !Direct) { return false; }
    // The automation runner can mark factories as non-interactive. Testing
    // FactoryCanImport("Curve.abc") against an unconditional 'true' would then
    // reject the exact safeguard we need for unattended asset imports.
    TestTrue(TEXT("Interactive router recognizes Alembic"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc"), false));
    TestTrue(TEXT("Interactive router recognizes uppercase Alembic extension"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.ABC"), false));
    TestFalse(TEXT("Automated import bypasses interactive Alembic chooser"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc"), true));
    TestFalse(TEXT("Router does not intercept FBX"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.fbx"), false));
    TestFalse(TEXT("Router does not intercept SVG"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.svg"), false));
    TestFalse(TEXT("Router does not intercept another extension after .abc"),
        UStrandAlembicImportRouterFactory::ShouldRouteAlembic(TEXT("Curve.abc.bak"), false));

    const bool bAutomatedImport = Router->IsAutomatedImport();
    AddInfo(FString::Printf(TEXT("Alembic router test environment: %s"),
        bAutomatedImport ? TEXT("automated import") : TEXT("interactive import")));
    TestEqual(TEXT("Live router respects its current import context"),
        Router->FactoryCanImport(TEXT("Curve.abc")), !bAutomatedImport);
    TestFalse(TEXT("Live router rejects FBX"), Router->FactoryCanImport(TEXT("Curve.fbx")));
    TestTrue(TEXT("Direct Strand importer accepts Alembic"), Direct->FactoryCanImport(TEXT("Curve.abc")));
    TestTrue(TEXT("Router has priority over explicit curve factory"), Router->ImportPriority > Direct->ImportPriority);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandDropLogic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandMultiAssetDropEligibilityTest,
    "Strand.Editor.Placement.MultiAssetDropEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandMultiAssetDropEligibilityTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* A = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineDataAsset* B = NewObject<USplineDataAsset>(GetTransientPackage());
    UObject* Unrelated = NewObject<UObject>(GetTransientPackage());

    TArray<UObject*> Assets;
    Assets.Add(A);
    TestFalse(TEXT("One Strand asset cannot trigger a multi-spline dialog"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(A);
    TestFalse(TEXT("Repeated previews of the same asset never trigger grouping"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(B);
    TestTrue(TEXT("A completed drop with two distinct Strand assets can offer grouping"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    Assets.Add(Unrelated);
    TestFalse(TEXT("Mixed types must use normal editor placement"),
        StrandDropLogic::HasMultipleDistinctStrandAssets(Assets));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Interfaces/IPluginManager.h"
#include "SplineImportHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandBlenderNurbsMissingKnotsTest,
    "Strand.Editor.Alembic.BlenderPeriodicNurbsWithoutKnots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandBlenderNurbsMissingKnotsTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
    if (!TestTrue(TEXT("Strand plugin is installed"), Plugin.IsValid())) { return false; }
    const FString Filename = FPaths::Combine(Plugin->GetBaseDir(),
        TEXT("Tests/Fixtures/Alembic/Blender_5.2.3_BezierAndPeriodicNurbs.abc"));
    if (!TestTrue(TEXT("Blender Alembic regression fixture is packaged"), FPaths::FileExists(Filename)))
    { return false; }
    TArray<FSplineImportedCurve> Curves;
    if (!TestTrue(TEXT("Blender Bézier and knotless periodic NURBS both import"),
        SplineAssetImport::LoadAlembicCurves(Filename, nullptr, Curves)))
    { return false; }
    TestEqual(TEXT("Both Blender curve objects are represented"), Curves.Num(), 2);
    bool bFoundBezier = false;
    bool bFoundNurbs = false;
    for (const FSplineImportedCurve& Curve : Curves)
    {
        if (Curve.SourceId.Contains(TEXT("NurbsCircle")))
        {
            bFoundNurbs = true;
            TestTrue(TEXT("Periodic NURBS is closed"), Curve.bClosedLoop);
            TestTrue(TEXT("Periodic NURBS yields editable samples"), Curve.Points.Num() >= 8);
        }
        else
        {
            bFoundBezier = true;
            TestTrue(TEXT("Blender Bézier source has native spline anchors"), Curve.Points.Num() >= 2);
        }
        USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
        Asset->Points = Curve.Points;
        Asset->bIsClosedLoop = Curve.bClosedLoop;
        USplineComponent* Preview = NewObject<USplineComponent>(GetTransientPackage());
        Asset->ApplyToSplineComponent(Preview);
        TestTrue(TEXT("Curve can be evaluated by a native spline component"),
            Preview->GetSplineLength() > 0.0f);
    }
    TestTrue(TEXT("NURBS exists in output"), bFoundNurbs);
    TestTrue(TEXT("Bézier exists in output"), bFoundBezier);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandSplineExport.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplineAuthoringExportTest,
    "Strand.Editor.Export.SvgAndAlembicRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplineAuthoringExportTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector::ZeroVector;
    A.LeaveTangent = FVector(100, 40, 0);
    A.ArriveTangent = FVector(100, 0, 0);
    A.PointType = ESplinePointType::CurveCustomTangent;
    B.LocalPosition = FVector(200, 100, 0);
    B.ArriveTangent = FVector(90, 50, 0);
    B.LeaveTangent = FVector(90, 50, 0);
    B.PointType = ESplinePointType::CurveCustomTangent;
    C.LocalPosition = FVector(300, -20, 0);
    C.ArriveTangent = FVector(40, -120, 0);
    C.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points = {A, B, C};
    TestTrue(TEXT("XY spline recognized as planar"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LocalPosition.Z = 15.0;
    TestFalse(TEXT("3D spline requires explicit SVG projection"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LocalPosition.Z = 0.0;
    Asset->Points[1].LeaveTangent.Z = 25.0;
    TestFalse(TEXT("Off-plane tangent handle prevents silent projection"),
        StrandSplineExport::IsPlanar(Asset, StrandSplineExport::EProjection::XY));
    Asset->Points[1].LeaveTangent.Z = 0.0;
    FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Strand"), TEXT("ExportTests"));
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString SvgPath = FPaths::Combine(Dir, TEXT("StrandSplineTest.svg"));
    const FString AbcPath = FPaths::Combine(Dir, TEXT("StrandSplineTest.abc"));
    FString Error;
    if (!TestTrue(TEXT("SVG export succeeds"),
        StrandSplineExport::WriteSvg(Asset, SvgPath, StrandSplineExport::EProjection::XY, Error)))
    { AddError(Error); return false; }
    FString Xml;
    TestTrue(TEXT("Exported SVG is UTF-8 text"), FFileHelper::LoadFileToString(Xml, *SvgPath));
    TestTrue(TEXT("SVG uses cubic paths"), Xml.Contains(TEXT(" C ")));
    TestTrue(TEXT("SVG uses declared coordinate bounds"), Xml.Contains(TEXT("viewBox=")));
    TArray<FSplineImportedCurve> SvgCurves;
    TestTrue(TEXT("SVG is understood by Strand's importer"), SplineAssetImport::LoadSvgCurves(SvgPath, SvgCurves));
    TestTrue(TEXT("SVG preserves anchors"), SvgCurves.Num() == 1 && SvgCurves[0].Points.Num() == 3);
    Error.Reset();
    if (!TestTrue(TEXT("Alembic export succeeds"), StrandSplineExport::WriteAlembic(Asset, AbcPath, Error)))
    { AddError(Error); return false; }
    TArray<FSplineImportedCurve> AbcCurves;
    TestTrue(TEXT("Alembic can be imported again"), SplineAssetImport::LoadAlembicCurves(AbcPath, nullptr, AbcCurves));
    if (TestTrue(TEXT("Alembic preserves the point count"), AbcCurves.Num() == 1 && AbcCurves[0].Points.Num() == 3))
    {
        TestTrue(TEXT("Alembic preserves 3D local position orientation"),
            AbcCurves[0].Points[1].LocalPosition.Equals(Asset->Points[1].LocalPosition, 0.005));
        TestTrue(TEXT("Alembic preserves cubic leave tangent"),
            AbcCurves[0].Points[0].LeaveTangent.Equals(Asset->Points[0].LeaveTangent, 0.005));
    }
    // Exercise closed-loop Bézier topology (3 anchors -> 9 exported control vertices).
    Asset->bIsClosedLoop = true;
    Error.Reset();
    TestTrue(TEXT("Closed Alembic export succeeds"), StrandSplineExport::WriteAlembic(Asset, AbcPath, Error));
    AbcCurves.Reset();
    TestTrue(TEXT("Closed Alembic re-imports"), SplineAssetImport::LoadAlembicCurves(AbcPath, nullptr, AbcCurves));
    TestTrue(TEXT("Closed topology preserved"),
        AbcCurves.Num() == 1 && AbcCurves[0].bClosedLoop && AbcCurves[0].Points.Num() == 3);
    IFileManager::Get().Delete(*SvgPath);
    IFileManager::Get().Delete(*AbcPath);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "StrandSplineEditOperations.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplineEditingOperationsTest,
    "Strand.Editor.Authoring.SplineEditingOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplineEditingOperationsTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(5.0, -7.0, 15.0);
    B.LocalPosition = FVector(120.0, 75.0, 45.0);
    C.LocalPosition = FVector(270.0, -30.0, -20.0);
    A.LeaveTangent = FVector(110.0, 180.0, 15.0);
    B.ArriveTangent = FVector(150.0, 80.0, 20.0);
    B.LeaveTangent = FVector(100.0, -45.0, 50.0);
    C.ArriveTangent = FVector(160.0, -70.0, -20.0);
    A.PointType = B.PointType = C.PointType = ESplinePointType::CurveCustomTangent;
    Asset->Points = {A, B, C};
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    Asset->ApplyToSplineComponent(Native);
    TArray<FVector> OriginalSamples;
    for (int32 I = 0; I <= 48; ++I)
    {
        OriginalSamples.Add(Native->GetLocationAtSplineInputKey(I * 2.0 / 48.0, ESplineCoordinateSpace::Local));
    }
    TestTrue(TEXT("Subdivide succeeds"), StrandSplineEditOperations::Subdivide(Asset->Points, false, Native));
    TestEqual(TEXT("Subdivide inserts one anchor per segment"), Asset->Points.Num(), 5);
    Asset->ApplyToSplineComponent(Native);
    double LargestDeviation = 0.0;
    for (int32 I = 0; I <= 48; ++I)
    {
        const FVector After = Native->GetLocationAtSplineInputKey(I * 4.0 / 48.0, ESplineCoordinateSpace::Local);
        LargestDeviation = FMath::Max(LargestDeviation, FVector::Distance(OriginalSamples[I], After));
    }
    TestTrue(TEXT("de Casteljau subdivision retains cubic shape to 0.01 cm"), LargestDeviation < 0.01);
    const TArray<FSplinePointData> BeforeReverse = Asset->Points;
    StrandSplineEditOperations::Reverse(Asset->Points, false);
    TestTrue(TEXT("Reverse flips endpoint order"), Asset->Points[0].LocalPosition.Equals(BeforeReverse.Last().LocalPosition));
    TestTrue(TEXT("Reverse swaps and negates arriving tangent"), Asset->Points[0].ArriveTangent.Equals(-BeforeReverse.Last().LeaveTangent));
    StrandSplineEditOperations::Mirror(Asset->Points, 0);
    StrandSplineEditOperations::Mirror(Asset->Points, 0);
    for (int32 I = 0; I < Asset->Points.Num(); ++I)
    {
        TestTrue(TEXT("Double mirror restores point coordinates"), Asset->Points[I].LocalPosition.Equals(BeforeReverse[BeforeReverse.Num() - I - 1].LocalPosition, 0.001));
    }
    StrandSplineEditOperations::Flatten(Asset->Points, 2);
    for (const FSplinePointData& Point : Asset->Points)
    {
        TestTrue(TEXT("Flatten XY zeros depth and both handles"),
            FMath::IsNearlyZero(Point.LocalPosition.Z) && FMath::IsNearlyZero(Point.ArriveTangent.Z) && FMath::IsNearlyZero(Point.LeaveTangent.Z));
    }
    StrandSplineEditOperations::MakeAllLinear(Asset->Points);
    for (const FSplinePointData& Point : Asset->Points) { TestTrue(TEXT("Linear mode"), Point.PointType == ESplinePointType::Linear); }
    StrandSplineEditOperations::MakeAllAuto(Asset->Points);
    for (const FSplinePointData& Point : Asset->Points) { TestTrue(TEXT("Auto mode"), Point.PointType == ESplinePointType::Curve); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSplinePenGeometryTest,
    "Strand.Editor.Authoring.SplinePenGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSplinePenGeometryTest::RunTest(const FString& Parameters)
{
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(GetTransientPackage());
    USplineComponent* Native = NewObject<USplineComponent>(GetTransientPackage());
    FSplinePointData A, B, C;
    A.LocalPosition = FVector(0, 10, 30);
    B.LocalPosition = FVector(120, 40, -20);
    C.LocalPosition = FVector(250, -20, 90);
    A.PointType = B.PointType = C.PointType = ESplinePointType::CurveCustomTangent;
    A.LeaveTangent = FVector(100, 160, 30);
    B.ArriveTangent = FVector(60, 90, 20);
    B.LeaveTangent = FVector(75, -80, 10);
    C.ArriveTangent = FVector(120, -70, 45);
    Asset->Points = { A, B, C };
    Asset->ApplyToSplineComponent(Native);
    // Inserting at a non-midpoint parameter must not reshape either segment.
    TArray<FVector> Before;
    for (int32 I = 0; I <= 80; ++I)
    {
        Before.Add(Native->GetLocationAtSplineInputKey(I / 40.0, ESplineCoordinateSpace::Local));
    }
    TestEqual(TEXT("Pen split index"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, 0.25), 1);
    TestEqual(TEXT("Pen split adds one point"), Asset->Points.Num(), 4);
    Asset->ApplyToSplineComponent(Native);
    double MaxDeviation = 0.0;
    for (int32 I = 0; I <= 80; ++I)
    {
        const double OldKey = I / 40.0;
        const double NewKey = OldKey <= 0.25 ? OldKey / 0.25
            : OldKey <= 1.0 ? 1.0 + (OldKey - 0.25) / 0.75 : OldKey + 1.0;
        MaxDeviation = FMath::Max(MaxDeviation, FVector::Distance(Before[I],
            Native->GetLocationAtSplineInputKey(NewKey, ESplineCoordinateSpace::Local)));
    }
    TestTrue(TEXT("Quarter-segment split preserves cubic to 0.01 cm"), MaxDeviation < 0.01);
    const int32 CountBeforeInvalid = Asset->Points.Num();
    TestEqual(TEXT("Cannot split open final endpoint"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, CountBeforeInvalid - 1, 0.5), INDEX_NONE);
    TestEqual(TEXT("Cannot split t=0 endpoint"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, false, Native, 0, 0.0), INDEX_NONE);
    TestEqual(TEXT("Invalid inserts never mutate"), Asset->Points.Num(), CountBeforeInvalid);
    TestEqual(TEXT("Pen prepend index"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(-50, 0, 0), true, false), 0);
    TestTrue(TEXT("Prepended anchor stays first"), Asset->Points[0].LocalPosition.Equals(FVector(-50, 0, 0)));
    TestEqual(TEXT("Pen append index"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(350, 0, 0), false, false), 5);
    TestTrue(TEXT("Pen handle edit applies"), StrandSplineEditOperations::SetPenHandles(
        Asset->Points, 5, FVector(10, 20, -5)));
    TestTrue(TEXT("Outgoing handle is 3x drag"), Asset->Points[5].LeaveTangent.Equals(FVector(30, 60, -15)));
    TestTrue(TEXT("Incoming handle mirrors outgoing"), Asset->Points[5].ArriveTangent.Equals(FVector(30, 60, -15)));
    TestEqual(TEXT("Closed splines cannot be extended"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(400, 0, 0), false, true), INDEX_NONE);
    TestEqual(TEXT("Count unchanged after closed extension"), Asset->Points.Num(), 6);
    // The last segment of a closed spline wraps back to point zero. Inserting
    // on this span must preserve closure and the two cubic subsegments.
    Asset->Points = { A, B, C };
    Asset->bIsClosedLoop = true;
    Asset->ApplyToSplineComponent(Native);
    const FVector ClosedBeforeA = Native->GetLocationAtSplineInputKey(2.73, ESplineCoordinateSpace::Local);
    const FVector ClosedBeforeB = Native->GetLocationAtSplineInputKey(2.90, ESplineCoordinateSpace::Local);
    TestEqual(TEXT("Closed wrap insert appends before closure"), StrandSplineEditOperations::InsertOnSegment(
        Asset->Points, true, Native, 2, 0.73), 3);
    Asset->ApplyToSplineComponent(Native);
    TestTrue(TEXT("Closed loop remains closed"), Native->IsClosedLoop());
    TestTrue(TEXT("Closed wrap insertion point stays on original curve"),
        ClosedBeforeA.Equals(Native->GetLocationAtSplineInputKey(3.0, ESplineCoordinateSpace::Local), 0.01));
    TestTrue(TEXT("Closed wrap second half remains on original curve"),
        ClosedBeforeB.Equals(Native->GetLocationAtSplineInputKey(3.0 + (0.90 - 0.73) / 0.27,
            ESplineCoordinateSpace::Local), 0.01));
    // Empty assets are valid while drawing a new path from scratch.
    Asset->Points.Empty();
    TestEqual(TEXT("First anchor in empty path"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(4, 5, 6), false, false), 0);
    TestEqual(TEXT("Second anchor in empty path"), StrandSplineEditOperations::AppendPenPoint(
        Asset->Points, FVector(44, 5, 6), false, false), 1);
    return true;
}
#endif
