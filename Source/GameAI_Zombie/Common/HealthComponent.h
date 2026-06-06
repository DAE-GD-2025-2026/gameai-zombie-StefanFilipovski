// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

// Declare OnDeath
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathSignature);
// Broadcast whenever damage is taken (so the AI can react to being hit — e.g. by an attacker it can't see).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDamagedSignature);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_ZOMBIE_API UHealthComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UHealthComponent();
	
	virtual void BeginPlay() override;
	

	UFUNCTION(BlueprintPure)
	int GetMaxHealth() const {return MaxHealth;}
	UFUNCTION(BlueprintPure)
	int GetHealth() const {return Health;}
	UFUNCTION(BlueprintPure)
	bool IsDead() const {return Health <= 0;}
	UFUNCTION(BlueprintPure)
	bool IsAlive() const {return !IsDead();}
	
	UFUNCTION(BlueprintCallable)
	void TakeDamage(int Amount);
	UFUNCTION(BlueprintCallable)
	void HealDamage(int Amount);
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Health")
	FOnDeathSignature OnDeath;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Health")
	FOnDamagedSignature OnDamaged;
	
protected:
	void HandleDeath() const;
	
	
	
private:
	UPROPERTY(EditDefaultsOnly, Category="Health")
	int MaxHealth{10};
	int Health{MaxHealth};
};
