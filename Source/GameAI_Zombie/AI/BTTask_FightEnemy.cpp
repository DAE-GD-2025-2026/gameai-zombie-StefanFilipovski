#include "BTTask_FightEnemy.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Common/SteeringComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Perception/AIPerceptionComponent.h"

namespace
{
	// True if there's grabbable loot nearby and the enemy isn't close - yield to Pickup before engaging.
	bool ShouldFinishLootingFirst(ASurvivorPawn* Survivor, float EnemyDist)
	{
		if (!Survivor || EnemyDist <= 700.f) return false; // enemy close fight, don't loot

		UInventoryComponent* Inv = Survivor->GetInventoryComponent();
		UAIPerceptionComponent* Perc = Survivor->GetPerceptionComponent();
		if (!Inv || !Perc) return false;

		bool bHasEmptySlot = false;
		int32 Consumables = 0, Weapons = 0, Medkits = 0, Food = 0;
		for (ABaseItem* S : Inv->GetInventory())
		{
			if (!S) { bHasEmptySlot = true; continue; }
			const EItemType T = S->GetItemType();
			if (T == EItemType::Pistol || T == EItemType::Shotgun) ++Weapons;
			else if (T == EItemType::Medkit) { ++Consumables; ++Medkits; }
			else if (T == EItemType::Food)   { ++Consumables; ++Food; }
		}

		TArray<AActor*> Perceived;
		Perc->GetCurrentlyPerceivedActors(nullptr, Perceived);
		const FVector MyLoc = Survivor->GetActorLocation();
		for (AActor* A : Perceived)
		{
			ABaseItem* It = Cast<ABaseItem>(A);
			if (!It || !IsValid(It) || It->GetItemType() == EItemType::Garbage) continue;
			if (FVector::Dist(MyLoc, It->GetActorLocation()) > 700.f) continue;

			// Only defer for loot we can actually carry.
			const EItemType IT = It->GetItemType();
			const bool bWeapon = (IT == EItemType::Pistol || IT == EItemType::Shotgun);
			const bool bNeeded = (IT == EItemType::Medkit && Medkits == 0) || (IT == EItemType::Food && Food == 0);
			const bool bGrabbable = bHasEmptySlot
				|| (bWeapon && Consumables >= 2)
				|| (bNeeded && Weapons >= 3);
			if (bGrabbable) return true;
		}
		return false;
	}
}

UBTTask_FightEnemy::UBTTask_FightEnemy()
{
	NodeName = "Fight Enemy";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_FightEnemy::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTFightMemory* Memory = reinterpret_cast<FBTFightMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;
	Memory->FireCooldown = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (!Enemy) return EBTNodeResult::Failed;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return EBTNodeResult::Failed;

	// Finish looting the room before engaging (unless the enemy is right on top of us).
	const float EnemyDist = FVector::Dist(Survivor->GetActorLocation(), Enemy->GetActorLocation());
	if (ShouldFinishLootingFirst(Survivor, EnemyDist))
	{
		AIC->StopMovement();
		return EBTNodeResult::Failed; // yield to Pickup
	}

	// Stop steering so it doesn't fight path-following.
	if (Survivor->GetSteeringComponent()) Survivor->GetSteeringComponent()->Stop();

	AIC->MoveToActor(Enemy, AttackRange - 50.f);

	return EBTNodeResult::InProgress;
}

void UBTTask_FightEnemy::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTFightMemory* Memory = reinterpret_cast<FBTFightMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;
	Memory->FireCooldown -= DeltaSeconds;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));

	// If enemy disappeared or timeout, abort
	if (!Enemy || !Survivor)
	{
		AIC->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	if (Memory->TimeElapsed >= TimeoutSeconds)
	{
		AIC->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const float DistToEnemy = FVector::Dist(Survivor->GetActorLocation(), Enemy->GetActorLocation());

	// Loot still to grab nearby and the enemy isn't on top of us -> finish looting first (yield to Pickup).
	if (ShouldFinishLootingFirst(Survivor, DistToEnemy))
	{
		AIC->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Always face the enemy so the cone stays on it and shots land.
	const FVector Direction = (Enemy->GetActorLocation() - Survivor->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero()) Survivor->SetActorRotation(Direction.Rotation());

	if (DistToEnemy <= AttackRange)
	{
		AIC->StopMovement();

		// Fire on a steady cadence and stay locked on until the enemy is dead, flees, or we run dry.
		if (Memory->FireCooldown <= 0.f)
		{
			UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
			bool bFired = false;
			if (Inventory)
			{
				const TArray<ABaseItem*>& Items = Inventory->GetInventory();
				for (int32 i = 0; i < Items.Num(); ++i)
				{
					ABaseItem* Item = Items[i];
					if (Item && (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun) && Item->GetValue() > 0)
					{
						Inventory->UseItem(i);
						const int32 AmmoLeft = Item->GetValue();
						UE_LOG(LogTemp, Log, TEXT("Fight: Fired weapon in slot %d (%d ammo left)"), i, AmmoLeft);
						if (AmmoLeft <= 0)
						{
							Inventory->RemoveItem(i);
							UE_LOG(LogTemp, Log, TEXT("Fight: Weapon in slot %d is empty, discarded"), i);
						}
						Memory->FireCooldown = FireInterval;
						bFired = true;
						break;
					}
				}
			}

			// No usable weapon left - yield so the BT can re-arm or flee.
			if (!bFired)
			{
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
				return;
			}
		}
	}
	else
	{
		// Keep chasing
		AIC->MoveToActor(Enemy, AttackRange - 50.f);
	}
}
