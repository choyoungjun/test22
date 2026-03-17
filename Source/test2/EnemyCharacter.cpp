// Fill out your copyright notice in the Description page of Project Settings.

#include "EnemyCharacter.h"

#include "EnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"

AEnemyCharacter::AEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCharacterMovement()->MaxWalkSpeed = 300.0f;
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	SpawnLocation = GetActorLocation();
}

FVector AEnemyCharacter::GetSpawnLocation() const
{
	return SpawnLocation;
}
