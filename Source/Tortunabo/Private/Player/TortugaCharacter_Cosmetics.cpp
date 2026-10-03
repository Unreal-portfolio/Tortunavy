// ─────────────────────────────────────────────────────────────────────────────
// TortugaCharacter — Cosméticos (casco, caparazón, color y ojos).
//
// El PlayerState replica los cuatro por separado (EquippedHelmetId, EquippedShellId, EquippedSkinId y EquippedEyesId) y cada OnRep llama
// aquí; el personaje guarda el conjunto en CosmeticLook y lo aplica entero con UTN_CosmeticLook, lo mismo que usan el
// tendero y las vistas previas de la tienda y el probador.
// ─────────────────────────────────────────────────────────────────────────────

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Core/TN_CoopPlayerState.h"

// ── Cosmetics ─────────────────────────────────────────────────────────────────

void ATortugaCharacter::UpdateHelmetMesh(FName HelmetId)
{
	CosmeticLook.HelmetId = HelmetId;
	RefreshCosmeticLook();
	UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] '%s' casco '%s'."), *GetName(), *HelmetId.ToString());
}

void ATortugaCharacter::UpdateSkinVisual(FName SkinId)
{
	CosmeticLook.SkinId = SkinId;
	if (const ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		CosmeticLook.ShellId = TNPS->EquippedShellId;
		CosmeticLook.EyesId = TNPS->EquippedEyesId;
	}
	RefreshCosmeticLook();
	UE_LOG(LogTortunabo, Log, TEXT("[TortugaCharacter] '%s' color '%s', caparazón '%s'."), *GetName(), *SkinId.ToString(), *CosmeticLook.ShellId.ToString());
}

void ATortugaCharacter::ApplyShellSlot()
{
	if (const ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		CosmeticLook.ShellId = TNPS->EquippedShellId;
		CosmeticLook.EyesId = TNPS->EquippedEyesId;
	}
	RefreshCosmeticLook();
}

void ATortugaCharacter::RefreshCosmeticLook()
{
	UTN_CosmeticLook::ApplyLook(this, GetMesh(), HelmetMeshComp, CosmeticLook, DefaultSkelMeshMaterials);
	// Material nuevo: TickEyes vuelve a escribir el parpadeo y la espiral.
	EyeBlinkApplied = -1.f;
	EyeDizzyApplied = -1.f;
}

bool ATortugaCharacter::ApplyCosmeticsFromPlayerState()
{
	if (const ATN_CoopPlayerState* TNPS = GetPlayerState<ATN_CoopPlayerState>())
	{
		CosmeticLook.HelmetId = TNPS->EquippedHelmetId;
		CosmeticLook.SkinId = TNPS->EquippedSkinId;
		CosmeticLook.ShellId = TNPS->EquippedShellId;
		CosmeticLook.EyesId = TNPS->EquippedEyesId;
		RefreshCosmeticLook();
		return true;
	}
	return false;
}
