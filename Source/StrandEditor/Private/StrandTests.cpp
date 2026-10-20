#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

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
    return true;
}
#endif
