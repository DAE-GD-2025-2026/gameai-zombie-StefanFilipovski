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
#include "NavigationSystem.h"
#include "NavigationPath.h"

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
	SightConfig->PeripheralVisionAngleDegrees = 120.0f; // Wide FOV to spot items in rooms
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
			// Lost sight of zombie — update last known location but DON'T clear TargetEnemy.
			// The BT service will clear it after a timeout if we truly can't see it anymore.
			// This prevents flickering at the perception boundary.
			AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
			if (CurrentTarget == Zombie)
			{
				BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Zombie->GetActorLocation());
			}

			// Zombie left sight — keeping target, no log to reduce spam
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

			// Skip if inventory is full — unless it's a weapon (we can drop consumables for weapons)
			UInventoryComponent* Inv = GetInventoryComponent();
			if (Inv)
			{
				bool bHasEmptySlot = false;
				bool bHasConsumable = false;
				for (ABaseItem* Slot : Inv->GetInventory())
				{
					if (Slot == nullptr) { bHasEmptySlot = true; break; }
					if (Slot->GetItemType() == EItemType::Food || Slot->GetItemType() == EItemType::Medkit)
						bHasConsumable = true;
				}
				if (!bHasEmptySlot)
				{
					const bool bIsWeapon = (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun);
					if (!bIsWeapon || !bHasConsumable)
					{
						return; // inventory full, can't make room
					}
					// Weapon spotted + we have a consumable to drop — let pickup task handle the swap
				}
			}

			// Priority: Weapons > Consumables. Never switch from a weapon to a consumable.
			// Within the same priority tier, prefer closer items.
			auto IsWeaponType = [](EItemType T) { return T == EItemType::Pistol || T == EItemType::Shotgun; };

			ABaseItem* CurrentTarget = Cast<ABaseItem>(BB->GetValueAsObject(FName("TargetItem")));
			if (!CurrentTarget || !IsValid(CurrentTarget))
			{
				BB->SetValueAsObject(FName("TargetItem"), Item);
				UE_LOG(LogTemp, Log, TEXT("Perception: Item SENSED - Type %d (new target)"),
					static_cast<int>(Item->GetItemType()));
			}
			else
			{
				const bool bNewIsWeapon = IsWeaponType(Item->GetItemType());
				const bool bCurrentIsWeapon = IsWeaponType(CurrentTarget->GetItemType());

				bool bShouldSwitch = false;
				if (bNewIsWeapon && !bCurrentIsWeapon)
				{
					// Always switch: weapon beats consumable
					bShouldSwitch = true;
				}
				else if (bNewIsWeapon == bCurrentIsWeapon)
				{
					// Same tier — prefer closer
					const float DistToNew = FVector::Dist(GetActorLocation(), Item->GetActorLocation());
					const float DistToCurrent = FVector::Dist(GetActorLocation(), CurrentTarget->GetActorLocation());
					bShouldSwitch = (DistToNew < DistToCurrent);
				}
				// else: new is consumable, current is weapon — never switch

				if (bShouldSwitch)
				{
					BB->SetValueAsObject(FName("TargetItem"), Item);
					UE_LOG(LogTemp, Log, TEXT("Perception: Item SENSED - Type %d (switching: %s)"),
						static_cast<int>(Item->GetItemType()),
						(bNewIsWeapon && !bCurrentIsWeapon) ? TEXT("weapon priority") : TEXT("closer"));
				}
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
	// Don't start running if we have no stamina — prevents toggle spam
	if (StaminaComponent && StaminaComponent->GetCurrentStamina() <= 0.f)
	{
		return;
	}
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

TArray<FVector> ASurvivorPawn::CalculatePath(const FVector& TargetLocation) const
{
	TArray<FVector> Path = {};
	// 1. Get the Navigation System
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) return Path;
	FVector StartLocation = GetActorLocation();

	AActor* ContextActor = Cast<AActor>(GetController());
	// 2. Find the path synchronously
	UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(GetWorld(), StartLocation, TargetLocation,
	                                                                   ContextActor);

	// 3. Extract the path points
	if (NavPath && NavPath->IsValid())
	{
		return NavPath->PathPoints;
	}

	return Path;
}
