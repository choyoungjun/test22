// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_PerformMeleeAttack.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

UBTTask_PerformMeleeAttack::UBTTask_PerformMeleeAttack()
{
	NodeName = TEXT("Perform Melee Attack");
}

EBTNodeResult::Type UBTTask_PerformMeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AEnemyAIController* AIController = Cast<AEnemyAIController>(OwnerComp.GetAIOwner());
	AEnemyCharacter* EnemyCharacter = AIController ? Cast<AEnemyCharacter>(AIController->GetPawn()) : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* TargetActor = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(AEnemyAIController::TargetActorKey)) : nullptr;
	if (!EnemyCharacter || !TargetActor)
	{
		return EBTNodeResult::Failed;
	}

	const float DistanceToTarget = FVector::Dist(EnemyCharacter->GetActorLocation(), TargetActor->GetActorLocation());
	if (DistanceToTarget > EnemyCharacter->AttackRange)
	{
		Blackboard->SetValueAsBool(AEnemyAIController::IsInAttackRangeKey, false);
		return EBTNodeResult::Failed;
	}

	UGameplayStatics::ApplyDamage(TargetActor, EnemyCharacter->AttackDamage, AIController, EnemyCharacter, nullptr);
	return EBTNodeResult::Succeeded;
}
