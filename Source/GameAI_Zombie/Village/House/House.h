// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "House.generated.h"

class UAIPerceptionStimuliSourceComponent;

USTRUCT(BlueprintType)
struct GAMEAI_ZOMBIE_API FHouseBounds
{
	GENERATED_BODY()

	FVector Origin;
	FVector Extent;
};

/** The four house layouts. Every type's opening(s) sit at the building's CORNERS, which is what lets
 *  the survivor reason about how to get in. */
UENUM(BlueprintType)
enum class EHouseType : uint8
{
	Square,         // BP_House  : square room, 1 corner opening
	SquareTwoDoor,  // BP_HouseA : square room, 2 corner openings
	Rectangle,      // BP_HouseB : long rectangle, 1 corner opening
	OpenCorners,    // BP_HouseC : all 4 corners open
	Unknown
};

UCLASS()
class GAMEAI_ZOMBIE_API AHouse : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHouse();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Lets the survivor's AIPerception (sight) discover this house, so exploration is
	// driven by perception rather than a global world query.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Perception")
	UAIPerceptionStimuliSourceComponent* StimuliSource;


public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	FHouseBounds GetBounds() const;

	/** Which layout this house is (derived from the blueprint class name). */
	EHouseType GetHouseType() const;

	/** Human-readable type, for logging/knowledge. */
	FString GetHouseTypeString() const;

	/** World positions of the building's four corners (where the openings are), each pulled INWARD
	 *  toward the centre by Inset so the point sits just inside the doorway gap — i.e. good navmesh
	 *  targets for "go through the opening". Order is deterministic. */
	void GetCornerOpenings(FVector OutCorners[4], float Inset = 90.f) const;
};
