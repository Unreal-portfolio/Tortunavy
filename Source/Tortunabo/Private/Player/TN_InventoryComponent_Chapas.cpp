// UTN_InventoryComponent — contador de chapas (#858). Misma clase en otra unidad de traducción: las chapas no ocupan ninguno
// de los dos huecos y no pesan. La lógica (tope y pago) está en Game/TN_ChapaRules.h.

#include "Player/TN_InventoryComponent.h"
#include "Core/TN_Log.h"
#include "Game/TN_ChapaRules.h"
#include "GameFramework/Actor.h"
#include "Settings/TN_EconomySettings.h"

int32 UTN_InventoryComponent::AddChapas(int32 Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return 0;
	}
	const int32 Accepted = TNChapaRules::AcceptedChapas(ChapaCount, Amount, UTN_EconomySettings::Get().MaxChapas);
	if (Accepted > 0)
	{
		ChapaCount += Accepted;
		UE_LOG(LogTortunabo, Verbose, TEXT("[Chapas] %s +%d (lleva %d)."), *GetNameSafe(Owner), Accepted, ChapaCount);
	}
	return Accepted;
}

bool UTN_InventoryComponent::TrySpendChapas(int32 Cost)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !TNChapaRules::CanSpend(ChapaCount, Cost))
	{
		return false;
	}
	ChapaCount -= Cost;
	UE_LOG(LogTortunabo, Verbose, TEXT("[Chapas] %s -%d (le quedan %d)."), *GetNameSafe(Owner), Cost, ChapaCount);
	return true;
}
