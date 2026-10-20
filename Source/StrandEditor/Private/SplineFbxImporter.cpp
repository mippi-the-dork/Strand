#include "SplineDataAssetFactory.h"
#include "SplineImportHelpers.h"
#include "SplineDataAsset.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <fbxsdk.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogSplineFbx, Log, All);

namespace
{
    FVector ConvertPoint(const FbxVector4& Point, const FbxAMatrix& Matrix)
    {
        const FbxVector4 P = Matrix.MultT(Point);
        return FVector(P[0], P[1], P[2]);
    }

    void PopulateFromSamples(const TArray<FVector>& Input, bool bClosed,
                             const FString& CurveName, TArray<FSplineImportedCurve>& Output,
                             bool bStraightLines)
    {
        TArray<FVector> Samples = Input;
        if (bClosed && Samples.Num() > 2 && Samples[0].Equals(Samples.Last(), 0.001))
        {
            Samples.Pop();
        }
        if (Samples.Num() < 2) { return; }
        if (Samples.Num() > 100000)
        {
            UE_LOG(LogSplineFbx, Warning, TEXT("Curve %s exceeds 100000 sampled points; skipped."), *CurveName);
            return;
        }
        FSplineImportedCurve Curve;
        Curve.Name = CurveName;
        Curve.bClosedLoop = bClosed;
        const int32 N = Samples.Num();
        Curve.Points.Reserve(N);
        // Input has already been converted to FBX scene axes/centimeters. Store geometry
        // relative to the first sample, not the FBX scene object's transform/placement.
        const FVector Origin = Samples[0];
        for (int32 I = 0; I < N; ++I)
        {
            FSplinePointData Point;
            Point.LocalPosition = Samples[I] - Origin;
            Point.PointType = bStraightLines ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
            if (!bStraightLines)
            {
                const FVector Prev = Samples[I == 0 ? (bClosed ? N - 1 : 0) : I - 1];
                const FVector Next = Samples[I == N - 1 ? (bClosed ? 0 : N - 1) : I + 1];
                const FVector Derivative = (Next - Prev) * (bClosed || (I > 0 && I < N - 1) ? 0.5 : 1.0);
                Point.ArriveTangent = Derivative;
                Point.LeaveTangent = Derivative;
                if (!Derivative.IsNearlyZero()) { Point.LocalRotation = Derivative.Rotation(); }
            }
            Curve.Points.Add(Point);
        }
        Output.Add(MoveTemp(Curve));
    }

    // The Autodesk SDK's NURBS knot list omits the two redundant endpoint knots.
    // Restore those, then evaluate the homogeneous de Boor recurrence (rational weights).
    bool SampleNurbs(FbxNurbsCurve* Curve, const FbxAMatrix& NodeTransform,
                     TArray<FVector>& OutSamples)
    {
        const int32 Count = Curve->GetControlPointsCount();
        const int32 Order = Curve->GetOrder();
        const int32 Degree = Order - 1;
        const int32 KnotCount = Curve->GetKnotCount();
        const double* RawKnots = Curve->GetKnotVector();
        const FbxVector4* CV = Curve->GetControlPoints();
        if (Count < 2 || Count > 100000 || Degree < 1 || Degree > 16 || !RawKnots || !CV ||
            KnotCount != Count + Order - 2)
        {
            return false;
        }
        TArray<double> Knots;
        Knots.Reserve(KnotCount + 2);
        Knots.Add(RawKnots[0]);
        for (int32 I = 0; I < KnotCount; ++I)
        {
            if (I && RawKnots[I] < RawKnots[I - 1]) { return false; }
            Knots.Add(RawKnots[I]);
        }
        Knots.Add(RawKnots[KnotCount - 1]);
        const double TMin = Knots[Degree];
        const double TMax = Knots[Count];
        if (TMax <= TMin) { return false; }

        // 12 subdivisions per nonzero knot span, bounded for defensive file parsing.
        int32 NonzeroSpans = 0;
        for (int32 I = Degree; I < Count; ++I)
        {
            if (Knots[I + 1] > Knots[I]) { ++NonzeroSpans; }
        }
        const int32 SampleCount = FMath::Clamp(NonzeroSpans * 12 + 1, 2, 8192);
        OutSamples.Reserve(SampleCount);
        for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
        {
            double T = FMath::Lerp(TMin, TMax, static_cast<double>(SampleIndex) / (SampleCount - 1));
            int32 Span = Count - 1;
            if (T < TMax)
            {
                for (int32 I = Degree; I < Count; ++I)
                {
                    if (T >= Knots[I] && T < Knots[I + 1]) { Span = I; break; }
                }
            }
            // Homogeneous coordinates preserve rational weights.
            struct FWeightedPoint { double X, Y, Z, W; };
            TArray<FWeightedPoint> D;
            D.Reserve(Order);
            for (int32 J = 0; J <= Degree; ++J)
            {
                const FbxVector4& V = CV[Span - Degree + J];
                const double W = Curve->IsRational() ? V[3] : 1.0;
                D.Add({V[0] * W, V[1] * W, V[2] * W, W});
            }
            for (int32 R = 1; R <= Degree; ++R)
            {
                for (int32 J = Degree; J >= R; --J)
                {
                    const int32 K = Span - Degree + J;
                    const double Denom = Knots[K + Degree - R + 1] - Knots[K];
                    const double Alpha = Denom > 1.e-12 ? (T - Knots[K]) / Denom : 0.0;
                    FWeightedPoint& A = D[J - 1];
                    FWeightedPoint& B = D[J];
                    B = {FMath::Lerp(A.X, B.X, Alpha), FMath::Lerp(A.Y, B.Y, Alpha),
                         FMath::Lerp(A.Z, B.Z, Alpha), FMath::Lerp(A.W, B.W, Alpha)};
                }
            }
            const FWeightedPoint& P = D[Degree];
            if (FMath::Abs(P.W) < 1.e-12) { return false; }
            const FVector Result = ConvertPoint(FbxVector4(P.X / P.W, P.Y / P.W, P.Z / P.W), NodeTransform);
            if (Result.ContainsNaN()) { return false; }
            if (OutSamples.IsEmpty() || !Result.Equals(OutSamples.Last(), 0.0001))
            {
                OutSamples.Add(Result);
            }
        }
        return OutSamples.Num() >= 2;
    }

    void VisitNode(FbxNode* Node, TArray<FSplineImportedCurve>& Output)
    {
        if (!Node) { return; }
        const FbxAMatrix GeoTransform(Node->GetGeometricTranslation(FbxNode::eSourcePivot),
            Node->GetGeometricRotation(FbxNode::eSourcePivot),
            Node->GetGeometricScaling(FbxNode::eSourcePivot));
        const FbxAMatrix Mat = Node->EvaluateGlobalTransform() * GeoTransform;
        for (int32 AttrIndex = 0; AttrIndex < Node->GetNodeAttributeCount(); ++AttrIndex)
        {
            FbxNodeAttribute* Attribute = Node->GetNodeAttributeByIndex(AttrIndex);
            if (!Attribute) { continue; }
            const FString Name = UTF8_TO_TCHAR(Node->GetName());
            if (Attribute->GetAttributeType() == FbxNodeAttribute::eNurbsCurve)
            {
                FbxNurbsCurve* Nurbs = static_cast<FbxNurbsCurve*>(Attribute);
                TArray<FVector> Samples;
                const bool bClosed = Nurbs->GetType() != FbxNurbsCurve::eOpen;
                if (!SampleNurbs(Nurbs, Mat, Samples))
                {
                    UE_LOG(LogSplineFbx, Warning, TEXT("Could not evaluate rational knots for %s; using control polygon."), *Name);
                    const FbxVector4* CV = Nurbs->GetControlPoints();
                    for (int32 K = 0; K < Nurbs->GetControlPointsCount() && CV; ++K)
                    {
                        Samples.Add(ConvertPoint(CV[K], Mat));
                    }
                }
                PopulateFromSamples(Samples, bClosed, Name, Output, false);
            }
            else if (Attribute->GetAttributeType() == FbxNodeAttribute::eLine)
            {
                FbxLine* Line = static_cast<FbxLine*>(Attribute);
                const FbxVector4* CV = Line->GetControlPoints();
                TArray<FVector> OneLine;
                int32 Section = 0;
                for (int32 K = 0; K < Line->GetIndexArraySize() && CV; ++K)
                {
                    const int32 Index = Line->GetPointIndexAt(K);
                    if (Index < 0 || Index >= Line->GetControlPointsCount()) { continue; }
                    OneLine.Add(ConvertPoint(CV[Index], Mat));
                    bool bFinish = false;
                    for (int32 E = 0; E < Line->GetEndPointCount(); ++E)
                    {
                        if (Line->GetEndPointAt(E) == K) { bFinish = true; break; }
                    }
                    if (bFinish)
                    {
                        PopulateFromSamples(OneLine, false, Name + FString::Printf(TEXT("_%d"), ++Section), Output, true);
                        OneLine.Reset();
                    }
                }
                PopulateFromSamples(OneLine, false, Name + TEXT("_Tail"), Output, true);
            }
        }
        for (int32 I = 0; I < Node->GetChildCount(); ++I) { VisitNode(Node->GetChild(I), Output); }
    }
}

UObject* USplineFbxFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
    EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
    FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    FbxManager* Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG(LogSplineFbx, Error, TEXT("Autodesk FBX SDK failed to start."));
        return nullptr;
    }
    Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
    FbxImporter* Importer = FbxImporter::Create(Manager, "StrandFbx");
    const FTCHARToUTF8 Utf8Path(*Filename);
    if (!Importer->Initialize(Utf8Path.Get(), -1, Manager->GetIOSettings()))
    {
        UE_LOG(LogSplineFbx, Error, TEXT("FBX open error: %s"), UTF8_TO_TCHAR(Importer->GetStatus().GetErrorString()));
        Manager->Destroy();
        return nullptr;
    }
    FbxScene* Scene = FbxScene::Create(Manager, "SplineAssetScene");
    if (!Importer->Import(Scene))
    {
        UE_LOG(LogSplineFbx, Error, TEXT("FBX parse error: %s"), UTF8_TO_TCHAR(Importer->GetStatus().GetErrorString()));
        Manager->Destroy();
        return nullptr;
    }
    Importer->Destroy();
    FbxAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd,
                  FbxAxisSystem::eLeftHanded).ConvertScene(Scene);
    FbxSystemUnit::cm.ConvertScene(Scene);

    TArray<FSplineImportedCurve> Curves;
    VisitNode(Scene->GetRootNode(), Curves);
    Manager->Destroy();
    return SplineAssetImport::CommitCurves(InParent, InName, Flags, Curves, Filename);
}
