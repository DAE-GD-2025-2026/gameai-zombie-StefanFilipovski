#include "BTTask_FightEnemy.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Survivor/SurvivorPawn.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"

UBTTask_FightEnemy::UBTTask_FightEnemy()
{
	NodeName = "Fight Enemy";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_FightEnemy::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTFightMemory* Memory = reinterpret_cast<FBTFightMemory*>(NodeMemory);
	Memory->TimeElapsed = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!AIC || !BB) return EBTNodeResult::Failed;

	AActor* Enemy = Cast<AActor>(BB->GetValueAsObject(FName("TargetEnemy")));
	if (!Enemy) return EBTNodeResult::Failed;

	AIC->MoveToActor(Enemy, AttackRange - 50.f);

	return EBTNodeResult::InProgress;
}

void UBTTask_FightEnemy::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTFightMemory* Memory = reinterpret_cast<FBTFightMemory*>(NodeMemory);
	Memory->TimeElapsed += DeltaSeconds;

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

	// Face the enemy
	const FVector Direction = (Enemy->GetActorLocation() - Survivor->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		Survivor->SetActorRotation(Direction.Rotation());
	}

	if (DistToEnemy <= AttackRange)
	{
		AIC->StopMovement();

		UInventoryComponent* Inventory = Survivor->GetInventoryComponent();
		if (Inventory)
		{
			const TArray<ABaseItem*>& Items = Inventory->GetInventory();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				ABaseItem* Item = Items[i];
				if (Item && (Item->GetItemType() == EItemType::Pistol || Item->GetItemType() == EItemType::Shotgun))
				{
					if (Item->GetValue() > 0)
					{
						Inventory->UseItem(i);
						UE_LOG(LogTemp, Log, TEXT("Fight: Fired weapon in slot %d"), i);
						FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
						return;
					}
				}
			}
		}

		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
	else
	{
		// Keep chasing
		AIC->MoveToActor(Enemy, AttackRange - 50.f);
	}
}
