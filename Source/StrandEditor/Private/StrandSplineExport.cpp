#include "StrandSplineExport.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/MessageDialog.h"
#include <vector>
#include <cfloat>
#include "Styling/CoreStyle.h"
#include <exception>

THIRD_PARTY_INCLUDES_START
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogStrandExport, Log, All);

namespace
{
    using StrandSplineExport::EProjection;

    bool ExtractPoints(const USplineDataAsset* Asset, TArray<FSplinePoint>& Out, FString& Error)
    {
        if (!IsValid(Asset) || Asset->Points.Num() < 2)
        {
            Error = TEXT("A Strand spline must contain at least two points to export.");
            return false;
        }
        if (Asset->Points.Num() > 100000)
        {
            Error = TEXT("Spline exceeds the 100,000-point export limit.");
            return false;
        }
        // Evaluate auto/clamped tangents through the actual Unreal spline implementation.
        TStrongObjectPtr<USplineComponent> Preview(NewObject<USplineComponent>(GetTransientPackage(), NAME_None, RF_Transient));
        Asset->ApplyToSplineComponent(Preview.Get());
        Out.Reserve(Preview->GetNumberOfSplinePoints());
        for (int32 Index = 0; Index < Preview->GetNumberOfSplinePoints(); ++Index)
        {
            const FSplinePoint Point = Preview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
            if (Point.Position.ContainsNaN() || Point.ArriveTangent.ContainsNaN() || Point.LeaveTangent.ContainsNaN())
            {
                Error = FString::Printf(TEXT("Point %d has non-finite geometry."), Index + 1);
                return false;
            }
            if (Point.Type == ESplinePointType::Constant)
            {
                Error = FString::Printf(TEXT("Point %d uses Constant interpolation. SVG and Alembic curve export cannot preserve this discontinuity. Convert it to Linear or Curve first."), Index + 1);
                return false;
            }
            Out.Add(Point);
        }
        return Out.Num() >= 2;
    }

    FVector2D ProjectSvg(const FVector& V, EProjection Plane)
    {
        // Strand's SVG importer with its default Mirror SVG X option maps (-svgX, -svgY) to Unreal XY.
        // XZ and YZ exports use the same orientation in their respective planes.
        switch (Plane)
        {
        case EProjection::XZ: return FVector2D(-V.X, -V.Z);
        case EProjection::YZ: return FVector2D(-V.Y, -V.Z);
        default: return FVector2D(-V.X, -V.Y);
        }
    }

    double RejectedAxis(const FVector& V, EProjection Plane)
    {
        switch (Plane)
        {
        case EProjection::XZ: return V.Y;
        case EProjection::YZ: return V.X;
        default: return V.Z;
        }
    }

    // Not every native spline point type has separate custom handles. The transient
    // USplineComponent has evaluated automatic tangent modes before we get here.
    bool SegmentIsLinear(const FSplinePoint& A) { return A.Type == ESplinePointType::Linear; }

    FString Coord(double N) { return FString::Printf(TEXT("%.12g"), N); }
    FString SvgPoint(const FVector2D& V) { return Coord(V.X) + TEXT(" ") + Coord(V.Y); }

    bool GetProjectionChoice(EProjection& Projection, const USplineDataAsset* Asset)
    {
        // XY is a lossless spatial projection of geometry which is already in XY.
        if (StrandSplineExport::IsPlanar(Asset, EProjection::XY))
        {
            Projection = EProjection::XY;
            return true;
        }
        // Every non-XY spline requires an explicit plane choice. Never silently
        // discard spatial depth, even if the curve happens to lie in XZ or YZ.
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Strand: Export 2D SVG")))
            .ClientSize(FVector2D(550, 190))
            .SupportsMinimize(false).SupportsMaximize(false);
        bool bAccepted = false;
        TWeakPtr<SWindow> WeakWindow = Window;
        auto MakePlaneButton = [&Projection, &bAccepted, WeakWindow](const FString& Label, EProjection Plane) -> TSharedRef<SWidget>
        {
            return SNew(SButton).Text(FText::FromString(Label))
                .ToolTipText(FText::FromString(TEXT("Project every spline anchor and tangent handle into the selected 2D plane.")))
                .OnClicked_Lambda([&Projection, &bAccepted, WeakWindow, Plane]()
                {
                    Projection = Plane;
                    bAccepted = true;
                    if (TSharedPtr<SWindow> W = WeakWindow.Pin()) { W->RequestDestroyWindow(); }
                    return FReply::Handled();
                });
        };
        Window->SetContent(
            SNew(SBorder).Padding(15.0f)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(STextBlock).Text(FText::FromString(TEXT("This spline contains depth outside the XY plane.")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [SNew(STextBlock).Text(FText::FromString(TEXT("SVG stores only 2D paths. Choose the plane to project onto.\nThe discarded axis cannot be reconstructed from the SVG; your Strand asset is not modified.")))
                    .AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
                    [MakePlaneButton(TEXT("Top (XY)"), EProjection::XY)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
                    [MakePlaneButton(TEXT("Front (XZ)"), EProjection::XZ)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
                    [MakePlaneButton(TEXT("Side (YZ)"), EProjection::YZ)]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    + SHorizontalBox::Slot().AutoWidth().Padding(3, 0)
                    [SNew(SButton).Text(FText::FromString(TEXT("Cancel")))
                        .OnClicked_Lambda([WeakWindow]()
                        {
                            if (TSharedPtr<SWindow> W = WeakWindow.Pin()) { W->RequestDestroyWindow(); }
                            return FReply::Handled();
                        })]]]);
        FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
        return bAccepted;
    }
}

bool StrandSplineExport::IsPlanar(const USplineDataAsset* Asset, EProjection Plane, double ToleranceCm)
{
    TArray<FSplinePoint> Points;
    FString Error;
    if (!ExtractPoints(Asset, Points, Error)) { return false; }
    const double Origin = RejectedAxis(Points[0].Position, Plane);
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        const FSplinePoint& P = Points[I];
        if (FMath::Abs(RejectedAxis(P.Position, Plane) - Origin) > ToleranceCm) { return false; }
        // Examine the handles too, not only the anchors. A spline can curve out
        // of plane even when all its control point positions lie in a plane.
        if (I < Points.Num() - 1 || Asset->bIsClosedLoop)
        {
            if (!SegmentIsLinear(P) &&
                FMath::Abs(RejectedAxis(P.LeaveTangent, Plane)) > ToleranceCm) { return false; }
            const FSplinePoint& Next = Points[(I + 1) % Points.Num()];
            if (!SegmentIsLinear(P) &&
                FMath::Abs(RejectedAxis(Next.ArriveTangent, Plane)) > ToleranceCm) { return false; }
        }
    }
    return true;
}

bool StrandSplineExport::WriteSvg(const USplineDataAsset* Asset, const FString& Filename,
    EProjection Plane, FString& OutError)
{
    TArray<FSplinePoint> Points;
    if (!ExtractPoints(Asset, Points, OutError)) { return false; }
    const int32 Count = Points.Num();
    double MinX = DBL_MAX, MinY = DBL_MAX, MaxX = -DBL_MAX, MaxY = -DBL_MAX;
    FString Path = TEXT("M ") + SvgPoint(ProjectSvg(Points[0].Position, Plane));
    auto Include = [&](const FVector2D& V)
    {
        MinX = FMath::Min(MinX, V.X); MinY = FMath::Min(MinY, V.Y);
        MaxX = FMath::Max(MaxX, V.X); MaxY = FMath::Max(MaxY, V.Y);
    };
    Include(ProjectSvg(Points[0].Position, Plane));
    const int32 Segments = Asset->bIsClosedLoop ? Count : Count - 1;
    for (int32 S = 0; S < Segments; ++S)
    {
        const FSplinePoint& A = Points[S];
        const FSplinePoint& B = Points[(S + 1) % Count];
        const FVector2D End = ProjectSvg(B.Position, Plane);
        if (SegmentIsLinear(A))
        {
            Path += TEXT(" L ") + SvgPoint(End);
            Include(End);
        }
        else
        {
            const FVector2D C1 = ProjectSvg(A.Position + A.LeaveTangent / 3.0, Plane);
            const FVector2D C2 = ProjectSvg(B.Position - B.ArriveTangent / 3.0, Plane);
            Path += TEXT(" C ") + SvgPoint(C1) + TEXT(" ") + SvgPoint(C2) + TEXT(" ") + SvgPoint(End);
            Include(C1); Include(C2); Include(End);
        }
    }
    if (Asset->bIsClosedLoop) { Path += TEXT(" Z"); }
    // The path uses source SVG coordinates; viewBox includes the entire curve.
    // Importers generally rebase the canvas origin, so local XYZ coordinates need
    // not round-trip numerically. The exported 2D curve shape is the authority.
    constexpr double Padding = 1.0;
    const double Vx = MinX - Padding, Vy = MinY - Padding;
    const double W = FMath::Max(MaxX - MinX + 2 * Padding, 2.0);
    const double H = FMath::Max(MaxY - MinY + 2 * Padding, 2.0);
    FString Xml = TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    Xml += FString::Printf(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%s %s %s %s\">\n"),
        *Coord(Vx), *Coord(Vy), *Coord(W), *Coord(H));
    Xml += TEXT("  <!-- Exported by Strand; dimensions use Unreal centimeters as unitless SVG coordinates. -->\n");
    Xml += TEXT("  <path d=\"") + Path + TEXT("\" fill=\"none\" stroke=\"#00B4A7\" stroke-width=\"1\"/>\n</svg>\n");
    if (!FFileHelper::SaveStringToFile(Xml, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("Unable to write SVG: %s"), *Filename);
        return false;
    }
    return true;
}

bool StrandSplineExport::WriteAlembic(const USplineDataAsset* Asset, const FString& Filename, FString& OutError)
{
    TArray<FSplinePoint> Points;
    if (!ExtractPoints(Asset, Points, OutError)) { return false; }
    namespace Abc = Alembic::Abc;
    namespace Geom = Alembic::AbcGeom;
    std::vector<Imath::V3f> CV;
    const int32 N = Points.Num();
    const int32 SegmentCount = Asset->bIsClosedLoop ? N : N - 1;
    CV.reserve(size_t(SegmentCount) * 3 + (Asset->bIsClosedLoop ? 0 : 1));
    // A line segment is exactly representable as a cubic Bézier; exporting the
    // entire asset as cubic supports a mixture of Linear and Curve point types.
    // Strand imports Alembic with Y-up conversion enabled by default. Write
    // (X,Z,Y) so this exporter round-trips that default orientation.
    auto Add = [&](const FVector& P)
    {
        CV.emplace_back(static_cast<float>(P.X), static_cast<float>(P.Z), static_cast<float>(P.Y));
    };
    for (int32 I = 0; I < SegmentCount; ++I)
    {
        const FSplinePoint& A = Points[I];
        const FSplinePoint& B = Points[(I + 1) % N];
        FVector H1, H2;
        if (SegmentIsLinear(A))
        {
            H1 = FMath::Lerp(A.Position, B.Position, 1.0 / 3.0);
            H2 = FMath::Lerp(A.Position, B.Position, 2.0 / 3.0);
        }
        else
        {
            H1 = A.Position + A.LeaveTangent / 3.0;
            H2 = B.Position - B.ArriveTangent / 3.0;
        }
        Add(A.Position); Add(H1); Add(H2);
    }
    if (!Asset->bIsClosedLoop) { Add(Points.Last().Position); }
    // Prevent replacing a valid user file with a partial write after an SDK exception.
    const FString TempPath = Filename + TEXT(".strand-export-temp.abc");
    try
    {
        const FTCHARToUTF8 UTF8Path(*TempPath);
        {
            Abc::OArchive Archive(Alembic::AbcCoreOgawa::WriteArchive(), UTF8Path.Get());
            Geom::OCurves Curve(Abc::OObject(Archive, Abc::kTop), "StrandSpline");
            const int32 VertexCount = static_cast<int32>(CV.size());
            const Geom::OCurvesSchema::Sample Sample(
                Abc::P3fArraySample(CV.data(), CV.size()),
                Abc::Int32ArraySample(&VertexCount, 1),
                Geom::kCubic, Asset->bIsClosedLoop ? Geom::kPeriodic : Geom::kNonPeriodic);
            Curve.getSchema().set(Sample);
        }
        if (!IFileManager::Get().Move(*Filename, *TempPath, true))
        {
            OutError = TEXT("Unable to move the Alembic export into its destination. Check file permissions.");
            IFileManager::Get().Delete(*TempPath);
            return false;
        }
    }
    catch (const std::exception& E)
    {
        OutError = UTF8_TO_TCHAR(E.what());
        IFileManager::Get().Delete(*TempPath);
        return false;
    }
    return true;
}

void StrandSplineExport::ExportInteractive(USplineDataAsset* Asset, bool bAlembic)
{
    if (!IsValid(Asset)) { return; }
    TArray<FSplinePoint> ValidatedPoints;
    FString ValidationError;
    if (!ExtractPoints(Asset, ValidatedPoints, ValidationError))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ValidationError));
        return;
    }
    EProjection Plane = EProjection::XY;
    if (!bAlembic && !GetProjectionChoice(Plane, Asset)) { return; }
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop)
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Desktop save dialogs are unavailable.")));
        return;
    }
    const FString Extension = bAlembic ? TEXT("abc") : TEXT("svg");
    const FString Filter = bAlembic ? TEXT("Alembic Curves (*.abc)|*.abc") : TEXT("SVG Vector Paths (*.svg)|*.svg");
    TArray<FString> Paths;
    if (!Desktop->SaveFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        bAlembic ? TEXT("Export Strand as Alembic") : TEXT("Export Strand as SVG"),
        FPaths::ProjectDir(), Asset->GetName() + TEXT(".") + Extension,
        Filter, EFileDialogFlags::None, Paths) || Paths.IsEmpty()) { return; }
    FString Path = Paths[0];
    if (!FPaths::GetExtension(Path).Equals(Extension, ESearchCase::IgnoreCase)) { Path += TEXT(".") + Extension; }
    FString Error;
    const bool bSuccess = bAlembic ? WriteAlembic(Asset, Path, Error) : WriteSvg(Asset, Path, Plane, Error);
    if (bSuccess)
    {
        UE_LOG(LogStrandExport, Display, TEXT("Exported %s as %s to %s"), *Asset->GetName(), bAlembic ? TEXT("Alembic") : TEXT("SVG"), *Path);
    }
    else
    {
        UE_LOG(LogStrandExport, Warning, TEXT("Strand export failed for %s: %s"), *Asset->GetName(), *Error);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString(TEXT("Strand export failed: ")) + Error));
    }
}
