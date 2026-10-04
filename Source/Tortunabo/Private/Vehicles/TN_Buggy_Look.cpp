// ATN_Buggy: modelo y pintura de la tienda (#297, #114). El aspecto sale del PlayerState de la conductora
// (EquippedBuggyLook, validado y replicado por el servidor), así que cada máquina pinta lo mismo sin RPC propias. El de
// serie con la pintura de serie es el buggy de siempre con la skin de su equipo (ApplyTint).

#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyCosmetics.h"
#include "Vehicles/TN_BuggyLookComponent.h"
#include "Core/TN_CoopPlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerState.h"

void ATN_Buggy::RefreshBuggyLook(bool bForce)
{
	if (GetNetMode() == NM_DedicatedServer || !BuggyLook)
	{
		return;
	}
	if (!BuggyLook->HasBuiltLook())
	{
		// Primera vez: el aspecto viste la carrocería y los neumáticos del buggy, y el buggy escucha los cambios de aspecto
		// de los PlayerState (el de su conductora lo repinta al momento).
		TArray<UStaticMeshComponent*> TireParts;
		for (UStaticMeshComponent* Tire : Tires) { TireParts.Add(Tire); }
		BuggyLook->UseVehicleParts(Body, TireParts);
		ATN_CoopPlayerState::OnAnyBuggyLookChanged.AddWeakLambda(this, [this](const ATN_CoopPlayerState* Changed) { NotifyDriverLookChanged(Changed); });
	}
	// Los pilotos IA también tienen PlayerState (para la tabla de puestos): llevan el de serie con la skin de su equipo.
	const ATN_CoopPlayerState* Driver = bDriverSeated ? Cast<ATN_CoopPlayerState>(DriverPlayerState) : nullptr;
	const FTN_BuggyLook Look = Driver && !Driver->IsABot() ? Driver->EquippedBuggyLook : FTN_BuggyLook();
	if (!BuggyLook->ApplyLook(Look, TeamIndex, bForce) || !BuggyLook->UsesTeamSkin())
	{
		return;
	}
	// De vuelta al de serie con su pintura: la skin del equipo otra vez en la carrocería y en los neumáticos.
	bool bStale = !TintMaterial || (Body && Body->GetMaterial(0) != TintMaterial);
	for (const UStaticMeshComponent* Tire : Tires)
	{
		bStale |= Tire && Tire->GetMaterial(0) != TintMaterial;
	}
	if (bStale)
	{
		TintMaterial = nullptr;
		ApplyTint();
	}
}

bool ATN_Buggy::UsesTeamSkin() const
{
	return !BuggyLook || BuggyLook->UsesTeamSkin();
}

FVector ATN_Buggy::GetExhaustLocal() const
{
	return BuggyLook ? BuggyLook->GetExhaustLocal(BoostEffectOffset) : BoostEffectOffset;
}

void ATN_Buggy::NotifyDriverLookChanged(const APlayerState* ChangedPlayerState)
{
	if (ChangedPlayerState && ChangedPlayerState == DriverPlayerState)
	{
		RefreshBuggyLook(false);
	}
}
