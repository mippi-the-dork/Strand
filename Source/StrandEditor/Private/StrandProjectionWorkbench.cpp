#include "StrandProjectionWorkbench.h"
#include "SplineEditorCommands.h"
#include "StrandSplineActor.h"
#include "StrandProjectionPlacement.h"
#include "StrandSurfaceTargetTrace.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Text/STextBlock.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "StrandProjectionWorkbench"

namespace
{
    struct FWorkbenchState
    {
        TWeakObjectPtr<USplineComponent> Source;
        FStrandProjectionOptions Options;
        TFunction<bool(USplineComponent*, const TArray<int32>&, bool)> OnApplied;
        TWeakPtr<SWindow> Window;
        double PlacementDistanceCm = 1250.0;
        bool bOrientToSource = false;
    };

    bool ValidTarget(AActor* Candidate, USplineComponent* Source)
    {
        return IsValid(Candidate) && IsValid(Source) && Candidate != Source->GetOwner()
            && Candidate->GetWorld() == Source->GetWorld();
    }

    TArray<AActor*> GetSelectedTargets(USplineComponent* Source)
    {
        TArray<AActor*> Result;
        for (AActor* Candidate : SplineEditorCommands::SelectedActors())
        {
            if (ValidTarget(Candidate, Source)) { Result.AddUnique(Candidate); }
        }
        return Result;
    }

    FVector GetLocalAnchorCenter(const USplineComponent* Source)
    {
        if (!IsValid(Source) || Source->GetNumberOfSplinePoints() == 0) { return FVector::ZeroVector; }
        FVector Center = FVector::ZeroVector;
        for (int32 Index = 0; Index < Source->GetNumberOfSplinePoints(); ++Index)
        { Center += Source->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local); }
        return Center / Source->GetNumberOfSplinePoints();
    }

    FString TargetSummary(const FWorkbenchState& State)
    {
        switch (State.Options.TargetScope)
        {
        case EStrandProjectionTargetScope::EntireLevel: return TEXT("All query-enabled scene collision; source actor excluded");
        case EStrandProjectionTargetScope::SingleActor:
            return State.Options.Target.IsValid() ? State.Options.Target->GetActorLabel() : TEXT("No target selected");
        case EStrandProjectionTargetScope::ActorSet:
            return FString::Printf(TEXT("%d selected target actor(s)"), State.Options.Targets.Num());
        default: return TEXT("Unknown target scope");
        }
    }

    FText FieldHelp(const FString& Name)
    {
        if (Name.Contains(TEXT("Distance"))) { return FText::FromString(TEXT("Maximum collision trace distance, in centimeters.")); }
        if (Name.Contains(TEXT("Strength"))) { return FText::FromString(TEXT("Blend from the original point (0) to the projected point (1).")); }
        if (Name.Contains(TEXT("Clearance"))) { return FText::FromString(TEXT("Offset the projected location away from the hit surface, in centimeters.")); }
        if (Name.Contains(TEXT("First Anchor"))) { return FText::FromString(TEXT("First anchor index eligible for projection, starting at zero.")); }
        if (Name.Contains(TEXT("Last Anchor"))) { return FText::FromString(TEXT("Final eligible anchor index; -1 includes the remaining anchors.")); }
        if (Name.Contains(TEXT("Spacing"))) { return FText::FromString(TEXT("Maximum desired distance between surface samples in centimeters; limited by the point budget.")); }
        if (Name.Contains(TEXT("Tolerance"))) { return FText::FromString(TEXT("Desired maximum surface deviation in centimeters. Lower values favor more detail.")); }
        if (Name.Contains(TEXT("Points"))) { return FText::FromString(TEXT("Maximum additional anchors generated. Projection uses the budget for best-effort accuracy.")); }
        if (Name.Contains(TEXT("Direction"))) { return FText::FromString(TEXT("World-space projection direction on this axis; used only in World Direction ray mode.")); }
        return FText::FromString(Name);
    }

    template<typename ValueType>
    TSharedPtr<INumericTypeInterface<ValueType>> FixedUnitInterface(EUnit Unit)
    {
        TSharedPtr<TNumericUnitTypeInterface<ValueType>> TypeInterface = MakeShared<TNumericUnitTypeInterface<ValueType>>(Unit);
        TypeInterface->FixedDisplayUnits = Unit;
        return TypeInterface;
    }

    template<typename ValueType>
    TSharedRef<SWidget> NumberRow(const FText& Label, const TFunction<ValueType(const FStrandProjectionOptions&)>& Get,
        const TFunction<void(FStrandProjectionOptions&, ValueType)>& Set, const TSharedRef<FWorkbenchState>& State)
    {
        const FString LabelName = Label.ToString();
        const bool bCentimeters = LabelName.Contains(TEXT("Distance")) || LabelName.Contains(TEXT("Clearance"))
            || LabelName.Contains(TEXT("Spacing")) || LabelName.Contains(TEXT("Tolerance"));
        TSharedPtr<INumericTypeInterface<ValueType>> TypeInterface = bCentimeters
            ? FixedUnitInterface<ValueType>(EUnit::Centimeters)
            : MakeShared<TDefaultNumericTypeInterface<ValueType>>();
        return SNew(SBox).HeightOverride(24)
          [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(Label).ToolTipText(FieldHelp(Label.ToString()))]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SNumericEntryBox<ValueType>)
                .AllowSpin(true)
                .ToolTipText(FieldHelp(Label.ToString()))
                .TypeInterface(TypeInterface)
                .Value_Lambda([State, Get]() -> TOptional<ValueType> { return Get(State->Options); })
                .OnValueCommitted_Lambda([State, Set](ValueType Value, ETextCommit::Type) { Set(State->Options, Value); })]];
    }
}

bool StrandProjectionWorkbench::UsesWorldDirection(const FStrandProjectionOptions& Options)
{
    return Options.RayMode == EStrandProjectionRayMode::WorldDirection;
}

bool StrandProjectionWorkbench::UsesContinuousSettings(const FStrandProjectionOptions& Options)
{
    return Options.bContinuousConform;
}

bool StrandProjectionWorkbench::ValidateOptions(const FStrandProjectionOptions& Options, int32 AnchorCount, FString& OutReason)
{
    OutReason.Reset();
    if (AnchorCount <= 0)
    {
        OutReason = TEXT("The level spline has no anchors to project.");
    }
    else if (!FMath::IsFinite(Options.MaxDistance) || Options.MaxDistance <= 0.0f)
    {
        OutReason = TEXT("Maximum trace distance must be greater than zero.");
    }
    else if (!FMath::IsFinite(Options.Strength) || Options.Strength < 0.0f || Options.Strength > 1.0f)
    {
        OutReason = TEXT("Projection strength must be between 0 and 1.");
    }
    else if (!FMath::IsFinite(Options.Clearance))
    {
        OutReason = TEXT("Surface clearance must be finite.");
    }
    else if (Options.FirstAnchor < 0 || Options.FirstAnchor >= AnchorCount ||
        (Options.LastAnchor >= 0 && (Options.LastAnchor >= AnchorCount || Options.LastAnchor < Options.FirstAnchor)))
    {
        OutReason = TEXT("The requested anchor range is outside the selected spline.");
    }
    else if (Options.bUseAnchorWeights && (Options.AnchorWeights.Num() != AnchorCount ||
        !Options.AnchorWeights.ContainsByPredicate([](double V) { return FMath::IsFinite(V) && V > 0.0; })))
    {
        OutReason = TEXT("The saved anchor-selection snapshot is empty or does not match the source spline.");
    }
    else if (UsesWorldDirection(Options) &&
        (Options.WorldDirection.ContainsNaN() || Options.WorldDirection.IsNearlyZero()))
    {
        OutReason = TEXT("World projection requires a nonzero, finite direction.");
    }
    else if (UsesContinuousSettings(Options) &&
        (!FMath::IsFinite(Options.ConformMaxSpacing) || Options.ConformMaxSpacing <= 0.0f ||
         !FMath::IsFinite(Options.ConformTolerance) || Options.ConformTolerance <= 0.0f ||
         Options.ConformMaxNewPoints < 1 || Options.ConformMaxNewPoints > 8192))
    {
        OutReason = TEXT("Continuous conform requires positive spacing and tolerance, with a new-point budget from 1 to 8192.");
    }
    else if (Options.TargetScope == EStrandProjectionTargetScope::SingleActor && !Options.Target.IsValid())
    {
        OutReason = TEXT("Choose a target actor using the Level Editor selection, or switch to All Scene Collision.");
    }
    else if (Options.TargetScope == EStrandProjectionTargetScope::ActorSet && Options.Targets.IsEmpty())
    {
        OutReason = TEXT("Choose at least one target actor using the Level Editor selection, or switch to All Scene Collision.");
    }
    return OutReason.IsEmpty();
}

TSharedPtr<SWindow> StrandProjectionWorkbench::Open(USplineComponent* Source, const FStrandProjectionOptions& InitialOptions,
    TFunction<bool(USplineComponent*, const TArray<int32>&, bool)> OnApplied, TFunction<void()> OnClosed)
{
    if (!IsValid(Source) || !IsValid(Source->GetOwner()) || !IsValid(Source->GetWorld())) { return nullptr; }
    TSharedRef<FWorkbenchState> State = MakeShared<FWorkbenchState>();
    State->Source = Source;
    State->Options = InitialOptions;
    State->OnApplied = MoveTemp(OnApplied);

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("Title", "Strand Projection"))
        .IsTopmostWindow(true)
        .ClientSize(FVector2D(960, 600))
        .SupportsMaximize(false).SupportsMinimize(false);

    State->Window = Window;
    const TSharedRef<TFunction<void()>> OnClosedHolder = MakeShared<TFunction<void()>>(MoveTemp(OnClosed));
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([OnClosedHolder](const TSharedRef<SWindow>&)
    {
        if (*OnClosedHolder) { (*OnClosedHolder)(); }
    }));

    auto TargetMenu = [State]() -> TSharedRef<SWidget>
    {
        FMenuBuilder Menu(true, nullptr);
        auto Add = [&Menu, State](EStrandProjectionTargetScope Scope, const FText& Name)
        {
            Menu.AddMenuEntry(Name,
                FText::FromString(TEXT("Choose collision scope or projection mode for the temporary spline.")), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([State, Scope]() { State->Options.TargetScope = Scope; })));
        };
        Add(EStrandProjectionTargetScope::EntireLevel, LOCTEXT("EntireLevel", "All Scene Collision"));
        Add(EStrandProjectionTargetScope::SingleActor, LOCTEXT("SingleActor", "Single Actor"));
        Add(EStrandProjectionTargetScope::ActorSet, LOCTEXT("ActorSet", "Actor Set"));
        return Menu.MakeWidget();
    };
    auto RayMenu = [State]() -> TSharedRef<SWidget>
    {
        FMenuBuilder Menu(true, nullptr);
        auto Add = [&Menu, State](EStrandProjectionRayMode Mode, const FText& Name)
        {
            Menu.AddMenuEntry(Name,
                FText::FromString(TEXT("Choose collision scope or projection mode for the temporary spline.")), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([State, Mode]() { State->Options.RayMode = Mode; })));
        };
        Add(EStrandProjectionRayMode::WorldDirection, LOCTEXT("WorldDirection", "World Direction"));
        Add(EStrandProjectionRayMode::CameraDirection, LOCTEXT("CameraDirection", "Camera Direction (Parallel)"));
        Add(EStrandProjectionRayMode::CameraProjection, LOCTEXT("CameraProjection", "Camera Projection (Perspective)"));
        return Menu.MakeWidget();
    };

    auto ReturnMenu = [State]() -> TSharedRef<SWidget>
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("ReturnRelative", "Relative (Local)"),
            LOCTEXT("ReturnRelativeTip", "Keep spline geometry local to its asset and discard the temporary placement."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([State]()
            { State->Options.bBakeWorldPlacementToAsset = false; })));
        Menu.AddMenuEntry(LOCTEXT("ReturnAbsolute", "Absolute (World)"),
            LOCTEXT("ReturnAbsoluteTip", "Bake world-space positions, tangents, rotations and scale into asset points."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([State]()
            { State->Options.bBakeWorldPlacementToAsset = true; })));
        return Menu.MakeWidget();
    };

    // Narrow, non-interactive axis strip matches the native transform-field visual language.
    auto AxisStrip = [](int32 Axis) -> TSharedRef<SWidget>
    {
        const FLinearColor AxisColor = Axis == 0 ? FLinearColor::FromSRGBColor(FColor(0xCB, 0x26, 0x00))
            : (Axis == 1 ? FLinearColor::FromSRGBColor(FColor(0x67, 0xA9, 0x03))
                         : FLinearColor::FromSRGBColor(FColor(0x2B, 0x7E, 0xEC)));
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor(AxisColor).Padding(0)
            [SNew(SBox).WidthOverride(4).HeightOverride(17)];
    };
    auto AxisField = [AxisStrip](int32 Axis, TSharedRef<SWidget> Input) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[AxisStrip(Axis)]
            + SHorizontalBox::Slot().FillWidth(1)[Input];
    };

    auto TransformAxis = [State, AxisField](int32 Axis, bool bRotation) -> TSharedRef<SWidget>
    {
        const FText Tip = bRotation
            ? LOCTEXT("TransformRotTip", "Temporary actor rotation (degrees). X = Roll, Y = Pitch, Z = Yaw. Changes affect preview placement only.")
            : LOCTEXT("TransformLocTip", "Temporary actor world location (cm). Changes affect preview placement only.");
        return AxisField(Axis, SNew(SNumericEntryBox<double>)
            .ToolTipText(Tip)
            .TypeInterface(FixedUnitInterface<double>(bRotation ? EUnit::Degrees : EUnit::Centimeters))
            .AllowSpin(true)
            .Value_Lambda([State, Axis, bRotation]() -> TOptional<double>
            {
                const USplineComponent* Source = State->Source.Get();
                if (!IsValid(Source) || !IsValid(Source->GetOwner())) { return TOptional<double>(); }
                const AActor* Actor = Source->GetOwner();
                if (!bRotation) { return Actor->GetActorLocation()[Axis]; }
                const FRotator R = Actor->GetActorRotation();
                return Axis == 0 ? R.Roll : (Axis == 1 ? R.Pitch : R.Yaw);
            })
            .OnValueCommitted_Lambda([State, Axis, bRotation](double Value, ETextCommit::Type)
            {
                USplineComponent* Source = State->Source.Get();
                if (!IsValid(Source) || !IsValid(Source->GetOwner()) || !FMath::IsFinite(Value)) { return; }
                AActor* Actor = Source->GetOwner();
                if (bRotation)
                {
                    FRotator R = Actor->GetActorRotation();
                    if (Axis == 0) { R.Roll = Value; }
                    else if (Axis == 1) { R.Pitch = Value; }
                    else { R.Yaw = Value; }
                    Actor->SetActorRotation(R);
                }
                else
                {
                    FVector P = Actor->GetActorLocation();
                    P[Axis] = Value;
                    Actor->SetActorLocation(P);
                }
            }));
    };
    auto TransformRow = [TransformAxis](const FText& Label, bool bRotation) -> TSharedRef<SWidget>
    {
        return SNew(SBox).HeightOverride(24)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.95f).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(Label)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
            [TransformAxis(0, bRotation)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
            [TransformAxis(1, bRotation)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
            [TransformAxis(2, bRotation)]];
    };

    // Projection settings are scoped to this temporary session; changing the
    // panel never edits a level actor or the backing Strand asset directly.
    const TSharedRef<SWidget> PlacementContent = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(LOCTEXT("CameraDistance", "Distance from"))
                    .ToolTipText(LOCTEXT("CameraDistanceHelp", "Distance from the camera when using Camera Placement, or from the selected target surface when using Surface Placement."))]
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SNumericEntryBox<double>)
                    .ToolTipText(LOCTEXT("CameraDistanceInputHelp", "Camera: distance forward from the camera. Surface: offset outward from the selected target hit surface, in centimeters."))
                    .AllowSpin(true).TypeInterface(FixedUnitInterface<double>(EUnit::Centimeters)).MinValue(1.0).MinSliderValue(1.0).MaxSliderValue(10000.0)
                    .Value_Lambda([State]() -> TOptional<double> { return State->PlacementDistanceCm; })
                    .OnValueChanged_Lambda([State](double V) { if (FMath::IsFinite(V)) { State->PlacementDistanceCm = FMath::Max(1.0, V); } })
                    .OnValueCommitted_Lambda([State](double V, ETextCommit::Type) { if (FMath::IsFinite(V)) { State->PlacementDistanceCm = FMath::Max(1.0, V); } })]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(LOCTEXT("OrientTo", "Orient to"))]
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SCheckBox)
                    .ToolTipText(LOCTEXT("MatchCameraRotationTip", "Camera Placement: match camera orientation. Surface Placement: align the spline up axis to the target surface normal."))
                    .IsChecked_Lambda([State]() { return State->bOrientToSource ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->bOrientToSource = V == ECheckBoxState::Checked; })]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)
                [SNew(SButton).Text(LOCTEXT("PlaceCamera", "Camera Placement"))
                .ToolTipText(LOCTEXT("PlaceCameraTip", "Place at the configured distance in front of the Level Editor camera. Orient to optionally matches its rotation. Only Projection Apply edits the asset."))
                .OnClicked_Lambda([State]()
                {
                    USplineComponent* Source = State->Source.Get();
                    if (!IsValid(Source) || !IsValid(Source->GetOwner()) || !GEditor) { return FReply::Handled(); }
                    if (!StrandProjectionPlacement::ValidCameraDistance(State->PlacementDistanceCm))
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidCameraDistance", "Placement distance must be a positive, finite value."));
                        return FReply::Handled();
                    }
                    bool bPlaced = false;
                    for (const FLevelEditorViewportClient* Viewport : GEditor->GetLevelViewportClients())
                    {
                        if (Viewport && Viewport->Viewport && Viewport->IsPerspective() && Viewport->GetWorld() == Source->GetWorld())
                        {
                            AActor* Owner = Source->GetOwner();
                            const FQuat Orientation = State->bOrientToSource
                                ? Viewport->GetViewRotation().Quaternion() : Owner->GetActorQuat();
                            const FVector Center = GetLocalAnchorCenter(Source);
                            const FTransform Placement = StrandProjectionPlacement::CameraPlacement(
                                Viewport->GetViewLocation(), Viewport->GetViewRotation(),
                                State->PlacementDistanceCm, Center, Orientation, Owner->GetActorScale3D());
                            Owner->SetActorLocationAndRotation(Placement.GetLocation(), Placement.GetRotation());
                            bPlaced = true;
                            break;
                        }
                    }
                    if (!bPlaced)
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoPerspectiveViewport", "Open a perspective Level Editor viewport in the active world before placing at the camera."));
                    }
                    return FReply::Handled();
                })]
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SButton).Text(LOCTEXT("PlaceSurface", "Surface Placement"))
                .ToolTipText(LOCTEXT("PlaceSurfaceTip", "Trace the first selected target surface, offset by Distance from (cm), and optionally orient the spline to its hit normal. Select Spline enables the native transform gizmo."))
                .OnClicked_Lambda([State]()
                {
                    USplineComponent* Source = State->Source.Get();
                    if (!IsValid(Source) || !IsValid(Source->GetOwner())) { return FReply::Handled(); }
                    const TArray<AActor*> Targets = GetSelectedTargets(Source);
                    if (Targets.IsEmpty()) { return FReply::Handled(); }
                    const FBox Bounds = Targets[0]->GetComponentsBoundingBox(true);
                    if (!Bounds.IsValid) { return FReply::Handled(); }
                    if (!StrandProjectionPlacement::ValidCameraDistance(State->PlacementDistanceCm))
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidSurfaceDistance", "Placement distance must be a positive, finite value."));
                        return FReply::Handled();
                    }
                    // The first selected actor supplies the surface. Trace from above its bounds
                    // to obtain an actual collision hit and normal, not merely the bounding-box top.
                    const FVector Start = Bounds.GetCenter() + FVector::UpVector * (Bounds.GetExtent().Size() + 100.0);
                    const FVector End = Bounds.GetCenter() - FVector::UpVector * (Bounds.GetExtent().Size() + 100.0);
                    FHitResult Hit;
                    // Match Strand's established target query contract. ActorLineTraceSingle
                    // can miss otherwise valid scaled StaticMeshActors, so query scene object
                    // collision and then fall back to the selected actor's primitive components.
                    // Only a hit owned by the explicitly selected actor is accepted.
                    if (!StrandSurfaceTargetTrace::FindTargetHit(Source->GetWorld(), Source->GetOwner(),
                        Targets[0], Start, End, Hit))
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("SurfacePlacementNoHit", "No query-enabled collision surface was found on the selected actor along its vertical center. Check collision, choose another surface, or use Camera Placement."));
                        return FReply::Handled();
                    }
                    AActor* Owner = Source->GetOwner();
                    FQuat Orientation = Owner->GetActorQuat();
                    if (State->bOrientToSource)
                    {
                        const FVector CurrentUp = Orientation.GetUpVector();
                        const FVector SurfaceUp = Hit.ImpactNormal.GetSafeNormal();
                        if (!SurfaceUp.IsNearlyZero())
                        {
                            Orientation = FQuat::FindBetweenNormals(CurrentUp, SurfaceUp) * Orientation;
                        }
                    }
                    const FVector DesiredCenter = Hit.ImpactPoint + Hit.ImpactNormal.GetSafeNormal() * State->PlacementDistanceCm;
                    const FVector Center = GetLocalAnchorCenter(Source);
                    const FVector ScaledCenter = Center * Owner->GetActorScale3D();
                    Owner->SetActorLocationAndRotation(DesiredCenter - Orientation.RotateVector(ScaledCenter), Orientation);
                    return FReply::Handled();
                })]]]
        + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [TransformRow(LOCTEXT("TemporaryLocation", "Temp Location"), false)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [TransformRow(LOCTEXT("TemporaryRotation", "Temp Rotation"), true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SButton)
            .Text(LOCTEXT("SelectTemporary", "Select Spline"))
            .ToolTipText(LOCTEXT("SelectTemporaryTip", "Select the temporary spline in the Level Editor. Move, rotate or scale it using Unreal's native transform widget. Re-select target actors to capture targets or place above a surface."))
            .OnClicked_Lambda([State]()
            {
                USplineComponent* Source = State->Source.Get();
                if (IsValid(Source) && IsValid(Source->GetOwner()) && GEditor)
                {
                    GEditor->SelectNone(false, true, false);
                    GEditor->SelectActor(Source->GetOwner(), true, true, true);
                }
                return FReply::Handled();
            })]
];

    const TSharedRef<SWidget> TargetsContent = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
        [SNew(SComboButton).ToolTipText(LOCTEXT("ScopeTip", "Choose all query-enabled scene collision, one actor, or a captured set of actors."))
            .OnGetMenuContent_Lambda(TargetMenu)
            .ButtonContent()[SNew(STextBlock).Text_Lambda([State]()
            {
                switch (State->Options.TargetScope)
                {
                case EStrandProjectionTargetScope::SingleActor: return LOCTEXT("Single", "Single Actor");
                case EStrandProjectionTargetScope::ActorSet: return LOCTEXT("Set", "Actor Set");
                default: return LOCTEXT("All", "All Scene Collision");
                }
            })]]
        + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)
        [SNew(SButton).ToolTipText(LOCTEXT("PickTargetsTip", "Capture selected level actors as the collision target set. Disabled when All Scene Collision is selected."))
            .Text(LOCTEXT("PickTargets", "Use Selected Level Actor(s) as Target(s)"))
            .IsEnabled_Lambda([State]() { return State->Options.TargetScope != EStrandProjectionTargetScope::EntireLevel; })
            .OnClicked_Lambda([State]()
            {
                TArray<AActor*> Targets = GetSelectedTargets(State->Source.Get());
                if (Targets.IsEmpty())
                {
                    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("PickHelp", "Select one or more target actors in the Level Editor, excluding the source spline actor, then click again."));
                    return FReply::Handled();
                }
                if (State->Options.TargetScope == EStrandProjectionTargetScope::SingleActor)
                {
                    if (Targets.Num() != 1)
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("PickOne", "Single Actor requires exactly one selected target actor (excluding the source)."));
                        return FReply::Handled();
                    }
                    State->Options.Target = Targets[0];
                }
                else
                {
                    State->Options.Targets.Reset();
                    for (AActor* Target : Targets) { State->Options.Targets.Add(Target); }
                }
                return FReply::Handled();
            })];

    const TSharedRef<SWidget> RaycastContent = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SComboButton).ToolTipText(LOCTEXT("RayModeTip", "Choose fixed world direction, parallel camera rays, or perspective rays through the Level Editor viewport."))
            .OnGetMenuContent_Lambda(RayMenu)
            .ButtonContent()[SNew(STextBlock).Text_Lambda([State]()
            {
                switch (State->Options.RayMode)
                {
                case EStrandProjectionRayMode::CameraDirection: return LOCTEXT("Parallel", "Camera Direction (Parallel)");
                case EStrandProjectionRayMode::CameraProjection: return LOCTEXT("Perspective", "Camera Projection (Perspective)");
                default: return LOCTEXT("World", "World Direction");
                }
            })]]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [NumberRow<int32>(LOCTEXT("First", "First Anchor"), [](const FStrandProjectionOptions& O) { return O.FirstAnchor; },
            [](FStrandProjectionOptions& O, int32 V) { O.FirstAnchor = FMath::Max(0, V); }, State)]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [NumberRow<int32>(LOCTEXT("Last", "Last Anchor"), [](const FStrandProjectionOptions& O) { return O.LastAnchor; },
            [](FStrandProjectionOptions& O, int32 V) { O.LastAnchor = FMath::Max(-1, V); }, State)]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SCheckBox).ToolTipText(LOCTEXT("NormalsTip", "Rotate projected anchor orientation to align its up vector with the collision hit normal."))
            .IsChecked_Lambda([State]() { return State->Options.bAlignNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState Value) { State->Options.bAlignNormals = Value == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("AlignNormals", "Align to Normals"))]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SCheckBox).ToolTipText(LOCTEXT("SelectionWeightTip", "Restrict projection to the saved Strand Editor anchor selection and soft-selection falloff."))
            .Visibility_Lambda([State]() { return State->Options.AnchorWeights.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            .IsChecked_Lambda([State]() { return State->Options.bUseAnchorWeights ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->Options.bUseAnchorWeights = V == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("UseSelection", "Use selected anchors and soft-selection falloff"))]]
]
        + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesWorldDirection(State->Options); })
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(0.95f).VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(LOCTEXT("Direction", "Direction"))
                        .ToolTipText(LOCTEXT("DirectionTip", "World-space trace vector (X, Y, Z). Only used in World Direction mode."))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
                    [AxisField(0, SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("DirectionXTip", "World trace X direction."))
                        .AllowSpin(true).Value_Lambda([State]() -> TOptional<double> { return State->Options.WorldDirection.X; })
                        .OnValueCommitted_Lambda([State](double V, ETextCommit::Type) { State->Options.WorldDirection.X = V; }))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
                    [AxisField(1, SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("DirectionYTip", "World trace Y direction."))
                        .AllowSpin(true).Value_Lambda([State]() -> TOptional<double> { return State->Options.WorldDirection.Y; })
                        .OnValueCommitted_Lambda([State](double V, ETextCommit::Type) { State->Options.WorldDirection.Y = V; }))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(2, 0)
                    [AxisField(2, SNew(SNumericEntryBox<double>).ToolTipText(LOCTEXT("DirectionZTip", "World trace Z direction."))
                        .AllowSpin(true).Value_Lambda([State]() -> TOptional<double> { return State->Options.WorldDirection.Z; })
                        .OnValueCommitted_Lambda([State](double V, ETextCommit::Type) { State->Options.WorldDirection.Z = V; }))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [NumberRow<float>(LOCTEXT("Distance", "Max Distance"), [](const FStrandProjectionOptions& O) { return O.MaxDistance; },
            [](FStrandProjectionOptions& O, float V) { O.MaxDistance = FMath::Max(0.0f, V); }, State)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [NumberRow<float>(LOCTEXT("Strength", "Strength"), [](const FStrandProjectionOptions& O) { return O.Strength; },
            [](FStrandProjectionOptions& O, float V) { O.Strength = FMath::Clamp(V, 0.0f, 1.0f); }, State)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [NumberRow<float>(LOCTEXT("Clearance", "Clearance"), [](const FStrandProjectionOptions& O) { return O.Clearance; },
            [](FStrandProjectionOptions& O, float V) { O.Clearance = V; }, State)]];

    const TSharedRef<SWidget> ConformContent = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
            [SNew(SCheckBox).ToolTipText(LOCTEXT("ContinuousTip", "Add intermediate anchors to follow collision geometry between original anchors."))
            .IsChecked_Lambda([State]() { return State->Options.bContinuousConform ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState Value) { State->Options.bContinuousConform = Value == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("ContinuousConform", "Continuous Conform"))]]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
            [SNew(SCheckBox).ToolTipText(LOCTEXT("SmoothTip", "Rebuild smooth tangents for conform points. Disabled means use linear segments for closer surface adherence."))
            .IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
            .IsChecked_Lambda([State]() { return State->Options.bConformSmoothTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->Options.bConformSmoothTangents = V == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("SmoothConform", "Reconstruct Tangents"))]]
]
        + SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
                [NumberRow<float>(LOCTEXT("ConformSpacing", "Max Sample Spacing"), [](const FStrandProjectionOptions& O) { return O.ConformMaxSpacing; },
            [](FStrandProjectionOptions& O, float V) { O.ConformMaxSpacing = V; }, State)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
                [NumberRow<float>(LOCTEXT("ConformTolerance", "Surface Error Tolerance"), [](const FStrandProjectionOptions& O) { return O.ConformTolerance; },
            [](FStrandProjectionOptions& O, float V) { O.ConformTolerance = V; }, State)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
            [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
                [NumberRow<int32>(LOCTEXT("ConformBudget", "Max New Points"), [](const FStrandProjectionOptions& O) { return O.ConformMaxNewPoints; },
            [](FStrandProjectionOptions& O, int32 V) { O.ConformMaxNewPoints = V; }, State)]]];

    const TSharedRef<SWidget> ReturnContent = SNew(SComboButton)
        .ToolTipText(LOCTEXT("OutputSpaceTip", "Relative keeps the projected spline local and discards temporary placement. Absolute bakes world positions, rotations, tangents, and scale into the asset."))
        .OnGetMenuContent_Lambda(ReturnMenu)
        .ButtonContent()[SNew(STextBlock).Text_Lambda([State]()
        {
            return State->Options.bBakeWorldPlacementToAsset
                ? LOCTEXT("AbsoluteSelected", "Absolute (World)")
                : LOCTEXT("RelativeSelected", "Relative (Local)");
        })];

    // Sections mirror Unreal's Details panel organization; review stays
    // visible while the author scrolls or collapses advanced settings.
    Window->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 9, 12, 10)
        [SNew(STextBlock)
            .Text(LOCTEXT("ProjectionSummary", "Project the current spline onto level collision. Apply or Cancel in the review window."))]
        + SVerticalBox::Slot().FillHeight(1).Padding(8, 0, 8, 0)
        [SNew(SScrollBox)
            + SScrollBox::Slot()
            [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .ToolTipText(LOCTEXT("PlacementAreaTip", "Position and rotate the temporary spline before projection."))
                .AreaTitle(LOCTEXT("PlacementArea", "Placement"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [PlacementContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .ToolTipText(LOCTEXT("TargetsAreaTip", "Choose collision sources to project onto."))
                .AreaTitle(LOCTEXT("TargetsArea", "Targets"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [TargetsContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .ToolTipText(LOCTEXT("RayAreaTip", "Choose how each spline sample traces toward geometry."))
                .AreaTitle(LOCTEXT("RayArea", "Projection Rays"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [RaycastContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .ToolTipText(LOCTEXT("ConformAreaTip", "Configure how intermediate spline points are generated."))
                .AreaTitle(LOCTEXT("ConformArea", "Surface Conform"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [ConformContent]]]
            ]]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 8)
        [SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(6)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [ReturnContent]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                [SNew(SButton).Text(LOCTEXT("Review", "Review Projection"))
                .ToolTipText(LOCTEXT("ReviewTip", "Show a non-destructive preview in the Level Editor. Apply or Cancel the result in the review window."))
            .OnClicked_Lambda([State]()
            {
                if (!State->Source.IsValid())
                {
                    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidSource", "The source level spline no longer exists."));
                    return FReply::Handled();
                }
                FString Reason;
                if (!StrandProjectionWorkbench::ValidateOptions(State->Options,
                    State->Source->GetNumberOfSplinePoints(), Reason))
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Reason));
                    return FReply::Handled();
                }
                SplineEditorCommands::ProjectComponentOntoScene(State->Source.Get(), State->Options,
                    [State, bBakeWorld = State->Options.bBakeWorldPlacementToAsset](USplineComponent* Result, const TArray<int32>& OriginIndices) -> bool
                    { return State->OnApplied ? State->OnApplied(Result, OriginIndices, bBakeWorld) : false; },
                    State->Window.Pin());
                return FReply::Handled();
            })]]]);
    // An independent top-level window must not minimize with the Strand asset editor.
    // Lifetime is still controlled by the asset editor through OnClosed and its destructor.
    FSlateApplication::Get().AddWindow(Window);
    return Window;
}

#undef LOCTEXT_NAMESPACE
