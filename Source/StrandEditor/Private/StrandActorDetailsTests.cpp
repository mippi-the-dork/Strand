#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "UObject/UnrealType.h"

// The actor Details layout moves the existing UPROPERTY handles into the
// Strand category, without introducing duplicate serialized settings.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandActorDetailsPropertyContractTest,
    "Strand.Editor.ActorDetails.NativePropertyContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandActorDetailsPropertyContractTest::RunTest(const FString& Parameters)
{
    const UClass* Class = AStrandSplineActor::StaticClass();
    const FName Names[] = {
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, bShowStrandBillboard),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandBillboardSize),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTargetScope),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTargets),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionRayMode),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTarget),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionDirection),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionMaxDistance),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionClearance),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionStrength),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionFirstAnchor),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionLastAnchor),
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, bStrandProjectionAlignNormals)
    };
    for (const FName& Name : Names)
    {
        const FProperty* Property = Class->FindPropertyByName(Name);
        TestNotNull(*FString::Printf(TEXT("Strand Details still exposes native property %s"), *Name.ToString()), Property);
    }
    const FProperty* Direction = Class->FindPropertyByName(
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionDirection));
    TestNotNull(TEXT("Projection direction is a native vector"), CastField<FStructProperty>(Direction));
    if (Direction)
    {
        TestTrue(TEXT("World Direction retains conditional metadata"),
            Direction->GetMetaData(TEXT("EditCondition")).Contains(TEXT("WorldDirection")));
    }
    return true;
}

// A multi-spline actor's Details groups must keep distinct component ownership.
// Editing one linked asset may not silently remap its neighboring spline.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrandActorDetailsPerSplineIsolationTest,
    "Strand.Editor.ActorDetails.MultiSplineLinkIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrandActorDetailsPerSplineIsolationTest::RunTest(const FString& Parameters)
{
    AStrandMultiSplineActor* Actor = NewObject<AStrandMultiSplineActor>(GetTransientPackage());
    if (!TestNotNull(TEXT("Multi-spline actor exists"), Actor)) { return false; }
    Actor->AddSplineComponent();
    Actor->AddSplineComponent();
    TArray<USplineComponent*> Components;
    Actor->GetComponents<USplineComponent>(Components);
    if (!TestEqual(TEXT("Three separate native splines"), Components.Num(), 3)) { return false; }
    USplineDataAsset* Assets[3] = {
        NewObject<USplineDataAsset>(GetTransientPackage()),
        NewObject<USplineDataAsset>(GetTransientPackage()),
        NewObject<USplineDataAsset>(GetTransientPackage())
    };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        UStrandSplineUserData::AssignAsset(Components[Index], Assets[Index]);
    }
    for (int32 Index = 0; Index < 3; ++Index)
    {
        TestTrue(*FString::Printf(TEXT("Component %d retains its own linked asset"), Index),
            UStrandSplineUserData::GetAssignedAsset(Components[Index]) == Assets[Index]);
    }
    UStrandSplineUserData::AssignAsset(Components[1], Assets[0]);
    TestTrue(TEXT("Reassigning middle spline does not affect root"),
        UStrandSplineUserData::GetAssignedAsset(Components[0]) == Assets[0]);
    TestTrue(TEXT("Reassigning middle spline does not affect last spline"),
        UStrandSplineUserData::GetAssignedAsset(Components[2]) == Assets[2]);
    return true;
}
#endif
