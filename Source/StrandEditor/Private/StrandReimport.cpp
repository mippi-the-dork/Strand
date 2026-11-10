#include "SplineDataAssetFactory.h"
#include "SplineDataAsset.h"
#include "SplineImportHelpers.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "StrandReimport"
DEFINE_LOG_CATEGORY_STATIC(LogStrandReimport, Log, All);

bool UStrandReimportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
    USplineDataAsset* Asset = Cast<USplineDataAsset>(Obj);
    if (!Asset || !Asset->AssetImportData || Asset->SourceCurveId.IsEmpty() ||
        (Asset->SourceImportFormat != TEXT("abc") && Asset->SourceImportFormat != TEXT("svg")))
    {
        return false;
    }
    const FString Filename = Asset->AssetImportData->GetFirstFilename();
    if (Filename.IsEmpty()) { return false; }
    OutFilenames.Add(Filename);
    return true;
}

void UStrandReimportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
    USplineDataAsset* Asset = Cast<USplineDataAsset>(Obj);
    if (Asset && Asset->AssetImportData && NewReimportPaths.Num() == 1 && !NewReimportPaths[0].IsEmpty())
    {
        Asset->Modify();
        Asset->AssetImportData->Modify();
        Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
        Asset->MarkPackageDirty();
    }
}

EReimportResult::Type UStrandReimportFactory::Reimport(UObject* Obj)
{
    USplineDataAsset* Asset = Cast<USplineDataAsset>(Obj);
    if (!Asset || !Asset->AssetImportData || Asset->SourceCurveId.IsEmpty())
    {
        return EReimportResult::Failed;
    }
    const FString Filename = Asset->AssetImportData->GetFirstFilename();
    if (!FPaths::FileExists(Filename))
    {
        UE_LOG(LogStrandReimport, Error, TEXT("Strand source no longer exists: %s"), *Filename);
        return EReimportResult::Failed;
    }
    TArray<FSplineImportedCurve> Curves;
    const bool bLoaded = Asset->SourceImportFormat == TEXT("abc")
        ? SplineAssetImport::LoadAlembicCurves(Filename, Asset, Curves)
        : (Asset->SourceImportFormat == TEXT("svg") && SplineAssetImport::LoadSvgCurves(Filename, Curves, Asset->bMirrorSvgXOnImport));
    if (!bLoaded) { return EReimportResult::Failed; }
    const FSplineImportedCurve* Match = Curves.FindByPredicate([Asset](const FSplineImportedCurve& Curve)
    {
        return Curve.SourceId == Asset->SourceCurveId;
    });
    if (!Match)
    {
        UE_LOG(LogStrandReimport, Error,
            TEXT("Curve '%s' was not found in %s. Asset left unchanged; remap manually before reimport."),
            *Asset->SourceCurveId, *Filename);
        return EReimportResult::Failed;
    }
    const FText Prompt = FText::Format(
        LOCTEXT("ConfirmOverwrite", "Reimporting will replace all saved points in '{0}' from its source curve. Local actor splines will not be changed. Continue?"),
        FText::FromString(Asset->GetName()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Prompt) != EAppReturnType::Yes)
    {
        return EReimportResult::Cancelled;
    }
    const FScopedTransaction Transaction(LOCTEXT("ReimportTransaction", "Reimport Strand Spline Curve"));
    Asset->Modify();
    Asset->Points = Match->Points;
    Asset->bIsClosedLoop = Match->bClosedLoop;
    Asset->AssetImportData->Modify();
    Asset->AssetImportData->Update(Filename);
    Asset->MarkPackageDirty();
    // Never automatically propagate a source reimport to tracked instances: designers
    // may have authored local modifications that cannot safely be merged.
    UE_LOG(LogStrandReimport, Display, TEXT("Reimported '%s' from '%s'"), *Asset->SourceCurveId, *Filename);
    return EReimportResult::Succeeded;
}
#undef LOCTEXT_NAMESPACE
