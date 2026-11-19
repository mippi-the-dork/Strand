#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "StrandSurfaceProjectionMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandSurfaceProjectionPlanMathTest,
    "Strand.Editor.SurfaceProjection.PreviewStrengthAndRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStrandSurfaceProjectionPlanMathTest::RunTest(const FString& Parameters)
{
    const FTransform Transform(FRotator(25.0, 35.0, -15.0), FVector(700.0, -240.0, 85.0), FVector(2.0, 1.5, 0.5));
    const FVector StartWorld(90.0, -30.0, 270.0);
    const FVector HitWorld(140.0, 50.0, 10.0);

    const FVector AtZero = StrandSurfaceProjectionMath::BlendWorldHitToLocal(Transform, StartWorld, HitWorld, 0.0);
    const FVector AtHalf = StrandSurfaceProjectionMath::BlendWorldHitToLocal(Transform, StartWorld, HitWorld, 0.5);
    const FVector AtFull = StrandSurfaceProjectionMath::BlendWorldHitToLocal(Transform, StartWorld, HitWorld, 1.0);
    TestTrue(TEXT("Zero strength preserves world point"), Transform.TransformPosition(AtZero).Equals(StartWorld, 0.001));
    TestTrue(TEXT("Half strength interpolates in world space"), Transform.TransformPosition(AtHalf).Equals((StartWorld + HitWorld) * 0.5, 0.001));
    TestTrue(TEXT("Full strength reaches world hit"), Transform.TransformPosition(AtFull).Equals(HitWorld, 0.001));
    TestTrue(TEXT("Strength is clamped"), Transform.TransformPosition(
        StrandSurfaceProjectionMath::BlendWorldHitToLocal(Transform, StartWorld, HitWorld, 2.0)).Equals(HitWorld, 0.001));

    TestFalse(TEXT("Range excludes lower anchor"), StrandSurfaceProjectionMath::IsAnchorEligible(0, 6, 2, 4));
    TestTrue(TEXT("Range includes first"), StrandSurfaceProjectionMath::IsAnchorEligible(2, 6, 2, 4));
    TestTrue(TEXT("Range includes last"), StrandSurfaceProjectionMath::IsAnchorEligible(4, 6, 2, 4));
    TestFalse(TEXT("Range excludes higher anchor"), StrandSurfaceProjectionMath::IsAnchorEligible(5, 6, 2, 4));
    TestTrue(TEXT("Negative last means all remaining"), StrandSurfaceProjectionMath::IsAnchorEligible(5, 6, 2, -1));
    TestFalse(TEXT("Out of bounds is excluded"), StrandSurfaceProjectionMath::IsAnchorEligible(6, 6, 0, -1));
    TestFalse(TEXT("Reversed range contains no anchors"), StrandSurfaceProjectionMath::IsAnchorEligible(3, 6, 5, 2));

    const FVector Normal = FVector(0.3, 0.4, 0.8660254).GetSafeNormal();
    const FVector Local = StrandSurfaceProjectionMath::WorldNormalToLocal(Transform, Normal);
    TestTrue(TEXT("Normal frame stays unit length"), FMath::IsNearlyEqual(Local.Size(), 1.0, 1.e-5));
    TestTrue(TEXT("Normal orientation survives frame conversion"),
        Transform.TransformVectorNoScale(Local).GetSafeNormal().Equals(Normal, 1.e-5));
    return true;
}
// Mathematical distinction between screen-perspective and camera-parallel rays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandProjectionCameraRaysTest,
    "Strand.Editor.SurfaceProjection.CameraAndParallelRays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandProjectionCameraRaysTest::RunTest(const FString& Parameters)
{
    const FVector Camera(-200, 0, 100);
    const FVector A(0, -75, 100);
    const FVector B(0, 75, 100);
    FVector StartA, EndA, StartB, EndB;
    const FVector Forward = FVector::ForwardVector;
    TestTrue(TEXT("Parallel ray A"), StrandSurfaceProjectionMath::MakeProjectionRay(A, Camera, Forward, false, 1000, StartA, EndA));
    TestTrue(TEXT("Parallel ray B"), StrandSurfaceProjectionMath::MakeProjectionRay(B, Camera, Forward, false, 1000, StartB, EndB));
    TestTrue(TEXT("Parallel starts at anchors"), StartA.Equals(A) && StartB.Equals(B));
    TestTrue(TEXT("Parallel has same direction"), (EndA - StartA).Equals(EndB - StartB));
    TestTrue(TEXT("Perspective ray A"), StrandSurfaceProjectionMath::MakeProjectionRay(A, Camera, Forward, true, 1000, StartA, EndA));
    TestTrue(TEXT("Perspective ray B"), StrandSurfaceProjectionMath::MakeProjectionRay(B, Camera, Forward, true, 1000, StartB, EndB));
    TestTrue(TEXT("Perspective starts from camera"), StartA.Equals(Camera) && StartB.Equals(Camera));
    TestFalse(TEXT("Perspective rays diverge per anchor"), (EndA - StartA).Equals(EndB - StartB));
    TestTrue(TEXT("Perspective A passes through anchor"), FVector::CrossProduct(EndA - StartA, A - Camera).IsNearlyZero(0.01));
    TestFalse(TEXT("Reject coincident perspective camera and anchor"), StrandSurfaceProjectionMath::MakeProjectionRay(Camera, Camera, Forward, true, 1000, StartA, EndA));
    TestFalse(TEXT("Reject zero length trace"), StrandSurfaceProjectionMath::MakeProjectionRay(A, Camera, Forward, false, 0, StartA, EndA));
    return true;
}
#endif
