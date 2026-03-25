// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "EnemyCharacter.h"

const FName AEnemyAIController::TargetActorKey(TEXT("TargetActor"));
const FName AEnemyAIController::PatrolLocationKey(TEXT("PatrolLocation"));
const FName AEnemyAIController::SpawnLocationKey(TEXT("SpawnLocation"));
const FName AEnemyAIController::IsInAttackRangeKey(TEXT("IsInAttackRange"));

AEnemyAIController::AEnemyAIController()
{
	BlackboardComponentRef = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));
	BehaviorTreeComponentRef = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTreeComponent"));
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AEnemyCharacter* EnemyCharacter = Cast<AEnemyCharacter>(InPawn);
	if (!EnemyCharacter || !EnemyCharacter->BehaviorTreeAsset)
	{
		return;
	}

	EnemyCharacter->CaptureSpawnLocation();

	UBlackboardComponent* BlackboardComponent = BlackboardComponentRef.Get();
	if (UseBlackboard(EnemyCharacter->BehaviorTreeAsset->BlackboardAsset, BlackboardComponent))
	{
		BlackboardComponentRef = BlackboardComponent;
		BlackboardComponentRef->SetValueAsVector(SpawnLocationKey, EnemyCharacter->GetSpawnLocation());
		RunBehaviorTree(EnemyCharacter->BehaviorTreeAsset);
	}
}

UBlackboardComponent* AEnemyAIController::GetEnemyBlackboard() const
{
	return BlackboardComponentRef;
}
