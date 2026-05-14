// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivorPawn.h"

#include "Common/HealthComponent.h"
#include "Common/InventoryComponent.h"
#include "Common/StaminaComponent.h"
#include "Items/BaseItem.h"
#include "Zombies/BaseZombie.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SurvivorAIController.h"

ASurvivorPawn::ASurvivorPawn()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	// Adding components
	HealthComponent = CreateDefaultSubobject<UHealthComponent>("HealthComponent");
	StaminaComponent = CreateDefaultSubobject<UStaminaComponent>("StaminaComponent");
	FloatingPawnMovement = CreateDefaultSubobject<UFloatingPawnMovement>("FloatingPawnMovement");
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>("InventoryComponent");
	AddOwnedComponent(HealthComponent);
	AddOwnedComponent(StaminaComponent);
	AddOwnedComponent(FloatingPawnMovement);
	AddOwnedComponent(InventoryComponent);
	
	// Senses
	PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComp"));
    
	// ---- Sight Sense ----
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1000.0f;
	SightConfig->LoseSightRadius = 1500.0f;
	SightConfig->PeripheralVisionAngleDegrees = 70.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

	// ---- Damage Sense ----
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));

	// Register the configs with the component
	PerceptionComp->ConfigureSense(*SightConfig);
	PerceptionComp->ConfigureSense(*DamageConfig);

	// Set the dominant sense if you want (optional)
	PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
}

void ASurvivorPawn::BeginPlay()
{
	Super::BeginPlay();
	
	if (PerceptionComp)
	{
		PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ASurvivorPawn::OnPerceptionUpdated);
	}
}

void ASurvivorPawn::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor) return;

	// Get our blackboard through the AI controller
	ASurvivorAIController* AIC = Cast<ASurvivorAIController>(GetController());
	if (!AIC) return;

	UBlackboardComponent* BB = AIC->GetBB();
	if (!BB) return;

	const bool bSensed = Stimulus.WasSuccessfullySensed();

	// --- ZOMBIE perceived ---
	if (ABaseZombie* Zombie = Cast<ABaseZombie>(Actor))
	{
		if (bSensed)
		{
			// We see (or got damaged by) a zombie — track it
			BB->SetValueAsObject(FName("TargetEnemy"), Zombie);
			BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Zombie->GetActorLocation());

			UE_LOG(LogTemp, Log, TEXT("Perception: Zombie SENSED at %s"), *Zombie->GetActorLocation().ToString());
		}
		else
		{
			// Lost sight of zombie — clear the actor ref but keep last known location for investigation
			AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
			if (CurrentTarget == Zombie)
			{
				BB->ClearValue(FName("TargetEnemy"));
				// LastKnownEnemyLocation intentionally kept so we can investigate
			}

			UE_LOG(LogTemp, Log, TEXT("Perception: Zombie LOST"));
		}
		return;
	}

	// --- ITEM perceived ---
	if (ABaseItem* Item = Cast<ABaseItem>(Actor))
	{
		if (bSensed)
		{
			// Skip garbage items
			if (Item->GetItemType() == EItemType::Garbage) return;

			// If we don't have a target item yet, take this one
			// (priority logic will be refined in the BTService later)
			AActor* CurrentItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
			if (!CurrentItem)
			{
				BB->SetValueAsObject(FName("TargetItem"), Item);
				UE_LOG(LogTemp, Log, TEXT("Perception: Item SENSED - Type %d"), static_cast<int>(Item->GetItemType()));
			}
		}
		else
		{
			// Lost sight of item — clear if it was our target
			AActor* CurrentItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
			if (CurrentItem == Item)
			{
				BB->ClearValue(FName("TargetItem"));
				UE_LOG(LogTemp, Log, TEXT("Perception: Item LOST"));
			}
		}
		return;
	}
}

void ASurvivorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ASurvivorPawn::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ASurvivorPawn::StartRunning()
{
	bIsRunning = true;
	FloatingPawnMovement->MaxSpeed = RunningSpeed;
}

void ASurvivorPawn::StopRunning()
{
	bIsRunning = false;
	FloatingPawnMovement->MaxSpeed = DefaultSpeed;
}

bool ASurvivorPawn::IsRunning() const
{
	return bIsRunning;
}
