#include "StrandProjectionWorkbench.h"
#include "SplineEditorCommands.h"
#include "StrandSplineActor.h"
#include "StrandProjectionPlacement.h"
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
#include "Widgets/Text/STextBlock.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "StrandProjectionWorkbench"

namespace
{
    struct FWorkbenchState
    {
        TWeakObjectPtr<USplineComponent> Source;
        FStrandProjectionOptions Options;
        TFunction<void(USplineComponent*, const TArray<int32>&, bool)> OnApplied;
        TWeakPtr<SWindow> Window;
        double CameraDistanceCm = 1250.0;
        bool bMatchCameraRotation = false;
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

    template<typename ValueType>
    TSharedRef<SWidget> NumberRow(const FText& Label, const TFunction<ValueType(const FStrandProjectionOptions&)>& Get,
        const TFunction<void(FStrandProjectionOptions&, ValueType)>& Set, const TSharedRef<FWorkbenchState>& State)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(Label)]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SNumericEntryBox<ValueType>)
                .AllowSpin(true)
                .Value_Lambda([State, Get]() -> TOptional<ValueType> { return Get(State->Options); })
                .OnValueCommitted_Lambda([State, Set](ValueType Value, ETextCommit::Type) { Set(State->Options, Value); })];
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
    TFunction<void(USplineComponent*, const TArray<int32>&, bool)> OnApplied, TFunction<void()> OnClosed)
{
    if (!IsValid(Source) || !IsValid(Source->GetOwner()) || !IsValid(Source->GetWorld())) { return nullptr; }
    TSharedRef<FWorkbenchState> State = MakeShared<FWorkbenchState>();
    State->Source = Source;
    State->Options = InitialOptions;
    State->OnApplied = MoveTemp(OnApplied);

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("Title", "Strand | Projection"))
        .IsTopmostWindow(true)
        .ClientSize(FVector2D(550, 670))
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
            Menu.AddMenuEntry(Name, FText::GetEmpty(), FSlateIcon(),
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
            Menu.AddMenuEntry(Name, FText::GetEmpty(), FSlateIcon(),
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

    // Projection settings are scoped to this temporary session; changing the
    // panel never edits a level actor or the backing Strand asset directly.
    const TSharedRef<SWidget> PlacementContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(LOCTEXT("CameraDistance", "Distance from Camera (cm)"))]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SNumericEntryBox<double>)
                .AllowSpin(true)
                .MinValue(1.0)
                .MinSliderValue(1.0)
                .MaxSliderValue(10000.0)
                .Value_Lambda([State]() -> TOptional<double> { return State->CameraDistanceCm; })
                .OnValueChanged_Lambda([State](double V) { if (FMath::IsFinite(V)) { State->CameraDistanceCm = FMath::Max(1.0, V); } })
                .OnValueCommitted_Lambda([State](double V, ETextCommit::Type) { if (FMath::IsFinite(V)) { State->CameraDistanceCm = FMath::Max(1.0, V); } })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [SNew(SCheckBox)
            .IsChecked_Lambda([State]() { return State->bMatchCameraRotation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->bMatchCameraRotation = V == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("MatchCameraRotation", "Orient spline to camera rotation"))]]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
            [SNew(SButton).Text(LOCTEXT("PlaceCamera", "Place at Level Camera"))
                .ToolTipText(LOCTEXT("PlaceCameraTip", "Position the temporary spline at the specified camera distance. Optionally match camera rotation. Does not modify the Strand asset until projection is applied."))
                .OnClicked_Lambda([State]()
                {
                    USplineComponent* Source = State->Source.Get();
                    if (!IsValid(Source) || !IsValid(Source->GetOwner()) || !GEditor) { return FReply::Handled(); }
                    if (!StrandProjectionPlacement::ValidCameraDistance(State->CameraDistanceCm))
                    {
                        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidCameraDistance", "Camera distance must be a positive, finite value."));
                        return FReply::Handled();
                    }
                    bool bPlaced = false;
                    for (const FLevelEditorViewportClient* Viewport : GEditor->GetLevelViewportClients())
                    {
                        if (Viewport && Viewport->Viewport && Viewport->IsPerspective() && Viewport->GetWorld() == Source->GetWorld())
                        {
                            AActor* Owner = Source->GetOwner();
                            const FQuat Orientation = State->bMatchCameraRotation
                                ? Viewport->GetViewRotation().Quaternion() : Owner->GetActorQuat();
                            const FVector Center = GetLocalAnchorCenter(Source);
                            const FTransform Placement = StrandProjectionPlacement::CameraPlacement(
                                Viewport->GetViewLocation(), Viewport->GetViewRotation(),
                                State->CameraDistanceCm, Center, Orientation, Owner->GetActorScale3D());
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
            [SNew(SButton).Text(LOCTEXT("PlaceSurface", "Place Above Selected Surface"))
                .ToolTipText(LOCTEXT("PlaceSurfaceTip", "Position the temporary spline near the first selected level target. Use Select Temporary Spline to adjust with the native transform gizmo."))
                .OnClicked_Lambda([State]()
                {
                    USplineComponent* Source = State->Source.Get();
                    if (!IsValid(Source) || !IsValid(Source->GetOwner())) { return FReply::Handled(); }
                    const TArray<AActor*> Targets = GetSelectedTargets(Source);
                    if (Targets.IsEmpty()) { return FReply::Handled(); }
                    const FBox Bounds = Targets[0]->GetComponentsBoundingBox(true);
                    if (!Bounds.IsValid) { return FReply::Handled(); }
                    FVector Direction = State->Options.RayMode == EStrandProjectionRayMode::WorldDirection
                        ? State->Options.WorldDirection.GetSafeNormal() : FVector::DownVector;
                    if (Direction.IsNearlyZero()) { Direction = FVector::DownVector; }
                    AActor* Owner = Source->GetOwner();
                    const FVector Center = GetLocalAnchorCenter(Source);
                    const FVector DesiredCenter = Bounds.GetCenter() - Direction * (Bounds.GetExtent().GetMax() + 200.0);
                    Owner->SetActorLocation(DesiredCenter - Owner->GetActorQuat().RotateVector(Center * Owner->GetActorScale3D()));
                    return FReply::Handled();
                })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [SNew(SButton)
            .Text(LOCTEXT("SelectTemporary", "Select Temporary Spline"))
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
        + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
        [SNew(STextBlock)
            .Text_Lambda([State]()
            {
                if (!State->Source.IsValid() || !IsValid(State->Source->GetOwner())) { return LOCTEXT("NoTempSource", "Source unavailable"); }
                return FText::FromString(FString::Printf(TEXT("Temporary transform: %s | Rotation: %s"),
                    *State->Source->GetOwner()->GetActorLocation().ToCompactString(),
                    *State->Source->GetOwner()->GetActorRotation().ToCompactString()));
            })
            .AutoWrapText(true)];

    const TSharedRef<SWidget> TargetsContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SComboButton).OnGetMenuContent_Lambda(TargetMenu)
            .ButtonContent()[SNew(STextBlock).Text_Lambda([State]()
            {
                switch (State->Options.TargetScope)
                {
                case EStrandProjectionTargetScope::SingleActor: return LOCTEXT("Single", "Single Actor");
                case EStrandProjectionTargetScope::ActorSet: return LOCTEXT("Set", "Actor Set");
                default: return LOCTEXT("All", "All Scene Collision");
                }
            })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [SNew(STextBlock).Text_Lambda([State]() { return FText::FromString(TargetSummary(*State)); }).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SButton).Text(LOCTEXT("PickTargets", "Use Selected Level Actor(s) as Target(s)"))
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

    const TSharedRef<SWidget> RaycastContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SComboButton).OnGetMenuContent_Lambda(RayMenu)
            .ButtonContent()[SNew(STextBlock).Text_Lambda([State]()
            {
                switch (State->Options.RayMode)
                {
                case EStrandProjectionRayMode::CameraDirection: return LOCTEXT("Parallel", "Camera Direction (Parallel)");
                case EStrandProjectionRayMode::CameraProjection: return LOCTEXT("Perspective", "Camera Projection (Perspective)");
                default: return LOCTEXT("World", "World Direction");
                }
            })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
        [SNew(STextBlock).Text(LOCTEXT("DirectionHelp", "Camera modes use the Level Editor viewport.")) .AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesWorldDirection(State->Options); })
         [NumberRow<double>(LOCTEXT("X", "Direction X"), [](const FStrandProjectionOptions& O) { return O.WorldDirection.X; },
            [](FStrandProjectionOptions& O, double V) { O.WorldDirection.X = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesWorldDirection(State->Options); })
         [NumberRow<double>(LOCTEXT("Y", "Direction Y"), [](const FStrandProjectionOptions& O) { return O.WorldDirection.Y; },
            [](FStrandProjectionOptions& O, double V) { O.WorldDirection.Y = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesWorldDirection(State->Options); })
         [NumberRow<double>(LOCTEXT("Z", "Direction Z"), [](const FStrandProjectionOptions& O) { return O.WorldDirection.Z; },
            [](FStrandProjectionOptions& O, double V) { O.WorldDirection.Z = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [NumberRow<float>(LOCTEXT("Distance", "Max Distance (cm)"), [](const FStrandProjectionOptions& O) { return O.MaxDistance; },
            [](FStrandProjectionOptions& O, float V) { O.MaxDistance = FMath::Max(0.0f, V); }, State)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [NumberRow<float>(LOCTEXT("Strength", "Strength (0 to 1)"), [](const FStrandProjectionOptions& O) { return O.Strength; },
            [](FStrandProjectionOptions& O, float V) { O.Strength = FMath::Clamp(V, 0.0f, 1.0f); }, State)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [NumberRow<float>(LOCTEXT("Clearance", "Clearance (cm)"), [](const FStrandProjectionOptions& O) { return O.Clearance; },
            [](FStrandProjectionOptions& O, float V) { O.Clearance = V; }, State)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [NumberRow<int32>(LOCTEXT("First", "First Anchor"), [](const FStrandProjectionOptions& O) { return O.FirstAnchor; },
            [](FStrandProjectionOptions& O, int32 V) { O.FirstAnchor = FMath::Max(0, V); }, State)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [NumberRow<int32>(LOCTEXT("Last", "Last Anchor (-1 = all)"), [](const FStrandProjectionOptions& O) { return O.LastAnchor; },
            [](FStrandProjectionOptions& O, int32 V) { O.LastAnchor = FMath::Max(-1, V); }, State)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
        [SNew(SCheckBox)
            .Visibility_Lambda([State]() { return State->Options.AnchorWeights.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            .IsChecked_Lambda([State]() { return State->Options.bUseAnchorWeights ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->Options.bUseAnchorWeights = V == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("UseSelection", "Use selected anchors and soft-selection falloff"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
        [SNew(SCheckBox)
            .IsChecked_Lambda([State]() { return State->Options.bAlignNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState Value) { State->Options.bAlignNormals = Value == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("AlignNormals", "Align up vectors to hit normals"))]];

    const TSharedRef<SWidget> ConformContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
        [SNew(SCheckBox)
            .IsChecked_Lambda([State]() { return State->Options.bContinuousConform ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState Value) { State->Options.bContinuousConform = Value == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("ContinuousConform", "Continuous Conform (insert points between anchors)"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
         [NumberRow<float>(LOCTEXT("ConformSpacing", "Max Sample Spacing (cm)"), [](const FStrandProjectionOptions& O) { return O.ConformMaxSpacing; },
            [](FStrandProjectionOptions& O, float V) { O.ConformMaxSpacing = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
         [NumberRow<float>(LOCTEXT("ConformTolerance", "Surface Error Tolerance (cm)"), [](const FStrandProjectionOptions& O) { return O.ConformTolerance; },
            [](FStrandProjectionOptions& O, float V) { O.ConformTolerance = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SBox).IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
         [NumberRow<int32>(LOCTEXT("ConformBudget", "Maximum New Points"), [](const FStrandProjectionOptions& O) { return O.ConformMaxNewPoints; },
            [](FStrandProjectionOptions& O, int32 V) { O.ConformMaxNewPoints = V; }, State)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [SNew(SCheckBox)
            .IsEnabled_Lambda([State]() { return StrandProjectionWorkbench::UsesContinuousSettings(State->Options); })
            .IsChecked_Lambda([State]() { return State->Options.bConformSmoothTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State](ECheckBoxState V) { State->Options.bConformSmoothTangents = V == ECheckBoxState::Checked; })
            [SNew(STextBlock).Text(LOCTEXT("SmoothConform", "Reconstruct smooth tangents (otherwise linear segments)"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 8)
        [SNew(STextBlock).Text(LOCTEXT("ContinuousHelp", "Adds points where projected geometry needs detail. The point limit is shared across eligible spans; limited results are flagged in the preview.")) .AutoWrapText(true)];

    const TSharedRef<SWidget> ReturnContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [SNew(SComboButton).OnGetMenuContent_Lambda(ReturnMenu)
            .ButtonContent()
            [SNew(STextBlock).Text_Lambda([State]()
            {
                return State->Options.bBakeWorldPlacementToAsset
                    ? LOCTEXT("AbsoluteSelected", "Absolute (World)")
                    : LOCTEXT("RelativeSelected", "Relative (Local)");
            })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 2)
        [SNew(STextBlock).AutoWrapText(true)
            .Text_Lambda([State]()
            {
                return State->Options.bBakeWorldPlacementToAsset
                    ? LOCTEXT("WorldReturnHelp", "Bakes the temporary actor's world placement into the asset. Place at identity to reproduce the world position.")
                    : LOCTEXT("LocalReturnHelp", "Keeps asset points local. Temporary actor placement is discarded.");
            })];

    // Sections mirror Unreal's Details panel organization; review stays
    // visible while the author scrolls or collapses advanced settings.
    Window->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 7)
        [SNew(STextBlock)
            .Text(LOCTEXT("ProjectionIntro", "Project the current spline onto level collision. Apply or Cancel in the review window."))
            .AutoWrapText(true)]
        + SVerticalBox::Slot().FillHeight(1).Padding(8, 0, 8, 0)
        [SNew(SScrollBox)
            + SScrollBox::Slot()
            [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .AreaTitle(LOCTEXT("PlacementArea", "Placement"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [PlacementContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .AreaTitle(LOCTEXT("TargetsArea", "Targets"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [TargetsContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .AreaTitle(LOCTEXT("RayArea", "Projection Rays"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [RaycastContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .AreaTitle(LOCTEXT("ConformArea", "Surface Conform"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [ConformContent]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SExpandableArea)
                .AreaTitle(LOCTEXT("ReturnArea", "Asset Output"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(10, 8))
                    [ReturnContent]]]
            ]]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 8)
        [SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(6)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(STextBlock)
                    .Text_Lambda([State]()
                    {
                        return State->Options.bContinuousConform
                            ? LOCTEXT("AdaptiveMode", "Continuous conform enabled")
                            : LOCTEXT("AnchorMode", "Anchor projection");
                    })]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).Text(LOCTEXT("Review", "Review Projection..."))
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
                    [State, bBakeWorld = State->Options.bBakeWorldPlacementToAsset](USplineComponent* Result, const TArray<int32>& OriginIndices) { if (State->OnApplied) { State->OnApplied(Result, OriginIndices, bBakeWorld); } },
                    State->Window.Pin());
                return FReply::Handled();
            })]]]);
    // An independent top-level window must not minimize with the Strand asset editor.
    // Lifetime is still controlled by the asset editor through OnClosed and its destructor.
    FSlateApplication::Get().AddWindow(Window);
    return Window;
}

#undef LOCTEXT_NAMESPACE
