// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_FindPatrolLocation.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "NavigationSystem.h"

UBTTask_FindPatrolLocation::UBTTask_FindPatrolLocation()
{
	NodeName = TEXT("Find Patrol Location");
}

EBTNodeResult::Type UBTTask_FindPatrolLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AEnemyAIController* AIController = Cast<AEnemyAIController>(OwnerComp.GetAIOwner());
	AEnemyCharacter* EnemyCharacter = AIController ? Cast<AEnemyCharacter>(AIController->GetPawn()) : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!AIController || !EnemyCharacter || !Blackboard)
	{
		return EBTNodeResult::Failed;
	}

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSystem)
	{
		return EBTNodeResult::Failed;
	}

	FNavLocation PatrolLocation;
	const FVector Origin = Blackboard->GetValueAsVector(AEnemyAIController::SpawnLocationKey);
	if (!NavSystem->GetRandomReachablePointInRadius(Origin, EnemyCharacter->PatrolRadius, PatrolLocation))
	{
		return EBTNodeResult::Failed;
	}

	Blackboard->SetValueAsVector(AEnemyAIController::PatrolLocationKey, PatrolLocation.Location);
	return EBTNodeResult::Succeeded;
}
