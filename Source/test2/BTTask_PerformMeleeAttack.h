// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PerformMeleeAttack.generated.h"

UCLASS()
class TEST2_API UBTTask_PerformMeleeAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PerformMeleeAttack();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
