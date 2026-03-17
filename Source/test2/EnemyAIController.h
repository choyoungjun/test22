// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyAIController.generated.h"

class UBehaviorTreeComponent;
class UBlackboardComponent;

UCLASS()
class TEST2_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	static const FName TargetActorKey;
	static const FName PatrolLocationKey;
	static const FName SpawnLocationKey;
	static const FName IsInAttackRangeKey;

	UBlackboardComponent* GetEnemyBlackboard() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UBlackboardComponent> BlackboardComponentRef;

	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UBehaviorTreeComponent> BehaviorTreeComponentRef;
};
