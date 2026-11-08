// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "StrandSplineEditOperations.h"

// All operations are asset-local, non-mutating on failure, and independent of Slate.
// Error measurements use sampled 3D curve positions, never just anchor chords.
namespace StrandSplineCleanup
{
    struct FResult
    {
        TArray<FSplinePointData> Points;
        double SampledMaxDeviationCm = 0.0;
        int32 Removed = 0;
    };

    inline FVector EvalNativeSegment(const USplineComponent* Native, int32 Segment, double Alpha)
    {
        const int32 Count = Native->GetNumberOfSplinePoints();
        const FSplinePoint A = Native->GetSplinePointAt(Segment, ESplineCoordinateSpace::Local);
        const FSplinePoint B = Native->GetSplinePointAt((Segment + 1) % Count, ESplineCoordinateSpace::Local);
        if (A.Type == ESplinePointType::Constant) { return A.Position; }
        if (A.Type == ESplinePointType::Linear) { return FMath::Lerp(A.Position, B.Position, Alpha); }
        return FMath::CubicInterp(A.Position, A.LeaveTangent, B.Position, B.ArriveTangent, Alpha);
    }

    inline bool BakeNative(const TArray<FSplinePointData>& Source, const USplineComponent* Native,
        TArray<FSplinePointData>& Out)
    {
        if (!IsValid(Native) || Source.Num() < 2 || Source.Num() != Native->GetNumberOfSplinePoints()) { return false; }
        TArray<FSplinePointData> Baked = Source;
        for (int32 I = 0; I < Source.Num(); ++I)
        {
            FSplinePointData& Point = Baked[I];
            const FSplinePoint NativePoint = Native->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
            if (NativePoint.Position.ContainsNaN() || NativePoint.ArriveTangent.ContainsNaN() || NativePoint.LeaveTangent.ContainsNaN()) { return false; }
            if (Point.PointType == ESplinePointType::Curve || Point.PointType == ESplinePointType::CurveClamped)
            {
                Point.ArriveTangent = NativePoint.ArriveTangent;
                Point.LeaveTangent = NativePoint.LeaveTangent;
                Point.PointType = ESplinePointType::CurveCustomTangent;
                Point.HandleMode = EStrandBezierHandleMode::Free;
            }
        }
        Out = MoveTemp(Baked);
        return true;
    }

    struct FSample
    {
        double T = 0.0;
        FVector P = FVector::ZeroVector;
    };

    // Samples every ORIGINAL source segment in the interval. The normalized t
    // follows cumulative sampled arc length, not anchor indices.
    inline bool ReferenceSamples(const USplineComponent* Native, int32 Begin, int32 End,
        TArray<FSample>& Out, int32 SamplesPerSegment = 18)
    {
        if (!IsValid(Native) || SamplesPerSegment < 4) { return false; }
        const int32 Count = Native->GetNumberOfSplinePoints();
        if (Count < 2 || Begin < 0 || Begin >= Count || End < 0 || End >= Count || Begin == End) { return false; }
        const int32 SegmentCount = (End - Begin + Count) % Count;
        if (SegmentCount <= 0 || SegmentCount >= Count) { return false; }
        const bool bClosed = Native->IsClosedLoop();
        if (!bClosed && End <= Begin) { return false; }
        TArray<FSample> Samples;
        Samples.Reserve(SegmentCount * SamplesPerSegment + 1);
        double Distance = 0.0;
        for (int32 S = 0; S < SegmentCount; ++S)
        {
            const int32 Segment = (Begin + S) % Count;
            for (int32 K = S == 0 ? 0 : 1; K <= SamplesPerSegment; ++K)
            {
                const FVector Position = EvalNativeSegment(Native, Segment, static_cast<double>(K) / SamplesPerSegment);
                if (Position.ContainsNaN()) { return false; }
                if (!Samples.IsEmpty()) { Distance += FVector::Distance(Samples.Last().P, Position); }
                FSample& Item = Samples.AddDefaulted_GetRef();
                Item.T = Distance;
                Item.P = Position;
            }
        }
        if (Distance < 1.e-8) { return false; }
        for (FSample& Item : Samples) { Item.T /= Distance; }
        Out = MoveTemp(Samples);
        return true;
    }

    inline FVector EvaluateFit(const FVector& A, const FVector& M0,
        const FVector& B, const FVector& M1, double T)
    {
        return FMath::CubicInterp(A, M0, B, M1, T);
    }

    // Fits both Hermite derivatives by a 2x2 least-squares solve in 3D.
    // The sample residual is measured after fitting, and the caller rejects
    // candidates that exceed its tolerance. No control point is projected to
    // a chord: the ORIGINAL curved trajectory is the reference.
    inline bool FitInterval(const USplineComponent* Native, int32 Begin, int32 End,
        FVector& OutStartLeave, FVector& OutEndArrive, double& OutError)
    {
        TArray<FSample> Samples;
        if (!ReferenceSamples(Native, Begin, End, Samples)) { return false; }
        const FVector A = Samples[0].P;
        const FVector B = Samples.Last().P;
        double AA = 0., BB = 0., CC = 0.;
        FVector DA = FVector::ZeroVector, DB = FVector::ZeroVector;
        for (const FSample& Sample : Samples)
        {
            const double T = Sample.T, T2 = T*T, T3 = T2*T;
            const double H00 = 2*T3-3*T2+1, H01 = -2*T3+3*T2;
            const double H10 = T3-2*T2+T, H11 = T3-T2;
            const FVector R = Sample.P - H00*A - H01*B;
            AA += H10*H10; BB += H10*H11; CC += H11*H11;
            DA += R*H10; DB += R*H11;
        }
        const double Det = AA*CC - BB*BB;
        if (!FMath::IsFinite(Det) || Det < 1.e-12) { return false; }
        const FVector M0 = (DA*CC-DB*BB)/Det;
        const FVector M1 = (DB*AA-DA*BB)/Det;
        if (M0.ContainsNaN() || M1.ContainsNaN() || M0.Size() > 1.e9 || M1.Size() > 1.e9) { return false; }
        double MaxError = 0.;
        for (int32 I = 0; I < Samples.Num(); ++I)
        {
            const FVector P = EvaluateFit(A, M0, B, M1, Samples[I].T);
            MaxError = FMath::Max(MaxError, FVector::Distance(P, Samples[I].P));
            if (I > 0) // Include intermediate samples in error measurement.
            {
                const double MidT = 0.5 * (Samples[I-1].T + Samples[I].T);
                const FVector Reference = FMath::Lerp(Samples[I-1].P, Samples[I].P, 0.5);
                // Polyline interpolation is only an approximation of the source.
                // Its midpoint is not counted toward tolerance, since it could
                // penalize an unchanged highly curved cubic.
                (void)MidT;
                (void)Reference;
            }
        }
        OutStartLeave = M0;
        OutEndArrive = M1;
        OutError = MaxError;
        return true;
    }

    inline bool Simplify(const TArray<FSplinePointData>& Source, const USplineComponent* Native,
        double ToleranceCm, bool bProtectCorners, const TArray<int32>* OnlyRemove,
        FResult& OutResult, int32 MaximumRemovals = MAX_int32)
    {
        const int32 Count = Source.Num();
        if (!IsValid(Native) || Count < 3 || Count > 512 || Native->GetNumberOfSplinePoints() != Count ||
            !FMath::IsFinite(ToleranceCm) || ToleranceCm <= 0.0 || ToleranceCm > 100000.0) { return false; }
        const bool bClosed = Native->IsClosedLoop();
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Source, Native, Baked)) { return false; }
        TSet<int32> Protected;
        Protected.Add(0); // Keep the original seam for deterministic closed paths.
        if (!bClosed) { Protected.Add(Count-1); }
        if (bProtectCorners)
        {
            for (int32 I : StrandSplineEditOperations::FindSharpCorners(Native, 30.0)) { Protected.Add(I); }
            for (int32 I = 0; I < Count; ++I)
            {
                if (Source[I].PointType == ESplinePointType::Constant) { Protected.Add(I); }
            }
        }
        TSet<int32> Allowed;
        if (OnlyRemove)
        {
            for (int32 I : *OnlyRemove) { Allowed.Add(I); }
        }
        TArray<int32> Indices;
        Indices.Reserve(Count);
        for (int32 I = 0; I < Count; ++I) { Indices.Add(I); }
        TArray<FSplinePointData> Working = MoveTemp(Baked);
        double LargestError = 0.0;
        int32 Removed = 0;
        while (Indices.Num() > (bClosed ? 3 : 2) && Removed < MaximumRemovals)
        {
            int32 Best = INDEX_NONE;
            double BestError = ToleranceCm;
            FVector BestLeave = FVector::ZeroVector, BestArrive = FVector::ZeroVector;
            for (int32 K = bClosed ? 0 : 1; K < (bClosed ? Indices.Num() : Indices.Num()-1); ++K)
            {
                if (Protected.Contains(Indices[K]) || (OnlyRemove && !Allowed.Contains(Indices[K]))) { continue; }
                const int32 Prev = (K-1+Indices.Num())%Indices.Num();
                const int32 Next = (K+1)%Indices.Num();
                FVector Leave, Arrive;
                double Error = 0.0;
                if (!FitInterval(Native, Indices[Prev], Indices[Next], Leave, Arrive, Error)) { continue; }
                if (Error <= BestError + 1.e-7)
                {
                    Best = K; BestError = Error;
                    BestLeave = Leave; BestArrive = Arrive;
                }
            }
            if (Best == INDEX_NONE) { break; }
            const int32 Prev = (Best-1+Indices.Num())%Indices.Num();
            const int32 Next = (Best+1)%Indices.Num();
            Working[Prev].LeaveTangent = BestLeave;
            Working[Prev].PointType = ESplinePointType::CurveCustomTangent;
            Working[Prev].HandleMode = EStrandBezierHandleMode::Free;
            Working[Next].ArriveTangent = BestArrive;
            // Do not change the successor's segment interpolation; the new
            // segment type is carried by the predecessor.
            Working.RemoveAt(Best);
            Indices.RemoveAt(Best);
            LargestError = FMath::Max(LargestError, BestError);
            ++Removed;
        }
        if (OnlyRemove && Removed != Allowed.Num()) { return false; }
        FResult Result;
        Result.Points = MoveTemp(Working);
        Result.Removed = Removed;
        Result.SampledMaxDeviationCm = LargestError;
        OutResult = MoveTemp(Result);
        return true;
    }

    inline bool Dissolve(const TArray<FSplinePointData>& Source, const USplineComponent* Native,
        const TArray<int32>& Selected, double ToleranceCm, FResult& Result)
    {
        if (Selected.IsEmpty()) { return false; }
        return Simplify(Source, Native, ToleranceCm, false, &Selected, Result);
    }

    inline bool RemoveDegenerate(const TArray<FSplinePointData>& Source, const USplineComponent* Native,
        double DistanceThresholdCm, double CurveToleranceCm, FResult& Result)
    {
        if (!IsValid(Native) || DistanceThresholdCm <= 0 || Source.Num() < 3) { return false; }
        TArray<int32> Duplicates;
        for (int32 I = 1; I < Source.Num() - (Native->IsClosedLoop() ? 0 : 1); ++I)
        {
            const FVector& P = Source[I].LocalPosition;
            if (FVector::Distance(P, Source[I-1].LocalPosition) <= DistanceThresholdCm)
            {
                Duplicates.Add(I);
            }
        }
        if (Duplicates.IsEmpty()) { return false; }
        // Partial removals are permitted for repair, unlike explicit dissolve.
        return Simplify(Source, Native, CurveToleranceCm, false, &Duplicates, Result, Duplicates.Num());
    }

    inline TArray<int32> FindDuplicateAnchors(const TArray<FSplinePointData>& Points,
        bool bClosed, double DistanceCm)
    {
        TArray<int32> Out;
        if (Points.Num() < 2 || !FMath::IsFinite(DistanceCm) || DistanceCm < 0) { return Out; }
        for (int32 I = 1; I < Points.Num(); ++I)
        {
            if (FVector::Distance(Points[I].LocalPosition, Points[I-1].LocalPosition) <= DistanceCm)
            { Out.Add(I); }
        }
        if (bClosed && FVector::Distance(Points.Last().LocalPosition, Points[0].LocalPosition) <= DistanceCm)
        { Out.AddUnique(Points.Num()-1); }
        return Out;
    }

    inline TArray<int32> SelectBySegmentLength(const USplineComponent* Native, double ThresholdCm, bool bShorter)
    {
        TArray<int32> Out;
        if (!IsValid(Native) || ThresholdCm < 0 || !FMath::IsFinite(ThresholdCm)) { return Out; }
        const int32 Count = Native->GetNumberOfSplinePoints();
        if (Count < 2) { return Out; }
        const int32 Segments = Count - (Native->IsClosedLoop() ? 0 : 1);
        for (int32 I = 0; I < Segments; ++I)
        {
            double Length = 0.0;
            FVector Last = EvalNativeSegment(Native, I, 0.0);
            for (int32 S = 1; S <= 24; ++S)
            {
                const FVector Next = EvalNativeSegment(Native, I, static_cast<double>(S)/24);
                Length += FVector::Distance(Last, Next);
                Last = Next;
            }
            if (bShorter ? Length < ThresholdCm : Length > ThresholdCm)
            {
                Out.AddUnique(I);
                Out.AddUnique((I+1)%Count);
            }
        }
        return Out;
    }

    inline bool SetContinuity(TArray<FSplinePointData>& Points, const USplineComponent* Native,
        const TArray<int32>& Selected, bool bC1)
    {
        if (!IsValid(Native) || Points.Num() != Native->GetNumberOfSplinePoints() || Selected.IsEmpty()) { return false; }
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Points, Native, Baked)) { return false; }
        bool bChanged = false;
        const int32 Count = Points.Num();
        for (int32 I : Selected)
        {
            if (!Baked.IsValidIndex(I) || (!Native->IsClosedLoop() && (I == 0 || I == Count-1))) { continue; }
            FSplinePointData& P = Baked[I];
            const int32 PrevIndex = (I-1+Count)%Count;
            if (Baked[PrevIndex].PointType == ESplinePointType::Constant ||
                P.PointType == ESplinePointType::Constant) { continue; }
            // Native Linear interpolation ignores the selected incoming
            // handle. Convert the preceding straight span to an equivalent
            // cubic before smoothing the junction, otherwise G1/C1 would
            // appear to do nothing at ordinary sharp polyline corners.
            if (Baked[PrevIndex].PointType == ESplinePointType::Linear)
            {
                const FVector Chord = P.LocalPosition - Baked[PrevIndex].LocalPosition;
                Baked[PrevIndex].LeaveTangent = Chord;
                Baked[PrevIndex].PointType = ESplinePointType::CurveCustomTangent;
                Baked[PrevIndex].HandleMode = EStrandBezierHandleMode::Free;
                P.ArriveTangent = Chord;
            }
            FVector In = P.ArriveTangent;
            FVector Out = P.LeaveTangent;
            if (In.IsNearlyZero()) { In = P.LocalPosition - Baked[(I-1+Count)%Count].LocalPosition; }
            if (Out.IsNearlyZero()) { Out = Baked[(I+1)%Count].LocalPosition - P.LocalPosition; }
            FVector Direction = (In.GetSafeNormal()+Out.GetSafeNormal()).GetSafeNormal();
            if (Direction.IsNearlyZero()) { Direction = !Out.IsNearlyZero() ? Out.GetSafeNormal() : In.GetSafeNormal(); }
            if (Direction.IsNearlyZero()) { continue; }
            double LenIn = In.Size(), LenOut = Out.Size();
            if (bC1) { LenIn = LenOut = (LenIn+LenOut)*0.5; }
            P.ArriveTangent = Direction*LenIn;
            P.LeaveTangent = Direction*LenOut;
            P.PointType = ESplinePointType::CurveCustomTangent;
            P.HandleMode = bC1 ? EStrandBezierHandleMode::Mirrored : EStrandBezierHandleMode::Aligned;
            bChanged = true;
        }
        if (!bChanged) { return false; }
        Points = MoveTemp(Baked);
        return true;
    }

    inline bool NormalizeSelectedTangents(TArray<FSplinePointData>& Points, const USplineComponent* Native,
        const TArray<int32>& Selected, double LengthCm)
    {
        if (!IsValid(Native) || Points.Num() != Native->GetNumberOfSplinePoints() ||
            Selected.IsEmpty() || LengthCm <= 0 || !FMath::IsFinite(LengthCm)) { return false; }
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Points, Native, Baked)) { return false; }
        bool bChanged = false;
        for (int32 I : Selected)
        {
            if (!Baked.IsValidIndex(I)) { continue; }
            FSplinePointData& P = Baked[I];
            bool bPointChanged = false;
            if (!P.ArriveTangent.IsNearlyZero()) { P.ArriveTangent = P.ArriveTangent.GetSafeNormal()*LengthCm; bPointChanged = true; }
            if (!P.LeaveTangent.IsNearlyZero()) { P.LeaveTangent = P.LeaveTangent.GetSafeNormal()*LengthCm; bPointChanged = true; }
            if (bPointChanged)
            {
                P.PointType = ESplinePointType::CurveCustomTangent;
                P.HandleMode = EStrandBezierHandleMode::Free;
                bChanged = true;
            }
        }
        if (!bChanged) { return false; }
        Points = MoveTemp(Baked);
        return true;
    }

    inline bool RecalculateSelectedTangents(TArray<FSplinePointData>& Points,
        const USplineComponent* Native, const TArray<int32>& Selected)
    {
        if (!IsValid(Native) || Points.Num() < 2 || Points.Num() != Native->GetNumberOfSplinePoints() || Selected.IsEmpty()) { return false; }
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Points, Native, Baked)) { return false; }
        const int32 Count = Baked.Num();
        const bool bClosed = Native->IsClosedLoop();
        bool bChanged = false;
        for (int32 I : Selected)
        {
            if (!Baked.IsValidIndex(I)) { continue; }
            FSplinePointData& Point = Baked[I];
            const FVector Position = Point.LocalPosition;
            const FVector Previous = Baked[(I-1+Count)%Count].LocalPosition;
            const FVector Next = Baked[(I+1)%Count].LocalPosition;
            FVector Derivative;
            if (!bClosed && I == 0) { Derivative = Next-Position; }
            else if (!bClosed && I == Count-1) { Derivative = Position-Previous; }
            else { Derivative = 0.5*(Next-Previous); }
            if (Derivative.IsNearlyZero()) { continue; }
            Point.ArriveTangent = Derivative;
            Point.LeaveTangent = Derivative;
            Point.PointType = ESplinePointType::CurveCustomTangent;
            Point.HandleMode = EStrandBezierHandleMode::Mirrored;
            bChanged = true;
        }
        if (!bChanged) { return false; }
        Points = MoveTemp(Baked);
        return true;
    }

    // Weld nearby endpoints of ONE open path into a closed loop. Preserve
    // evaluated tangents, keep the first anchor as the seam, and remove the
    // redundant final anchor. The gap must be explicitly within tolerance.
    inline bool WeldLoopEndpoints(TArray<FSplinePointData>& Points, bool& bClosed,
        const USplineComponent* Native, double MaximumGapCm)
    {
        if (bClosed || Points.Num() < 3 || !IsValid(Native) ||
            Native->GetNumberOfSplinePoints() != Points.Num() ||
            MaximumGapCm < 0 || !FMath::IsFinite(MaximumGapCm) ||
            FVector::Distance(Points[0].LocalPosition, Points.Last().LocalPosition) > MaximumGapCm)
        { return false; }
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Points, Native, Baked)) { return false; }
        Baked[0].ArriveTangent = Baked.Last().ArriveTangent;
        Baked.RemoveAt(Baked.Num()-1);
        Points = MoveTemp(Baked);
        bClosed = true;
        return true;
    }

    // De Casteljau subdivisions are exact in parameter space. Distance mode
    // estimates arc-length fractions from 128 samples per source segment.
    inline bool SubdivideExact(const TArray<FSplinePointData>& Source, const USplineComponent* Native,
        int32 Cuts, bool bDistanceUniform, TArray<FSplinePointData>& OutPoints)
    {
        const int32 Count = Source.Num();
        if (!IsValid(Native) || Count < 2 || Count != Native->GetNumberOfSplinePoints() ||
            Cuts < 1 || Cuts > 64 || static_cast<int64>(Count)*(Cuts+1) > 16384) { return false; }
        TArray<FSplinePointData> Baked;
        if (!BakeNative(Source, Native, Baked)) { return false; }
        const bool bClosed = Native->IsClosedLoop();
        TArray<FSplinePointData> Result;
        Result.Reserve(Count + (Count-(bClosed?0:1))*Cuts);
        Result.Add(Baked[0]);
        const int32 Segments = Count - (bClosed ? 0 : 1);
        for (int32 I = 0; I < Segments; ++I)
        {
            const int32 J = (I+1)%Count;
            const FSplinePoint A = Native->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
            const FSplinePoint B = Native->GetSplinePointAt(J, ESplineCoordinateSpace::Local);
            TArray<double> Fractions;
            Fractions.Reserve(Cuts);
            if (bDistanceUniform)
            {
                TArray<double> Lengths;
                Lengths.Add(0.0);
                FVector Prev = EvalNativeSegment(Native, I, 0);
                for (int32 S = 1; S <= 128; ++S)
                {
                    const FVector Next = EvalNativeSegment(Native, I, static_cast<double>(S)/128);
                    Lengths.Add(Lengths.Last() + FVector::Distance(Prev, Next));
                    Prev = Next;
                }
                const double TotalLength = Lengths.Last();
                for (int32 C = 1; C <= Cuts; ++C)
                {
                    const double Target = TotalLength*C/(Cuts+1);
                    int32 Index = 1;
                    while (Index < 128 && Lengths[Index] < Target) { ++Index; }
                    const double Span = Lengths[Index]-Lengths[Index-1];
                    const double Lerp = Span > 1.e-12 ? (Target-Lengths[Index-1])/Span : 0.0;
                    Fractions.Add((Index-1+Lerp)/128.0);
                }
            }
            else { for (int32 C = 1; C <= Cuts; ++C) { Fractions.Add(static_cast<double>(C)/(Cuts+1)); } }
            FVector R0 = A.Position;
            FVector R1 = A.Position + A.LeaveTangent/3.0;
            FVector R2 = B.Position - B.ArriveTangent/3.0;
            FVector R3 = B.Position;
            if (A.Type == ESplinePointType::Linear)
            { R1 = FMath::Lerp(R0, R3, 1.0/3.0); R2 = FMath::Lerp(R0, R3, 2.0/3.0); }
            if (A.Type == ESplinePointType::Constant) { R1 = R2 = R3 = R0; }
            double PreviousFraction = 0.0;
            for (int32 C = 0; C < Cuts; ++C)
            {
                // Split only at interior parameters. Splitting at 1.0
                // degenerates the final control polygon and destroys the
                // original endpoint derivative.
                const double GlobalT = Fractions[C];
                if (!FMath::IsFinite(GlobalT) || GlobalT <= PreviousFraction || GlobalT >= 1.0)
                { return false; }
                const double T = (GlobalT - PreviousFraction) / (1.0 - PreviousFraction);
                const FVector Q0 = FMath::Lerp(R0, R1, T);
                const FVector Q1 = FMath::Lerp(R1, R2, T);
                const FVector Q2 = FMath::Lerp(R2, R3, T);
                const FVector Z0 = FMath::Lerp(Q0, Q1, T);
                const FVector Z1 = FMath::Lerp(Q1, Q2, T);
                const FVector PointAt = FMath::Lerp(Z0, Z1, T);

                // The LEFT cubic is [R0, Q0, Z0, PointAt]. The right cubic
                // [PointAt, Z1, Q2, R3] becomes the remaining span.
                FSplinePointData& Start = Result.Last();
                Start.LeaveTangent = 3.0 * (Q0 - R0);
                Start.PointType = A.Type == ESplinePointType::Constant ? ESplinePointType::Constant :
                    A.Type == ESplinePointType::Linear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
                Start.HandleMode = EStrandBezierHandleMode::Free;

                FSplinePointData Inserted = Baked[I];
                Inserted.LocalPosition = PointAt;
                Inserted.ArriveTangent = 3.0 * (PointAt - Z0);
                Inserted.LeaveTangent = FVector::ZeroVector; // Filled by next piece.
                Inserted.PointType = A.Type == ESplinePointType::Constant ? ESplinePointType::Constant :
                    A.Type == ESplinePointType::Linear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
                Inserted.HandleMode = EStrandBezierHandleMode::Free;
                Inserted.LocalRotation = FQuat::Slerp(Baked[I].LocalRotation.Quaternion(),
                    Baked[J].LocalRotation.Quaternion(), GlobalT).Rotator();
                Inserted.LocalScale = FMath::Lerp(Baked[I].LocalScale, Baked[J].LocalScale, GlobalT);
                Result.Add(MoveTemp(Inserted));

                R0 = PointAt;
                R1 = Z1;
                R2 = Q2;
                PreviousFraction = GlobalT;
            }

            // Append the unsplit remainder directly. Preserve the successor
            // anchor's ORIGINAL outgoing interpolation and attributes; only
            // its incoming tangent belongs to the subdivided predecessor.
            FSplinePointData& LastStart = Result.Last();
            LastStart.LeaveTangent = 3.0 * (R1 - R0);
            LastStart.PointType = A.Type == ESplinePointType::Constant ? ESplinePointType::Constant :
                A.Type == ESplinePointType::Linear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
            LastStart.HandleMode = EStrandBezierHandleMode::Free;
            FSplinePointData EndPoint = Baked[J];
            EndPoint.ArriveTangent = 3.0 * (R3 - R2);
            if (bClosed && I == Count - 1)
            {
                // The seam is represented by the original first anchor,
                // never a duplicated endpoint. Keep its outgoing data.
                Result[0].ArriveTangent = EndPoint.ArriveTangent;
            }
            else
            {
                Result.Add(MoveTemp(EndPoint));
            }
        }
        for (const FSplinePointData& P : Result)
        { if (P.LocalPosition.ContainsNaN() || P.ArriveTangent.ContainsNaN() || P.LeaveTangent.ContainsNaN()) { return false; } }
        OutPoints = MoveTemp(Result);
        return true;
    }
}
