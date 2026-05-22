#include "BTService_UpdateBlackboard.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/HealthComponent.h"
#include "Common/StaminaComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "PurgeZones/PurgeZone.h"
#include "Kismet/GameplayStatics.h"

UBTService_UpdateBlackboard::UBTService_UpdateBlackboard()
{
	NodeName = "Update Survivor Blackboard";
	Interval = 0.25f;       // Update 4x per second — plenty fast, not wasteful
	RandomDeviation = 0.05f;
}

void UBTService_UpdateBlackboard::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!BB || !AIC) return;

	ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(AIC->GetPawn());
	if (!Survivor) return;

	// --- Health ---
	UHealthComponent* Health = Survivor->GetHealthComponent();
	if (Health)
	{
		const float HealthPct = (Health->GetMaxHealth() > 0)
			? static_cast<float>(Health->GetHealth()) / static_cast<float>(Health->GetMaxHealth())
			: 0.f;
		BB->SetValueAsFloat(FName("HealthPercent"), HealthPct);
	}

	// --- Weapon status ---
	UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
	bool bHasWeapon = false;
	if (Inventory)
	{
		for (ABaseItem* Item : Inventory->GetInventory())
		{
			if (Item && (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun))
			{
				if (Item->GetValue() > 0) // has ammo
				{
					bHasWeapon = true;
					break;
				}
			}
		}
	}
	BB->SetValueAsBool(FName("bHasWeapon"), bHasWeapon);

	// --- Consumable needs ---
	const float HealthPct = BB->GetValueAsFloat(FName("HealthPercent"));
	bool bHasMedkit = false;
	bool bHasFood = false;
	if (Inventory)
	{
		for (ABaseItem* Item : Inventory->GetInventory())
		{
			if (Item && Item->GetValue() > 0)
			{
				if (Item->GetItemType() == EItemType::Medkit) bHasMedkit = true;
				if (Item->GetItemType() == EItemType::Food) bHasFood = true;
			}
		}
	}
	// Need heal: HP < 70% and we have a medkit
	BB->SetValueAsBool(FName("bNeedsHeal"), HealthPct < 0.7f && bHasMedkit);

	// Need food: stamina < 40% and we have food
	UStaminaComponent* Stamina = Survivor->GetStaminaComponent();
	const float StaminaPct = (Stamina && Stamina->GetMaxStamina() > 0)
		? Stamina->GetCurrentStamina() / Stamina->GetMaxStamina()
		: 1.f;
	BB->SetValueAsBool(FName("bNeedsFood"), StaminaPct < 0.4f && bHasFood);

	// --- Purge zone danger ---
	bool bPurgeNearby = false;
	TArray<AActor*> PurgeZones;
	UGameplayStatics::GetAllActorsOfClass(Survivor->GetWorld(), APurgeZone::StaticClass(), PurgeZones);
	for (AActor* PZ : PurgeZones)
	{
		if (PZ && FVector::Dist(Survivor->GetActorLocation(), PZ->GetActorLocation()) < 1500.f)
		{
			bPurgeNearby = true;
			break;
		}
	}

	// --- Flee decision ---
	const bool bEnemyVisible = BB->GetValueAsObject(FName("TargetEnemy")) != nullptr;
	// Flee if: purge zone nearby, or (enemy visible and low HP without weapon), or critically low HP
	const bool bShouldFlee = bPurgeNearby || (bEnemyVisible && ((HealthPct < 0.3f && !bHasWeapon) || HealthPct < 0.15f));
	BB->SetValueAsBool(FName("bShouldFlee"), bShouldFlee);

	// --- Validate TargetEnemy (clear if dead, destroyed, or too far) ---
	AActor* TargetEnemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (TargetEnemy)
	{
		if (!IsValid(TargetEnemy))
		{
			BB->ClearValue(FName("TargetEnemy"));
		}
		else
		{
			// If enemy is beyond lose-sight range + buffer, clear it
			const float DistToEnemy = FVector::Dist(Survivor->GetActorLocation(), TargetEnemy->GetActorLocation());
			if (DistToEnemy > 2000.f) // well beyond lose-sight radius of 1500
			{
				BB->ClearValue(FName("TargetEnemy"));
				UE_LOG(LogTemp, Log, TEXT("Service: Enemy too far (%.0f), clearing target"), DistToEnemy);
			}
		}
	}

	// --- Validate TargetItem (clear if destroyed or picked up) ---
	AActor* TargetItem = Cast<AActor>(BB->GetValueAsObject(FName("TargetItem")));
	if (TargetItem && !IsValid(TargetItem))
	{
		BB->ClearValue(FName("TargetItem"));
	}

	// --- Self location ---
	BB->SetValueAsVector(FName("SelfLocation"), Survivor->GetActorLocation());
}
