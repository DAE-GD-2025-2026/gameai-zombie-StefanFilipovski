// Local movement steering: Seek, Arrive, Flee, Wander, Obstacle Avoidance and Separation, weighted-blended.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SteeringComponent.generated.h"

UENUM(BlueprintType)
enum class ESteeringMode : uint8
{
	Idle    UMETA(DisplayName = "Idle"),
	Seek    UMETA(DisplayName = "Seek"),
	Arrive  UMETA(DisplayName = "Arrive"),
	Flee    UMETA(DisplayName = "Flee"),
	Wander  UMETA(DisplayName = "Wander")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_ZOMBIE_API USteeringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USteeringComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void SeekTo(const FVector& Target);     
	void ArriveAt(const FVector& Target);   
	void FleeFrom(const FVector& Threat);   
	void WanderAround();                    
	void Stop();                            

	
	bool HasArrived(float Radius) const;

	// Always-on blended behaviors can be toggled per situation.
	void SetObstacleAvoidanceEnabled(bool bEnabled) { bUseObstacleAvoidance = bEnabled; }
	void SetSeparationEnabled(bool bEnabled) { bUseSeparation = bEnabled; }

protected:
	virtual void BeginPlay() override;

	// Individual steering behaviors
	FVector Seek(const FVector& Target) const;
	FVector Arrive(const FVector& Target) const;
	FVector Flee(const FVector& Threat) const;
	FVector Wander(float DeltaTime);
	FVector ObstacleAvoidance(const FVector& DesiredDir) const;
	FVector Separation() const;

	
	float GetReferenceSpeed() const;

	// Tunables
	UPROPERTY(EditAnywhere, Category = "Steering|Arrive")
	float ArriveSlowRadius = 200.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Arrive")
	float ArriveStopRadius = 60.f;

	// Tuned for long, smooth sweeps to cover ground.
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderRadius = 90.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderDistance = 450.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Wander")
	float WanderJitter = 15.f;

	UPROPERTY(EditAnywhere, Category = "Steering|Avoidance")
	float AvoidanceDistance = 150.f;    
	UPROPERTY(EditAnywhere, Category = "Steering|Avoidance")
	float AvoidanceWeight = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Steering|Separation")
	float SeparationRadius = 220.f;
	UPROPERTY(EditAnywhere, Category = "Steering|Separation")
	float SeparationWeight = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Steering")
	float PrimaryWeight = 1.0f;

	
	UPROPERTY(EditAnywhere, Category = "Steering")
	bool bFaceTravelDirection = false;

	UPROPERTY(EditAnywhere, Category = "Steering|Debug")
	bool bDrawDebug = false;

private:
	ESteeringMode Mode = ESteeringMode::Idle;
	FVector TargetLocation = FVector::ZeroVector;

	bool bUseObstacleAvoidance = true;
	bool bUseSeparation = false;

	// Wander internal state
	float WanderAngle = 0.f;

	UPROPERTY()
	APawn* OwnerPawn = nullptr;
};
