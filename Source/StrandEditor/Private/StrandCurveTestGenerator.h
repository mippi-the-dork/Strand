// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

namespace StrandCurveTestGenerator
{
    /** Write two native curve-only Alembic files using Unreal's bundled SDK. */
    bool Generate(FString& OutDirectory, FString& OutError);
}
