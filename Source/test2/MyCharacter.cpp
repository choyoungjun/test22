// Fill out your copyright notice in the Description page of Project Settings.


#include "MyCharacter.h"

#include "Engine/Engine.h"

// Sets default values
AMyCharacter::AMyCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AMyCharacter::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
}

// Called every frame
void AMyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

float AMyCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	CurrentHealth = FMath::Clamp(CurrentHealth - AppliedDamage, 0.0f, MaxHealth);

	if (GEngine)
	{
		const FString DebugMessage = FString::Printf(TEXT("Player HP: %.0f / %.0f"), CurrentHealth, MaxHealth);
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 1.5f, FColor::Red, DebugMessage);
	}

	if (CurrentHealth <= 0.0f)
	{
		DisableInput(nullptr);
	}

	return AppliedDamage;
}

