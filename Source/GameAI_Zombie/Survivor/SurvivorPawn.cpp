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
#include "DrawDebugHelpers.h"
#include "Items/ItemType.h"

// Runtime toggle for the on-screen AI debug visualizers
static TAutoConsoleVariable<int32> CVarSurvivorDebug(
	TEXT("ai.SurvivorDebug"), 1,
	TEXT("Draw Survivor AI debug visualizers (0 = off, 1 = on)."), ECVF_Cheat);

ASurvivorPawn::ASurvivorPawn()
{
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

	// Sight Sense 
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1400.0f;
	SightConfig->LoseSightRadius = 1900.0f;
	
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

	// Damage Sense 
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));

	// Hearing Sense 
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 1300.0f;
	HearingConfig->SetMaxAge(1.5f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;

	// Register the configs with the component
	PerceptionComp->ConfigureSense(*SightConfig);
	PerceptionComp->ConfigureSense(*DamageConfig);
	PerceptionComp->ConfigureSense(*HearingConfig);

	
	PerceptionComp->SetDominantSense(SightConfig->GetSenseImplementation());
}

void ASurvivorPawn::BeginPlay()
{
	Super::BeginPlay();

	if (PerceptionComp)
	{
		PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ASurvivorPawn::OnPerceptionUpdated);
	}

	// Pin to spawn ground height.
	GroundZ = GetActorLocation().Z;
	bGroundZSet = true;

	// Anchor exploration to the spawn point.
	SpawnLocation = GetActorLocation();
	LastHouseDiscoveryTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// Start the survival timer.
	SpawnTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &ASurvivorPawn::OnSurvivorDeath);
		HealthComponent->OnDamaged.AddDynamic(this, &ASurvivorPawn::OnSurvivorDamaged);
	}
	UE_LOG(LogTemp, Warning, TEXT("SURVIVOR: spawned at %s - run started"), *GetActorLocation().ToString());
}

void ASurvivorPawn::OnSurvivorDeath()
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Survived = Now - SpawnTime;
	UE_LOG(LogTemp, Warning, TEXT("SURVIVOR: DIED - survived %.1f seconds (%.0f min %.0f sec)"),
		Survived, FMath::FloorToFloat(Survived / 60.f), FMath::Fmod(Survived, 60.f));
}

void ASurvivorPawn::OnSurvivorDamaged()
{
	// On hit, target the nearest perceived zombie.
	ASurvivorAIController* AIC = Cast<ASurvivorAIController>(GetController());
	if (!AIC) return;
	UBlackboardComponent* BB = AIC->GetBB();
	if (!BB || !PerceptionComp) return;

	TArray<AActor*> Perceived;
	PerceptionComp->GetCurrentlyPerceivedActors(nullptr, Perceived);

	const FVector MyLoc = GetActorLocation();
	ABaseZombie* Nearest = nullptr;
	float BestDist = MAX_FLT;
	for (AActor* A : Perceived)
	{
		ABaseZombie* Z = Cast<ABaseZombie>(A);
		if (!Z) continue;
		const float D = FVector::Dist2D(MyLoc, Z->GetActorLocation());
		if (D < BestDist) { BestDist = D; Nearest = Z; }
	}

	if (Nearest)
	{
		BB->SetValueAsObject(FName("TargetEnemy"), Nearest);
		BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Nearest->GetActorLocation());
		UE_LOG(LogTemp, Warning, TEXT("Damage: hit - engaging nearest perceived zombie (%.0f units)"), BestDist);
	}
	else
	{
		// Blind-spot hit: turn toward the last known enemy.
		const FVector LastKnown = BB->GetValueAsVector(FName("LastKnownEnemyLocation"));
		FVector Face = (!LastKnown.IsZero())
			? (LastKnown - MyLoc).GetSafeNormal2D()
			: (-GetActorForwardVector().GetSafeNormal2D());
		if (!Face.IsNearlyZero())
		{
			SetActorRotation(Face.Rotation());
			UE_LOG(LogTemp, Warning, TEXT("Damage: hit from blind spot - turning to reacquire the attacker"));
		}
	}
}

void ASurvivorPawn::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor) return;

	ASurvivorAIController* AIC = Cast<ASurvivorAIController>(GetController());
	if (!AIC) return;

	UBlackboardComponent* BB = AIC->GetBB();
	if (!BB) return;

	const bool bSensed = Stimulus.WasSuccessfullySensed();

	// House seen: remember it for exploration.
	if (AHouse* House = Cast<AHouse>(Actor))
	{
		if (bSensed && !KnownHouses.Contains(Actor))
		{
			KnownHouses.Add(Actor);
			LastHouseDiscoveryTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastHouseDiscoveryTime;
			UE_LOG(LogTemp, Log, TEXT("Perception: house discovered - %s at %s (%d known)"),
				*House->GetHouseTypeString(), *Actor->GetActorLocation().ToString(), KnownHouses.Num());
		}
		return;
	}

	// Purge zone seen: remember it to flee later.
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

	// Zombie seen: track it.
	if (ABaseZombie* Zombie = Cast<ABaseZombie>(Actor))
	{
		if (bSensed)
		{
			BB->SetValueAsObject(FName("TargetEnemy"), Zombie);
			BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Zombie->GetActorLocation());

			UE_LOG(LogTemp, Log, TEXT("Perception: Zombie SENSED at %s"), *Zombie->GetActorLocation().ToString());
		}
		else
		{
			// Lost sight: keep the target, just update last known location.
			AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
			if (CurrentTarget == Zombie)
			{
				BB->SetValueAsVector(FName("LastKnownEnemyLocation"), Zombie->GetActorLocation());
			}
		}
		return;
	}

	// Item seen.
	if (ABaseItem* Item = Cast<ABaseItem>(Actor))
	{
		if (bSensed)
		{
			// Skip garbage items
			if (Item->GetItemType() == EItemType::Garbage) return;

			// Remember weapon spots to re-arm later.
			if (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun)
			{
				bool bAlreadyKnown = false;
				for (const FVector& L : KnownWeaponLocations)
				{
					if (FVector::Dist2D(L, Item->GetActorLocation()) < 250.f) { bAlreadyKnown = true; break; }
				}
				if (!bAlreadyKnown) KnownWeaponLocations.Add(Item->GetActorLocation());
			}

			// Skip when full, unless it's a weapon.
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
					int32 WeaponCount = 0, MedkitCount = 0, FoodCount = 0;
					for (ABaseItem* CntSlot : Inv->GetInventory())
					{
						if (!CntSlot) continue;
						const EItemType ST = CntSlot->GetItemType();
						if (ST == EItemType::Pistol || ST == EItemType::Shotgun) ++WeaponCount;
						else if (ST == EItemType::Medkit) ++MedkitCount;
						else if (ST == EItemType::Food) ++FoodCount;
					}
					const EItemType NT = Item->GetItemType();
					const bool bIsWeapon = (NT == EItemType::Pistol || NT == EItemType::Shotgun);
					// Drop a spare weapon for a consumable we lack.
					const bool bNeededConsumable = (NT == EItemType::Medkit && MedkitCount == 0)
						|| (NT == EItemType::Food && FoodCount == 0);
					const bool bCanDropForWeapon = bIsWeapon && bHasConsumable;
					const bool bCanDropForConsumable = bNeededConsumable && WeaponCount >= 3;
					if (!bCanDropForWeapon && !bCanDropForConsumable)
					{
						return; 
					}
					// Let the pickup task handle the swap.
				}
			}

			// Priority: urgent medkit when hurt, then weapons, then consumables; ties go to the closer item.
			const float HpPct = (HealthComponent && HealthComponent->GetMaxHealth() > 0)
				? static_cast<float>(HealthComponent->GetHealth()) / static_cast<float>(HealthComponent->GetMaxHealth())
				: 1.f;
			auto ItemPriority = [HpPct](EItemType T) -> int
			{
				if (HpPct < 0.3f && T == EItemType::Medkit) return 3;
				if (T == EItemType::Pistol || T == EItemType::Shotgun) return 2; 
				if (T == EItemType::Medkit || T == EItemType::Food) return 1; 
				return 0;
			};

			ABaseItem* CurrentTarget = Cast<ABaseItem>(BB->GetValueAsObject(FName("TargetItem")));
			if (!CurrentTarget || !IsValid(CurrentTarget))
			{
				BB->SetValueAsObject(FName("TargetItem"), Item);
				UE_LOG(LogTemp, Log, TEXT("Perception: Item SENSED - Type %d (new target)"),
					static_cast<int>(Item->GetItemType()));
			}
			else
			{
				const int NewPri = ItemPriority(Item->GetItemType());
				const int CurPri = ItemPriority(CurrentTarget->GetItemType());

				bool bShouldSwitch = false;
				if (NewPri > CurPri)
				{
					bShouldSwitch = true; // higher-priority item (weapon, or first aid when hurt)
				}
				else if (NewPri == CurPri)
				{
					// Same tier: prefer closer.
					const float DistToNew = FVector::Dist(GetActorLocation(), Item->GetActorLocation());
					const float DistToCurrent = FVector::Dist(GetActorLocation(), CurrentTarget->GetActorLocation());
					bShouldSwitch = (DistToNew < DistToCurrent);
				}
				// Lower priority: keep current target.

				if (bShouldSwitch)
				{
					BB->SetValueAsObject(FName("TargetItem"), Item);
					UE_LOG(LogTemp, Log, TEXT("Perception: Item SENSED - Type %d (switching, priority %d)"),
						static_cast<int>(Item->GetItemType()), NewPri);
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

	// A zone only hurts at detonation; treat it as dangerous only when we're inside and it's about to blow.
	const float DangerLead = 2.0f;

	// Once committed to a zone, keep stepping out until clear of it.
	if (APurgeZone* Latched = Cast<APurgeZone>(LatchedPurgeZone.Get()))
	{
		const float Dist = FVector::Dist2D(MyLoc, Latched->GetActorLocation());
		const float Remaining = Latched->GetTimeTillPurge() - Latched->GetTimePassed();
		if (Dist > Latched->GetRadius() + 90.f || Remaining > DangerLead + 1.0f) LatchedPurgeZone = nullptr;
		else return Latched;
	}

	// Only flee a zone we're inside that's about to detonate.
	APurgeZone* Nearest = nullptr;
	float NearestDist = MAX_FLT;
	for (const TWeakObjectPtr<AActor>& Z : KnownPurgeZones)
	{
		APurgeZone* Zone = Cast<APurgeZone>(Z.Get());
		if (!Zone) continue;

		const float Remaining = Zone->GetTimeTillPurge() - Zone->GetTimePassed();
		if (Remaining > DangerLead) continue; 

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

	
	if (bGroundZSet)
	{
		FVector Loc = GetActorLocation();
		if (!FMath::IsNearlyEqual(Loc.Z, GroundZ, 1.0f))
		{
			Loc.Z = GroundZ;
			SetActorLocation(Loc, false);
		}
	}

	
	if (AController* C = GetController())
	{
		C->SetControlRotation(GetActorRotation());
	}

	DrawDebug();
}

void ASurvivorPawn::DrawDebug() const
{
	if (!bDrawDebug || CVarSurvivorDebug.GetValueOnGameThread() == 0) return;
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Loc = GetActorLocation();
	const FVector Ground(Loc.X, Loc.Y, Loc.Z + 5.f);
	const FVector PlaneY(1.f, 0.f, 0.f), PlaneZ(0.f, 1.f, 0.f); 

	//Perception ranges 
	const float SightR = SightConfig ? SightConfig->SightRadius : 1400.f;
	const float LoseR  = SightConfig ? SightConfig->LoseSightRadius : 1900.f;
	const float HearR  = HearingConfig ? HearingConfig->HearingRange : 1300.f;
	const float HalfFOV = SightConfig ? SightConfig->PeripheralVisionAngleDegrees : 90.f;
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();

	// Draw the sight cone wedge.
	auto DrawSightCone = [&](float Radius, FColor Color, float Thickness)
	{
		const FVector EdgeL = Ground + Fwd.RotateAngleAxis(-HalfFOV, FVector::UpVector) * Radius;
		const FVector EdgeR = Ground + Fwd.RotateAngleAxis(+HalfFOV, FVector::UpVector) * Radius;
		DrawDebugLine(World, Ground, EdgeL, Color, false, -1.f, 0, Thickness);
		DrawDebugLine(World, Ground, EdgeR, Color, false, -1.f, 0, Thickness);
		const int32 Segs = 24;
		FVector Prev = EdgeL;
		for (int32 i = 1; i <= Segs; ++i)
		{
			const float Ang = -HalfFOV + (2.f * HalfFOV) * (static_cast<float>(i) / Segs);
			const FVector P = Ground + Fwd.RotateAngleAxis(Ang, FVector::UpVector) * Radius;
			DrawDebugLine(World, Prev, P, Color, false, -1.f, 0, Thickness);
			Prev = P;
		}
	};
	DrawSightCone(SightR, FColor(0, 220, 0), 3.f); // sight gain cone
	DrawSightCone(LoseR,  FColor(0, 90, 0),  1.f); // sight lose cone
	DrawDebugCircle(World, Ground, HearR, 48, FColor(220, 200, 0), false, -1.f, 0, 1.f, PlaneY, PlaneZ, false); // hearing circle

	
	DrawDebugLine(World, Ground, Ground + Fwd * 160.f, FColor::White, false, -1.f, 0, 3.f);

	
	for (const TWeakObjectPtr<AActor>& H : KnownHouses)
	{
		if (AHouse* House = Cast<AHouse>(H.Get()))
		{
			const FVector HL = House->GetActorLocation();
			DrawDebugSphere(World, HL, 90.f, 12, FColor::Cyan, false, -1.f, 0, 2.f);
			DrawDebugString(World, HL + FVector(0, 0, 130), House->GetHouseTypeString(), nullptr, FColor::Cyan, 0.f, true);
		}
	}


	for (const TWeakObjectPtr<AActor>& Z : KnownPurgeZones)
	{
		if (AActor* PZ = Z.Get())
			DrawDebugSphere(World, PZ->GetActorLocation(), 110.f, 12, FColor::Orange, false, -1.f, 0, 2.f);
	}

	
	if (PerceptionComp)
	{
		TArray<AActor*> Perceived;
		PerceptionComp->GetCurrentlyPerceivedActors(nullptr, Perceived);
		for (AActor* A : Perceived)
		{
			if (Cast<ABaseZombie>(A))     DrawDebugSphere(World, A->GetActorLocation(), 60.f, 10, FColor::Red,   false, -1.f, 0, 2.f);
			else if (Cast<ABaseItem>(A))  DrawDebugSphere(World, A->GetActorLocation(), 45.f, 8,  FColor::Green, false, -1.f, 0, 2.f);
		}
	}

	
	FString State = TEXT("EXPLORE");
	if (ASurvivorAIController* AIC = Cast<ASurvivorAIController>(GetController()))
	{
		if (UBlackboardComponent* BB = AIC->GetBB())
		{
			AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
			AActor* Item  = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
			if (Enemy)
			{
				DrawDebugLine(World, Ground, Enemy->GetActorLocation(), FColor::Red, false, -1.f, 0, 2.f);
				DrawDebugSphere(World, Enemy->GetActorLocation(), 75.f, 12, FColor::Red, false, -1.f, 0, 3.f);
			}
			if (Item)
			{
				DrawDebugLine(World, Ground, Item->GetActorLocation(), FColor::Green, false, -1.f, 0, 2.f);
			}

			const bool bHasW = BB->GetValueAsBool(FName("bHasWeapon"));
			if (BB->GetValueAsBool(FName("bShouldFlee"))) State = TEXT("FLEE");
			else if (Enemy && bHasW)                      State = TEXT("FIGHT");
			else if (Item)                                State = TEXT("PICKUP");
			else                                          State = TEXT("EXPLORE");
		}
	}

	// On-screen status panel
	if (GEngine)
	{
		const int32 HP = HealthComponent ? HealthComponent->GetHealth() : 0;
		const int32 MaxHP = HealthComponent ? HealthComponent->GetMaxHealth() : 0;
		const float StamPct = (StaminaComponent && StaminaComponent->GetMaxStamina() > 0.f)
			? 100.f * StaminaComponent->GetCurrentStamina() / StaminaComponent->GetMaxStamina() : 0.f;

		int32 Weapons = 0, Consumables = 0;
		if (InventoryComponent)
		{
			for (ABaseItem* S : InventoryComponent->GetInventory())
			{
				if (!S) continue;
				const EItemType T = S->GetItemType();
				if (T == EItemType::Pistol || T == EItemType::Shotgun) ++Weapons;
				else if (T == EItemType::Food || T == EItemType::Medkit) ++Consumables;
			}
		}

		GEngine->AddOnScreenDebugMessage(101, 0.f, FColor::Yellow, FString::Printf(TEXT("STATE: %s"), *State));
		GEngine->AddOnScreenDebugMessage(102, 0.f, FColor::White,
			FString::Printf(TEXT("HP %d/%d   Stamina %.0f%%"), HP, MaxHP, StamPct));
		GEngine->AddOnScreenDebugMessage(103, 0.f, FColor::White,
			FString::Printf(TEXT("Bag: %d weapon(s), %d consumable(s)"), Weapons, Consumables));
		GEngine->AddOnScreenDebugMessage(104, 0.f, FColor::Cyan,
			FString::Printf(TEXT("Houses known: %d"), KnownHouses.Num()));
		GEngine->AddOnScreenDebugMessage(105, 0.f, FColor::Silver,
			TEXT("[green]=sight [yellow]=hearing  cyan=house  red=zombie  green=item"));
	}
}

void ASurvivorPawn::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ASurvivorPawn::StartRunning()
{
	// Don't run without stamina.
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
