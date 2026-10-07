#include "World/Beach/TN_BeachGullZone.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemySynth.h"
#include "World/Beach/TN_BeachGullTuning.h"
#include "World/Beach/TN_BeachStorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachEnemyKit.h"
#include "TN_BeachEnemyMeshes.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_Log.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_TurtleAnimInstance.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_CombatTuning.h"
#include "TimerManager.h"
#include "World/TN_HazardEffects.h"

namespace TNBeachGull
{
	/** Pieza de arte de cada hueso de una gaviota o un pelícano (Docs/Arte_Assets.md): pivote en su articulación. */
	inline FName BirdSlot(bool bPelican, TNFauna::ETNFaunaBone Bone)
	{
		using EBone = TNFauna::ETNFaunaBone;
		switch (Bone)
		{
		case EBone::Body: return bPelican ? TN_ART("Beach.Pelican.Body") : TN_ART("Beach.Gull.Body");
		case EBone::Head: return bPelican ? TN_ART("Beach.Pelican.Head") : TN_ART("Beach.Gull.Head");
		case EBone::WingL: return bPelican ? TN_ART("Beach.Pelican.WingLeft") : TN_ART("Beach.Gull.WingLeft");
		case EBone::WingR: return bPelican ? TN_ART("Beach.Pelican.WingRight") : TN_ART("Beach.Gull.WingRight");
		case EBone::LegBL: return bPelican ? TN_ART("Beach.Pelican.LegLeft") : TN_ART("Beach.Gull.LegLeft");
		case EBone::LegBR: return bPelican ? TN_ART("Beach.Pelican.LegRight") : TN_ART("Beach.Gull.LegRight");
		default: return NAME_None;
		}
	}

	/** Tiempo entre ataques (s) mientras haya tortugas debajo (TN_BeachGullTuning.h: el nerf de la ronda 4). */
	constexpr float AttackMin = TNBeachGullTuning::AttackIntervalMin;
	constexpr float AttackMax = TNBeachGullTuning::AttackIntervalMax;
	/** Aviso en la arena: sombra dura y negra que nace de este radio (cm), con esta opacidad, y aparece en MarkerFadeIn s. */
	constexpr float MarkerStartRadius = 35.f;
	constexpr float MarkerOpacity = 0.88f;
	constexpr float MarkerFadeIn = 0.35f;
	constexpr float MarkerEdge = 0.92f;

	// ── Cagada ──
	/**
	 * Vuela sobre la tortuga (s), la cagada cae en FallTime desde PoopHeight (cm: 2,1 s para verla venir y apartarse) y
	 * el ataque acaba en PoopEnd.
	 */
	constexpr float DropTime = TNBeachGullTuning::PoopDropTime;
	constexpr float FallTime = TNBeachGullTuning::PoopFallTime;
	constexpr float PoopEnd = DropTime + FallTime + 1.75f;
	constexpr float PoopHeight = 3000.f;
	/**
	 * Radio de la mancha (cm, por el tamaño; es también hasta dónde crece su sombra y lo que alcanza el golpe, con su
	 * holgura: TNBeachGullTuning) y tamaño del pegote que cae.
	 */
	constexpr float SplatRadius = TNBeachGullTuning::SplatRadius;
	constexpr float DropScale = 1.5f;
	/** Empujón de la manchada (cm/s); su derribo y el tiempo que la zona la deja en paz, en UTN_CombatTuning. */
	constexpr float PoopPush = 240.f;
	/** Radio con el que nace la sombra de la cagada (cm): lo bastante grande para leerse desde que se suelta. */
	constexpr float PoopMarkerStartRadius = 90.f;

	// ── Aviso sobre la tortuga objetivo de la cagada ──
	/**
	 * Un signo de exclamación sobre su cabeza (WarnAbove cm por encima) empieza WarnLead s antes de soltar la cagada y
	 * parpadea de WarnRateSlow a WarnRateFast veces por segundo según cae (está encendido WarnOnFraction de cada
	 * parpadeo). Mide 105 cm; lejos se agranda (distancia / WarnGrowDist, entre 1 y WarnMaxScale) para leerse.
	 */
	constexpr float WarnLead = 0.5f;
	constexpr float WarnRateSlow = 2.f;
	constexpr float WarnRateFast = 12.f;
	constexpr float WarnOnFraction = 0.6f;
	constexpr float WarnAbove = 40.f;
	constexpr float WarnGrowDist = 1500.f;
	constexpr float WarnMaxScale = 3.5f;

	// ── La cagada pintada en la tortuga (decal) ──
	/**
	 * Vive StainLife s: entera hasta StainFadeStart, luego se seca desde los bordes y se desvanece hasta desaparecer. El
	 * decal proyecta en una caja de medio lado StainHalfSize y media profundidad StainHalfDepth (cm) que sale del hueso de
	 * la espalda StainBackCm por detrás (el caparazón está a ~24) y StainUpCm por encima, mirando hacia delante y abajo.
	 */
	constexpr float StainLife = 12.f;
	constexpr float StainFadeStart = 8.f;
	constexpr float StainHalfSize = 38.f;
	constexpr float StainHalfDepth = 30.f;
	constexpr float StainBackCm = 21.f;
	constexpr float StainUpCm = 10.f;
	constexpr int32 MaxStains = 12;

	// ── Picado ──
	/**
	 * Sube y se coloca (s), baja en picado DiveTime s siguiendo a la tortuga (desde que aparece la sombra hay 2,3 s para
	 * reaccionar), abre el pico JawLead s antes de llegar y llega abajo en StrikeTime.
	 */
	constexpr float ClimbTime = TNBeachGullTuning::DiveClimbTime;
	constexpr float DiveTime = TNBeachGullTuning::DiveTime;
	constexpr float StrikeTime = ClimbTime + DiveTime;
	constexpr float JawLead = 0.45f;
	/**
	 * De dónde arranca el picado (cm desde el blanco: casi encima, para que baje en picado de verdad) y radio en el que
	 * la coge (por el tamaño, con la holgura de TNBeachGullTuning).
	 */
	constexpr float DiveStartDist = 1800.f;
	constexpr float DiveStartHeight = 4600.f;
	constexpr float GrabRadius = TNBeachGullTuning::GrabRadius;
	/** Al llegar abajo frena levantando el morro (grados) con la cabeza gacha. */
	constexpr float StrikePitch = 12.f;
	constexpr float StrikeHeadPitch = -30.f;
	/**
	 * Picado fallido: desde StrikeTime baja en PeckDown s hasta clavar el pico en la arena (con el morro y la cabeza hacia
	 * abajo), pica hasta PeckHold y remonta.
	 */
	constexpr float PeckDown = 0.14f;
	constexpr float PeckHold = 0.55f;
	constexpr float PeckPitch = -6.f;
	constexpr float PeckHeadPitch = -55.f;
	/** Mareada: cae a la arena dando tumbos (s) y, pasado el mareo, despega hacia su círculo (s). */
	constexpr float DazeFall = 0.8f;
	constexpr float DazeTakeOff = 1.8f;
	/** Solo se le puede dar con algo lanzado si su cuerpo está a menos de esto sobre la arena (cm). */
	constexpr float HittableHeight = 1800.f;

	// ── Agarre ──
	/**
	 * Con la tortuga en el pico: tirón (s), sube aleteando fuerte hasta RiseEnd, vuela y la suelta en CarryTime. La sube
	 * CarryHeight y la lleva CarryBack hacia la salida (cm). El pico pasa de la pose del picado a la del agarre en
	 * CatchBlend (s).
	 */
	constexpr float TugTime = 0.35f;
	constexpr float RiseEnd = 2.2f;
	constexpr float CarryTime = 3.3f;
	constexpr float CarryHeight = 2600.f;
	constexpr float CarryBack = 1500.f;
	constexpr float CatchBlend = 0.15f;
	/** Al soltarla: empujón de la bola hacia la salida (cm/s); el aturdimiento tras caer, en UTN_CombatTuning. */
	constexpr float ReleaseLaunch = 350.f;
	constexpr float DiveEndHit = StrikeTime + CarryTime + 2.4f;
	constexpr float DiveEndMiss = StrikeTime + PeckHold + 2.9f;
	/** Del hueso de la espalda de la tortuga (Spine2) a la superficie del caparazón que muerde el pico (cm). */
	constexpr float ShellBack = 35.f;
	/**
	 * Cerca del frente de la tormenta (a menos de esto por delante, cm, o detrás) no se la lleva en el pico: el vuelo la
	 * echaría CarryBack hacia la salida, dentro de la tormenta, y la patada, la recolocación y la red de seguridad se
	 * encadenaban (ronda 4: «segunda gaviota + caparazón»). Ahí caga en vez de picar, y si pica, falla.
	 */
	constexpr float StormNoCarryReach = CarryBack + 1000.f;

	// ── Vuelo en círculos ──
	/** Capas de altura (cm sobre la zona): una por pájaro, barajadas, separadas LayerStep. */
	constexpr float LayerBase = 3200.f;
	constexpr float LayerStep = 800.f;
	/** Velocidad por el círculo (cm/s) y lo que deriva el centro de cada círculo (cm). */
	constexpr float GullSpeedMin = 900.f;
	constexpr float GullSpeedMax = 1300.f;
	constexpr float PelicanSpeedMin = 700.f;
	constexpr float PelicanSpeedMax = 900.f;
	constexpr float Drift = 700.f;

	inline float Smooth01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	/** Hueso de la espalda de la tortuga por el que la sujeta el pico. */
	inline FName SpineBone()
	{
		static const FName Name(TEXT("Spine2"));
		return Name;
	}

	/**
	 * Coloca una sombra de pájaro: en Ground con radio Radius (0 la esconde), opacidad Opacity y nitidez Sharp (0 borde
	 * difuminado, 1 nítido; cambia de malla por tramos). Bucket guarda la malla que lleva.
	 */
	inline void PlaceBirdShadow(UStaticMeshComponent* Comp, int32& Bucket, const FVector& Ground, float Radius, float Opacity, float Sharp)
	{
		if (!Comp)
		{
			return;
		}
		TNBeachKit::PlaceShadow(Comp, Ground, Radius);
		if (Radius <= 1.f)
		{
			return;
		}
		static const float InnerFrac[3] = { 0.35f, 0.6f, 0.85f };
		const int32 Want = Sharp < 0.33f ? 0 : (Sharp < 0.66f ? 1 : 2);
		if (Want != Bucket)
		{
			Bucket = Want;
			Comp->SetStaticMesh(TNBeachKit::ShadowDiscEdge(InnerFrac[Want]));
		}
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}

	/**
	 * Aviso duro en la arena (la sombra del picado o de la cagada): disco negro de borde neto en At, tumbado sobre la
	 * cuesta (Normal) y un poco levantado para que no se hunda en ella; Radius u Opacity a 0 lo esconden.
	 */
	inline void PlaceMarker(UStaticMeshComponent* Comp, const FVector& At, const FVector& Normal, float Radius, float Opacity)
	{
		if (!Comp)
		{
			return;
		}
		const bool bShow = Radius > 1.f && Opacity > 0.01f;
		if (Comp->IsVisible() != bShow)
		{
			Comp->SetVisibility(bShow);
		}
		if (!bShow)
		{
			return;
		}
		const FVector Up = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
		const double S = Radius / 100.0;
		Comp->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Up).ToQuat(), At + Up * 25.0, FVector(S, S, 1.0)));
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}

	/**
	 * Material del decal de la cagada (Scripts/create_poop_decal.py). Null si aún no se ha creado: no se recuerda que falta
	 * (el script puede ejecutarse con el editor abierto entre dos partidas) y se pide de nuevo la siguiente vez.
	 */
	inline UMaterialInterface* PoopDecalMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_PoopSplatDecal.M_PoopSplatDecal"), nullptr, LOAD_NoWarn);
		}
		return Cached.Get();
	}

	/**
	 * Material de los avisos duros (M_ProcFXHard, del mismo script): translúcido sin luz como M_ProcFXSoft pero sin su fundido
	 * por profundidad (80 cm), que dejaba un disco a 25 cm de la arena a medio ver. Null si falta: se usa el suave.
	 */
	inline UMaterialInterface* HardFxMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXHard.M_ProcFXHard"), nullptr, LOAD_NoWarn);
		}
		return Cached.Get();
	}

	/** Pone en Comp el material dinámico de un aviso duro (el suave de siempre si falta el duro) con la opacidad Opacity. */
	inline void SetupHardMid(UStaticMeshComponent* Comp, float Opacity)
	{
		if (!Comp)
		{
			return;
		}
		if (UMaterialInterface* Hard = HardFxMaterial())
		{
			Comp->CreateDynamicMaterialInstance(0, Hard);
		}
		TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
	}

	/**
	 * Sujeta Decal al hueso de la espalda de Turtle, ya apuntando a su caparazón. Las medidas salen de la postura de
	 * referencia de la malla (en su espacio mira a +Y y arriba es +Z; el caparazón queda hacia -Y): el centro, StainBackCm
	 * por detrás del hueso y StainUpCm por encima, y proyecta hacia delante y abajo (como cae: por detrás y desde arriba).
	 * El decal va con el hueso, así que sigue al ragdoll y a la bola. Escala 1 en el mundo (DecalSize va en cm). False sin
	 * malla ni hueso.
	 */
	inline bool PlaceStainDecal(UDecalComponent* Decal, ACharacter* Turtle)
	{
		USkeletalMeshComponent* Mesh = Turtle ? Turtle->GetMesh() : nullptr;
		const USkinnedAsset* Asset = Mesh ? Mesh->GetSkinnedAsset() : nullptr;
		const int32 BoneIndex = Asset ? Asset->GetRefSkeleton().FindBoneIndex(SpineBone()) : INDEX_NONE;
		if (!Decal || BoneIndex == INDEX_NONE)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		const TArray<FTransform>& Pose = Ref.GetRefBonePose();
		FTransform BoneCS = Pose[BoneIndex];
		for (int32 Parent = Ref.GetParentIndex(BoneIndex); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
		{
			BoneCS = BoneCS * Pose[Parent];
		}
		const double MeshScale = FMath::Max(0.01, static_cast<double>(Mesh->GetComponentScale().Z));
		const FVector Center = BoneCS.GetLocation() + FVector(0.0, -StainBackCm / MeshScale, StainUpCm / MeshScale);
		const FVector Into = FVector(0.0, 0.85, -0.53).GetSafeNormal();
		const FTransform Wanted(FRotationMatrix::MakeFromXZ(Into, FVector::UpVector).ToQuat(), Center);
		const FTransform Rel = Wanted.GetRelativeTransform(BoneCS);
		Decal->SetupAttachment(Mesh, SpineBone());
		Decal->SetRelativeLocationAndRotation(Rel.GetLocation(), Rel.GetRotation());
		Decal->SetAbsolute(false, false, true);
		Decal->SetRelativeScale3D(FVector::OneVector);
		return true;
	}
}

ATN_BeachGullZone::ATN_BeachGullZone()
{
	bUsesMover = false;
	NetFrequencyNear = 10.f;
	SetNetUpdateFrequency(NetFrequencyNear);
}

void ATN_BeachGullZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachGullZone, Attack);
	DOREPLIFETIME(ATN_BeachGullZone, ShellStains);
}

void ATN_BeachGullZone::ApplySpec()
{
	SizeK = FMath::Clamp(Spec.SizeScale, 0.75f, 1.3f);
	// Solo ataca a quien está bastante dentro de la zona (antes, a su huella + 8 m).
	AttackRadius = GetFootprintRadius() * TNBeachGullTuning::AttackFootprintScale + TNBeachGullTuning::AttackRadiusPad;
	CircleRadius = FMath::Max(3500.f, GetFootprintRadius() * 1.25f);
	BuildBirds();
}

void ATN_BeachGullZone::BeginPlay()
{
	Super::BeginPlay();
	// Hacia donde se la lleva: la dirección editable de la zona, en planta (sin ella, -X).
	CourseBack = CourseBack.GetSafeNormal2D();
	if (CourseBack.IsNearlyZero())
	{
		CourseBack = FVector(-1.0, 0.0, 0.0);
	}
	if (HasAuthority())
	{
		NextAttackTime = ServerNow(this) + ServerRng.FRandRange(2.f, 5.f);
	}
}

void ATN_BeachGullZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndHoldTurtle();
	for (UStaticMeshComponent* Splat : Splats)
	{
		if (Splat)
		{
			Splat->DestroyComponent();
		}
	}
	Splats.Reset();
	for (UDecalComponent* Decal : StainDecals)
	{
		if (Decal)
		{
			Decal->DestroyComponent();
		}
	}
	StainDecals.Reset();
	StainMids.Reset();
	StainBorn.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachGullZone::Tick(float DeltaSeconds)
{
	// El blanco que se ve, antes de mover los pájaros (en los clientes, el replicado sin saltos).
	UpdateShownAim(DeltaSeconds);
	Super::Tick(DeltaSeconds);
	// Después de mover los pájaros: la tortuga que va en el pico, en el pico.
	TickHold();
}

FVector ATN_BeachGullZone::CurrentAim() const
{
	return bShownAimValid ? ShownAim : FVector(Attack.Aim);
}

void ATN_BeachGullZone::UpdateShownAim(float DeltaSeconds)
{
	const FVector Rep = Attack.Aim;
	// En el servidor, el de verdad; en un ataque nuevo o con el blanco ya quieto, sin más.
	if (HasAuthority() || !bShownAimValid || ShownAimSerial != Attack.Serial || Attack.bLocked)
	{
		ShownAim = Rep;
		ShownAimSerial = Attack.Serial;
		bShownAimValid = true;
		return;
	}
	// Hacia el replicado (llega a 10 Hz) algo más deprisa de lo que se mueve el blanco: sin saltos ni retraso.
	FVector Delta = Rep - ShownAim;
	const double Dist = Delta.Size();
	const double MaxStep = TNBeachGullTuning::MaxChaseSpeed() * 1.6 * DeltaSeconds;
	if (Dist > 3000.0 || Dist <= MaxStep)
	{
		ShownAim = Rep;
		return;
	}
	ShownAim += Delta / Dist * MaxStep;
}

FVector ATN_BeachGullZone::GroundNormalAt(const FVector& Where) const
{
	const float Z0 = GroundAt(Where);
	const float Zx = GroundAt(Where + FVector(150.0, 0.0, 0.0));
	const float Zy = GroundAt(Where + FVector(0.0, 150.0, 0.0));
	const FVector Normal = FVector::CrossProduct(FVector(150.0, 0.0, Zx - Z0), FVector(0.0, 150.0, Zy - Z0)).GetSafeNormal();
	return Normal.Z > 0.2 ? Normal : FVector::UpVector;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pájaros
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGullZone::BuildBirds()
{
	using namespace TNBeachGull;
	if (Birds.Num() > 0)
	{
		return;
	}
	// Parámetros de vuelo en todas las máquinas (los mismos con la misma semilla).
	const uint32 Seed = static_cast<uint32>(Spec.Seed);
	const int32 NumGulls = 3 + (TNBeachKit::Hash01(Seed * 5u + 2u) < 0.5f ? 1 : 0);
	const bool bWithPelican = TNBeachKit::Hash01(Seed * 3u + 1u) < 0.6f;
	const int32 Count = NumGulls + (bWithPelican ? 1 : 0);
	const float Turn = TNBeachKit::Hash01(Seed * 7u + 5u) < 0.5f ? 1.f : -1.f;
	// Una capa de altura por pájaro, barajadas: nunca dos a la misma altura.
	TArray<int32> HeightLayers;
	for (int32 k = 0; k < Count; ++k)
	{
		HeightLayers.Add(k);
	}
	for (int32 k = Count - 1; k > 0; --k)
	{
		const int32 j = FMath::Min(k, static_cast<int32>(TNBeachKit::Hash01(Seed * 11u + static_cast<uint32>(k) * 13u) * static_cast<float>(k + 1)));
		HeightLayers.Swap(k, j);
	}
	// Centros repartidos alrededor del de la zona con el ángulo áureo: cada uno en su sitio.
	const float StartAngle = 2.f * PI * TNBeachKit::Hash01(Seed * 17u + 3u);
	for (int32 b = 0; b < Count; ++b)
	{
		const uint32 B = static_cast<uint32>(b);
		FBird& Bird = Birds.AddDefaulted_GetRef();
		Bird.bPelican = bWithPelican && b == Count - 1;
		const float CenterAngle = StartAngle + 2.39996f * static_cast<float>(b);
		const float CenterDist = CircleRadius * FMath::Lerp(0.12f, 0.6f, TNBeachKit::Hash01(Seed + B * 19u + 7u));
		Bird.CenterOffset = FVector2D(FMath::Cos(CenterAngle), FMath::Sin(CenterAngle)) * CenterDist;
		Bird.Radius = FMath::Max(1800.f, CircleRadius * FMath::Lerp(0.45f, 0.9f, TNBeachKit::Hash01(Seed + B * 17u)));
		Bird.Ratio = FMath::Lerp(0.65f, 1.f, TNBeachKit::Hash01(Seed + B * 23u + 1u));
		Bird.OvalYaw = 2.f * PI * TNBeachKit::Hash01(Seed + B * 29u + 5u);
		Bird.Height = LayerBase + LayerStep * static_cast<float>(HeightLayers[b]) + 400.f * (TNBeachKit::Hash01(Seed + B * 31u) - 0.5f);
		Bird.Phase = 2.f * PI * TNBeachKit::Hash01(Seed + B * 13u);
		// Casi todas giran hacia el mismo lado; alguna, al revés.
		const float Dir = TNBeachKit::Hash01(Seed + B * 37u + 9u) < 0.3f ? -Turn : Turn;
		const float SpeedK = TNBeachKit::Hash01(Seed + B * 47u + 4u);
		const float Speed = Bird.bPelican ? FMath::Lerp(PelicanSpeedMin, PelicanSpeedMax, SpeedK) : FMath::Lerp(GullSpeedMin, GullSpeedMax, SpeedK);
		Bird.AngSpeed = Dir * Speed / Bird.Radius;
		Bird.DriftPhase = 2.f * PI * TNBeachKit::Hash01(Seed + B * 41u + 2u);
		Bird.Scale = (Bird.bPelican ? 24.f : 28.f) * FMath::Sqrt(SizeK);
		Bird.Span = (Bird.bPelican ? 170.f : 90.f) * Bird.Scale;
		Bird.SquawkTimer = 2.f + 6.f * TNBeachKit::Hash01(Seed + B * 43u);
	}
	if (!bHasScreen)
	{
		return;
	}

	// Mallas de la fauna (gaviota y pelícano) con el pico de abajo aparte, compartidas por pieza (TNBeachMeshes::BuildBirdParts).
	TArray<TNFauna::FTNFaunaPart> GullParts;
	TArray<TNFauna::FTNFaunaPart> PelicanParts;
	TNFauna::FTNFaunaRig GullRig;
	TNFauna::FTNFaunaRig PelicanRig;
	TNFauna::FTNFaunaBirdJaw GullJaw;
	TNFauna::FTNFaunaBirdJaw PelicanJaw;
	TNBeachMeshes::BuildBirdParts(false, GullParts, GullRig, GullJaw);
	if (bWithPelican)
	{
		TNBeachMeshes::BuildBirdParts(true, PelicanParts, PelicanRig, PelicanJaw);
	}
	for (int32 b = 0; b < Birds.Num(); ++b)
	{
		FBird& Bird = Birds[b];
		const TArray<TNFauna::FTNFaunaPart>& Parts = Bird.bPelican ? PelicanParts : GullParts;
		USceneComponent* BirdRoot = NewObject<USceneComponent>(this, NAME_None, RF_Transient);
		BirdRoot->SetupAttachment(GetRootComponent());
		BirdRoot->SetAbsolute(true, true, true);
		BirdRoot->RegisterComponent();
		BirdRoots.Add(BirdRoot);
		Bird.FirstPart = BirdParts.Num();
		UStaticMeshComponent* BodyComp = nullptr;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				const TNFauna::FTNFaunaPart& Part = Parts[i];
				const bool bIsBody = Part.Bone == TNFauna::ETNFaunaBone::Body;
				if ((Pass == 0) != bIsBody)
				{
					continue;
				}
				const TNProcMesh::FTNProcMeshBuffers& Buffers = Part.Mesh;
				UStaticMesh* Mesh = TNBeachKit::CachedMesh(TNBeachMeshes::BirdPartKey(Bird.bPelican, i),
					[&Buffers](TNProcMesh::FTNProcMeshBuffers& M) { M = Buffers; });
				USceneComponent* Parent = bIsBody ? BirdRoot : static_cast<USceneComponent*>(BodyComp);
				UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, Parent ? Parent : BirdRoot, Mesh, Part.Pivot, false, TNBeachGull::BirdSlot(Bird.bPelican, Part.Bone));
				if (bIsBody && !BodyComp)
				{
					BodyComp = Comp;
				}
				const int32 Index = BirdParts.Add(Comp);
				PartPivots.Add(Part.Pivot);
				switch (Part.Bone)
				{
				case TNFauna::ETNFaunaBone::WingL: Bird.WingL = Index; break;
				case TNFauna::ETNFaunaBone::WingR: Bird.WingR = Index; break;
				case TNFauna::ETNFaunaBone::LegBL: Bird.LegL = Index; break;
				case TNFauna::ETNFaunaBone::LegBR: Bird.LegR = Index; break;
				case TNFauna::ETNFaunaBone::Head: Bird.Head = Index; break;
				default: break;
				}
			}
		}
		Bird.NumParts = BirdParts.Num() - Bird.FirstPart;
		// Pico de abajo en su base: se abre para coger a la tortuga y para graznar.
		UStaticMeshComponent* HeadComp = BirdParts.IsValidIndex(Bird.Head) ? BirdParts[Bird.Head].Get() : nullptr;
		const bool bPelican = Bird.bPelican;
		const TNFauna::FTNFaunaBirdJaw& JawData = bPelican ? PelicanJaw : GullJaw;
		UStaticMesh* JawMesh = TNBeachKit::CachedMesh(TNBeachMeshes::BirdJawKey(bPelican),
			[&JawData](TNProcMesh::FTNProcMeshBuffers& M) { M = JawData.Mesh; });
		Jaws.Add(HeadComp ? TNBeachKit::AddPart(this, HeadComp, JawMesh, TNBeachMeshes::BirdGeom(bPelican).BeakBase, false,
			bPelican ? TN_ART("Beach.Pelican.Jaw") : TN_ART("Beach.Gull.Jaw")) : nullptr);

		UStaticMeshComponent* Shadow = TNBeachKit::AddShadow(this, 0.38f);
		TNBeachKit::PlaceShadow(Shadow, FVector::ZeroVector, 0.f);
		Shadows.Add(Shadow);
		Bird.ShadowZ = static_cast<float>(GetActorLocation().Z);
		Bird.ShadowTimer = 0.05f * static_cast<float>(b);
	}

	UStaticMesh* DropMesh = TNBeachKit::CachedMesh(TEXT("Beach.Dropping"), [](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildDropping(M); });
	Dropping = TNBeachKit::AddPart(this, GetRootComponent(), DropMesh, FVector::ZeroVector, true);
	if (Dropping)
	{
		Dropping->SetAbsolute(true, true, true);
		Dropping->SetVisibility(false);
	}
	// Avisos duros en la arena: la sombra de la cagada que cae y la del picado (discos negros de borde neto; su opacidad
	// va en el material). Por encima de las demás sombras.
	for (TObjectPtr<UStaticMeshComponent>* Marker : { &DropShadow, &DiveMarker })
	{
		*Marker = TNBeachKit::AddShadow(this, 0.6f);
		if (UStaticMeshComponent* Comp = *Marker)
		{
			Comp->SetStaticMesh(TNBeachKit::ShadowDiscEdge(MarkerEdge));
			Comp->SetTranslucentSortPriority(4);
			SetupHardMid(Comp, 0.f);
			Comp->SetVisibility(false);
		}
	}
	// Signo de exclamación sobre la tortuga objetivo de la cagada (amarillo con borde rojo oscuro; mira a la cámara).
	WarnMark = TNBeachKit::AddPart(this, GetRootComponent(),
		TNBeachKit::CachedMesh(TEXT("Beach.WarnMark"), [](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildWarningMark(M); }, TNBeachKit::EBeachMeshMat::SoftVertexAlpha),
		FVector::ZeroVector, false);
	if (WarnMark)
	{
		WarnMark->SetAbsolute(true, true, true);
		WarnMark->SetTranslucentSortPriority(6);
		SetupHardMid(WarnMark, 1.f);
		WarnMark->SetVisibility(false);
	}

	using TNAmbientFX::EShape;
	TNAmbientFX::FEmitterDesc DropDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.97f, 0.97f, 0.94f), false, 1.f, 40, 0.f, 1000.f, -1800.f, 0.5f, 1.f, 55.f, 40.f);
	DropDesc.Spread = 0.9f;
	DropDesc.SpawnRadius = 150.f;
	TNBeachKit::InitEmitter(Droplets, this, DropDesc, Seed + 51u);
	TNAmbientFX::FEmitterDesc FeatherDesc = TNBeachKit::MakeDesc(EShape::Flake, FLinearColor(0.95f, 0.95f, 0.93f), false, 1.f, 30, 0.f, 500.f, -120.f, 1.5f, 3.f, 90.f, 70.f);
	FeatherDesc.Spread = 1.f;
	FeatherDesc.SpawnRadius = 300.f;
	TNBeachKit::InitEmitter(Feathers, this, FeatherDesc, Seed + 52u);
	// Estela de gotitas que deja la cagada al caer.
	TNAmbientFX::FEmitterDesc TrailDesc = TNBeachKit::MakeDesc(EShape::Drop, FLinearColor(0.98f, 0.98f, 0.95f), false, 1.f, 40, 0.f, 80.f, -300.f, 0.35f, 0.6f, 45.f, 12.f);
	TrailDesc.Spread = 0.5f;
	TrailDesc.SpawnRadius = 40.f;
	TNBeachKit::InitEmitter(Trail, this, TrailDesc, Seed + 53u);
	// Arena que levantan las alas al coger a una tortuga.
	TNAmbientFX::FEmitterDesc SandDesc = TNBeachKit::MakeDesc(EShape::Puff, FLinearColor(0.86f, 0.76f, 0.56f), true, 0.55f, 30, 0.f, 700.f, -60.f, 1.f, 2.f, 250.f, 600.f);
	SandDesc.Spread = 1.f;
	SandDesc.SpawnRadius = 400.f;
	TNBeachKit::InitEmitter(SandPuff, this, SandDesc, Seed + 54u);
	// Granos de arena que saltan al picar en el sitio (el picado fallido).
	TNAmbientFX::FEmitterDesc PeckDesc = TNBeachKit::MakeDesc(EShape::Ember, FLinearColor(0.82f, 0.72f, 0.52f), false, 1.f, 40, 0.f, 1100.f, -1500.f, 0.6f, 1.1f, 30.f, 22.f);
	PeckDesc.Spread = 0.8f;
	PeckDesc.SpawnRadius = 90.f;
	TNBeachKit::InitEmitter(PeckSand, this, PeckDesc, Seed + 55u);
	GetVoice(GetRootComponent(), 2500.f, 16000.f);
}

FVector ATN_BeachGullZone::CirclePos(const FBird& Bird, double Now) const
{
	// Óvalo propio alrededor de su centro (desplazado del de la zona y que deriva despacio) y a su altura.
	const double A = Bird.Phase + Bird.AngSpeed * Now;
	const double Lx = FMath::Cos(A) * Bird.Radius;
	const double Ly = FMath::Sin(A) * Bird.Radius * Bird.Ratio;
	const double Co = FMath::Cos(Bird.OvalYaw);
	const double So = FMath::Sin(Bird.OvalYaw);
	const double Dx = TNBeachGull::Drift * FMath::Sin(0.05 * Now + Bird.DriftPhase);
	const double Dy = TNBeachGull::Drift * FMath::Cos(0.041 * Now + Bird.DriftPhase * 1.3);
	return GetActorLocation() + FVector(Bird.CenterOffset.X + Lx * Co - Ly * So + Dx, Bird.CenterOffset.Y + Lx * So + Ly * Co + Dy,
		Bird.Height + 250.0 * FMath::Sin(0.37 * Now + Bird.Phase * 3.0));
}

FVector ATN_BeachGullZone::AttackPos(const FBird& Bird, double Now, float Tau) const
{
	using namespace TNBeachGull;
	const FVector Circle = CirclePos(Bird, Now);
	if (Attack.Kind == 3)
	{
		// Mareada: cae a la arena dando tumbos, se queda sentada lo que dura el mareo y despega hacia su círculo.
		const FVector HitAt = Attack.Aim;
		const FVector Sit = Attack.Hold;
		const float StunFor = FMath::Max(DazeFall, HitStunEndTime - Attack.StartTime);
		if (Tau < DazeFall)
		{
			const float A = Tau / DazeFall;
			return FMath::Lerp(HitAt, Sit, static_cast<double>(A * A)) + FVector(0.0, 0.0, 300.0 * FMath::Sin(PI * A) * (1.f - A));
		}
		if (Tau < StunFor)
		{
			return Sit;
		}
		const float B = Smooth01((Tau - StunFor) / DazeTakeOff);
		return FMath::Lerp(Sit, Circle, static_cast<double>(B)) + FVector(0.0, 0.0, 900.0 * FMath::Sin(PI * B));
	}
	const FVector From = CirclePos(Bird, Attack.StartTime);
	// El blanco sigue a la tortuga (ServerTrackAim, a lo que toque en cada tramo): el pájaro baja siguiéndolo.
	const FVector Target = CurrentAim();
	if (Attack.Kind == 1)
	{
		// Cagada: arco hasta encima de la tortuga, pasa de largo subiendo y vuelve a su vuelta.
		const FVector Over = Target + FVector(0.0, 0.0, PoopHeight);
		if (Tau < DropTime)
		{
			const float A = Smooth01(Tau / DropTime);
			return FMath::Lerp(From, Over, static_cast<double>(A)) + FVector(0.0, 0.0, 800.0 * FMath::Sin(PI * A));
		}
		FVector Dir = Over - From;
		Dir.Z = 0.0;
		Dir = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
		const float Past = FMath::Min(Tau, DropTime + FallTime) - DropTime;
		const FVector Beyond = Over + Dir * (Past * 1800.0) + FVector(0.0, 0.0, Past * 400.0);
		if (Tau < DropTime + FallTime)
		{
			return Beyond;
		}
		const float B = Smooth01((Tau - DropTime - FallTime) / (PoopEnd - DropTime - FallTime));
		return FMath::Lerp(Beyond, Circle, static_cast<double>(B));
	}

	// Picado: se coloca casi encima y alto, baja acelerando siguiendo al blanco (los dos extremos se mueven con él) y frena
	// con el pico en su caparazón.
	FVector Away = From - Target;
	Away.Z = 0.0;
	Away = Away.IsNearlyZero() ? FVector::ForwardVector : Away.GetSafeNormal();
	const FVector DiveStart = Target + Away * DiveStartDist + FVector(0.0, 0.0, DiveStartHeight);
	if (Tau < ClimbTime)
	{
		return FMath::Lerp(From, DiveStart, static_cast<double>(Smooth01(Tau / ClimbTime)));
	}
	const FVector Strike = StrikeRoot(Bird, Now);
	if (Tau < StrikeTime || Attack.Result == 0)
	{
		const float U = FMath::Clamp((Tau - ClimbTime) / (StrikeTime - ClimbTime), 0.f, 1.f);
		return FMath::Lerp(DiveStart, Strike, static_cast<double>(U * U));
	}
	if (Attack.Result == 1)
	{
		// Tras soltarla (el agarre lo coloca VisualTick): remonta desde donde la soltó y vuelve a su vuelta.
		const FVector Release = RootForGrip(Bird, GripPath(CarrySeconds()), CarryRotation(CarrySeconds()), CarryHeadPitch(CarrySeconds()));
		const float B = Smooth01((Tau - StrikeTime - CarrySeconds()) / (DiveEndHitTime() - StrikeTime - CarrySeconds()));
		return FMath::Lerp(Release + FVector(0.0, 0.0, 1500.0 * B), Circle, static_cast<double>(B));
	}
	// Fallo: baja igual hasta clavar el pico en la arena (o en lo que la cubría), pica dos veces, remonta de largo y vuelve.
	const float U = Tau - StrikeTime;
	const FVector Peck = PeckRoot(Bird);
	if (U < PeckDown)
	{
		return FMath::Lerp(Strike, Peck, static_cast<double>(Smooth01(U / PeckDown)));
	}
	if (U < PeckHold)
	{
		// Picotazos: el cuerpo sube y baja un poco con cada uno.
		return Peck + FVector(0.0, 0.0, 45.0 * FMath::Abs(FMath::Sin((U - PeckDown) * 14.f)));
	}
	FVector Fwd = Target - DiveStart;
	Fwd.Z = 0.0;
	Fwd = Fwd.IsNearlyZero() ? FVector::ForwardVector : Fwd.GetSafeNormal();
	const float K = FMath::Min(U - PeckHold, 0.9f);
	const FVector Pull = Peck + Fwd * (K * 3000.0) + FVector(0.0, 0.0, K * K * 3500.0);
	if (U < PeckHold + 0.9f)
	{
		return Pull;
	}
	const float B = Smooth01((U - PeckHold - 0.9f) / (DiveEndMiss - StrikeTime - PeckHold - 0.9f));
	return FMath::Lerp(Pull, Circle, static_cast<double>(B));
}

float ATN_BeachGullZone::GroundAt(const FVector& Where) const
{
	// La arena del generador, sin trazas (un pájaro que se abre por encima de la selva cruzaría los muros invisibles de
	// los lados, y una traza que empieza dentro de uno da en su punto de partida); sin generador, traza.
	float Z = static_cast<float>(GetActorLocation().Z);
	GroundHeightAt(FVector(Where.X, Where.Y, FMath::Max(Where.Z, GetActorLocation().Z)), Z);
	return Z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Agarre: las mismas cuentas en todas las máquinas
// ─────────────────────────────────────────────────────────────────────────────

FVector ATN_BeachGullZone::GripOffset(const FBird& Bird, float HeadPitch) const
{
	const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(Bird.bPelican);
	return G.BodyPivot + G.HeadPivot + FRotator(HeadPitch, 0.f, 0.f).RotateVector(G.Grip);
}

FVector ATN_BeachGullZone::RootForGrip(const FBird& Bird, const FVector& Grip, const FRotator& Rot, float HeadPitch) const
{
	return Grip - Rot.RotateVector(GripOffset(Bird, HeadPitch) * Bird.Scale);
}

float ATN_BeachGullZone::GripDropFor(const ATortugaCharacter* Turtle) const
{
	const UCapsuleComponent* Capsule = Turtle ? Turtle->GetCapsuleComponent() : nullptr;
	return Capsule ? Capsule->GetScaledCapsuleHalfHeight() * 0.75f : 70.f;
}

float ATN_BeachGullZone::CarrySeconds() const
{
	return TNBeachGull::CarryTime;
}

float ATN_BeachGullZone::CarryRise() const
{
	return TNBeachGull::CarryHeight;
}

float ATN_BeachGullZone::CarryDistance() const
{
	return TNBeachGull::CarryBack;
}

float ATN_BeachGullZone::DiveEndHitTime() const
{
	return TNBeachGull::DiveEndHit - TNBeachGull::CarryTime + CarrySeconds();
}

float ATN_BeachGullZone::HeldYaw() const
{
	return static_cast<float>(CourseBack.Rotation().Yaw);
}

FVector ATN_BeachGullZone::GripPath(float U) const
{
	using namespace TNBeachGull;
	const FVector Start = FVector(Attack.Hold) + FVector(0.0, 0.0, GripDropFor(Attack.Victim));
	// Tirón hacia arriba (la tortuga se resiste), subida aleteando y un vuelo corto meciéndola antes de soltarla.
	const float Rise = Smooth01((U - TugTime) / (RiseEnd - TugTime));
	const float Tug = U < TugTime ? 110.f * FMath::Sin(PI * U / TugTime) : 0.f;
	const float Glide = U > RiseEnd ? 80.f * FMath::Sin((U - RiseEnd) * 6.f) : 0.f;
	const float Along = FMath::Pow(FMath::Clamp((U - 0.2f) / (CarrySeconds() - 0.2f), 0.f, 1.f), 1.5f);
	return Start + CourseBack * (CarryDistance() * Along) + FVector(0.0, 0.0, CarryRise() * Rise + Tug + Glide);
}

FRotator ATN_BeachGullZone::CarryRotation(float U) const
{
	using namespace TNBeachGull;
	const FVector A = GripPath(FMath::Max(0.f, U - 0.05f));
	const FVector B = GripPath(U + 0.05f);
	const FVector D = B - A;
	float Pitch = FMath::Clamp(0.6f * FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(D.Z), FMath::Max(1.f, static_cast<float>(D.Size2D())))), -10.f, 30.f);
	if (U < TugTime)
	{
		// Tirando de ella hacia arriba con el morro levantado.
		Pitch = FMath::Lerp(StrikePitch, 25.f, U / TugTime);
	}
	return FRotator(Pitch, HeldYaw(), 6.f * FMath::Sin(U * 7.f));
}

float ATN_BeachGullZone::CarryHeadPitch(float U) const
{
	using namespace TNBeachGull;
	// Cabeza gacha con la tortuga en el pico, sacudiéndola un poco (más en el tirón).
	return U < TugTime ? -30.f + 10.f * FMath::Sin(U * 14.f) : -38.f + 6.f * FMath::Sin(U * 9.f);
}

FVector ATN_BeachGullZone::StrikeRoot(const FBird& Bird, double Now) const
{
	using namespace TNBeachGull;
	const ATortugaCharacter* Victim = Attack.Victim;
	const float Drop = GripDropFor(Victim);
	FVector Grip;
	if (Attack.Kind == 2 && Attack.Result == 2)
	{
		// Falla: el pico llega justo encima de donde va a picar (la arena o la sombrilla que la cubría).
		Grip = FVector(Attack.Hold) + FVector(0.0, 0.0, 160.0);
	}
	else
	{
		// El blanco está en la arena: el caparazón de una tortuga de pie, media cápsula más arriba.
		const UCapsuleComponent* Capsule = Victim ? Victim->GetCapsuleComponent() : nullptr;
		const float Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;
		Grip = CurrentAim() + FVector(0.0, 0.0, Half + Drop);
	}
	return RootForGrip(Bird, Grip, FRotator(StrikePitch, DiveYaw(Bird, Grip), 0.f), StrikeHeadPitch);
}

float ATN_BeachGullZone::DiveYaw(const FBird& Bird, const FVector& Target) const
{
	FVector Dir = Target - CirclePos(Bird, Attack.StartTime);
	Dir.Z = 0.0;
	return Dir.IsNearlyZero() ? 0.f : static_cast<float>(Dir.Rotation().Yaw);
}

FVector ATN_BeachGullZone::PeckRoot(const FBird& Bird) const
{
	using namespace TNBeachGull;
	// El pico clavado en la arena (un pelo por encima del punto), con el morro y la cabeza hacia abajo.
	const FVector Grip = FVector(Attack.Hold) + FVector(0.0, 0.0, 15.0);
	return RootForGrip(Bird, Grip, FRotator(PeckPitch, DiveYaw(Bird, Grip), 0.f), PeckHeadPitch);
}

void ATN_BeachGullZone::BirdPose(int32 Index, double Now, FVector& OutRoot, FRotator& OutRot) const
{
	using namespace TNBeachGull;
	const FBird& Bird = Birds[Index];
	const bool bAttacking = Attack.Kind != 0 && Attack.Bird == Index;
	const float Tau = bAttacking ? static_cast<float>(Now - static_cast<double>(Attack.StartTime)) : 0.f;
	const float U = Tau - StrikeTime;
	if (bAttacking && Attack.Kind == 2 && Attack.Result == 1 && U >= 0.f && U < CarrySeconds())
	{
		OutRot = CarryRotation(U);
		OutRoot = RootForGrip(Bird, GripPath(U), OutRot, CarryHeadPitch(U));
		return;
	}
	OutRoot = bAttacking ? AttackPos(Bird, Now, Tau) : CirclePos(Bird, Now);
	// El giro que se ve (en el servidor sin pantalla, solo el rumbo del picado): basta para su cuerpo.
	OutRot = FRotator(Bird.Pitch, Bird.Yaw, Bird.Bank);
	if (!bHasScreen && bAttacking && Attack.Kind == 2)
	{
		OutRot = FRotator(-40.f, DiveYaw(Bird, CurrentAim()), 0.f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

int32 ATN_BeachGullZone::PickBird(const FVector& Where, bool bForPoop) const
{
	const double Now = ServerNow(this);
	int32 Best = INDEX_NONE;
	double BestSq = 1.0e18;
	for (int32 b = 0; b < Birds.Num(); ++b)
	{
		if (bForPoop && Birds[b].bPelican)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared2D(CirclePos(Birds[b], Now), Where);
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = b;
		}
	}
	return Best;
}

void ATN_BeachGullZone::DebugAttackNow(int32 InKind)
{
	if (!HasAuthority() || Birds.Num() == 0 || Attack.Kind != 0)
	{
		return;
	}
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	ATortugaCharacter* Best = nullptr;
	double BestSq = 1e30;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		const double DistSq = FVector::DistSquared2D(Turtle->GetActorLocation(), GetActorLocation());
		if (CanBeHit(Turtle) && DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = Turtle;
		}
	}
	if (!Best)
	{
		return;
	}
	const uint8 Kind = InKind == 1 || InKind == 2 ? static_cast<uint8>(InKind) : static_cast<uint8>(ServerRng.FRand() < 0.5f ? 1 : 2);
	int32 BirdIndex = PickBird(Best->GetActorLocation(), Kind == 1);
	if (BirdIndex == INDEX_NONE)
	{
		BirdIndex = 0;
	}
	StartAttack(Best, Kind, BirdIndex);
}

bool ATN_BeachGullZone::IsNearStormFront(const FVector& At) const
{
	const ATN_BeachStorm* Storm = ATN_BeachStorm::FindStorm(this);
	return Storm && Storm->IsStormActive() && Storm->IsBehindFront(At, -TNBeachGull::StormNoCarryReach);
}

void ATN_BeachGullZone::DebugGrab(ATortugaCharacter* Turtle, int32 Times)
{
	if (!HasAuthority() || !IsValid(Turtle) || Birds.Num() == 0)
	{
		return;
	}
	DebugGrabVictim = Turtle;
	DebugGrabsLeft = FMath::Clamp(Times, 1, 10);
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s cogerá %d veces a %s con el pico (TN.Beach.Gull.Grab)."), *GetName(), DebugGrabsLeft, *GetNameSafe(Turtle));
}

void ATN_BeachGullZone::StartAttack(ATortugaCharacter* Victim, uint8 InKind, int32 BirdIndex)
{
	Attack.Victim = Victim;
	// El blanco nace en la arena bajo la tortuga y la sigue (ServerTrackAim).
	FVector Aim = Victim ? Victim->GetActorLocation() : GetActorLocation();
	Aim.Z = GroundAt(Aim);
	Attack.Aim = Aim;
	Attack.Hold = FVector::ZeroVector;
	Attack.StartTime = static_cast<float>(ServerNow(this));
	Attack.Kind = InKind;
	Attack.Bird = static_cast<uint8>(FMath::Clamp(BirdIndex, 0, 255));
	Attack.Result = 0;
	Attack.bLocked = 0;
	Attack.ReleaseTime = 0.f;
	++Attack.Serial;
	bReleased = false;
	bRoofChecked = false;
	AimChase = TNBeachGullTuning::FChaseState();
	ForceNetUpdate();
	OnAttackChanged();
}

void ATN_BeachGullZone::EndAttack(double Now)
{
	if (GetHeldTurtle())
	{
		EndHoldTurtle();
	}
	Attack.Kind = 0;
	Attack.Victim = nullptr;
	NextAttackTime = Now + ServerRng.FRandRange(TNBeachGull::AttackMin, TNBeachGull::AttackMax);
	ForceNetUpdate();
	OnAttackChanged();
}

void ATN_BeachGullZone::ServerTick(float DeltaSeconds)
{
	const double Now = ServerNow(this);
	PruneShellStains(Now);
	if (Attack.Kind == 3)
	{
		// Mareada en la arena: cuando se le pasa y ha despegado, vuelve a su círculo (y la zona, a atacar).
		if (Now > static_cast<double>(HitStunEndTime) + TNBeachGull::DazeTakeOff)
		{
			EndAttack(Now);
		}
		return;
	}
	if (Attack.Kind != 0)
	{
		const float Tau = static_cast<float>(Now - static_cast<double>(Attack.StartTime));
		if (Attack.Kind == 1)
		{
			ServerPoop(Tau, DeltaSeconds);
		}
		else
		{
			ServerDive(Tau, DeltaSeconds);
		}
		if (Attack.Kind != 0 && Tau > 14.f)
		{
			EndAttack(Now);
		}
		return;
	}
	// Pruebas (TN.Beach.Gull.Grab): agarres seguidos a la misma tortuga, cada uno en cuanto esté libre (sin bola, derribo,
	// aturdimiento, panzazo ni sombrilla); mientras, no ataca a nadie más.
	if (DebugGrabsLeft > 0)
	{
		ATortugaCharacter* Target = DebugGrabVictim.Get();
		if (!IsValid(Target) || Target->IsDead())
		{
			DebugGrabsLeft = 0;
			return;
		}
		if (Birds.Num() == 0 || IsHitStunned() || !CanBeHit(Target) || Target->IsInShell() || Target->IsBellyPoseActive()
			|| Target->HasUmbrellaProtection())
		{
			return;
		}
		--DebugGrabsLeft;
		IgnoreTurtle(Target, 0.f);
		StartAttack(Target, 2, FMath::Max(0, PickBird(Target->GetActorLocation(), false)));
		// El picado ya va por su último medio segundo: el pájaro llega en seguida y la coge si no se mueve.
		Attack.StartTime -= TNBeachGull::StrikeTime - 0.5f;
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s: agarre de prueba a %s (quedan %d)."), *GetName(), *GetNameSafe(Target), DebugGrabsLeft);
		return;
	}
	if (Now < NextAttackTime || Birds.Num() == 0 || IsHitStunned())
	{
		return;
	}
	// A por una de las tortugas que están debajo (sin sombrilla, sin aturdir, sin derribar).
	TArray<ATortugaCharacter*> Turtles;
	GatherTurtles(this, Turtles);
	TArray<ATortugaCharacter*> Candidates;
	for (ATortugaCharacter* Turtle : Turtles)
	{
		if (IsTargetable(Turtle) && !Turtle->HasUmbrellaProtection() && FVector::Dist2D(Turtle->GetActorLocation(), GetActorLocation()) < AttackRadius)
		{
			Candidates.Add(Turtle);
		}
	}
	if (Candidates.Num() == 0)
	{
		NextAttackTime = Now + 0.5;
		return;
	}
	ATortugaCharacter* Victim = Candidates[ServerRng.RandRange(0, Candidates.Num() - 1)];
	// Va el pájaro más cercano: la mitad de las veces una gaviota caga; si no, picado (el pelícano solo pica). Junto al
	// frente de la tormenta, siempre cagada: el picado se la llevaría hacia atrás, dentro.
	const bool bPoop = ServerRng.FRand() < 0.5f || IsNearStormFront(Victim->GetActorLocation());
	int32 BirdIndex = PickBird(Victim->GetActorLocation(), bPoop);
	uint8 Kind = static_cast<uint8>(bPoop ? 1 : 2);
	if (BirdIndex == INDEX_NONE)
	{
		BirdIndex = PickBird(Victim->GetActorLocation(), false);
		Kind = 2;
	}
	StartAttack(Victim, Kind, FMath::Max(0, BirdIndex));

	if (IsDebugDraw())
	{
		DrawDebugCircle(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, 30.0), AttackRadius, 48, FColor::Yellow, false, 2.f, 0, 10.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
}

void ATN_BeachGullZone::ServerTrackAim(float DeltaSeconds, float Tau, const TNBeachGullTuning::FChasePlan& Plan)
{
	// El blanco sigue a la tortuga por la arena: más rápido de lo que anda y más despacio de lo que corre (#636) y, en el último tramo, lanzado por la línea
	// que ella llevaba (TNBeachGullTuning::StepAim).
	const ATortugaCharacter* Victim = Attack.Victim;
	if (!IsValid(Victim) || Attack.bLocked)
	{
		return;
	}
	const FVector Cur = Attack.Aim;
	const FVector Want = Victim->GetActorLocation();
	const FVector Vel = Victim->GetVelocity();
	const FVector2D Step = TNBeachGullTuning::StepAim(Plan, Tau, AimChase, FVector2D(Cur.X, Cur.Y), FVector2D(Want.X, Want.Y),
		FVector2D(Vel.X, Vel.Y), DeltaSeconds);
	FVector Next(Step.X, Step.Y, Cur.Z);
	Next.Z = GroundAt(Next);
	Attack.Aim = Next;
}

void ATN_BeachGullZone::ServerPoop(float Tau, float DeltaSeconds)
{
	using namespace TNBeachGull;
	// Mientras vuela encima y mientras cae, el blanco sigue a la tortuga más despacio de lo que corre (#636); los últimos 1,5 s
	// (el «!» fijo) cae por la línea que ella llevaba: esprintando en línea recta, girando corriendo, dándose la vuelta o con la plancha a tiempo, se libra.
	if (Attack.Result == 0 && Tau < DropTime + FallTime)
	{
		ServerTrackAim(DeltaSeconds, Tau, TNBeachGullTuning::PoopPlan());
	}
	if (Attack.Result == 0 && Tau >= DropTime + FallTime)
	{
		// Donde cae de verdad: la traza desde arriba da en el techo si lo hay (a cubierto, la cagada cae encima), también en
		// lo alto de un castillo o de una fortaleza; la sombra de cada máquina se pone en el mismo sitio.
		FVector Point = Attack.Aim;
		float Z = static_cast<float>(Point.Z);
		if (TraceDropSurface(this, Point, Z, nullptr, this))
		{
			Point.Z = Z;
		}
		Attack.Aim = Point;
		Attack.bLocked = 1;
		const FVector Impact = Attack.Aim;
		TArray<ATortugaCharacter*> Hit;
		TArray<ATortugaCharacter*> Turtles;
		GatherTurtles(this, Turtles);
		for (ATortugaCharacter* Turtle : Turtles)
		{
			if (!CanBeHit(Turtle) || Turtle->HasUmbrellaProtection())
			{
				continue;
			}
			const FVector At = Turtle->GetActorLocation();
			// A cubierto (bajo una sombrilla, en el castillo) la mancha cae encima, no en ella.
			if (!TNBeachGullTuning::IsInsideHit(FVector::Dist2D(At, Impact), SplatRadius, SizeK, TNBeachGullTuning::SplatPad)
				|| FMath::Abs(At.Z - Impact.Z) > 300.0)
			{
				continue;
			}
			// Tirada en plancha en el momento justo: le pasa por encima.
			if (TNBeach::IsDodgingByBellyDive(Turtle))
			{
				UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s esquiva la cagada de %s en plancha."), *GetNameSafe(Turtle), *GetName());
				continue;
			}
			// El pegote la tumba de espaldas: derribo con ragdoll y mareo, un empujoncito hacia fuera.
			FVector Away = At - Impact;
			Away.Z = 0.0;
			Away = Away.IsNearlyZero() ? Turtle->GetActorForwardVector() * -1.0 : Away.GetSafeNormal();
			const FVector Spin = FVector::CrossProduct(FVector::UpVector, Away) * 200.0;
			KnockDownTurtle(Turtle, UTN_CombatTuning::Get().GullZonePoopKnockSeconds, Away * PoopPush + FVector(0.0, 0.0, 120.0), Spin);
			// Además del derribo, el daño de la caca de la hoja (#871).
			TNHazard::Apply(UTN_HazardTuning::Get().SeagullDropping, Turtle, this);
			IgnoreTurtle(Turtle, UTN_CombatTuning::Get().GullZonePoopIgnoreSeconds);
			Hit.Add(Turtle);
		}
		Attack.Result = Hit.Num() > 0 ? 1 : 2;
		// Las manchas como estado (OnRep_ShellStains): las ve también quien entra o reconecta mientras siguen frescas.
		const double StainNow = ServerNow(this);
		PruneShellStains(StainNow);
		for (int32 HitIndex = 0; HitIndex < Hit.Num(); ++HitIndex)
		{
			FTNBeachGullStain& Stain = ShellStains.AddDefaulted_GetRef();
			Stain.Turtle = Hit[HitIndex];
			Stain.ServerTime = static_cast<float>(StainNow);
			Stain.Serial = Attack.Serial;
			Stain.Variant = static_cast<uint8>(HitIndex);
		}
		while (ShellStains.Num() > TNBeachGull::MaxStains)
		{
			ShellStains.RemoveAt(0);
		}
		ForceNetUpdate();
		MulticastSplat(Impact, Hit);
		OnAttackChanged();
	}
	if (Tau >= PoopEnd)
	{
		EndAttack(ServerNow(this));
	}
}

void ATN_BeachGullZone::ServerDive(float Tau, float DeltaSeconds)
{
	using namespace TNBeachGull;
	// Hasta el golpe, el blanco (y con él el pájaro y la sombra) sigue a la tortuga más despacio de lo que corre (#636); en el
	// último tramo del picado (alas plegadas) va lanzado por la línea que ella llevaba: un giro corriendo o una plancha a
	// tiempo lo hacen fallar.
	if (Attack.Result == 0 && Tau < StrikeTime)
	{
		ServerTrackAim(DeltaSeconds, Tau, TNBeachGullTuning::DivePlan());
	}
	// Al abrir el pico: con algo encima (sombrilla, techo) no puede bajar; fallará y picará en lo que la cubre.
	if (!bRoofChecked && Attack.Result == 0 && Tau >= StrikeTime - JawLead)
	{
		bRoofChecked = true;
		const FVector Point = Attack.Aim;
		FHitResult RoofHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BeachGullRoof), false);
		if (GetWorld()->LineTraceSingleByObjectType(RoofHit, Point + FVector(0.0, 0.0, 3000.0), Point + FVector(0.0, 0.0, 250.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Attack.Result = 2;
			Attack.Hold = RoofHit.ImpactPoint;
			Attack.bLocked = 1;
			ForceNetUpdate();
			OnAttackChanged();
		}
	}
	if (Attack.Result == 0 && Tau >= StrikeTime)
	{
		Attack.bLocked = 1;
		// La coge si está bajo el pico, de pie (ni en pleno panzazo, que la esquiva, ni en bola ni en brazos de otra).
		ATortugaCharacter* Caught = nullptr;
		const FVector Point = Attack.Aim;
		float Best = GrabRadius * SizeK + TNBeachGullTuning::GrabPad;
		TArray<ATortugaCharacter*> Turtles;
		GatherTurtles(this, Turtles);
		for (ATortugaCharacter* Turtle : Turtles)
		{
			if (!IsTargetable(Turtle) || Turtle->HasUmbrellaProtection() || Turtle->IsBellyPoseActive() || Turtle->IsInShell())
			{
				continue;
			}
			// Junto al frente de la tormenta no se la lleva (el vuelo la metería dentro): pica en la arena.
			if (IsNearStormFront(Turtle->GetActorLocation()))
			{
				continue;
			}
			const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
			if (Carry && Carry->IsBeingCarried())
			{
				continue;
			}
			const FVector At = Turtle->GetActorLocation();
			const float Dist = static_cast<float>(FVector::Dist2D(At, Point));
			if (Dist < Best && FMath::Abs(At.Z - Point.Z) < 350.0)
			{
				Best = Dist;
				Caught = Turtle;
			}
		}
		if (Caught)
		{
			Attack.Victim = Caught;
			Attack.Result = 1;
			Attack.Hold = Caught->GetActorLocation();
			bReleased = false;
			IgnoreTurtle(Caught, UTN_CombatTuning::Get().GullZoneGrabIgnoreSeconds);
			// Si llevaba a otra en brazos, la suelta.
			if (UTN_CarryComponent* Carry = Caught->GetCarryComponent())
			{
				if (Carry->IsCarrying())
				{
					Carry->ForceRelease(false);
				}
			}
			// El movimiento se apaga ya en el servidor (los clientes, al recibir el ataque).
			BeginHoldTurtle(Caught);
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] %s coge a %s con el pico."), *GetName(), *GetNameSafe(Caught));
		}
		else
		{
			// Nadie bajo el pico: pica igual en la arena donde iba.
			Attack.Result = 2;
			Attack.Hold = Attack.Aim;
		}
		ForceNetUpdate();
		OnAttackChanged();
	}
	if (Attack.Result == 1 && !bReleased)
	{
		ATortugaCharacter* Carried = Attack.Victim;
		const float U = Tau - StrikeTime;
		const bool bGone = !IsValid(Carried) || Carried->IsDead();
		// Se escurre si se mete en el caparazón (o si algo la derriba); si no, la suelta al acabar el vuelo.
		const bool bSlipped = !bGone && (Carried->IsInShell() || Carried->IsKnockedDown());
		if (bGone || bSlipped || U >= CarrySeconds())
		{
			ReleaseCarried();
		}
	}
	if (Attack.Result != 0 && Tau >= (Attack.Result == 1 ? DiveEndHitTime() : DiveEndMiss))
	{
		EndAttack(ServerNow(this));
	}
}

void ATN_BeachGullZone::OnHoldAborted(ATortugaCharacter* Turtle)
{
	// Se la quitan del pico (red de seguridad): soltada ya, sin la bola de la caída ni el empujón hacia la salida.
	if (!HasAuthority() || !Turtle || Attack.Victim != Turtle || Attack.Result != 1 || bReleased)
	{
		return;
	}
	bReleased = true;
	Attack.ReleaseTime = static_cast<float>(ServerNow(this));
	ForceNetUpdate();
}

void ATN_BeachGullZone::OnHeldTurtleSlips(ATortugaCharacter* Turtle)
{
	// Se mete en el caparazón colgando del pico: se escurre y cae en bola aturdida, la misma suelta que al acabar el vuelo y
	// antes de que nazca su bola (la bola nace de StunTurtle, ya sin nadie que la sujete).
	if (HasAuthority() && Turtle && Attack.Kind == 2 && Attack.Result == 1 && Attack.Victim == Turtle && !bReleased)
	{
		ReleaseCarried();
		return;
	}
	Super::OnHeldTurtleSlips(Turtle);
}

void ATN_BeachGullZone::ReleaseCarried()
{
	using namespace TNBeachGull;
	ATortugaCharacter* Carried = Attack.Victim;
	bReleased = true;
	// Los clientes la sueltan en cuanto les llega, vaya como vaya su reloj; y la sujeción, con su seguro, se deshace del
	// todo aquí (movimiento, correcciones al dueño, pataleta).
	Attack.ReleaseTime = static_cast<float>(ServerNow(this));
	EndHoldTurtle();
	if (IsValid(Carried) && !Carried->IsDead() && !Carried->IsKnockedDown())
	{
		// Cae en bola y sigue aturdida un rato al llegar al suelo (si cae dentro de la tormenta, la patada la saca).
		float GroundZ = static_cast<float>(Attack.Hold.Z);
		GroundHeightAt(Carried->GetActorLocation(), GroundZ);
		const float Height = FMath::Max(0.f, static_cast<float>(Carried->GetActorLocation().Z) - GroundZ);
		const float FallSeconds = FMath::Sqrt(2.f * Height / UTN_CombatTuning::Get().GullZoneGravity);
		const FVector Push = CourseBack * ReleaseLaunch;
		StunTurtle(Carried, FallSeconds + UTN_CombatTuning::Get().GullZoneAfterDropStunSeconds, Push + FVector(0.0, 0.0, -50.0));
		// Gaviota 1 (#871): soltada desde lo alto, la caída es mortal. Muere al llegar al suelo, no en el aire.
		const UTN_HazardTuning& Hazards = UTN_HazardTuning::Get();
		if (TNHazard::GullDropKills(Height, Hazards.GullDropFatalHeight, Hazards.GullDrop.bKills))
		{
			FTimerHandle FallKill;
			TWeakObjectPtr<ATortugaCharacter> WeakCarried(Carried);
			TWeakObjectPtr<ATN_BeachGullZone> WeakThis(this);
			GetWorldTimerManager().SetTimer(FallKill, FTimerDelegate::CreateWeakLambda(this, [WeakCarried, WeakThis]()
			{
				TNHazard::Apply(UTN_HazardTuning::Get().GullDrop, WeakCarried.Get(), WeakThis.Get());
			}), FMath::Max(0.05f, FallSeconds), false);
		}
	}
	ForceNetUpdate();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mareo por lo que se le lanza
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGullZone::ApplyHitStun(float Seconds, AActor* InstigatorActor)
{
	Super::ApplyHitStun(Seconds, InstigatorActor);
	if (!HasAuthority() || !IsHitStunned() || Birds.Num() == 0 || Attack.Kind == 3)
	{
		// Ya mareada: solo se alarga (el final va en HitStunEndTime).
		return;
	}
	// El pájaro que atacaba (o, sin ataque, el más cercano a lo que le ha dado).
	int32 BirdIndex = Attack.Kind != 0 ? static_cast<int32>(Attack.Bird) : INDEX_NONE;
	if (!Birds.IsValidIndex(BirdIndex))
	{
		BirdIndex = FMath::Max(0, PickBird(InstigatorActor ? InstigatorActor->GetActorLocation() : GetActorLocation(), false));
	}
	const double Now = ServerNow(this);
	FVector Root;
	FRotator Rot;
	BirdPose(BirdIndex, Now, Root, Rot);
	// Suelta a la que llevara en el pico.
	if (Attack.Kind == 2 && Attack.Result == 1 && !bReleased)
	{
		ReleaseCarried();
	}
	Attack.Kind = 3;
	Attack.Bird = static_cast<uint8>(BirdIndex);
	Attack.Victim = nullptr;
	Attack.Aim = Root;
	Attack.Hold = FVector(Root.X, Root.Y, GroundAt(Root));
	Attack.StartTime = static_cast<float>(Now);
	Attack.Result = 0;
	Attack.bLocked = 1;
	++Attack.Serial;
	ForceNetUpdate();
	OnAttackChanged();
}

bool ATN_BeachGullZone::GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const
{
	using namespace TNBeachGull;
	if (!Birds.IsValidIndex(Attack.Bird))
	{
		return false;
	}
	const double Now = ServerNow(this);
	const float Tau = static_cast<float>(Now - static_cast<double>(Attack.StartTime));
	// Solo el que baja en picado (también picando en el sitio), el que lleva una tortuga y el ya mareado.
	bool bHittable = false;
	if (Attack.Kind == 2)
	{
		bHittable = Tau >= ClimbTime && (Attack.Result != 1 || Tau - StrikeTime < CarrySeconds());
	}
	else if (Attack.Kind == 3)
	{
		bHittable = true;
	}
	if (!bHittable)
	{
		return false;
	}
	const FBird& Bird = Birds[Attack.Bird];
	FVector Root;
	FRotator Rot;
	BirdPose(Attack.Bird, Now, Root, Rot);
	const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(Bird.bPelican);
	const FVector BodyAt = Root + Rot.RotateVector(G.BodyPivot * Bird.Scale);
	// A tiro de piedra: cerca de la arena.
	if (BodyAt.Z - GroundAt(BodyAt) > HittableHeight)
	{
		return false;
	}
	OutA = BodyAt;
	OutB = Root + Rot.RotateVector((G.BodyPivot + G.HeadPivot) * Bird.Scale);
	OutRadius = Bird.Span * 0.12f;
	return true;
}

FVector ATN_BeachGullZone::GetHitStunAnchor() const
{
	if (Attack.Kind == 3 && Birds.IsValidIndex(Attack.Bird))
	{
		const FBird& Bird = Birds[Attack.Bird];
		const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(Bird.bPelican);
		FVector Root = Bird.Pos;
		FRotator Rot(Bird.Pitch, Bird.Yaw, Bird.Bank);
		if (Root.IsZero())
		{
			BirdPose(Attack.Bird, ServerNow(this), Root, Rot);
		}
		return Root + Rot.RotateVector((G.BodyPivot + G.HeadPivot) * Bird.Scale) + FVector(0.0, 0.0, 12.0 * Bird.Scale);
	}
	return GetActorLocation() + FVector(0.0, 0.0, 3000.0);
}

float ATN_BeachGullZone::GetHitStunScale() const
{
	const bool bPelican = Birds.IsValidIndex(Attack.Bird) && Birds[Attack.Bird].bPelican;
	return (bPelican ? 9.f : 7.f) * SizeK;
}

// ─────────────────────────────────────────────────────────────────────────────
// La tortuga en el pico (todas las máquinas)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGullZone::TickHold()
{
	using namespace TNBeachGull;
	// Quién va en el pico ahora según el ataque replicado: cogida, sin soltar (en el servidor, bReleased; en los clientes,
	// Attack.ReleaseTime, que llega aunque su reloj vaya por detrás), dentro del vuelo y de pie (ni en bola, ni derribada,
	// ni mareada).
	ATortugaCharacter* Want = nullptr;
	float U = 0.f;
	if (Attack.Kind == 2 && Attack.Result == 1 && Attack.ReleaseTime <= 0.f && !(HasAuthority() && bReleased))
	{
		ATortugaCharacter* Victim = Attack.Victim;
		U = static_cast<float>(ServerNow(this) - static_cast<double>(Attack.StartTime)) - StrikeTime;
		if (IsValid(Victim) && !Victim->IsDead() && U >= 0.f && U < CarrySeconds() && !Victim->IsInShell() && !Victim->IsKnockedDown()
			&& !TNBeach::IsTurtleStunned(Victim))
		{
			Want = Victim;
		}
	}
	if (GetHeldTurtle() != Want)
	{
		if (GetHeldTurtle())
		{
			EndHoldTurtle();
		}
		if (Want)
		{
			BeginHoldTurtle(Want);
		}
	}
	if (!Want || GetHeldTurtle() != Want)
	{
		return;
	}
	// La espalda de su caparazón en el pico (la sujeción la coloca por su hueso de la espalda o por la cápsula).
	PlaceHeldTurtle(GripPath(U) + CourseBack * ShellBack, HeldYaw());
}

// ─────────────────────────────────────────────────────────────────────────────
// Efectos
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGullZone::OnRep_Attack()
{
	OnAttackChanged();
}

void ATN_BeachGullZone::OnAttackChanged()
{
	if (!bHasScreen)
	{
		SeenSerial = Attack.Serial;
		SeenResult = Attack.Result;
		return;
	}
	const bool bValidBird = Birds.IsValidIndex(Attack.Bird);
	const float Pitch = bValidBird && Birds[Attack.Bird].bPelican ? 0.55f : 1.f;
	if (Attack.Serial != SeenSerial)
	{
		SeenSerial = Attack.Serial;
		SeenResult = 0;
		WarnPhase = 0.f;
		// Ataque nuevo: dónde cae su cagada se mira en el primer fotograma (no se queda la altura del anterior).
		DropGroundTimer = 0.f;
		DropSurfaceLift = 0.f;
		bSwoopPlayed = false;
		bWhistlePlayed = false;
		bReleasePlayed = false;
		bPeckPlayed = false;
		if (Attack.Kind == 3 && bValidBird)
		{
			// Le han dado: plumas por el aire y un graznido que se viene abajo.
			const FVector At = Attack.Aim;
			TNBeachKit::BurstAt(Feathers, At + FVector(0.0, 0.0, 200.0), FVector::UpVector, 16);
			if (Voice)
			{
				Voice->SetWorldLocation(At);
				Voice->Play(ETNBeachSfx::Squawk, Pitch * 0.7f, 1.2f);
			}
			Birds[Attack.Bird].JawOpenLeft = 0.6f;
		}
		else if (Attack.Kind != 0 && Voice && bValidBird)
		{
			Voice->SetWorldLocation(Birds[Attack.Bird].Pos);
			Voice->Play(ETNBeachSfx::Squawk, Pitch, 1.1f);
			Birds[Attack.Bird].JawOpenLeft = 0.35f;
		}
	}
	if (Attack.Result != SeenResult)
	{
		SeenResult = Attack.Result;
		if (Attack.Kind == 2 && bValidBird)
		{
			const ATortugaCharacter* Victim = Attack.Victim;
			if (Voice)
			{
				// Con la tortuga: pico que se cierra de golpe y graznido. Sin ella: graznido de rabia (el picotazo, al llegar).
				Voice->SetWorldLocation(Birds[Attack.Bird].Pos);
				if (Attack.Result == 1)
				{
					Voice->Play(ETNBeachSfx::Clack, 1.6f, 1.1f);
				}
				Voice->Play(ETNBeachSfx::Squawk, Pitch * (Attack.Result == 1 ? 1.15f : 0.85f), 1.2f);
			}
			if (Attack.Result == 1 && Victim)
			{
				const FVector At = Victim->GetActorLocation();
				TNBeachKit::BurstAt(Feathers, At + FVector(0.0, 0.0, 250.0), FVector::UpVector, 18);
				TNBeachKit::BurstAt(SandPuff, At - FVector(0.0, 0.0, 60.0), FVector::UpVector, 12);
				UTN_BeachCameraShake::Kick(this, At, 0.55f, 400.f, 2500.f);
				ShowPop(NSLOCTEXT("TNBeach", "GullGrab", "¡ÑAC!"), FColor(255, 200, 40), At + FVector(0.0, 0.0, 300.0), 150.f);
			}
		}
	}
}

void ATN_BeachGullZone::MulticastSplat_Implementation(FVector_NetQuantize Where, const TArray<ATortugaCharacter*>& Hit)
{
	if (!bHasScreen)
	{
		return;
	}
	const FVector At = Where;
	if (Voice)
	{
		Voice->SetWorldLocation(At);
		Voice->Play(ETNBeachSfx::Splat, 1.f, 1.4f);
	}
	TNBeachKit::BurstAt(Droplets, At + FVector(0.0, 0.0, 40.0), FVector::UpVector, 26);
	SpawnSplat(At, nullptr, TNBeachGull::SplatRadius * SizeK / 100.f, 12.f);
	int32 StainVariant = 0;
	for (ATortugaCharacter* Turtle : Hit)
	{
		if (Turtle)
		{
			// En los clientes la pinta OnRep_ShellStains (también a quien entra luego); aquí solo el anfitrión con pantalla.
			if (HasAuthority())
			{
				SpawnStain(Turtle, Attack.Serial, StainVariant);
			}
			++StainVariant;
			TNBeachKit::BurstAt(Droplets, Turtle->GetActorLocation() + FVector(0.0, 0.0, 120.0), FVector::UpVector, 14);
			UTN_BeachCameraShake::Kick(this, Turtle->GetActorLocation(), 0.4f, 200.f, 1200.f);
		}
	}
	ShowPop(NSLOCTEXT("TNBeach", "GullSplat", "¡PLOF!"), FColor(250, 250, 240), At + FVector(0.0, 0.0, 280.0), Hit.Num() > 0 ? 170.f : 120.f);
}

void ATN_BeachGullZone::SpawnSplat(const FVector& Where, ATortugaCharacter* InTurtle, float InScale, float Life)
{
	UStaticMesh* Mesh = nullptr;
	if (InTurtle)
	{
		Mesh = TNBeachKit::CachedMesh(TEXT("Beach.ShellSplat"), [](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildShellSplat(M); });
	}
	else
	{
		Mesh = TNBeachKit::CachedMesh(TEXT("Beach.Splat"), [](TNProcMesh::FTNProcMeshBuffers& M) { TNBeachMeshes::BuildSplat(M, 77u); });
	}
	UStaticMeshComponent* Comp = TNBeachKit::AddPart(this, GetRootComponent(), Mesh, FVector::ZeroVector, false);
	if (!Comp)
	{
		return;
	}
	if (InTurtle)
	{
		// En el caparazón, pegado a su espalda: con el ragdoll del derribo va con el cuerpo.
		// Pegado al caparazón (el hueso de la espalda está a ~24 cm de su superficie): solo es el plan B de SpawnStain.
		TNBeachKit::AttachToTurtleBack(Comp, InTurtle, InScale, 24.f, 6.f);
	}
	else
	{
		const float Yaw = 360.f * TNBeachKit::Hash01(static_cast<uint32>(Clock * 1000.f) + static_cast<uint32>(Splats.Num()) * 7u);
		Comp->SetAbsolute(true, true, true);
		Comp->SetWorldTransform(FTransform(FRotator(0.f, Yaw, 0.f), Where + FVector(0.0, 0.0, 4.0), FVector(InScale)));
	}
	Splats.Add(Comp);
	SplatBorn.Add(Clock);
	SplatLife.Add(Life);
	SplatScale.Add(InScale);
}

void ATN_BeachGullZone::PruneShellStains(double Now)
{
	const int32 Before = ShellStains.Num();
	ShellStains.RemoveAll([Now](const FTNBeachGullStain& Stain)
	{
		return !Stain.Turtle || Now - static_cast<double>(Stain.ServerTime) >= static_cast<double>(TNBeachGull::StainLife);
	});
	if (ShellStains.Num() != Before)
	{
		ForceNetUpdate();
	}
}

void ATN_BeachGullZone::OnRep_ShellStains()
{
	if (!bHasScreen)
	{
		return;
	}
	const double Now = ServerNow(this);
	TSet<uint16> Current;
	for (const FTNBeachGullStain& Stain : ShellStains)
	{
		const uint16 Key = static_cast<uint16>((static_cast<uint16>(Stain.Serial) << 8) | Stain.Variant);
		Current.Add(Key);
		// Sin su tortuga todavía (aún no ha llegado a esta máquina): vuelve a llamarse cuando llegue.
		if (!Stain.Turtle || ShownStainKeys.Contains(Key))
		{
			continue;
		}
		const float Age = FMath::Max(0.f, static_cast<float>(Now - static_cast<double>(Stain.ServerTime)));
		if (Age < TNBeachGull::StainLife)
		{
			SpawnStain(Stain.Turtle, Stain.Serial, Stain.Variant, Age);
		}
		ShownStainKeys.Add(Key);
	}
	// Solo se recuerdan las que siguen en la lista (el Serial da la vuelta a los 256 ataques).
	ShownStainKeys = ShownStainKeys.Intersect(Current);
}

void ATN_BeachGullZone::SpawnStain(ATortugaCharacter* InTurtle, uint8 Serial, int32 Variant, float Age)
{
	using namespace TNBeachGull;
	if (!InTurtle || Age >= StainLife)
	{
		return;
	}
	UMaterialInterface* Material = PoopDecalMaterial();
	UDecalComponent* Decal = Material ? NewObject<UDecalComponent>(this, NAME_None, RF_Transient) : nullptr;
	UMaterialInstanceDynamic* Mid = Decal ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (!Mid || !PlaceStainDecal(Decal, InTurtle))
	{
		// Sin el material (falta ejecutar Scripts/create_poop_decal.py) o sin hueso: el pegote de siempre, pero pequeño y pegado.
		SpawnSplat(FVector::ZeroVector, InTurtle, 0.6f, StainLife - Age);
		return;
	}
	// Si se acumulan, se va la más vieja.
	if (StainDecals.Num() >= MaxStains)
	{
		if (StainDecals[0])
		{
			StainDecals[0]->DestroyComponent();
		}
		StainDecals.RemoveAt(0);
		StainMids.RemoveAt(0);
		StainBorn.RemoveAt(0);
	}
	static const FName SeedName(TEXT("Seed"));
	static const FName FadeName(TEXT("Fade"));
	// La misma forma en todas las máquinas: sale del ataque (su número de serie replicado) y de cuál de las manchadas es.
	Mid->SetScalarParameterValue(SeedName, 1.f + 97.f * TNBeachKit::Hash01(static_cast<uint32>(Serial) * 131u + static_cast<uint32>(Variant) * 17u + 7u));
	Mid->SetScalarParameterValue(FadeName, 1.f);
	Decal->SetDecalMaterial(Mid);
	Decal->DecalSize = FVector(StainHalfDepth, StainHalfSize, StainHalfSize);
	Decal->FadeScreenSize = 0.002f;
	Decal->RegisterComponent();
	StainDecals.Add(Decal);
	StainMids.Add(Mid);
	StainBorn.Add(Clock - Age);
}

void ATN_BeachGullZone::TickStains()
{
	using namespace TNBeachGull;
	static const FName FadeName(TEXT("Fade"));
	for (int32 s = StainDecals.Num() - 1; s >= 0; --s)
	{
		UDecalComponent* Decal = StainDecals[s];
		const float Age = Clock - StainBorn[s];
		// Se va a su hora o si su tortuga ya no está (sin la malla a la que iba sujeta no hay dónde pintar).
		if (!Decal || Age >= StainLife || !Decal->GetAttachParent())
		{
			if (Decal)
			{
				Decal->DestroyComponent();
			}
			StainDecals.RemoveAt(s);
			StainMids.RemoveAt(s);
			StainBorn.RemoveAt(s);
			continue;
		}
		// Entera hasta StainFadeStart; después se seca desde los bordes y se desvanece (el material lo hace con Fade).
		if (Age > StainFadeStart && StainMids.IsValidIndex(s) && StainMids[s])
		{
			StainMids[s]->SetScalarParameterValue(FadeName, 1.f - Smooth01((Age - StainFadeStart) / (StainLife - StainFadeStart)));
		}
	}
}

void ATN_BeachGullZone::TickWarnMark(float DeltaSeconds, double Now, const FVector& View, bool bNear)
{
	using namespace TNBeachGull;
	if (!WarnMark)
	{
		return;
	}
	bool bShow = false;
	ATortugaCharacter* Victim = Attack.Victim;
	if (bNear && Attack.Kind == 1 && Attack.Result == 0 && IsValid(Victim) && !Victim->IsDead())
	{
		const float Tau = static_cast<float>(Now - static_cast<double>(Attack.StartTime));
		const float WarnStart = DropTime - WarnLead;
		const float FallEnd = DropTime + FallTime;
		if (Tau >= WarnStart && Tau < FallEnd)
		{
			// Cuanto más cae, más deprisa parpadea (empieza a 2 Hz y acaba a 12 Hz); cuando ya cae por la línea que llevaba la
			// tortuga (TNBeachGullTuning::IsCommitted), se queda fijo: es el momento de girar.
			const float U = FMath::Clamp((Tau - WarnStart) / (FallEnd - WarnStart), 0.f, 1.f);
			WarnPhase += DeltaSeconds * FMath::Lerp(WarnRateSlow, WarnRateFast, U * U);
			bShow = TNBeachGullTuning::IsCommitted(TNBeachGullTuning::PoopPlan(), Tau) || FMath::Frac(WarnPhase) < WarnOnFraction;
			// Sobre su cabeza, de cara a la cámara (solo gira en vertical) y más grande cuanto más lejos.
			const FVector Head = Victim->GetActorLocation() + FVector(0.0, 0.0, Victim->GetSimpleCollisionHalfHeight() + WarnAbove + 6.0 * FMath::Sin(Clock * 9.f));
			FVector ToCamera = View - Head;
			ToCamera.Z = 0.0;
			const float Yaw = ToCamera.IsNearlyZero() ? 0.f : static_cast<float>(ToCamera.Rotation().Yaw);
			const float Grow = FMath::Clamp(static_cast<float>(FVector::Dist(View, Head)) / WarnGrowDist, 1.f, WarnMaxScale);
			WarnMark->SetWorldTransform(FTransform(FRotator(0.f, Yaw, 0.f), Head, FVector(Grow)));
		}
	}
	if (WarnMark->IsVisible() != bShow)
	{
		WarnMark->SetVisibility(bShow);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Visual
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachGullZone::PoseBird(int32 Index, float DeltaSeconds, bool bAttacking, float Tau)
{
	using namespace TNBeachGull;
	FBird& Bird = Birds[Index];
	const float Rate = Bird.bPelican ? 0.7f : 1.f;
	// Planea con rachas de aleteo; en el picado, alas recogidas; llevándola, aleteo fuerte.
	float Flap = 6.f + 3.f * FMath::Sin(Clock * 1.3f + Bird.Phase);
	const float Cycle = FMath::Fmod(Clock * 0.25f + Bird.Phase, 1.f);
	if (Cycle < 0.3f)
	{
		Flap = 35.f * FMath::Sin(Clock * 2.f * PI * 2.4f * Rate);
	}
	float Sweep = 0.f;
	float Tuck = -70.f;
	float HeadPitch = 0.f;
	float HeadYaw = 6.f * FMath::Sin(Clock * 0.9f + Bird.Phase);
	float JawTarget = 0.f;
	if (bAttacking && Attack.Kind == 2)
	{
		const float U = Tau - StrikeTime;
		if (Tau < ClimbTime)
		{
			// Sube a colocarse aleteando.
			Flap = 40.f * FMath::Sin(Clock * 2.f * PI * 3.f * Rate);
		}
		else if (Tau < StrikeTime - JawLead)
		{
			// En picado: alas medio abiertas mientras aún corrige hacia la tortuga; recogidas del todo hacia atrás cuando ya va
			// lanzado por su línea (el aviso para girar: TNBeachGullTuning::IsCommitted).
			const bool bCommitted = TNBeachGullTuning::IsCommitted(TNBeachGullTuning::DivePlan(), Tau);
			Sweep = bCommitted ? 60.f : 25.f;
			Flap = bCommitted ? 4.f : 14.f * FMath::Sin(Clock * 2.f * PI * 2.f * Rate);
			HeadPitch = bCommitted ? -28.f : -15.f;
			Tuck = -85.f;
		}
		else if (Tau < StrikeTime || Attack.Result == 0)
		{
			// Frena: alas abiertas hacia delante, patas por delante y el pico abierto.
			Sweep = -15.f;
			Flap = 28.f * FMath::Sin(Clock * 2.f * PI * 4.f * Rate);
			Tuck = 35.f;
			HeadPitch = StrikeHeadPitch;
			HeadYaw = 0.f;
			JawTarget = 40.f;
		}
		else if (Attack.Result == 1 && U < CarrySeconds())
		{
			// Con la tortuga en el pico: cabeza gacha (la del agarre), aleteo fuerte al subir y más suave al volar.
			HeadPitch = CarryHeadPitch(U);
			HeadYaw = 0.f;
			JawTarget = 9.f + 3.f * FMath::Sin(Clock * 11.f);
			Flap = U < RiseEnd ? 50.f * FMath::Sin(Clock * 2.f * PI * 3.4f * Rate) : 22.f * FMath::Sin(Clock * 2.f * PI * 2.f * Rate);
			Sweep = -5.f;
			Tuck = 15.f + 10.f * FMath::Sin(Clock * 6.f);
		}
		else if (Attack.Result == 1)
		{
			// La ha soltado: pico abierto un momento y remonta.
			JawTarget = U < CarrySeconds() + 0.5f ? 35.f : 0.f;
			Flap = 40.f * FMath::Sin(Clock * 2.f * PI * 3.f * Rate);
		}
		else if (U < PeckHold)
		{
			// Falla y pica en el sitio: cabeza abajo con dos picotazos, alas abiertas para no caerse y patas delante.
			const float Bob = FMath::Abs(FMath::Sin(FMath::Max(0.f, U - PeckDown) * 14.f));
			HeadPitch = PeckHeadPitch + 25.f * Bob;
			HeadYaw = 0.f;
			JawTarget = 22.f * Bob;
			Flap = 18.f * FMath::Sin(Clock * 2.f * PI * 3.f * Rate);
			Sweep = -15.f;
			Tuck = 30.f;
		}
		else
		{
			Flap = 40.f * FMath::Sin(Clock * 2.f * PI * 3.f * Rate);
		}
	}
	else if (bAttacking && Attack.Kind == 3)
	{
		const float StunFor = FMath::Max(DazeFall, HitStunEndTime - Attack.StartTime);
		if (Tau < DazeFall)
		{
			// Cae dando tumbos, aleteando como puede.
			Flap = 55.f * FMath::Sin(Clock * 2.f * PI * 4.f * Rate);
			Sweep = -10.f;
			HeadPitch = 25.f * FMath::Sin(Clock * 9.f);
			Tuck = 0.f;
			JawTarget = 30.f;
		}
		else if (Tau < StunFor)
		{
			// Sentada en la arena, alas caídas y la cabeza dando vueltas.
			Flap = -30.f + 3.f * FMath::Sin(Clock * 2.f);
			Sweep = 25.f;
			Tuck = 0.f;
			HeadPitch = -15.f + 8.f * FMath::Sin(Clock * 1.7f);
			HeadYaw = 30.f * FMath::Sin(Clock * 2.6f);
			JawTarget = 14.f;
		}
		else
		{
			// Despega aleteando fuerte.
			Flap = 45.f * FMath::Sin(Clock * 2.f * PI * 3.2f * Rate);
			Tuck = -40.f;
		}
	}
	else if (bAttacking && Attack.Kind == 1 && Tau < DropTime + 0.4f)
	{
		HeadPitch = -25.f;
	}
	if (Bird.JawOpenLeft > 0.f)
	{
		Bird.JawOpenLeft -= DeltaSeconds;
		JawTarget = FMath::Max(JawTarget, 28.f);
	}
	Bird.Jaw = FMath::FInterpTo(Bird.Jaw, JawTarget, DeltaSeconds, 18.f);
	auto PosePart = [this](int32 PartIndex, const FRotator& Rot)
	{
		if (BirdParts.IsValidIndex(PartIndex) && PartPivots.IsValidIndex(PartIndex))
		{
			TNBeachKit::Pose(BirdParts[PartIndex], PartPivots[PartIndex], Rot);
		}
	};
	PosePart(Bird.WingL, FRotator(0.f, -Sweep, Flap));
	PosePart(Bird.WingR, FRotator(0.f, Sweep, -Flap));
	PosePart(Bird.LegL, FRotator(Tuck, 0.f, 0.f));
	PosePart(Bird.LegR, FRotator(Tuck, 0.f, 0.f));
	PosePart(Bird.Head, FRotator(HeadPitch, HeadYaw, 0.f));
	if (Jaws.IsValidIndex(Index) && Jaws[Index])
	{
		TNBeachKit::Pose(Jaws[Index], TNBeachMeshes::BirdGeom(Bird.bPelican).BeakBase, FRotator(-Bird.Jaw, 0.f, 0.f));
	}
}

void ATN_BeachGullZone::VisualTick(float DeltaSeconds)
{
	using namespace TNBeachGull;
	Clock += DeltaSeconds;
	const double Now = ServerNow(this);
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);
	const bool bNear = ViewDistance < GetVisualRange();
	const float Dt = FMath::Max(DeltaSeconds, 1e-3f);

	for (int32 i = 0; i < Birds.Num(); ++i)
	{
		FBird& Bird = Birds[i];
		const bool bAttacking = Attack.Kind != 0 && Attack.Bird == i;
		const float Tau = bAttacking ? static_cast<float>(Now - static_cast<double>(Attack.StartTime)) : 0.f;
		const float U = Tau - StrikeTime;
		const bool bCarry = bAttacking && Attack.Kind == 2 && Attack.Result == 1 && U >= 0.f && U < CarrySeconds();
		const bool bFirst = Bird.Pos.IsZero();
		FVector NewPos;
		if (bCarry)
		{
			// Con la tortuga en el pico: el giro va hacia el del agarre y la raíz se coloca para que el pico esté en su
			// caparazón (la tortuga la coloca TickHold con el mismo camino).
			const FRotator Want = CarryRotation(U);
			const FRotator Shown = FMath::RInterpTo(FRotator(Bird.Pitch, Bird.Yaw, Bird.Bank), Want, Dt, 8.f);
			Bird.Pitch = static_cast<float>(Shown.Pitch);
			Bird.Yaw = static_cast<float>(Shown.Yaw);
			Bird.Bank = static_cast<float>(Shown.Roll);
			NewPos = RootForGrip(Bird, GripPath(U), Shown, CarryHeadPitch(U));
			if (U < CatchBlend)
			{
				NewPos = FMath::Lerp(StrikeRoot(Bird, Now), NewPos, static_cast<double>(Smooth01(U / CatchBlend)));
			}
			const FVector Vel = (NewPos - Bird.Pos) / Dt;
			Bird.Vel = bFirst ? FVector::ZeroVector : FMath::Lerp(Bird.Vel, Vel, static_cast<double>(1.f - FMath::Exp(-Dt / 0.15f)));
			Bird.Pos = NewPos;
		}
		else
		{
			NewPos = bAttacking ? AttackPos(Bird, Now, Tau) : CirclePos(Bird, Now);
			const FVector Vel = (NewPos - Bird.Pos) / Dt;
			Bird.Vel = bFirst ? FVector::ZeroVector : FMath::Lerp(Bird.Vel, Vel, static_cast<double>(1.f - FMath::Exp(-Dt / 0.15f)));
			Bird.Pos = NewPos;
			if (Bird.Vel.SizeSquared2D() > 2500.0)
			{
				const float NewYaw = static_cast<float>(Bird.Vel.Rotation().Yaw);
				const float YawRate = FMath::FindDeltaAngleDegrees(Bird.Yaw, NewYaw) / Dt;
				Bird.Yaw = FMath::FixedTurn(Bird.Yaw, NewYaw, 400.f * Dt);
				Bird.Bank = FMath::FInterpTo(Bird.Bank, FMath::Clamp(YawRate * 0.3f, -40.f, 40.f), Dt, 3.f);
			}
			float Climb = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Bird.Vel.Z, FMath::Max(1.0, Bird.Vel.Size2D()))));
			// Al final del picado frena levantando el morro (el pico llega por delante, abierto); si falla, baja el morro
			// para picar en la arena (como lo cuenta PeckRoot).
			const bool bPecking = bAttacking && Attack.Kind == 2 && Attack.Result == 2 && U >= -0.5f && U < PeckHold;
			if (bAttacking && Attack.Kind == 2 && Tau > StrikeTime - 0.5f)
			{
				const float Brake = Smooth01((Tau - (StrikeTime - 0.5f)) / 0.4f);
				if (Attack.Result != 2)
				{
					Climb = FMath::Lerp(Climb, StrikePitch, Brake);
				}
				else if (bPecking)
				{
					Climb = FMath::Lerp(Climb, PeckPitch, Brake);
				}
			}
			Bird.Pitch = FMath::FInterpTo(Bird.Pitch, FMath::Clamp(Climb, -60.f, 45.f), Dt, bPecking ? 12.f : 5.f);
			if (bAttacking && Attack.Kind == 3)
			{
				// Mareada: da tumbos al caer y se tambalea sentada.
				const float StunFor = FMath::Max(DazeFall, HitStunEndTime - Attack.StartTime);
				if (Tau < DazeFall)
				{
					Bird.Bank = 35.f * FMath::Sin(Clock * 11.f);
				}
				else if (Tau < StunFor)
				{
					Bird.Bank = 8.f * FMath::Sin(Clock * 2.2f);
				}
			}
		}
		if (BirdRoots.IsValidIndex(i) && BirdRoots[i])
		{
			BirdRoots[i]->SetWorldTransform(FTransform(FRotator(Bird.Pitch, Bird.Yaw, Bird.Bank), Bird.Pos, FVector(Bird.Scale)));
		}
		if (bNear || bCarry)
		{
			PoseBird(i, DeltaSeconds, bAttacking, Tau);
		}

		// Sombra en la arena: la de verdad, bajo el cuerpo; cuanto más baja el pájaro, más pequeña, nítida y oscura (el
		// aviso del picado, duro y negro, va aparte: DiveMarker).
		const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(Bird.bPelican);
		const FVector BodyAt = Bird.Pos + FRotator(Bird.Pitch, Bird.Yaw, Bird.Bank).RotateVector(G.BodyPivot * Bird.Scale);
		Bird.ShadowTimer -= DeltaSeconds;
		if (Bird.ShadowTimer <= 0.f)
		{
			Bird.ShadowTimer = bAttacking ? 0.1f : 0.25f;
			Bird.ShadowZ = GroundAt(BodyAt);
		}
		const float Height = FMath::Max(0.f, static_cast<float>(BodyAt.Z) - Bird.ShadowZ);
		// Nítida y de unos 4 m a ras de arena; en lo alto, más grande y tenue.
		const float FreeH = FMath::Clamp(Height / 8000.f, 0.f, 1.f);
		const float ShadowR = Bird.Span * 0.2f * (0.75f + 0.5f * FreeH);
		const float Opacity = FMath::Lerp(0.5f, 0.12f, FreeH);
		if (Shadows.IsValidIndex(i))
		{
			PlaceBirdShadow(Shadows[i], Bird.ShadowEdge, FVector(BodyAt.X, BodyAt.Y, Bird.ShadowZ), bNear ? ShadowR : 0.f, Opacity, 1.f - FreeH);
		}

		// Picado fallido: el picotazo en la arena (o en la sombrilla), con arena que salta y el golpe.
		if (bAttacking && Attack.Kind == 2 && Attack.Result == 2 && U >= PeckDown && !bPeckPlayed)
		{
			bPeckPlayed = true;
			const FVector PeckAt = Attack.Hold;
			if (bNear)
			{
				TNBeachKit::BurstAt(SandPuff, PeckAt + FVector(0.0, 0.0, 40.0), FVector::UpVector, 8);
				TNBeachKit::BurstAt(PeckSand, PeckAt + FVector(0.0, 0.0, 30.0), FVector::UpVector, 26);
				UTN_BeachCameraShake::Kick(this, PeckAt, 0.3f, 400.f, 2500.f);
				ShowPop(NSLOCTEXT("TNBeach", "GullPeck", "¡PIC!"), FColor(255, 240, 200), PeckAt + FVector(0.0, 0.0, 260.0), 130.f);
				if (Voice)
				{
					Voice->SetWorldLocation(PeckAt);
					Voice->Play(ETNBeachSfx::Slam, 1.8f, 0.8f);
					Voice->Play(ETNBeachSfx::Clack, 2.f, 1.f);
				}
			}
		}

		// Graznidos sueltos (abren el pico), el silbido del picado y la suelta.
		if (Voice && bNear)
		{
			Bird.SquawkTimer -= DeltaSeconds;
			if (Bird.SquawkTimer <= 0.f)
			{
				Bird.SquawkTimer = 5.f + 7.f * TNBeachKit::Hash01(static_cast<uint32>(Clock * 100.f) + static_cast<uint32>(i) * 97u);
				Bird.JawOpenLeft = 0.3f;
				Voice->SetWorldLocation(Bird.Pos);
				Voice->Play(ETNBeachSfx::Squawk, (Bird.bPelican ? 0.55f : 1.f) * (0.9f + 0.2f * TNBeachKit::Hash01(static_cast<uint32>(i) + 5u)), 0.7f);
			}
			if (bAttacking && Attack.Kind == 2 && !bSwoopPlayed && Tau >= ClimbTime)
			{
				bSwoopPlayed = true;
				Voice->SetWorldLocation(Bird.Pos);
				Voice->Play(ETNBeachSfx::Swoop, Bird.bPelican ? 0.7f : 1.f, 1.3f);
			}
			if (bAttacking && Attack.Kind == 2 && Attack.Result == 1 && !bReleasePlayed && U >= CarrySeconds())
			{
				bReleasePlayed = true;
				Voice->SetWorldLocation(Bird.Pos);
				Voice->Play(ETNBeachSfx::Squawk, Bird.bPelican ? 0.6f : 1.2f, 1.2f);
				TNBeachKit::BurstAt(Feathers, Bird.Pos, FVector::UpVector, 8);
			}
		}
		// Arena que levantan las alas mientras tira de ella cerca del suelo.
		if (bCarry && U < TugTime + 0.3f && bNear)
		{
			DropTrailTimer -= DeltaSeconds;
			if (DropTrailTimer <= 0.f)
			{
				DropTrailTimer = 0.12f;
				TNBeachKit::BurstAt(SandPuff, FVector(Attack.Hold) - FVector(0.0, 0.0, 60.0), FVector::UpVector, 3);
			}
		}
	}

	// Cagada que cae (grande, con su estela, siguiendo al blanco) y su sombra dura y negra: nace diminuta al soltarla y
	// crece hasta el tamaño de la mancha según cae (2,1 s para apartarse corriendo).
	bool bShowDrop = false;
	if (Attack.Kind == 1 && Dropping)
	{
		const float Tau = static_cast<float>(Now - static_cast<double>(Attack.StartTime));
		if (Tau >= DropTime && Tau < DropTime + FallTime && Attack.Result == 0)
		{
			const FVector Target = CurrentAim();
			const float U = (Tau - DropTime) / FallTime;
			// Donde cae de verdad: lo primero firme desde arriba (la arena, o un castillo, una fortaleza o una sombrilla si está
			// encima), con la misma traza que el impacto del servidor (ServerPoop). La sombra y la cagada van ahí, no a la arena
			// de debajo, que queda tapada.
			DropGroundTimer -= DeltaSeconds;
			if (DropGroundTimer <= 0.f)
			{
				DropGroundTimer = 0.1f;
				float SurfaceZ = static_cast<float>(Target.Z);
				FVector SurfaceNormal = FVector::UpVector;
				DropSurfaceLift = TraceDropSurface(this, Target, SurfaceZ, &SurfaceNormal, this) ? SurfaceZ - static_cast<float>(Target.Z) : 0.f;
				// En la arena, su cuesta de siempre; encima de algo, la cara en la que cae (si es más o menos plana).
				DropNormal = FMath::Abs(DropSurfaceLift) < 30.f ? GroundNormalAt(Target) : (SurfaceNormal.Z > 0.5 ? SurfaceNormal : FVector::UpVector);
			}
			const FVector Landing = Target + FVector(0.0, 0.0, DropSurfaceLift);
			// Sale de debajo de la cola del pájaro y cae acelerando.
			const FVector Top = Target + FVector(0.0, 0.0, FMath::Max(PoopHeight + 300.0, DropSurfaceLift + 300.0));
			const FVector At = FMath::Lerp(Top, Landing, static_cast<double>(FMath::Pow(U, 1.8f)));
			Dropping->SetWorldTransform(FTransform(FRotator(8.f * FMath::Sin(Clock * 9.f), Clock * 60.f, 0.f), At, FVector(SizeK * DropScale)));
			const float Grow = 0.45f * U + 0.55f * FMath::Pow(U, 1.6f);
			const float Fade = FMath::Clamp((Tau - DropTime) / MarkerFadeIn, 0.f, 1.f);
			PlaceMarker(DropShadow, Landing, DropNormal, FMath::Lerp(PoopMarkerStartRadius, SplatRadius * SizeK, Grow), MarkerOpacity * Fade);
			bShowDrop = true;
			DropTrailTimer -= DeltaSeconds;
			if (DropTrailTimer <= 0.f)
			{
				DropTrailTimer = 0.04f;
				TNBeachKit::BurstAt(Trail, At + FVector(0.0, 0.0, 90.0 * SizeK), FVector::UpVector, 1);
			}
			if (!bWhistlePlayed && Voice)
			{
				// Silbido de lo que cae: aviso para apartarse.
				bWhistlePlayed = true;
				Voice->SetWorldLocation(Target + FVector(0.0, 0.0, 600.0));
				Voice->Play(ETNBeachSfx::Swoop, 1.9f, 0.7f);
			}
		}
	}
	if (Dropping && Dropping->IsVisible() != bShowDrop)
	{
		Dropping->SetVisibility(bShowDrop);
	}
	if (!bShowDrop)
	{
		PlaceMarker(DropShadow, FVector::ZeroVector, FVector::UpVector, 0.f, 0.f);
	}

	// Aviso del picado: sombra dura y negra donde va a dar (sigue a la tortuga con el blanco). Nace diminuta al empezar
	// a bajar y crece con el pájaro; si falla, se queda donde pica y se desvanece al remontar.
	float MarkerR = 0.f;
	float MarkerA = 0.f;
	FVector MarkerAt = FVector::ZeroVector;
	if (Attack.Kind == 2 && Attack.Result != 1 && Birds.IsValidIndex(Attack.Bird))
	{
		const float Tau = static_cast<float>(Now - static_cast<double>(Attack.StartTime));
		if (Tau >= ClimbTime && Tau < StrikeTime + PeckHold + 0.4f)
		{
			const float Prog = FMath::Clamp((Tau - ClimbTime) / DiveTime, 0.f, 1.f);
			// Crece como baja el pájaro (acelerando), con un poco desde el principio para que se vea nacer.
			const float Grow = 0.3f * Prog + 0.7f * Prog * Prog;
			const float In = FMath::Clamp((Tau - ClimbTime) / MarkerFadeIn, 0.f, 1.f);
			const float Out = Tau > StrikeTime + PeckHold ? 1.f - (Tau - StrikeTime - PeckHold) / 0.4f : 1.f;
			MarkerR = FMath::Lerp(MarkerStartRadius, GrabRadius * SizeK * 1.1f, Grow);
			MarkerA = MarkerOpacity * In * FMath::Clamp(Out, 0.f, 1.f);
			MarkerAt = Attack.Result == 2 ? FVector(Attack.Hold) : CurrentAim();
			MarkerGroundTimer -= DeltaSeconds;
			if (MarkerGroundTimer <= 0.f)
			{
				MarkerGroundTimer = 0.1f;
				MarkerNormal = GroundNormalAt(MarkerAt);
			}
		}
	}
	PlaceMarker(DiveMarker, MarkerAt, MarkerNormal, bNear ? MarkerR : 0.f, MarkerA);

	// Aviso de la cagada sobre la tortuga objetivo y las cagadas pintadas en las tortugas (se secan y se van).
	TickWarnMark(DeltaSeconds, Now, View, bNear);
	TickStains();

	// Manchas: duran su vida y se encogen el último segundo.
	for (int32 s = Splats.Num() - 1; s >= 0; --s)
	{
		UStaticMeshComponent* Splat = Splats[s];
		const float Age = Clock - SplatBorn[s];
		if (!Splat || Age >= SplatLife[s])
		{
			if (Splat)
			{
				Splat->DestroyComponent();
			}
			Splats.RemoveAt(s);
			SplatBorn.RemoveAt(s);
			SplatLife.RemoveAt(s);
			SplatScale.RemoveAt(s);
			continue;
		}
		const float Left = SplatLife[s] - Age;
		if (Left < 1.f)
		{
			const float K = SplatScale[s] * FMath::Max(0.01f, Left);
			// La de la arena es plana (encoge a lo ancho); la del caparazón, entera.
			const bool bFlat = Splat->GetAttachParent() == GetRootComponent();
			Splat->SetWorldScale3D(FVector(K, K, bFlat ? SplatScale[s] : K));
		}
	}

	TNBeachKit::TickEmitterIfBusy(Droplets, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Feathers, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(Trail, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(SandPuff, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(PeckSand, DeltaSeconds, View);
}
