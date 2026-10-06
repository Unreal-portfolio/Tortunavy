#include "Player/TN_TurtleSurface.h"
#include "Player/TortugaCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace TNTurtleSurface
{
	template <int32 N>
	bool ContainsAnyWord(const FString& Text, const TCHAR* const (&Words)[N])
	{
		for (const TCHAR* Word : Words)
		{
			if (Text.Contains(Word, ESearchCase::CaseSensitive)) { return true; }
		}
		return false;
	}

	/**
	 * Superficie por nombres (ya en minúsculas: componente, clase y nombre del actor, malla y material). Por orden: agua,
	 * arena (el castillo de arena del lobby es arena aunque diga «castle»), madera, tierra y roca.
	 */
	uint8 PresetFromNames(const FString& Text)
	{
		static const TCHAR* const WaterWords[] = { TEXT("water"), TEXT("agua"), TEXT("puddle"), TEXT("charco") };
		static const TCHAR* const SandWords[] = { TEXT("sand"), TEXT("arena"), TEXT("beach"), TEXT("playa"), TEXT("dune"), TEXT("duna"),
			TEXT("landscape") };
		static const TCHAR* const WoodWords[] = { TEXT("wood"), TEXT("madera"), TEXT("plank"), TEXT("tablon"), TEXT("tablón"),
			TEXT("bridge"), TEXT("puente"), TEXT("log_"), TEXT("tronco"), TEXT("crate"), TEXT("barrel"), TEXT("barril"), TEXT("dock"),
			TEXT("muelle"), TEXT("pier"), TEXT("boat"), TEXT("barco"), TEXT("raft"), TEXT("balsa"), TEXT("deck"), TEXT("pallet"),
			TEXT("palet"), TEXT("fence"), TEXT("valla"), TEXT("table"), TEXT("mesa"), TEXT("shell"), TEXT("tortuga"), TEXT("turtle") };
		static const TCHAR* const SoilWords[] = { TEXT("grass"), TEXT("hierba"), TEXT("cesped"), TEXT("césped"), TEXT("dirt"),
			TEXT("tierra"), TEXT("mud"), TEXT("barro"), TEXT("fango"), TEXT("leaf"), TEXT("leaves"), TEXT("hoja"), TEXT("moss"),
			TEXT("musgo"), TEXT("soil"), TEXT("jungle"), TEXT("selva"), TEXT("garden"), TEXT("jardin"), TEXT("jardín"), TEXT("turf") };
		static const TCHAR* const RockWords[] = { TEXT("rock"), TEXT("roca"), TEXT("stone"), TEXT("piedra"), TEXT("cliff"),
			TEXT("boulder"), TEXT("pebble"), TEXT("brick"), TEXT("ladrillo"), TEXT("concrete"), TEXT("castle"), TEXT("castillo"),
			TEXT("tower"), TEXT("torre"), TEXT("wall"), TEXT("muro"), TEXT("tile"), TEXT("baldosa"), TEXT("marble") };
		if (ContainsAnyWord(Text, WaterWords)) { return static_cast<uint8>(Water); }
		if (ContainsAnyWord(Text, SandWords)) { return static_cast<uint8>(Sand); }
		if (ContainsAnyWord(Text, WoodWords)) { return static_cast<uint8>(Wood); }
		if (ContainsAnyWord(Text, SoilWords)) { return static_cast<uint8>(Soil); }
		if (ContainsAnyWord(Text, RockWords)) { return static_cast<uint8>(Rock); }
		return PresetUnknown;
	}

	void PresetWeights(uint8 Preset, float OutWeights[Num])
	{
		for (int32 s = 0; s < Num; ++s) { OutWeights[s] = 0.f; }
		OutWeights[Preset < Num ? Preset : Rock] = 1.f;
	}

	uint8 ClassifyByNames(const UPrimitiveComponent* Comp, FNameCache* Cache)
	{
		if (!Comp) { return PresetUnknown; }
		const TObjectKey<UPrimitiveComponent> Key(Comp);
		if (Cache)
		{
			if (const uint8* Found = Cache->Find(Key))
			{
				return *Found;
			}
			if (Cache->Num() > 256) { Cache->Reset(); }
		}

		FString Names = Comp->GetName();
		if (const AActor* CompOwner = Comp->GetOwner())
		{
			Names += TEXT(" ");
			Names += CompOwner->GetClass()->GetName();
			Names += TEXT(" ");
			Names += CompOwner->GetName();
		}
		if (const UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Comp))
		{
			if (const UStaticMesh* MeshAsset = MeshComp->GetStaticMesh())
			{
				Names += TEXT(" ");
				Names += MeshAsset->GetName();
			}
		}
		if (const UMaterialInterface* Material = Comp->GetMaterial(0))
		{
			Names += TEXT(" ");
			Names += Material->GetName();
		}
		Names.ToLowerInline();
		const uint8 Preset = PresetFromNames(Names);
		if (Cache) { Cache->Add(Key, Preset); }
		return Preset;
	}

	void Resolve(const FHitResult* Hit, FNameCache* Cache, float OutWeights[Num])
	{
		float W[Num];
		PresetWeights(PresetUnknown, W);

		const bool bHit = Hit != nullptr && Hit->bBlockingHit;
		const UPrimitiveComponent* HitComp = bHit ? Hit->GetComponent() : nullptr;
		const AActor* HitActor = bHit ? Hit->GetActor() : nullptr;
		if (bHit)
		{
			// Encima de otra tortuga: su caparazón suena hueco y resbala, como un tablón.
			const uint8 Preset = Cast<ATortugaCharacter>(HitActor) ? static_cast<uint8>(Wood) : ClassifyByNames(HitComp, Cache);
			PresetWeights(Preset, W);
		}

		// Normalizados (sin datos, roca).
		float Total = 0.f;
		for (int32 s = 0; s < Num; ++s)
		{
			W[s] = FMath::Max(0.f, W[s]);
			Total += W[s];
		}
		if (Total <= 1e-4f)
		{
			PresetWeights(PresetUnknown, W);
			Total = 1.f;
		}
		for (int32 s = 0; s < Num; ++s)
		{
			OutWeights[s] = W[s] / Total;
		}
	}

	bool Probe(UWorld* World, const AActor* Ignore, const FVector& From, const FVector& FootLocation,
		FNameCache* Cache, float OutWeights[Num])
	{
		FHitResult Hit;
		bool bHit = false;
		if (World)
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TNTurtleSurface), true, Ignore);
			Query.bReturnFaceIndex = true;
			bHit = World->LineTraceSingleByChannel(Hit, From, FootLocation - FVector(0.0, 0.0, 60.0), ECC_Visibility, Query);
		}
		Resolve(bHit ? &Hit : nullptr, Cache, OutWeights);
		return bHit;
	}
}
