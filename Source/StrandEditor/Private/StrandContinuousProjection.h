#pragma once

#include "CoreMinimal.h"

// Adaptive, side-effect-free subdivision planner. Evaluate(t) returns the
// projected *local* curve position at t in [0,1]. A fixed spacing criterion
// catches small surface features even when midpoint deviation is low.
namespace StrandContinuousProjection
{
    struct FSample
    {
        double T = 0.0;
        FVector Position = FVector::ZeroVector;
    };

    // Best-effort planner. The point budget limits output density, not whether
    // a valid preview can be produced. Refine the most under-resolved interval
    // first, instead of exhausting the budget at one end of a span.
    inline bool BuildSegment(const FVector& Start, const FVector& End,
        double SegmentLength, double MaxSpacing, double Tolerance, int32 MaxInserted,
        TFunctionRef<FVector(double)> Evaluate, TArray<FSample>& OutMidpoints,
        bool* OutResolutionLimited = nullptr)
    {
        OutMidpoints.Reset();
        if (OutResolutionLimited) { *OutResolutionLimited = false; }
        if (MaxInserted < 0 || !FMath::IsFinite(SegmentLength) || SegmentLength < 0.0 ||
            !FMath::IsFinite(MaxSpacing) || MaxSpacing <= 0.0 ||
            !FMath::IsFinite(Tolerance) || Tolerance <= 0.0 || Start.ContainsNaN() || End.ContainsNaN())
        { return false; }

        struct FInterval
        {
            double A, B;
            FVector P0, P1, Midpoint;
            int32 Depth = 0;
            double Priority = 0.0;
        };
        TArray<FInterval> Pending;
        auto AddInterval = [&](double A, double B, const FVector& P0,
            const FVector& P1, int32 Depth) -> bool
        {
            const FVector Mid = Evaluate((A + B) * 0.5);
            if (Mid.ContainsNaN()) { return false; }
            const double Error = FVector::Distance(Mid, (P0 + P1) * 0.5);
            // Accuracy is the first priority when points are scarce. Previously a
            // long, nearly flat interval could beat a short interval with
            // significant surface deviation. Spacing is the fallback once
            // the measured deviation is within tolerance.
            const double SpacingDemand = SegmentLength * (B - A) / MaxSpacing;
            const double ErrorDemand = Error / Tolerance;
            const double Priority = ErrorDemand > 1.0
                ? 1000.0 + ErrorDemand : SpacingDemand;
            Pending.Add({A, B, P0, P1, Mid, Depth, Priority});
            return true;
        };
        if (!AddInterval(0.0, 1.0, Start, End, 0)) { return false; }
        bool bLimited = false;
        while (!Pending.IsEmpty())
        {
            int32 BestIndex = INDEX_NONE;
            double BestPriority = 1.0;
            bool bDepthLimited = false;
            for (int32 I = 0; I < Pending.Num(); ++I)
            {
                const FInterval& Interval = Pending[I];
                if (Interval.Priority > 1.0 && Interval.Depth >= 12) { bDepthLimited = true; }
                if (Interval.Priority > BestPriority && Interval.Depth < 12)
                {
                    BestIndex = I;
                    BestPriority = Interval.Priority;
                }
            }
            if (BestIndex == INDEX_NONE)
            {
                bLimited = bDepthLimited;
                break;
            }
            if (OutMidpoints.Num() >= MaxInserted)
            {
                bLimited = true;
                break;
            }
            const FInterval Chosen = Pending[BestIndex];
            const double M = (Chosen.A + Chosen.B) * 0.5;
            Pending.RemoveAtSwap(BestIndex);
            if (!AddInterval(Chosen.A, M, Chosen.P0, Chosen.Midpoint, Chosen.Depth + 1) ||
                !AddInterval(M, Chosen.B, Chosen.Midpoint, Chosen.P1, Chosen.Depth + 1))
            {
                OutMidpoints.Reset();
                return false;
            }
            OutMidpoints.Add({M, Chosen.Midpoint});
        }
        OutMidpoints.Sort([](const FSample& A, const FSample& B) { return A.T < B.T; });
        if (OutResolutionLimited) { *OutResolutionLimited = bLimited; }
        return true;
    }

    // Estimate the *actual projected surface* complexity of a span using
    // three interior samples. Midpoint-only measurement misses symmetric
    // peaks/troughs and can mistakenly classify a complex span as flat.
    // Sample scores are dimensionless, so different-length spans compete
    // fairly for a fixed point budget. Spacing contributes a baseline need,
    // but large deviations receive the higher priority.
    inline double EstimateSpanDemand(const FVector& Start, const FVector& End,
        double Length, double MaxSpacing, double Tolerance,
        const FVector& Quarter, const FVector& Mid, const FVector& ThreeQuarter)
    {
        if (Start.ContainsNaN() || End.ContainsNaN() || Quarter.ContainsNaN() ||
            Mid.ContainsNaN() || ThreeQuarter.ContainsNaN() ||
            !FMath::IsFinite(Length) || !FMath::IsFinite(MaxSpacing) ||
            !FMath::IsFinite(Tolerance) || MaxSpacing <= 0.0 || Tolerance <= 0.0)
        { return 0.0; }
        const double E1 = FVector::Distance(Quarter, FMath::Lerp(Start, End, 0.25));
        const double E2 = FVector::Distance(Mid, (Start + End) * 0.5);
        const double E3 = FVector::Distance(ThreeQuarter, FMath::Lerp(Start, End, 0.75));
        const double ErrorDemand = FMath::Max3(E1, E2, E3) / Tolerance;
        // A short smooth span needs little budget. An uneven span must
        // outrank an equally long flat span, even after the spacing cap.
        const double SpacingNeed = FMath::Max(0.0, Length / MaxSpacing - 1.0);
        return FMath::Max(0.01, 0.5 * SpacingNeed + 4.0 * ErrorDemand);
    }

    // Preassign fair point budgets by measured need rather than raw length.
    // Original anchors are outside this budget. Allocate a minimum point to
    // every eligible span when possible, then use largest-remainder rounding.
    inline void AllocateSpanBudgets(const TArray<double>& Demands, int32 Budget,
        TArray<int32>& OutAllocations)
    {
        OutAllocations.Init(0, Demands.Num());
        Budget = FMath::Max(0, Budget);
        int32 Eligible = 0;
        double Sum = 0.0;
        for (double Demand : Demands)
        {
            if (Demand > 0.0 && FMath::IsFinite(Demand))
            {
                ++Eligible;
                Sum += Demand;
            }
        }
        if (Eligible == 0 || Budget == 0) { return; }
        // Guarantee one anchor per eligible span where budget permits.
        if (Budget >= Eligible)
        {
            for (int32 I = 0; I < Demands.Num(); ++I)
            {
                if (Demands[I] > 0.0 && FMath::IsFinite(Demands[I]))
                { OutAllocations[I] = 1; }
            }
            Budget -= Eligible;
        }
        if (Budget == 0) { return; }
        TArray<double> Fractions;
        Fractions.Init(-1.0, Demands.Num());
        int32 Allocated = 0;
        for (int32 I = 0; I < Demands.Num(); ++I)
        {
            if (Demands[I] <= 0.0 || !FMath::IsFinite(Demands[I])) { continue; }
            const double Exact = Budget * (Demands[I] / Sum);
            const int32 Whole = FMath::FloorToInt(Exact);
            OutAllocations[I] += Whole;
            Allocated += Whole;
            Fractions[I] = Exact - Whole;
        }
        for (int32 Remainder = Budget - Allocated; Remainder > 0; --Remainder)
        {
            int32 BestIndex = INDEX_NONE;
            double BestFraction = -1.0;
            for (int32 I = 0; I < Fractions.Num(); ++I)
            {
                if (Fractions[I] > BestFraction)
                { BestFraction = Fractions[I]; BestIndex = I; }
            }
            if (BestIndex == INDEX_NONE) { break; }
            ++OutAllocations[BestIndex];
            Fractions[BestIndex] = -1.0;
        }
    }

    // Reconstruct smooth Hermite tangents using neighboring projected positions.
    // Clamp by the shorter neighboring edge, so sharp folds cannot overshoot far
    // off the sampled surface. Tangents are derivatives per native spline input key.
    inline FVector TangentAt(const FVector& Previous, const FVector& Current, const FVector& Next)
    {
        const double Before = FVector::Distance(Previous, Current);
        const double After = FVector::Distance(Current, Next);
        if (Before <= UE_SMALL_NUMBER || After <= UE_SMALL_NUMBER) { return FVector::ZeroVector; }
        const FVector Direction = (Next - Previous).GetSafeNormal();
        return Direction * FMath::Min((Before + After) * 0.5, FMath::Min(Before, After) * 1.5);
    }
}
