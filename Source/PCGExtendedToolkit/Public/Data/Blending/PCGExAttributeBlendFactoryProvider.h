﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDetailsData.h"
#include "PCGExPointsProcessor.h"
#include "PCGExProxyDataBlending.h"

#include "PCGExAttributeBlendFactoryProvider.generated.h"

namespace PCGExDataBlending
{
	class FProxyDataBlenderBase;

	const FName SourceConstantA = FName("Constant A");
	const FName SourceConstantB = FName("Constant B");
}

UENUM()
enum class EPCGExOperandAuthority : uint8
{
	A      = 0 UMETA(DisplayName = "Operand A", ToolTip="Type of operand A will drive the output type, thus converting operand B to the same type for the operation."),
	B      = 1 UMETA(DisplayName = "Operand B", ToolTip="Type of operand B will drive the output type, thus converting operand A to the same type for the operation."),
	Custom = 2 UMETA(DisplayName = "Custom", ToolTip="Select a specific type to output the result to."),
	Auto   = 3 UMETA(DisplayName = "Auto", ToolTip="Takes an informed guess based on settings & existing data. Usually works well, but not fool-proof."),
};

USTRUCT(BlueprintType)
struct PCGEXTENDEDTOOLKIT_API FPCGExAttributeBlendWeight
{
	GENERATED_BODY()

	FPCGExAttributeBlendWeight()
	{
		LocalWeightCurve.EditorCurveData.AddKey(0, 0);
		LocalWeightCurve.EditorCurveData.AddKey(1, 1);
	}

	~FPCGExAttributeBlendWeight() = default;

	/** Type of Weight */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType WeightInput = EPCGExInputValueType::Constant;

	/** Attribute to read weight value from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight (Attr)", EditCondition="WeightInput!=EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector WeightAttribute;

	/** Constant weight value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight", EditCondition="WeightInput==EPCGExInputValueType::Constant", EditConditionHides))
	double Weight = 0.5;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve the weight value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Weight Curve", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalWeightCurve;

	/** Curve the weight value will be remapped over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Weight Curve", EditCondition="!bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> WeightCurve = TSoftObjectPtr<UCurveFloat>(PCGEx::WeightDistributionLinear);

	const FRichCurve* ScoreCurveObj = nullptr;

	void Init();

	PCGEX_SETTING_VALUE_GET(Weight, double, WeightInput, WeightAttribute, Weight)
};

USTRUCT(BlueprintType)
struct PCGEXTENDEDTOOLKIT_API FPCGExAttributeBlendConfig
{
	GENERATED_BODY()

	FPCGExAttributeBlendConfig()
	{
		OperandA.Update("@Last");
		OperandB.Update("@Last");
		OutputTo.Update("Result");
	}

	~FPCGExAttributeBlendConfig() = default;

	UPROPERTY(meta=(PCG_NotOverridable, HideInDetailPanel))
	bool bRequiresWeight = false;

	/** Blendmode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExABBlendingType BlendMode = EPCGExABBlendingType::CopySource;

	/** Operand A. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Operand B. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandB;

	/** Weight settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bRequiresWeight", EditConditionHides, HideEditConditionToggle))
	FPCGExAttributeBlendWeight Weighting;

	/** Output to (AB blend). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OutputTo;

	/** Which type should be used for the output value. Only used if the output is not a point property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExOperandAuthority OutputType = EPCGExOperandAuthority::Auto;

	/** Which type should be used for the output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Type", EditCondition="OutputType==EPCGExOperandAuthority::Custom", EditConditionHides))
	EPCGMetadataTypes CustomType = EPCGMetadataTypes::Double;

	/** If enabled, new attributes will only be created for the duration of the blend, and properties will be restored to their original values once the blend is complete. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bTransactional = false;

	void Init();
};


/**
 * 
 */
class PCGEXTENDEDTOOLKIT_API FPCGExAttributeBlendOperation : public FPCGExOperation
{
public:
	FPCGExAttributeBlendConfig Config;

	TSharedPtr<PCGExData::FFacade> ConstantA;
	TSharedPtr<PCGExData::FFacade> ConstantB;

	int32 OpIdx = -1;
	TSharedPtr<TArray<TSharedPtr<FPCGExAttributeBlendOperation>>> SiblingOperations;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade);

	virtual void Blend(const int32 Index, FPCGPoint& Point)
	{
		Blender->Blend(Index, Point, Config.Weighting.ScoreCurveObj->Eval(Weight->Read(Index)));
	}

	virtual void Blend(const int32 SourceIndex, const FPCGPoint& SourcePoint, const int32 TargetIndex, FPCGPoint& TargetPoint)
	{
		Blender->Blend(SourceIndex, SourcePoint, TargetIndex, TargetPoint, Config.Weighting.ScoreCurveObj->Eval(Weight->Read(SourceIndex)));
	}

	virtual void Blend(const int32 SourceIndex, const FPCGPoint& SourcePoint, const int32 TargetIndex, FPCGPoint& TargetPoint, const double InWeight)
	{
		Blender->Blend(SourceIndex, SourcePoint, TargetIndex, TargetPoint, Config.Weighting.ScoreCurveObj->Eval(InWeight));
	}

	virtual void CompleteWork(TSet<TSharedPtr<PCGExData::FBufferBase>>& OutDisabledBuffers);

protected:
	bool CopyAndFixSiblingSelector(FPCGExContext* InContext, FPCGAttributePropertyInputSelector& Selector) const;

	TSharedPtr<PCGExDetails::TSettingValue<double>> Weight;
	TSharedPtr<PCGExDataBlending::FProxyDataBlenderBase> Blender;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Blending")
class PCGEXTENDEDTOOLKIT_API UPCGExAttributeBlendFactory : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	FPCGExAttributeBlendConfig Config;
	TSharedPtr<PCGExData::FFacade> ConstantA;
	TSharedPtr<PCGExData::FFacade> ConstantB;

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::Blending; }
	virtual TSharedPtr<FPCGExAttributeBlendOperation> CreateOperation(FPCGExContext* InContext) const;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return
			PCGExHelpers::HasDataOnPin(InContext, PCGExDataBlending::SourceConstantA) ||
			PCGExHelpers::HasDataOnPin(InContext, PCGExDataBlending::SourceConstantB);
	}

	virtual bool Prepare(FPCGExContext* InContext) override;

	virtual void RegisterAssetDependencies(FPCGExContext* InContext) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Blending")
class PCGEXTENDEDTOOLKIT_API UPCGExAttributeBlendFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(
		BlendOp, "BlendOp", "Creates a single Blend Operation node, to be used with the Attribute Blender.",
		PCGEX_FACTORY_NAME_PRIORITY)
	virtual FLinearColor GetNodeTitleColor() const override { return GetDefault<UPCGExGlobalSettings>()->NodeColorMisc; }
	//PCGEX_NODE_POINT_FILTER(PCGExPointFilter::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters, true)

#if PCGEX_ENGINE_VERSION > 503
	virtual bool CanUserEditTitle() const override { return false; }
#endif
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;

#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

	virtual FName GetMainOutputPin() const override { return PCGExDataBlending::OutputBlendingLabel; }
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Priority.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	int32 Priority;

	/** Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAttributeBlendConfig Config;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

protected:
	virtual bool IsCacheable() const override { return true; }
};

namespace PCGExDataBlending
{
	bool PrepareBlendOps(
		FPCGExContext* InContext,
		const TSharedRef<PCGExData::FFacade>& InDataFacade,
		const TArray<TObjectPtr<const UPCGExAttributeBlendFactory>>& InFactories,
		const TSharedPtr<TArray<TSharedPtr<FPCGExAttributeBlendOperation>>>& OutOperations);

	class FBlendOpsManager : public TSharedFromThis<FBlendOpsManager>
	{
	protected:
		TSharedPtr<PCGExData::FFacade> DataFacade;
		TSharedPtr<TArray<TSharedPtr<FPCGExAttributeBlendOperation>>> Operations;

	public:
		explicit FBlendOpsManager(const TSharedPtr<PCGExData::FFacade>& InDataFacade);
		bool Init(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExAttributeBlendFactory>>& InFactories);

		FORCEINLINE void Blend(const int32 Index, FPCGPoint& Point) const
		{
			for (int i = 0; i < Operations->Num(); i++) { (*(Operations->GetData() + i))->Blend(Index, Point); }
		}

		FORCEINLINE void Blend(const int32 SourceIndex, const FPCGPoint& SourcePoint, const int32 TargetIndex, FPCGPoint& TargetPoint) const
		{
			for (int i = 0; i < Operations->Num(); i++) { (*(Operations->GetData() + i))->Blend(SourceIndex, SourcePoint, TargetIndex, TargetPoint); }
		}

		FORCEINLINE void Blend(const int32 SourceIndex, const FPCGPoint& SourcePoint, const int32 TargetIndex, FPCGPoint& TargetPoint, const double InWeight) const
		{
			for (int i = 0; i < Operations->Num(); i++) { (*(Operations->GetData() + i))->Blend(SourceIndex, SourcePoint, TargetIndex, TargetPoint, InWeight); }
		}

		void Cleanup(FPCGExContext* InContext);
	};
}
