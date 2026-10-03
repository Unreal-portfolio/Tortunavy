#pragma once

#include "CoreMinimal.h"
#include "Core/TN_ProjectMaterials.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Multiplayer/TN_LocalViews.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "Art/TN_Art.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"
#include "World/ProcMap/TN_ProcMapRuntimeMesh.h"

/**
 * Utilidades de los enemigos de la playa (solo visual): materiales de color de vértice, mallas en ejecución guardadas
 * por nombre (se comparten entre actores y el recolector las libera cuando nadie las usa), piezas animables, emisores de
 * partículas propios (TNAmbientFX, sin el registro global) y la sombra redonda que se pinta en la arena.
 */
namespace TNBeachKit
{
	using TNProcMesh::FTNProcMeshBuffers;

	/** Material opaco de color de vértice (el de la vegetación y la fauna; si no está, el de los cosméticos). */
	inline UMaterialInterface* SolidMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage"), nullptr, LOAD_NoWarn);
			if (!Mat)
			{
				Mat = TNMaterials::VertexColor();
			}
			Cached = Mat;
		}
		return Cached.Get();
	}

	/** Material translúcido sin luz (alfa del vértice = opacidad): sombras, velos y efectos. */
	inline UMaterialInterface* SoftMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXSoft.M_ProcFXSoft"), nullptr, LOAD_NoWarn);
			Cached = Mat ? Mat : SolidMaterial();
		}
		return Cached.Get();
	}

	/** Cómo se guarda el alfa del vértice de una malla. */
	enum class EBeachMeshMat : uint8
	{
		/** Opaca (alfa 0: sin balanceo de viento). */
		Solid,
		/** Translúcida con el alfa de los propios buffers. */
		SoftVertexAlpha
	};

	/**
	 * Malla estática en ejecución guardada por nombre: la primera vez se construye con Build; después se reutiliza
	 * mientras algún componente la use (referencia débil: si nadie la usa, el recolector la libera y se rehace).
	 */
	inline UStaticMesh* CachedMesh(const FString& Key, TFunctionRef<void(FTNProcMeshBuffers&)> Build, EBeachMeshMat Mat = EBeachMeshMat::Solid)
	{
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		if (const TWeakObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
		{
			if (UStaticMesh* Existing = Found->Get())
			{
				return Existing;
			}
		}
		FTNProcMeshBuffers B;
		Build(B);
		const bool bSoft = Mat == EBeachMeshMat::SoftVertexAlpha;
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, bSoft ? SoftMaterial() : SolidMaterial(), false, 0.f, 1.f, bSoft ? -2.f : -1.f);
		Cache.Add(Key, Mesh);
		return Mesh;
	}

	/** Pieza de malla enganchada a Parent en RelLoc, sin colisión (móvil, sombra opcional). */
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& RelLoc, bool bShadow = true)
	{
		if (!Owner || !Parent)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(bShadow);
		Comp->bReceivesDecals = false;
		Comp->SetStaticMesh(Mesh);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLoc);
		Comp->RegisterComponent();
		return Comp;
	}

	/**
	 * Lo mismo, como pieza de arte Slot (Docs/Arte_Assets.md, TNArt::ApplyToComponent): con sustituto, la malla de arte va
	 * de hija de la pieza y se mueve, se esconde y se enseña con ella. Pivote: el origen de la pieza (su articulación).
	 */
	inline UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& RelLoc, bool bShadow, FName Slot)
	{
		UStaticMeshComponent* Comp = AddPart(Owner, Parent, Mesh, RelLoc, bShadow);
		TNArt::ApplyToComponent(Comp, Slot);
		return Comp;
	}

	/** Coloca la pieza en su pivote con un giro y una escala (evita tocar el componente si no está). */
	inline void Pose(UStaticMeshComponent* Comp, const FVector& Loc, const FRotator& Rot, const FVector& Scale = FVector::OneVector)
	{
		if (Comp)
		{
			Comp->SetRelativeTransform(FTransform(Rot, Loc, Scale));
		}
	}

	/** Añade el triángulo y escribe el alfa de sus tres vértices con AlphaOf(posición). */
	template <typename TAlphaFn>
	inline void AddTriAlpha(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, const FVector& C, const FVector& Hint, const FLinearColor& Color, TAlphaFn AlphaOf)
	{
		const int32 Before = M.Verts.Num();
		M.AddTri(A, B, C, Hint, Color);
		for (int32 i = Before; i < M.Verts.Num(); ++i)
		{
			M.Colors[i].A = AlphaOf(M.Verts[i]);
		}
	}

	/**
	 * Sombra redonda de radio 100 cm mirando arriba: casi negra y con el borde difuminado (alfa del vértice). Se escala en
	 * cada uso; Opacity va en el nombre de la caché.
	 */
	inline UStaticMesh* ShadowDisc(float Opacity = 0.45f)
	{
		const FString Key = FString::Printf(TEXT("Beach.Shadow.%d"), FMath::RoundToInt32(Opacity * 100.f));
		return CachedMesh(Key, [Opacity](FTNProcMeshBuffers& M)
		{
			constexpr int32 Seg = 24;
			const FLinearColor Dark(0.02f, 0.02f, 0.03f, 1.f);
			auto AlphaOf = [Opacity](const FVector& P)
			{
				const double R = P.Size2D() / 100.0;
				return R < 0.55 ? Opacity : static_cast<float>(Opacity * FMath::Clamp((1.0 - R) / 0.45, 0.0, 1.0));
			};
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Seg;
				const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
				const FVector I0(FMath::Cos(A0) * 55.0, FMath::Sin(A0) * 55.0, 0.0);
				const FVector I1(FMath::Cos(A1) * 55.0, FMath::Sin(A1) * 55.0, 0.0);
				const FVector O0(FMath::Cos(A0) * 100.0, FMath::Sin(A0) * 100.0, 0.0);
				const FVector O1(FMath::Cos(A1) * 100.0, FMath::Sin(A1) * 100.0, 0.0);
				AddTriAlpha(M, FVector::ZeroVector, I0, I1, FVector::UpVector, Dark, AlphaOf);
				AddTriAlpha(M, I0, O0, O1, FVector::UpVector, Dark, AlphaOf);
				AddTriAlpha(M, I0, O1, I1, FVector::UpVector, Dark, AlphaOf);
			}
		}, EBeachMeshMat::SoftVertexAlpha);
	}

	/** Pieza de sombra en el suelo (sin sombra propia, sin colisión, absoluta: se coloca en el mundo). */
	inline UStaticMeshComponent* AddShadow(AActor* Owner, float Opacity = 0.45f)
	{
		UStaticMeshComponent* Comp = AddPart(Owner, Owner ? Owner->GetRootComponent() : nullptr, ShadowDisc(Opacity), FVector::ZeroVector, false);
		if (Comp)
		{
			Comp->SetAbsolute(true, true, true);
			Comp->SetTranslucentSortPriority(2);
		}
		return Comp;
	}

	/** Coloca una sombra en el mundo: en Ground (un pelo por encima), con radio Radius (cm). Radius <= 0 la esconde. */
	inline void PlaceShadow(UStaticMeshComponent* Comp, const FVector& Ground, float Radius)
	{
		if (!Comp)
		{
			return;
		}
		const bool bShow = Radius > 1.f;
		if (Comp->IsVisible() != bShow)
		{
			Comp->SetVisibility(bShow);
		}
		if (bShow)
		{
			const double S = Radius / 100.0;
			Comp->SetWorldTransform(FTransform(FQuat::Identity, Ground + FVector(0.0, 0.0, 12.0), FVector(S, S, 1.0)));
		}
	}

	/**
	 * Sombra redonda de radio 100 cm con alfa 1 en el centro y el borde difuminado desde InnerFrac del radio (0,55 =
	 * blanda, como ShadowDisc; 0,85 = nítida). La opacidad va en el parámetro «Opacity» del material (SetOpacity), así una
	 * misma malla sirve para una sombra que se oscurece al acercarse.
	 */
	inline UStaticMesh* ShadowDiscEdge(float InnerFrac)
	{
		const FString Key = FString::Printf(TEXT("Beach.ShadowEdge.%d"), FMath::RoundToInt32(InnerFrac * 100.f));
		return CachedMesh(Key, [InnerFrac](FTNProcMeshBuffers& M)
		{
			constexpr int32 Seg = 28;
			const double Inner = 100.0 * FMath::Clamp(static_cast<double>(InnerFrac), 0.1, 0.95);
			const FLinearColor Dark(0.02f, 0.02f, 0.03f, 1.f);
			auto AlphaOf = [Inner](const FVector& P)
			{
				const double R = P.Size2D();
				return R <= Inner + 0.5 ? 1.f : static_cast<float>(FMath::Clamp((100.0 - R) / FMath::Max(1.0, 100.0 - Inner), 0.0, 1.0));
			};
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A0 = TNProcMap::TwoPi * k / Seg;
				const double A1 = TNProcMap::TwoPi * (k + 1) / Seg;
				const FVector I0(FMath::Cos(A0) * Inner, FMath::Sin(A0) * Inner, 0.0);
				const FVector I1(FMath::Cos(A1) * Inner, FMath::Sin(A1) * Inner, 0.0);
				const FVector O0(FMath::Cos(A0) * 100.0, FMath::Sin(A0) * 100.0, 0.0);
				const FVector O1(FMath::Cos(A1) * 100.0, FMath::Sin(A1) * 100.0, 0.0);
				AddTriAlpha(M, FVector::ZeroVector, I0, I1, FVector::UpVector, Dark, AlphaOf);
				AddTriAlpha(M, I0, O0, O1, FVector::UpVector, Dark, AlphaOf);
				AddTriAlpha(M, I0, O1, I1, FVector::UpVector, Dark, AlphaOf);
			}
		}, EBeachMeshMat::SoftVertexAlpha);
	}

	/**
	 * Material dinámico del material suave (M_ProcFXSoft: color = color del vértice sin luz, opacidad = alfa del vértice
	 * × «Opacity») puesto en Comp; si ya lo tiene, el mismo.
	 */
	inline UMaterialInstanceDynamic* SoftMID(UStaticMeshComponent* Comp)
	{
		if (!Comp)
		{
			return nullptr;
		}
		if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)))
		{
			return Existing;
		}
		return Comp->CreateDynamicMaterialInstance(0, SoftMaterial());
	}

	/** Parámetro «Opacity» de M_ProcFXSoft, M_ProcFXCloud y M_ProcStormVeil (multiplica el alfa del vértice). */
	inline void SetOpacity(UMaterialInstanceDynamic* Mid, float Opacity)
	{
		if (Mid)
		{
			static const FName OpacityName(TEXT("Opacity"));
			Mid->SetScalarParameterValue(OpacityName, FMath::Max(0.f, Opacity));
		}
	}

	/**
	 * Surco de arrastre en la arena (el cangrejo que frena clavando las patas): franja de 100 cm de largo (X) por 44 de
	 * ancho (Y), hundida y oscura en medio con dos lomos claros de arena apartada a los lados; se difumina en las puntas.
	 * Se escala al usarla; la opacidad, con SoftMID + SetOpacity.
	 */
	inline UStaticMesh* FurrowStrip()
	{
		return CachedMesh(TEXT("Beach.Furrow"), [](FTNProcMeshBuffers& M)
		{
			const FLinearColor Groove(0.5f, 0.4f, 0.27f, 1.f);
			const FLinearColor Ridge(0.95f, 0.86f, 0.66f, 1.f);
			auto EndFade = [](double X) { return FMath::Clamp((50.0 - FMath::Abs(X)) / 18.0, 0.0, 1.0); };
			const double Bands[4] = { -22.0, -9.0, 9.0, 22.0 };
			constexpr int32 NX = 5;
			for (int32 b = 0; b < 3; ++b)
			{
				const bool bGroove = b == 1;
				const FLinearColor& Color = bGroove ? Groove : Ridge;
				const double Peak = bGroove ? 0.7 : 0.45;
				auto AlphaOf = [&EndFade, Peak](const FVector& P) { return static_cast<float>(Peak * EndFade(P.X)); };
				for (int32 i = 0; i < NX; ++i)
				{
					const double X0 = -50.0 + 100.0 * i / NX;
					const double X1 = -50.0 + 100.0 * (i + 1) / NX;
					const FVector A(X0, Bands[b], bGroove ? 0.0 : 2.0);
					const FVector B(X1, Bands[b], bGroove ? 0.0 : 2.0);
					const FVector C(X1, Bands[b + 1], bGroove ? 0.0 : 2.0);
					const FVector D(X0, Bands[b + 1], bGroove ? 0.0 : 2.0);
					AddTriAlpha(M, A, B, C, FVector::UpVector, Color, AlphaOf);
					AddTriAlpha(M, A, C, D, FVector::UpVector, Color, AlphaOf);
				}
			}
		}, EBeachMeshMat::SoftVertexAlpha);
	}

	/** Emisor de partículas propio del actor (no pasa por el registro global de TNAmbientFX). Nada en servidor dedicado. */
	inline void InitEmitter(TNAmbientFX::FEmitter& E, AActor* Owner, const TNAmbientFX::FEmitterDesc& Desc, uint32 Seed)
	{
		E = TNAmbientFX::FEmitter();
		if (!Owner || !Owner->GetWorld() || Owner->GetWorld()->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		E.Desc = Desc;
		E.RateScale = 0.f;
		E.Rng ^= Seed * 2654435761u + 1u;
		E.Particles.SetNum(Desc.MaxParticles);
		E.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Desc.MaxParticles);
		E.ISM = TNAmbientFX::MakeISM(Owner, TNAmbientFX::ShapeMesh(Desc.Shape, Desc.Color, Desc.bSoft, Desc.Alpha, Desc.bCloud), Desc.MaxParticles, false);
	}

	/** true si al emisor le queda alguna partícula viva. */
	inline bool AnyAlive(const TNAmbientFX::FEmitter& E)
	{
		for (const TNAmbientFX::FParticle& P : E.Particles)
		{
			if (P.bAlive)
			{
				return true;
			}
		}
		return false;
	}

	/** Mueve el emisor si nace algo o le queda algo vivo (si no, no cuesta nada). */
	inline void TickEmitterIfBusy(TNAmbientFX::FEmitter& E, float Dt, const FVector& View)
	{
		if (!E.ISM.IsValid())
		{
			return;
		}
		if (E.RateScale > 0.f || E.bAwake || AnyAlive(E))
		{
			TNAmbientFX::TickEmitter(E, Dt, View);
			if (E.RateScale <= 0.f && !AnyAlive(E))
			{
				E.bAwake = false;
			}
		}
	}

	/** Estallido de Count partículas desde Origin hacia Dir. */
	inline void BurstAt(TNAmbientFX::FEmitter& E, const FVector& Origin, const FVector& Dir, int32 Count)
	{
		if (!E.ISM.IsValid())
		{
			return;
		}
		E.Origin = Origin;
		E.Desc.Direction = Dir;
		TNAmbientFX::Burst(E, Count);
	}

	/** Descripción de un emisor con lo habitual rellenado. */
	inline TNAmbientFX::FEmitterDesc MakeDesc(TNAmbientFX::EShape Shape, const FLinearColor& Color, bool bSoft, float Alpha, int32 MaxParticles,
		float Rate, float Speed, float Gravity, float LifeMin, float LifeMax, float SizeStart, float SizeEnd)
	{
		TNAmbientFX::FEmitterDesc D;
		D.Shape = Shape;
		D.Color = Color;
		D.bSoft = bSoft;
		D.bCloud = bSoft && Shape == TNAmbientFX::EShape::Puff;
		D.Alpha = Alpha;
		D.MaxParticles = MaxParticles;
		D.Rate = Rate;
		D.Speed = Speed;
		D.SpeedJitter = 0.4f;
		D.Spread = 0.6f;
		D.Gravity = Gravity;
		D.Drag = 0.6f;
		D.LifeMin = LifeMin;
		D.LifeMax = LifeMax;
		D.SizeStart = SizeStart;
		D.SizeEnd = SizeEnd;
		D.WakeDistance = 60000.f;
		return D;
	}

	/**
	 * Cámara local: posición. InOutLoc entra con el sitio de lo que se mira: con la pantalla partida (#311), la cámara local
	 * más cercana a él. Falso (y InOutLoc sin tocar) sin jugador local.
	 */
	inline bool LocalCamera(const UWorld* World, FVector& InOutLoc)
	{
		return TNLocalViews::ClosestCamera(World, InOutLoc, InOutLoc);
	}

	/**
	 * Engancha Comp a la espalda de la tortuga (el caparazón): al hueso Spine2 de su malla, BackCm por detrás y UpCm por
	 * encima de él en la postura de referencia, mirando hacia fuera; con el ragdoll del derribo va pegado al cuerpo. Sin
	 * ese hueso, a la cápsula (55 cm por encima del centro). WorldScale es su escala en el mundo (no hereda la de la malla).
	 */
	inline void AttachToTurtleBack(UStaticMeshComponent* Comp, ACharacter* Turtle, float WorldScale, float BackCm = 50.f, float UpCm = 18.f)
	{
		if (!Comp || !Turtle || !Turtle->GetRootComponent())
		{
			return;
		}
		static const FName SpineBone(TEXT("Spine2"));
		USkeletalMeshComponent* Mesh = Turtle->GetMesh();
		const USkinnedAsset* Asset = Mesh ? Mesh->GetSkinnedAsset() : nullptr;
		const int32 BoneIndex = Asset ? Asset->GetRefSkeleton().FindBoneIndex(SpineBone) : INDEX_NONE;
		if (BoneIndex == INDEX_NONE)
		{
			Comp->AttachToComponent(Turtle->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Comp->SetRelativeLocation(FVector(0.0, 0.0, 55.0));
			Comp->SetAbsolute(false, false, true);
			Comp->SetWorldScale3D(FVector(WorldScale));
			return;
		}
		// Hueso en el espacio de la malla con la postura de referencia (la de ahora puede ser ya la del ragdoll).
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		const TArray<FTransform>& Pose = Ref.GetRefBonePose();
		FTransform BoneCS = Pose[BoneIndex];
		for (int32 Parent = Ref.GetParentIndex(BoneIndex); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		{
			BoneCS = BoneCS * Pose[Parent];
		}
		// Espacio de la malla de la tortuga: mira a +Y y arriba es +Z, así que el caparazón queda hacia -Y.
		const double MeshScale = FMath::Max(0.01, static_cast<double>(Mesh->GetComponentScale().Z));
		const FVector Outward = FVector(0.0, -0.75, 0.66).GetSafeNormal();
		const FVector At = BoneCS.GetLocation() + FVector(0.0, -BackCm / MeshScale, UpCm / MeshScale);
		const FTransform Wanted(FRotationMatrix::MakeFromZ(Outward).ToQuat(), At);
		const FTransform Rel = Wanted.GetRelativeTransform(BoneCS);
		Comp->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SpineBone);
		Comp->SetRelativeLocationAndRotation(Rel.GetLocation(), Rel.GetRotation());
		Comp->SetAbsolute(false, false, true);
		Comp->SetWorldScale3D(FVector(WorldScale));
	}

	/** Hash estable de un entero a [0, 1). */
	inline float Hash01(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352du;
		X ^= X >> 15;
		X *= 0x846ca68bu;
		X ^= X >> 16;
		return static_cast<float>(X & 0xFFFFFF) / 16777216.f;
	}

	/**
	 * Presupuesto de un sonido de ambiente que se repite en muchas instancias a la vez (las burbujas de los pulpos de las
	 * siete pozas). Cada emisor anuncia su distancia al oyente local en cada fotograma (AmbientVoiceTouch) y, cuando le toca
	 * sonar, pide sitio (AmbientVoiceClaim): solo suenan los MaxAudible más cercanos y nunca dos disparos con menos de
	 * MinGapSeconds de diferencia, sean de quien sean. Sin esto, cada pulpo cercano burbujeaba por su cuenta y varias pozas
	 * a la vez sonaban a una fuente que no para. Solo el hilo de juego; el estado es por mundo (dos ventanas de PIE en el
	 * mismo proceso no se pisan) y se descarta al cambiar de mundo.
	 */
	struct FAmbientVoiceBudget
	{
		struct FEntry
		{
			const void* Who = nullptr;
			float Distance = 0.f;
			double Seen = 0.0;
		};

		TArray<FEntry> Entries;
		double LastClaim = -1.0e9;
	};

	inline FAmbientVoiceBudget& AmbientVoiceBudgetFor(const UWorld* World)
	{
		static TMap<const UWorld*, FAmbientVoiceBudget> Budgets;
		if (!Budgets.Contains(World) && Budgets.Num() >= 4)
		{
			// Mundos de partidas anteriores: es estado de un rato, se descarta.
			Budgets.Reset();
		}
		return Budgets.FindOrAdd(World);
	}

	/** El emisor Who está a Distance cm del oyente local: se llama en cada fotograma mientras pueda llegar a sonar. */
	inline void AmbientVoiceTouch(const UWorld* World, const void* Who, float Distance)
	{
		if (!World || !Who)
		{
			return;
		}
		FAmbientVoiceBudget& Budget = AmbientVoiceBudgetFor(World);
		const double Now = World->GetTimeSeconds();
		// Fuera los que llevan más de 1 s sin anunciarse (lejos, destruidos) o de un mundo que ha reiniciado su reloj.
		Budget.Entries.RemoveAll([Who, Now](const FAmbientVoiceBudget::FEntry& Entry)
		{
			return Entry.Who != Who && (Now - Entry.Seen > 1.0 || Entry.Seen > Now);
		});
		FAmbientVoiceBudget::FEntry* Mine = Budget.Entries.FindByPredicate([Who](const FAmbientVoiceBudget::FEntry& Entry) { return Entry.Who == Who; });
		if (!Mine)
		{
			Mine = &Budget.Entries.AddDefaulted_GetRef();
			Mine->Who = Who;
		}
		Mine->Distance = Distance;
		Mine->Seen = Now;
	}

	/**
	 * Who pide sonar ahora. true si es de los MaxAudible más cercanos de los que se han anunciado en el último medio
	 * segundo y han pasado MinGapSeconds desde el último disparo concedido a cualquiera (lo concedido queda apuntado).
	 */
	inline bool AmbientVoiceClaim(const UWorld* World, const void* Who, int32 MaxAudible, float MinGapSeconds)
	{
		if (!World || !Who)
		{
			return false;
		}
		FAmbientVoiceBudget& Budget = AmbientVoiceBudgetFor(World);
		const double Now = World->GetTimeSeconds();
		if (Now < Budget.LastClaim)
		{
			Budget.LastClaim = -1.0e9;
		}
		if (Now - Budget.LastClaim < static_cast<double>(MinGapSeconds))
		{
			return false;
		}
		const FAmbientVoiceBudget::FEntry* Mine = Budget.Entries.FindByPredicate([Who](const FAmbientVoiceBudget::FEntry& Entry) { return Entry.Who == Who; });
		if (!Mine)
		{
			return false;
		}
		int32 Closer = 0;
		for (const FAmbientVoiceBudget::FEntry& Entry : Budget.Entries)
		{
			if (Entry.Who != Who && Now - Entry.Seen <= 0.5 && Entry.Distance < Mine->Distance)
			{
				++Closer;
			}
		}
		if (Closer >= MaxAudible)
		{
			return false;
		}
		Budget.LastClaim = Now;
		return true;
	}
}
