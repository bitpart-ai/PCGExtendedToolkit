﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExInstancedFactory.h"
#include "PCGExOperation.h"


#include "Graph/PCGExCluster.h"
#include "Graph/Pathfinding/Heuristics/PCGExHeuristics.h"
#include "UObject/Object.h"
#include "PCGExSearchOperation.generated.h"

namespace PCGExPathfinding
{
	struct FExtraWeights;
}

class FPCGExHeuristicOperation;

namespace PCGExCluster
{
	class FCluster;
}

/**
 * 
 */
UCLASS(Abstract)
class PCGEXTENDEDTOOLKIT_API UPCGExSearchOperation : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	PCGExCluster::FCluster* Cluster = nullptr;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual void PrepareForCluster(PCGExCluster::FCluster* InCluster);
	virtual bool ResolveQuery(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		const TSharedPtr<PCGExHeuristics::FHeuristicsHandler>& Heuristics, const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback = nullptr) const;

	/** */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bEarlyExit = true;
};
