
#include "../Flare.h"
#include "FlareFleet.h"
#include "FlareCompany.h"
#include "FlareSimulatedSector.h"

/*----------------------------------------------------
	Constructor
----------------------------------------------------*/

UFlareFleet::UFlareFleet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UFlareFleet::Load(const FFlareFleetSave& Data)
{
	FleetCompany = Cast<UFlareCompany>(GetOuter());
	Game = FleetCompany->GetGame();
	FleetData = Data;
	IsShipListLoaded = false;
}

FFlareFleetSave* UFlareFleet::Save()
{
	return &FleetData;
}

/*----------------------------------------------------
	Gameplay
----------------------------------------------------*/


bool UFlareFleet::IsTraveling()
{
	return CurrentTravel != NULL;
}

void UFlareFleet::AddShip(UFlareSimulatedSpacecraft* Ship)
{
	if(IsTraveling())
	{
		FLOGV("Fleet Disband fail: '%s' is travelling", *GetFleetName());
		return;
	}

	if(GetCurrentSector() != Ship->GetCurrentSector())
	{
		FLOGV("Fleet Merge fail: '%s' is the sector '%s' but '%s' is the sector '%s'",
			  *GetFleetName(),
			  *GetCurrentSector()->GetSectorName(),
			  *Ship->GetImmatriculation(),
			  *Ship->GetCurrentSector()->GetSectorName());
		return;
	}

	UFlareFleet* OldFleet = Ship->GetCurrentFleet();
	if(OldFleet)
	{
		OldFleet->RemoveShip(Ship);
	}

	FleetData.ShipImmatriculations.Add(Ship->GetImmatriculation());
	FleetShips.AddUnique(Ship);
	Ship->SetCurrentFleet(this);

}

void UFlareFleet::RemoveShip(UFlareSimulatedSpacecraft* Ship)
{
	if(IsTraveling())
	{
		FLOGV("Fleet RemoveShip fail: '%s' is travelling", *GetFleetName());
		return;
	}

	FleetData.ShipImmatriculations.Remove(Ship->GetImmatriculation());
	FleetShips.Remove(Ship);
	Ship->SetCurrentFleet(NULL);
}

/** Remove all ship from the fleet and delete it. Not possible during travel */
void UFlareFleet::Disband()
{
	if(IsTraveling())
	{
		FLOGV("Fleet Disband fail: '%s' is travelling", *GetFleetName());
		return;
	}

	GetCurrentSector()->DisbandFleet(this);
	FleetCompany->RemoveFleet(this);

}

void UFlareFleet::Merge(UFlareFleet* Fleet)
{
	if(Fleet->IsTraveling())
	{
		FLOGV("Fleet Merge fail: '%s' is travelling", *Fleet->GetFleetName());
		return;
	}

	if(IsTraveling())
	{
		FLOGV("Fleet Merge fail: '%s' is travelling", *GetFleetName());
		return;
	}

	if(GetCurrentSector() != Fleet->GetCurrentSector())
	{
		FLOGV("Fleet Merge fail: '%s' is the sector '%s' but '%s' is the sector '%s'",
			  *GetFleetName(),
			  *GetCurrentSector()->GetSectorName(),
			  *Fleet->GetFleetName(),
			  *Fleet->GetCurrentSector()->GetSectorName());
		return;
	}

	TArray<UFlareSimulatedSpacecraft*> Ships = Fleet->GetShips();
	Fleet->Disband();
	for(int ShipIndex = 0; ShipIndex < Ships.Num(); ShipIndex++)
	{
		AddShip(Ships[ShipIndex]);
	}
}

void UFlareFleet::SetCurrentSector(UFlareSimulatedSector* Sector)
{
	CurrentSector = Sector;
	CurrentTravel = NULL;
	InitShipList();
}

void UFlareFleet::SetCurrentTravel(UFlareTravel* Travel)
{
	CurrentSector = NULL;
	CurrentTravel = Travel;
	InitShipList();
}

void UFlareFleet::InitShipList()
{
	if(!IsShipListLoaded)
	{
		IsShipListLoaded = true;
		FleetShips.Empty();
		for(int ShipIndex = 0; ShipIndex < FleetData.ShipImmatriculations.Num(); ShipIndex++)
		{
			UFlareSimulatedSpacecraft* Ship = FleetCompany->FindSpacecraftByImmatriculation(FleetData.ShipImmatriculations[ShipIndex]);
			Ship->SetCurrentFleet(this);
			FleetShips.Add(Ship);
		}
	}
}

/*----------------------------------------------------
	Getters
----------------------------------------------------*/


TArray<UFlareSimulatedSpacecraft*>& UFlareFleet::GetShips()
{
	InitShipList();

	return FleetShips;
}
