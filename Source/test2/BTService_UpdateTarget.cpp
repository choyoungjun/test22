// Fill out your copyright notice in the Description page of Project Settings.

#include "BTService_UpdateTarget.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "EnemyAIController.h"
#include "EnemyCharacter.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UBTService_UpdateTarget::UBTService_UpdateTarget()
{
	NodeName = TEXT("Update Target");
	Interval = 0.2f;
	RandomDeviation = 0.05f;
}

void UBTService_UpdateTarget::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AEnemyAIController* AIController = Cast<AEnemyAIController>(OwnerComp.GetAIOwner());
	AEnemyCharacter* EnemyCharacter = AIController ? Cast<AEnemyCharacter>(AIController->GetPawn()) : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!AIController || !EnemyCharacter || !Blackboard)
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(EnemyCharacter, 0);
	if (!PlayerPawn)
	{
		Blackboard->ClearValue(AEnemyAIController::TargetActorKey);
		Blackboard->SetValueAsBool(AEnemyAIController::IsInAttackRangeKey, false);
		return;
	}

	const float DistanceToPlayer = FVector::Dist(EnemyCharacter->GetActorLocation(), PlayerPawn->GetActorLocation());
	const bool bCanKeepTarget =
		DistanceToPlayer <= EnemyCharacter->LoseTargetRadius &&
		(DistanceToPlayer <= EnemyCharacter->DetectionRadius || Blackboard->GetValueAsObject(AEnemyAIController::TargetActorKey) == PlayerPawn) &&
		AIController->LineOfSightTo(PlayerPawn);

	if (!bCanKeepTarget)
	{
		Blackboard->ClearValue(AEnemyAIController::TargetActorKey);
		Blackboard->SetValueAsBool(AEnemyAIController::IsInAttackRangeKey, false);
		return;
	}

	Blackboard->SetValueAsObject(AEnemyAIController::TargetActorKey, PlayerPawn);
	Blackboard->SetValueAsBool(AEnemyAIController::IsInAttackRangeKey, DistanceToPlayer <= EnemyCharacter->AttackRange);
}
