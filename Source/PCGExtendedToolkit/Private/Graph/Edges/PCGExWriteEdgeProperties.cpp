﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graph/Edges/PCGExWriteEdgeProperties.h"

#include "Data/Blending/PCGExMetadataBlender.h"

#define LOCTEXT_NAMESPACE "PCGExEdgesToPaths"
#define PCGEX_NAMESPACE WriteEdgeProperties

PCGExData::EIOInit UPCGExWriteEdgePropertiesSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::Forward; }
PCGExData::EIOInit UPCGExWriteEdgePropertiesSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::Duplicate; }

TArray<FPCGPinProperties> UPCGExWriteEdgePropertiesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (bWriteHeuristics) { PCGEX_PIN_FACTORIES(PCGExGraph::SourceHeuristicsLabel, "Heuristics that will be computed and written.", Required, {}) }
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(WriteEdgeProperties)

bool FPCGExWriteEdgePropertiesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExEdgesProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

	PCGEX_FOREACH_FIELD_EDGEEXTRAS(PCGEX_OUTPUT_VALIDATE_NAME)

	return true;
}

bool FPCGExWriteEdgePropertiesElement::ExecuteInternal(
	FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExWriteEdgePropertiesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(WriteEdgeProperties)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters<PCGExWriteEdgeProperties::FBatch>(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExWriteEdgeProperties::FBatch>& NewBatch)
			{
				if (Settings->bWriteHeuristics) { NewBatch->SetWantsHeuristics(true); }
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGEx::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}


namespace PCGExWriteEdgeProperties
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(TSharedPtr<PCGExMT::FTaskManager> InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExWriteEdgeProperties::Process);

		EdgeDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!FClusterProcessor::Process(InAsyncManager)) { return false; }

		if (!DirectionSettings.InitFromParent(ExecutionContext, GetParentBatch<FBatch>()->DirectionSettings, EdgeDataFacade))
		{
			return false;
		}

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = EdgeDataFacade;
			PCGEX_FOREACH_FIELD_EDGEEXTRAS(PCGEX_OUTPUT_INIT)
		}

		bSolidify = Settings->SolidificationAxis != EPCGExMinimalAxis::None;

		if (bSolidify)
		{
#define PCGEX_CREATE_LOCAL_AXIS_SET_CONST(_AXIS) if (Settings->bWriteRadius##_AXIS){\
			SolidificationRad##_AXIS = PCGExDetails::MakeSettingValue(Settings->Radius##_AXIS##Input, Settings->Radius##_AXIS##SourceAttribute, Settings->Radius##_AXIS##Constant);\
			if(!SolidificationRad##_AXIS->Init(Context, Settings->Radius##_AXIS##Source == EPCGExClusterComponentSource::Edge ? EdgeDataFacade : VtxDataFacade, false)){ return false; } }
			PCGEX_FOREACH_XYZ(PCGEX_CREATE_LOCAL_AXIS_SET_CONST)
#undef PCGEX_CREATE_LOCAL_AXIS_SET_CONST

			SolidificationLerp = Settings->GetValueSettingSolidificationLerp();
			if (!SolidificationLerp->Init(Context, EdgeDataFacade, false)) { return false; }
		}

		if (Settings->bEndpointsBlending)
		{
			MetadataBlender = MakeShared<PCGExDataBlending::FMetadataBlender>(const_cast<FPCGExBlendingDetails*>(&Settings->BlendingSettings));
			MetadataBlender->PrepareForData(EdgeDataFacade, VtxDataFacade, PCGExData::ESource::In, true, &PCGExGraph::ProtectedClusterAttributes);
		}

		StartWeight = FMath::Clamp(Settings->EndpointsWeights, 0, 1);
		EndWeight = 1 - StartWeight;

		StartParallelLoopForEdges();

		return true;
	}

	void FProcessor::PrepareSingleLoopScopeForEdges(const PCGExMT::FScope& Scope)
	{
		FClusterProcessor::PrepareSingleLoopScopeForEdges(Scope);
		EdgeDataFacade->Fetch(Scope);
	}

	void FProcessor::ProcessSingleEdge(const int32 EdgeIndex, PCGExGraph::FEdge& Edge, const PCGExMT::FScope& Scope)
	{
		DirectionSettings.SortEndpoints(Cluster.Get(), Edge);

		const PCGExCluster::FNode& StartNode = *Cluster->GetEdgeStart(Edge);
		const PCGExCluster::FNode& EndNode = *Cluster->GetEdgeEnd(Edge);

		double BlendWeightStart = StartWeight;
		double BlendWeightEnd = EndWeight;

		const FVector A = Cluster->GetPos(StartNode);
		const FVector B = Cluster->GetPos(EndNode);

		const FVector EdgeDirection = (A - B).GetSafeNormal();
		const double EdgeLength = FVector::Distance(A, B);

		PCGEX_OUTPUT_VALUE(EdgeDirection, Edge.PointIndex, EdgeDirection);
		PCGEX_OUTPUT_VALUE(EdgeLength, Edge.PointIndex, EdgeLength);

		if (Settings->bWriteHeuristics)
		{
			switch (Settings->HeuristicsMode)
			{
			case EPCGExHeuristicsWriteMode::EndpointsOrder:
				PCGEX_OUTPUT_VALUE(Heuristics, Edge.PointIndex, HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode));
				break;
			case EPCGExHeuristicsWriteMode::Smallest:
				PCGEX_OUTPUT_VALUE(
					Heuristics, Edge.PointIndex, FMath::Min(
						HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode),
						HeuristicsHandler->GetEdgeScore(EndNode, StartNode, Edge, EndNode, StartNode)));
				break;
			case EPCGExHeuristicsWriteMode::Highest:
				PCGEX_OUTPUT_VALUE(
					Heuristics, Edge.PointIndex, FMath::Max(
						HeuristicsHandler->GetEdgeScore(StartNode, EndNode, Edge, StartNode, EndNode),
						HeuristicsHandler->GetEdgeScore(EndNode, StartNode, Edge, EndNode, StartNode)));
				break;
			default: ;
			}
		}

		FPCGPoint& MutableTarget = EdgeDataFacade->Source->GetMutablePoint(Edge.PointIndex);

		auto MetadataBlend = [&]()
		{
			const PCGExData::FPointRef Target = EdgeDataFacade->Source->GetOutPointRef(Edge.PointIndex);
			MetadataBlender->PrepareForBlending(Target);
			MetadataBlender->Blend(Target, VtxDataFacade->Source->GetInPointRef(Edge.Start), Target, BlendWeightStart);
			MetadataBlender->Blend(Target, VtxDataFacade->Source->GetInPointRef(Edge.End), Target, BlendWeightEnd);
			MetadataBlender->CompleteBlending(Target, 2, BlendWeightStart + BlendWeightEnd);
		};

		if (bSolidify)
		{
			FRotator EdgeRot;
			FVector TargetBoundsMin = MutableTarget.BoundsMin;
			FVector TargetBoundsMax = MutableTarget.BoundsMax;

			FVector TargetScale = MutableTarget.Transform.GetScale3D();
			
			const FVector InvScale = FVector::One() / TargetScale;

			BlendWeightStart = FMath::Clamp(SolidificationLerp->Read(Edge.PointIndex), 0, 1);
			BlendWeightEnd = 1 - BlendWeightStart;
			
#define PCGEX_SOLIDIFY_DIMENSION(_AXIS)\
			if (Settings->SolidificationAxis == EPCGExMinimalAxis::_AXIS){\
				TargetBoundsMin._AXIS = (-EdgeLength * BlendWeightEnd) * InvScale._AXIS;\
				TargetBoundsMax._AXIS = (EdgeLength * BlendWeightStart) * InvScale._AXIS;\
			}else if(SolidificationRad##_AXIS){\
				double Rad = 0;\
				if (Settings->Radius##_AXIS##Source == EPCGExClusterComponentSource::Vtx) { Rad = FMath::Lerp(SolidificationRad##_AXIS->Read(Edge.Start), SolidificationRad##_AXIS->Read(Edge.End), BlendWeightStart); }\
				else { Rad = SolidificationRad##_AXIS->Read(Edge.PointIndex); }\
				TargetBoundsMin._AXIS = -Rad * InvScale._AXIS;\
				TargetBoundsMax._AXIS = Rad * InvScale._AXIS;\
			}
			
			PCGEX_FOREACH_XYZ(PCGEX_SOLIDIFY_DIMENSION)
#undef PCGEX_SOLIDIFY_DIMENSION

			switch (Settings->SolidificationAxis)
			{
			default:
			case EPCGExMinimalAxis::X:
				EdgeRot = FRotationMatrix::MakeFromX(EdgeDirection).Rotator();
				break;
			case EPCGExMinimalAxis::Y:
				EdgeRot = FRotationMatrix::MakeFromY(EdgeDirection).Rotator();
				break;
			case EPCGExMinimalAxis::Z:
				EdgeRot = FRotationMatrix::MakeFromZ(EdgeDirection).Rotator();
				break;
			}

			if (MetadataBlender) { MetadataBlend(); } // Blend first THEN apply bounds otherwise it gets overwritten

			MutableTarget.Transform = FTransform(EdgeRot, FMath::Lerp(B, A, BlendWeightEnd), TargetScale);

			MutableTarget.BoundsMin = TargetBoundsMin;
			MutableTarget.BoundsMax = TargetBoundsMax;
		}
		else if (Settings->bWriteEdgePosition)
		{
			BlendWeightStart = Settings->EdgePositionLerp;
			BlendWeightEnd = 1 - Settings->EdgePositionLerp;

			if (MetadataBlender) { MetadataBlend(); }

			MutableTarget.Transform.SetLocation(FMath::Lerp(B, A, Settings->EdgePositionLerp));
		}
		else
		{
			if (MetadataBlender) { MetadataBlend(); }
		}
	}

	void FProcessor::CompleteWork()
	{
		EdgeDataFacade->Write(AsyncManager);
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

		if (Settings->bEndpointsBlending)
		{
			Settings->BlendingSettings.RegisterBuffersDependencies(Context, VtxDataFacade, FacadePreloader, &PCGExGraph::ProtectedClusterAttributes);
		}

		DirectionSettings.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(WriteEdgeProperties)

		DirectionSettings = Settings->DirectionSettings;

		if (!DirectionSettings.Init(ExecutionContext, VtxDataFacade, Context->GetEdgeSortingRules()))
		{
			bIsBatchValid = false;
			return;
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
