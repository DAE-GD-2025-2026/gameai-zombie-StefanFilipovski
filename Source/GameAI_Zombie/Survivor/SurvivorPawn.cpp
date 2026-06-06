// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivorPawn.h"

#include "Common/HealthComponent.h"
#include "Common/InventoryComponent.h"
#include "Common/StaminaComponent.h"
#include "Common/SteeringComponent.h"
#include "Items/BaseItem.h"
#include "Zombies/BaseZombie.h"
#include "Village/House/House.h"
#include "PurgeZones/PurgeZone.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SurvivorAIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"

ASurvivorPawn::ASurvivorPawn()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Adding components
	HealthComponent = CreateDefaultSubobject<UHealthComponent>("HealthComponent");
	StaminaComponent = CreateDefaultSubobject<UStaminaComponent>("StaminaComponent");
	FloatingPawnMovement = CreateDefaultSubobject<UFloatingPawnMovement>("FloatingPawnMovement");
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>("InventoryComponent");
	SteeringComponent = CreateDefaultSubobject<USteeringComponent>("SteeringComponent");
	AddOwnedComponent(HealthComponent);
	AddOwnedComponent(StaminaComponent);
	AddOwnedComponent(FloatingPawnMovement);
	AddOwnedComponent(InventoryComponent);
	AddOwnedComponent(SteeringComponent);

	// Senses
	PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComp"));

	// ---- Sight Sense ----
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1400.0f;       // reach far enough to spot houses/items while roaming
	SightConfig->LoseSightRadius = 1900.0f;
	// Forward 240° vision cone (±120° half-angle) for spotting houses and items. The rear blind spot
	// is handled two ways: a one-time 360° spin scan on spawn (to find the local cluster), and the
	// Hearing sense for all-around threat awareness during play.
	SightConfig->PeripheralVisionAngleDegrees = 120.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

	// ---- Damage Sense ----
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));

	// ---- Hearing Sense ----
	// Zombies emit silent noise events (see ABaseZombie), so we sense nearby ones from ANY direction,
	// even outside the sight cone (e.g. a chaser behind us). This is the textbook second sense.
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 1300.0f;
	HearingConfig->SetMaxAge(1.5f); // forget a noise 1.5s after it was last heard
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;

	// Register the configs with the component
	PerceptionComp->ConfigureSense(*SightConfig);
	PerceptionComp->ConfigureSense(*DamageConfig);
	PerceptionComp->ConfigureSense(*HearingConfig);

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

	// Remember our spawn ground height — we'll pin to it so collisions can't elevate us.
	GroundZ = GetActorLocation().Z;
	bGroundZSet = true;

	// Remember where we spawned — the initial house cluster is always nearby, so exploration
	// anchors here instead of drifting off across the map.
	SpawnLocation = GetActorLocation();
	LastHouseDiscoveryTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// Start the survival timer and listen for death so we can report how long we lasted.
	SpawnTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &ASurvivorPawn::OnSurvivorDeath);
		HealthComponent->OnDamaged.AddDynamic(this, &ASurvivorPawn::OnSurvivorDamaged);
	}
	UE_LOG(LogTemp, Warning, TEXT("SURVIVOR: spawned at %s — run started"), *GetActorLocation().ToString());
}

void ASurvivorPawn::OnSurvivorDeath()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Survived = Now - SpawnTime;
	UE_LOG(LogTemp, Warning, TEXT("SURVIVOR: DIED — survived %.1f seconds (%.0f min %.0f sec)"),
		Survived, FMath::FloorToFloat(Survived / 60.f), FMath::Fmod(Survived, 60.f));
}

void ASurvivorPawn::OnSurvivorDamaged()
{
	// We were hit — the attacker may be in our sight blind spot or behind us, and the damage carried no
	// instigator. Acquire the NEAREST zombie (a melee attacker is right on top of us) as the target so
	// the Fight branch turns to face it and shoots back, instead of taking free hits from behind.
	ASurvivorAIController* AIC = Cast<ASurvivorAIController>(GetController());
	if (!AIC || !GetWorld()) return;
	UBlackboardComponent* BB = AIC->GetBB();
	if (!BB) return;

	const FVector MyLoc = GetActorLocation();
	ABaseZombie* Nearest = nullptr;
	float BestDist = 700.f; // melee range — close enough that it's almost certainly our attacker
	for (TActorIterator<ABaseZombie> It(GetWorld()); It; ++It)
	{
		ABaseZombie* Z = *It;
		if (!Z) continue;
		const float D = FVector::Dist2D(MyLoc, Z->GetActorLocation());
		if (D < BestDist) { BestDist = D; Nearest = Z; }
	}

	if (Nearest)
	{
		BB->SetValueAsObject(FName("TargetEnemy"), Nearest);
		BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Nearest->GetActorLocation());
		UE_LOG(LogTemp, Warning, TEXT("Damage: hit by an unseen enemy — turning to fight nearest zombie (%.0f units)"), BestDist);
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

	// Houses are discovered by the radial live query in Tick (not the sight cone), so ignore them here.
	if (Cast<AHouse>(Actor)) return;

	// --- PURGE ZONE perceived: remember it so we can flee its blast radius ---
	if (Cast<APurgeZone>(Actor))
	{
		if (bSensed && !KnownPurgeZones.Contains(Actor))
		{
			KnownPurgeZones.Add(Actor);
			UE_LOG(LogTemp, Warning, TEXT("Perception: PURGE ZONE detected at %s"),
				*Actor->GetActorLocation().ToString());
		}
		return;
	}

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

			// Remember weapon spots so we can come back to re-arm if we run dry later.
			if (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun)
			{
				bool bAlreadyKnown = false;
				for (const FVector& L : KnownWeaponLocations)
				{
					if (FVector::Dist2D(L, Item->GetActorLocation()) < 250.f) { bAlreadyKnown = true; break; }
				}
				if (!bAlreadyKnown) KnownWeaponLocations.Add(Item->GetActorLocation());
			}

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

void ASurvivorPawn::ForgetWeaponNear(const FVector& Loc, float Radius)
{
	for (int32 i = KnownWeaponLocations.Num() - 1; i >= 0; --i)
	{
		if (FVector::Dist2D(KnownWeaponLocations[i], Loc) < Radius) KnownWeaponLocations.RemoveAt(i);
	}
}

APurgeZone* ASurvivorPawn::GetActivePurgeZoneDanger() const
{
	const FVector MyLoc = GetActorLocation();

	// Hysteresis: if we already committed to leaving a zone, keep escaping it until we've clearly
	// cleared the blast radius. This stops the in/out jitter at the boundary.
	if (APurgeZone* Latched = Cast<APurgeZone>(LatchedPurgeZone.Get()))
	{
		const float Dist = FVector::Dist2D(MyLoc, Latched->GetActorLocation());
		if (Dist > Latched->GetRadius() + 250.f) LatchedPurgeZone = nullptr; // cleared
		else                                     return Latched;             // still escaping
	}

	// Otherwise only react when we're actually inside (or right at the edge of) a zone — the map
	// spawns many small zones, so reacting to distant ones would block normal movement constantly.
	APurgeZone* Nearest = nullptr;
	float NearestDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& Z : KnownPurgeZones)
	{
		APurgeZone* Zone = Cast<APurgeZone>(Z.Get());
		if (!Zone) continue;

		const float Dist = FVector::Dist2D(MyLoc, Zone->GetActorLocation());
		if (Dist < Zone->GetRadius() + 40.f && Dist < NearestDist)
		{
			NearestDist = Dist;
			Nearest = Zone;
		}
	}
	if (Nearest) LatchedPurgeZone = Nearest;
	return Nearest;
}

void ASurvivorPawn::RequestScan()
{
	if (bScanning) return;
	bScanning = true;
	ScanElapsed = 0.f;
	ScanStartYaw = GetActorRotation().Yaw;
}

void ASurvivorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Live house query: periodically become aware of any house within HouseSenseRadius, in ANY
	// direction. This is how we acquire house knowledge — distance-based proximity rather than the
	// forward sight cone — so we reliably notice nearby buildings instead of walking past ones that
	// happen to be off to our side. Knowledge still grows with exploration (we only learn houses we
	// come near), we just don't depend on facing them.
	HouseQueryTimer += DeltaTime;
	if (HouseQueryTimer >= HouseQueryInterval && GetWorld())
	{
		HouseQueryTimer = 0.f;
		const FVector MyLoc = GetActorLocation();
		for (TActorIterator<AHouse> It(GetWorld()); It; ++It)
		{
			AHouse* H = *It;
			if (!H) continue;
			if (FVector::Dist2D(MyLoc, H->GetActorLocation()) > HouseSenseRadius) continue;
			if (KnownHouses.Contains(H)) continue;

			KnownHouses.Add(H);
			LastHouseDiscoveryTime = GetWorld()->GetTimeSeconds();
			UE_LOG(LogTemp, Log, TEXT("Knowledge: house discovered — %s at %s (%d known)"),
				*H->GetHouseTypeString(), *H->GetActorLocation().ToString(), KnownHouses.Num());
		}
	}

	// Keep the survivor pinned to its ground height. Without this, being pushed into a wall
	// or walking into a zombie can make the capsule ride up and "climb" onto geometry.
	if (bGroundZSet)
	{
		FVector Loc = GetActorLocation();
		if (!FMath::IsNearlyEqual(Loc.Z, GroundZ, 1.0f))
		{
			Loc.Z = GroundZ;
			SetActorLocation(Loc, false); // no sweep — just correct the height
		}
	}
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
