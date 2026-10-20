#include "SplineImportHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "EditorFramework/AssetImportData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSplineImport, Log, All);

USplineDataAsset* SplineAssetImport::CommitCurves(UObject* Parent, FName RequestedName, EObjectFlags Flags,
    TArray<FSplineImportedCurve>& Curves, const FString& SourceFile)
{
    Curves.RemoveAll([](const FSplineImportedCurve& Curve) { return Curve.Points.Num() < 2; });
    if (!IsValid(Parent) || Curves.IsEmpty())
    {
        UE_LOG(LogSplineImport, Error, TEXT("No usable curves found in %s"), *SourceFile);
        return nullptr;
    }

    const FString Format = FPaths::GetExtension(SourceFile).ToLower();
    auto RecordSource = [&](USplineDataAsset* Asset, const FSplineImportedCurve& Curve, int32 Index)
    {
        Asset->SourceImportFormat = Format;
        Asset->SourceCurveId = Curve.SourceId.IsEmpty()
            ? Curve.Name + FString::Printf(TEXT("#%d"), Index)
            : Curve.SourceId;
        if (Asset->AssetImportData) { Asset->AssetImportData->Update(SourceFile); }
    };

    USplineDataAsset* Primary = NewObject<USplineDataAsset>(Parent, RequestedName, Flags | RF_Transactional);
    if (!Primary) { return nullptr; }
    Primary->Points = MoveTemp(Curves[0].Points);
    Primary->bIsClosedLoop = Curves[0].bClosedLoop;
    RecordSource(Primary, Curves[0], 0);
    Primary->MarkPackageDirty();

    IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
    const FString PackagePath = FPackageName::GetLongPackagePath(Parent->GetOutermost()->GetName());
    const FString BaseName = RequestedName.ToString();
    for (int32 I = 1; I < Curves.Num(); ++I)
    {
        const FString RawSuffix = Curves[I].Name.IsEmpty() ? FString::Printf(TEXT("Curve_%03d"), I + 1)
                                                            : Curves[I].Name;
        FString Suffix;
        for (TCHAR C : RawSuffix.Left(48))
        {
            Suffix.AppendChar(FChar::IsAlnum(C) || C == TCHAR('_') ? C : TCHAR('_'));
        }
        if (Suffix.IsEmpty()) { Suffix = TEXT("Curve"); }
        FString UniquePackage, UniqueAsset;
        AssetTools.CreateUniqueAssetName(PackagePath / BaseName, TEXT("_") + Suffix,
                                         UniquePackage, UniqueAsset);
        UPackage* NewPackage = CreatePackage(*UniquePackage);
        if (!NewPackage)
        {
            UE_LOG(LogSplineImport, Error, TEXT("Could not create package %s"), *UniquePackage);
            continue;
        }
        USplineDataAsset* Extra = NewObject<USplineDataAsset>(NewPackage, FName(*UniqueAsset),
            RF_Public | RF_Standalone | RF_Transactional);
        Extra->Points = MoveTemp(Curves[I].Points);
        Extra->bIsClosedLoop = Curves[I].bClosedLoop;
        RecordSource(Extra, Curves[I], I);
        FAssetRegistryModule::AssetCreated(Extra);
        NewPackage->MarkPackageDirty();
        UE_LOG(LogSplineImport, Display, TEXT("Created sibling spline %s from %s"), *UniquePackage, *SourceFile);
    }
    return Primary;
}
