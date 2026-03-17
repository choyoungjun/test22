// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EnemyCharacter.generated.h"

class UBehaviorTree;

UCLASS()
class TEST2_API AEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "AI")
	FVector GetSpawnLocation() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	float PatrolRadius = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	float DetectionRadius = 1500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	float LoseTargetRadius = 1800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	float AttackRange = 200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	float AttackDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "AI")
	FVector SpawnLocation = FVector::ZeroVector;
};
