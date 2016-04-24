
#include "../../Flare.h"

#include "FlareSpacecraftDamageSystem.h"
#include "../FlareSpacecraft.h"
#include "../../Game/FlareGame.h"
#include "../../Player/FlarePlayerController.h"
#include "../FlareEngine.h"
#include "../FlareOrbitalEngine.h"
#include "../FlareShell.h"

#define LOCTEXT_NAMESPACE "FlareSpacecraftDamageSystem"

/*----------------------------------------------------
	Constructor
----------------------------------------------------*/

UFlareSpacecraftDamageSystem::UFlareSpacecraftDamageSystem(const class FObjectInitializer& PCIP)
	: Super(PCIP)
	, Spacecraft(NULL)
	, LastDamageCauser(NULL)
{
}


/*----------------------------------------------------
	Gameplay events
----------------------------------------------------*/

void UFlareSpacecraftDamageSystem::TickSystem(float DeltaSeconds)
{
	// Apply heat variation : add producted heat then substract radiated heat.

	// Get the to heat production and heat sink surface
	float HeatProduction = 0.f;
	float HeatSinkSurface = 0.f;

	for (int32 i = 0; i < Components.Num(); i++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[i]);
		HeatProduction += Component->GetHeatProduction();
		HeatSinkSurface += Component->GetHeatSinkSurface();
	}

	// Add a part of sun radiation to ship heat production
	// Sun flow is 3.094KW/m^2 and keep only half and modulate 90% by sun occlusion
	HeatProduction += HeatSinkSurface * 3.094 * 0.5 * (1 - 0.9 * Spacecraft->GetGame()->GetPlanetarium()->GetSunOcclusion());

	// Heat up
	Data->Heat += HeatProduction * DeltaSeconds;
	// Radiate: Stefan-Boltzmann constant=5.670373e-8
	float Temperature = Data->Heat / Description->HeatCapacity;
	float HeatRadiation = 0.f;
	if (Temperature > 0)
	{
		HeatRadiation = HeatSinkSurface * 5.670373e-8 * FMath::Pow(Temperature, 4) / 1000;
	}
	// Don't radiate too much energy : negative temperature is not possible
	Data->Heat -= FMath::Min(HeatRadiation * DeltaSeconds, Data->Heat);

	// Overheat after 800°K, compute heat damage from temperature beyond 800°K : 0.005%/(°K*s)
	float OverheatDamage = (Temperature - GetOverheatTemperature()) * DeltaSeconds * 0.00005;
	float BurningDamage = FMath::Max((Temperature - GetBurnTemperature()) * DeltaSeconds * 0.0001, 0.0);

	// Update component temperature and apply heat damage
	for (int32 i = 0; i < Components.Num(); i++)
	{
		// Apply temperature
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[i]);

		// Overheat apply damage is necessary
		if (OverheatDamage > 0)
		{
			Component->ApplyHeatDamage(Component->GetTotalHitPoints() * OverheatDamage, Component->GetTotalHitPoints() * BurningDamage);
		}
	}

	// If damage have been applied, power production may have change
	if (OverheatDamage > 0)
	{
		UpdatePower();
	}

	// Power outage
	if (Data->PowerOutageDelay > 0)
	{
		Data->PowerOutageDelay -=  DeltaSeconds;
		if (Data->PowerOutageDelay <=0)
		{
			Data->PowerOutageDelay = 0;
			UpdatePower(); // To update light
		}
	}

	// Update Alive status
	if (WasAlive && !IsAlive())
	{
		AFlarePlayerController* PC = Spacecraft->GetGame()->GetPC();

		// Player kill
		if (PC && LastDamageCauser == PC->GetShipPawn() && Spacecraft != PC->GetShipPawn())
		{
			PC->Notify(LOCTEXT("ShipKilled", "Target destroyed"),
				FText::Format(LOCTEXT("ShipKilledFormat", "You destroyed a ship ({0}-class)"), Spacecraft->GetDescription()->Name),
				FName("ship-killed"),
				EFlareNotification::NT_Info);
		}

		// Company kill
		else if (PC && LastDamageCauser && PC->GetCompany() == LastDamageCauser->GetCompany())
		{
			PC->Notify(LOCTEXT("ShipKilledCompany", "Target destroyed"),
				FText::Format(LOCTEXT("ShipKilledCompanyFormat", "Your {0}-class ship destroyed a ship ({1}-class)"),
					Spacecraft->GetDescription()->Name,
					LastDamageCauser->GetDescription()->Name),
				FName("ship-killed"),
				EFlareNotification::NT_Info);
		}

		WasAlive = false;
		OnControlLost();
	}

	TimeSinceLastExternalDamage += DeltaSeconds;
}

void UFlareSpacecraftDamageSystem::Initialize(AFlareSpacecraft* OwnerSpacecraft, FFlareSpacecraftSave* OwnerData)
{
	Spacecraft = OwnerSpacecraft;
	Components = Spacecraft->GetComponentsByClass(UFlareSpacecraftComponent::StaticClass());
	Description = Spacecraft->GetDescription();
	Data = OwnerData;
}

void UFlareSpacecraftDamageSystem::Start()
{
	// Reload components
	Components = Spacecraft->GetComponentsByClass(UFlareSpacecraftComponent::StaticClass());

	TArray<UFlareSpacecraftComponent*> PowerSources;
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
		// Fill power sources
		if (Component->IsGenerator())
		{
			PowerSources.Add(Component);
		}
	}

	// Second pass, update component power sources and update power
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
		Component->UpdatePowerSources(&PowerSources);
	}
	UpdatePower();

	// Init alive status
	WasAlive = IsAlive();
	TimeSinceLastExternalDamage = 10000;
}

void UFlareSpacecraftDamageSystem::SetLastDamageCauser(AFlareSpacecraft* Ship)
{
	LastDamageCauser = Ship;
}

void UFlareSpacecraftDamageSystem::UpdatePower()
{
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
		Component->UpdatePower();
	}
}

void UFlareSpacecraftDamageSystem::OnControlLost()
{
	AFlarePlayerController* PC = Spacecraft->GetGame()->GetPC();

	// Lost player ship
	if (Spacecraft == PC->GetShipPawn())
	{
		if (LastDamageCauser)
		{
			PC->Notify(
				LOCTEXT("ShipDestroyed", "Your ship has been destroyed"),
				FText::Format(LOCTEXT("ShipDestroyedFormat", "Your ship was destroyed by a {0}-class ship"), LastDamageCauser->GetDescription()->Name),
				FName("ship-destroyed"),
				EFlareNotification::NT_Military, EFlareMenu::MENU_Company);
		}
		else
		{
			PC->Notify(
				LOCTEXT("ShipCrashed", "Crash"),
				FText::FText(LOCTEXT("ShipDestroyedFormat", "You crashed your ship")),
				FName("ship-crashed"),
				EFlareNotification::NT_Military, EFlareMenu::MENU_Company);
		}

		PC->GetMenuManager()->OpenMenu(EFlareMenu::MENU_Dashboard);
	}

	// Lost company ship
	else if (Spacecraft->GetCompany() == PC->GetCompany())
	{
		PC->Notify(LOCTEXT("ShipDestroyedCompany", "One of your ships has been destroyed"),
			FText::Format(LOCTEXT("ShipDestroyedCompanyFormat", "Your {0}-class ship was destroyed"), Spacecraft->GetDescription()->Name),
			FName("ship-killed"),
			EFlareNotification::NT_Military);
	}

	Spacecraft->GetNavigationSystem()->Undock();
	Spacecraft->GetNavigationSystem()->AbortAllCommands();
}

void UFlareSpacecraftDamageSystem::OnCollision(class AActor* Other, FVector HitLocation, FVector NormalImpulse)
{
	// If receive hit from over actor, like a ship we must apply collision damages.
	// The applied damage energy is 0.2% of the kinetic energy of the other actor. The kinetic
	// energy is calculated from the relative speed between the 2 actors, and only with the relative
	// speed projected in the axis of the collision normal: if 2 very fast ship only slightly touch,
	// only few energy will be decipated by the impact.
	//
	// The damages are applied only to the current actor, the ReceiveHit method of the other actor
	// will also call an it will apply its collision damages itself.

	// If the other actor is a projectile, specific weapon damage code is done in the projectile hit
	// handler: in this case we ignore the collision
	AFlareShell* OtherProjectile = Cast<AFlareShell>(Other);
	if (OtherProjectile)
	{
		return;
	}

	UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Other->GetRootComponent());
	if (!OtherRoot)
	{
		return;
	}

	// Relative velocity
	FVector DeltaVelocity = ((OtherRoot->GetPhysicsLinearVelocity() - Spacecraft->Airframe->GetPhysicsLinearVelocity()) / 100);

	// Compute the relative velocity in the impact axis then compute kinetic energy
	/*float ImpactSpeed = DeltaVelocity.Size();
	float ImpactMass = FMath::Min(Spacecraft->GetSpacecraftMass(), OtherRoot->GetMass());
	*/

	//200 m /s -> 6301.873047 * 20000 -> 300 / 2 damage

	float ImpactSpeed = 0;
	float ImpactEnergy = 0;
	float ImpactMass = Spacecraft->GetSpacecraftMass();

	// Check if the mass was set and is valid
	if (ImpactMass > KINDA_SMALL_NUMBER)
	{
		ImpactSpeed = NormalImpulse.Size() / (ImpactMass * 100.f);
		ImpactEnergy = ImpactMass * ImpactSpeed / 8402.f;
	}

	float  Radius = 0.2 + FMath::Sqrt(ImpactEnergy) * 0.11;
	//FLOGV("OnCollision %s", *Spacecraft->GetImmatriculation().ToString());
	//FLOGV("  OtherRoot->GetPhysicsLinearVelocity()=%s", *OtherRoot->GetPhysicsLinearVelocity().ToString());
	//FLOGV("  OtherRoot->GetPhysicsLinearVelocity().Size()=%f", OtherRoot->GetPhysicsLinearVelocity().Size());
	//FLOGV("  Spacecraft->Airframe->GetPhysicsLinearVelocity()=%s", *Spacecraft->Airframe->GetPhysicsLinearVelocity().ToString());
	//FLOGV("  Spacecraft->Airframe->GetPhysicsLinearVelocity().Size()=%f", Spacecraft->Airframe->GetPhysicsLinearVelocity().Size());
	//FLOGV("  dot=%f", FVector::DotProduct(DeltaVelocity.GetUnsafeNormal(), HitNormal.GetUnsafeNormal()));
	/*FLOGV("  DeltaVelocity=%s", *DeltaVelocity.ToString());
	FLOGV("  ImpactSpeed=%f", ImpactSpeed);
	FLOGV("  ImpactMass=%f", ImpactMass);
	FLOGV("  ImpactEnergy=%f", ImpactEnergy);
	FLOGV("  Radius=%f", Radius);*/



	bool HasHit = false;
	FHitResult BestHitResult;
	float BestHitDistance = 0;

	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
		if (Component)
		{
			FHitResult HitResult(ForceInit);
			FCollisionQueryParams TraceParams(FName(TEXT("Fragment Trace")), true);
			TraceParams.bTraceComplex = true;
			TraceParams.bReturnPhysicalMaterial = false;
			Component->LineTraceComponent(HitResult, HitLocation, HitLocation + Spacecraft->GetLinearVelocity().GetUnsafeNormal() * 10000, TraceParams);

			if (HitResult.Actor.IsValid()){
				float HitDistance = (HitResult.Location - HitLocation).Size();
				if (!HasHit || HitDistance < BestHitDistance)
				{
					BestHitDistance = HitDistance;
					BestHitResult = HitResult;
				}

				//FLOGV("Collide hit %s at a distance=%f", *Component->GetReadableName(), HitDistance);
				HasHit = true;
			}
		}

	}

	if (HasHit)
	{
		//DrawDebugLine(Spacecraft->GetWorld(), HitLocation, BestHitResult.Location, FColor::Magenta, true);
	}
	else
	{
		int32 BestComponentIndex = -1;
		BestHitDistance = 0;

		for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
		{
			UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
			if (Component)
			{
				float ComponentDistance = (Component->GetComponentLocation() - HitLocation).Size();
				if (BestComponentIndex == -1 || BestHitDistance > ComponentDistance)
				{
					BestComponentIndex = ComponentIndex;
					BestHitDistance = ComponentDistance;
				}
			}
		}
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[BestComponentIndex]);


		FCollisionQueryParams TraceParams(FName(TEXT("Fragment Trace")), true);
		TraceParams.bTraceComplex = true;
		TraceParams.bReturnPhysicalMaterial = false;
		Component->LineTraceComponent(BestHitResult, HitLocation, Component->GetComponentLocation(), TraceParams);
		//DrawDebugLine(Spacecraft->GetWorld(), HitLocation, BestHitResult.Location, FColor::Yellow, true);


	}

	AFlareSpacecraft* OtherSpacecraft = Cast<AFlareSpacecraft>(Other);
	UFlareCompany* DamageSource = NULL;
	if (OtherSpacecraft)
	{
		DamageSource = OtherSpacecraft->GetCompany();
		LastDamageCauser = OtherSpacecraft;
	}
	else
	{
		LastDamageCauser = NULL;
	}
	ApplyDamage(ImpactEnergy, Radius, BestHitResult.Location, EFlareDamage::DAM_Collision, DamageSource);
}

void UFlareSpacecraftDamageSystem::ApplyDamage(float Energy, float Radius, FVector Location, EFlareDamage::Type DamageType, UFlareCompany* DamageSource)
{
	// The damages are applied to all component touching the sphere defined by the radius and the
	// location in parameter.
	// The maximum damage are applied to a component only if its bounding sphere touch the center of
	// the damage sphere. There is a linear decrease of damage with a minumum of 0 if the 2 sphere
	// only touch.

	//FLOGV("Apply %f damages to %s with radius %f at %s", Energy, *(Spacecraft->GetImmatriculation().ToString()), Radius, *Location.ToString());
	//DrawDebugSphere(Spacecraft->GetWorld(), Location, Radius * 100, 12, FColor::Red, true);

	bool IsAliveBeforeDamage = IsAlive();

	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);

		float ComponentSize;
		FVector ComponentLocation;
		Component->GetBoundingSphere(ComponentLocation, ComponentSize);

		float Distance = (ComponentLocation - Location).Size() / 100.0f;
		float IntersectDistance =  Radius + ComponentSize/100 - Distance;

		//DrawDebugSphere(Spacecraft->GetWorld(), ComponentLocation, ComponentSize, 12, FColor::Green, true);

		// Hit this component
		if (IntersectDistance > 0)
		{
			//FLOGV("Component %s. ComponentSize=%f, Distance=%f, IntersectDistance=%f", *(Component->GetReadableName()), ComponentSize, Distance, IntersectDistance);
			float Efficiency = FMath::Clamp(IntersectDistance / Radius , 0.0f, 1.0f);
			float InflictedDamageRatio = Component->ApplyDamage(Energy * Efficiency);

			if(DamageSource != NULL && DamageSource != Spacecraft->GetCompany())
			{
				Spacecraft->GetCompany()->GiveReputation(DamageSource, - (InflictedDamageRatio * 30), true);
			}
		}
	}

	// Update power
	UpdatePower();

	// Heat the ship
	Data->Heat += Energy;

	switch (DamageType) {
	case EFlareDamage::DAM_ArmorPiercing:
	case EFlareDamage::DAM_HighExplosive:
	case EFlareDamage::DAM_HEAT:
		//FLOGV("%s Reset TimeSinceLastExternalDamage", *Spacecraft->GetImmatriculation().ToString());
		TimeSinceLastExternalDamage = 0;
		break;
	case EFlareDamage::DAM_Collision:
	case EFlareDamage::DAM_Overheat:
	default:
		// Don't reset timer
		break;
	}
}

void UFlareSpacecraftDamageSystem::OnElectricDamage(float DamageRatio)
{
	float MaxPower = 0.f;
	float AvailablePower = 0.f;

	float Total = 0.f;
	float GeneratorCount = 0;
	for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
	{
		UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
		MaxPower += Component->GetMaxGeneratedPower();
		AvailablePower += Component->GetGeneratedPower();
	}

	float PowerRatio = AvailablePower/MaxPower;


	//FLOGV("OnElectricDamage initial PowerOutageDelay=%f, PowerOutageAcculumator=%f, DamageRatio=%f, PowerRatio=%f", Data->PowerOutageDelay, Data->PowerOutageAcculumator, DamageRatio, PowerRatio);


	Data->PowerOutageAcculumator += DamageRatio * 2.f;
	if (!Data->PowerOutageDelay)
	{
		if (FMath::FRand() > 1.f + PowerRatio / 2.f - Data->PowerOutageAcculumator / 2.f)
		{
			Data->PowerOutageDelay += FMath::FRandRange(1, 5) /  (2 * PowerRatio);
			Data->PowerOutageAcculumator = -Data->PowerOutageAcculumator * PowerRatio;
			UpdatePower();
		}
	}
}

float UFlareSpacecraftDamageSystem::GetSubsystemHealth(EFlareSubsystem::Type Type, bool WithArmor, bool WithAmmo) const
{
	float Health = 0.f;

	switch(Type)
	{
		case EFlareSubsystem::SYS_Propulsion:
		{
			TArray<UActorComponent*> Engines = Spacecraft->GetComponentsByClass(UFlareEngine::StaticClass());
			float Total = 0.f;
			float EngineCount = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < Engines.Num(); ComponentIndex++)
			{
				UFlareEngine* Engine = Cast<UFlareEngine>(Engines[ComponentIndex]);
				if (Engine->IsA(UFlareOrbitalEngine::StaticClass()))
				{
					EngineCount+=1.f;
					Total+=Engine->GetDamageRatio(WithArmor)*(Engine->IsPowered() ? 1 : 0);
				}
			}
			Health = Total/EngineCount;
		}
		break;
		case EFlareSubsystem::SYS_RCS:
		{
			TArray<UActorComponent*> Engines = Spacecraft->GetComponentsByClass(UFlareEngine::StaticClass());
			float Total = 0.f;
			float EngineCount = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < Engines.Num(); ComponentIndex++)
			{
				UFlareEngine* Engine = Cast<UFlareEngine>(Engines[ComponentIndex]);
				if (!Engine->IsA(UFlareOrbitalEngine::StaticClass()))
				{
					EngineCount+=1.f;
					Total+=Engine->GetDamageRatio(WithArmor)*(Engine->IsPowered() ? 1 : 0);
				}
			}
			Health = Total/EngineCount;
		}
		break;
		case EFlareSubsystem::SYS_LifeSupport:
		{
			if (Spacecraft->GetCockpit())
			{
				Health = Spacecraft->GetCockpit()->GetDamageRatio(WithArmor) * (Spacecraft->GetCockpit()->IsPowered() ? 1 : 0);
			}
			else
			{
				// No cockpit mean no destructible
				Health = 1.0f;
			}
		}
		break;
		case EFlareSubsystem::SYS_Power:
		{
			float Total = 0.f;
			float GeneratorCount = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
			{
				UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
				if (Component->IsGenerator())
				{
					GeneratorCount+=1.f;
					Total+=Component->GetDamageRatio(WithArmor);
				}
			}
			Health = Total/GeneratorCount;
		}
		break;
		case EFlareSubsystem::SYS_Weapon:
		{
			TArray<UActorComponent*> Weapons = Spacecraft->GetComponentsByClass(UFlareWeapon::StaticClass());
			float Total = 0.f;
			for (int32 ComponentIndex = 0; ComponentIndex < Weapons.Num(); ComponentIndex++)
			{
				UFlareWeapon* Weapon = Cast<UFlareWeapon>(Weapons[ComponentIndex]);
				Total += Weapon->GetDamageRatio(WithArmor)*(Weapon->IsPowered() ? 1 : 0)*((Weapon->GetCurrentAmmo() > 0 || !WithAmmo) ? 1 : 0);
			}
			if(Weapons.Num() == 0)
			{
				Health = 0;
			}
			else
			{
				Health = Total/Weapons.Num();
			}

		}
		break;
		case EFlareSubsystem::SYS_Temperature:
		{
			float Total = 0.f;
			float HeatSinkCount = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ComponentIndex++)
			{
				UFlareSpacecraftComponent* Component = Cast<UFlareSpacecraftComponent>(Components[ComponentIndex]);
				if (Component->IsHeatSink())
				{
					HeatSinkCount+=1.f;
					Total+=Component->GetDamageRatio(WithArmor) * (Spacecraft->GetCockpit() && Spacecraft->GetCockpit()->IsPowered() ? 1 : 0);
				}
			}
			Health = Total/HeatSinkCount;
		}
		break;
	}

	return Health;
}

float UFlareSpacecraftDamageSystem::GetWeaponGroupHealth(int32 GroupIndex, bool WithArmor, bool WithAmmo) const
{
	 FFlareWeaponGroup* WeaponGroup = Spacecraft->GetWeaponsSystem()->GetWeaponGroup(GroupIndex);
	 float Health = 0.0;

	 float Total = 0.f;
	 for (int32 ComponentIndex = 0; ComponentIndex < WeaponGroup->Weapons.Num(); ComponentIndex++)
	 {
		 UFlareWeapon* Weapon = Cast<UFlareWeapon>(WeaponGroup->Weapons[ComponentIndex]);
		 Total += Weapon->GetDamageRatio(WithArmor)*(Weapon->IsPowered() ? 1 : 0)*((Weapon->GetCurrentAmmo() > 0 || !WithAmmo) ? 1 : 0);
	 }
	 Health = Total/WeaponGroup->Weapons.Num();

	 return Health;
}

float UFlareSpacecraftDamageSystem::GetTemperature() const
{
	return Data->Heat / Description->HeatCapacity;
}


bool UFlareSpacecraftDamageSystem::IsAlive() const
{
	return GetSubsystemHealth(EFlareSubsystem::SYS_LifeSupport) > 0;
}

bool UFlareSpacecraftDamageSystem::IsPowered() const
{
	if (Spacecraft->GetCockpit())
	{
		return Spacecraft->GetCockpit()->IsPowered();
	}
	else
	{
		return false;
	}
}

bool UFlareSpacecraftDamageSystem::HasPowerOutage() const
{
	return GetPowerOutageDuration() > 0.f;
}

float UFlareSpacecraftDamageSystem::GetPowerOutageDuration() const
{
	return Data->PowerOutageDelay;
}

#undef LOCTEXT_NAMESPACE
