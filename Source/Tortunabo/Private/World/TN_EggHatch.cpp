// ─────────────────────────────────────────────────────────────────────────────
// Salida con huevos: la pausa de 1 s en el huevo roto antes del lanzamiento, común a la carrera
// (TN_BeachRaceGenerator_Start.cpp) y al cooperativo (ATN_ProcStartStructure). Ver TN_EggHatch.h.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/TN_EggHatch.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "Player/TortugaCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "../Lobby/TN_CastleKit.h"

namespace TNEggHatchDetail
{
	// ── Pose (segundos desde que se rompe la tapa) ──
	/** Se agacha un instante: cuánto (fracción del alto) y en cuánto. */
	constexpr double CrouchDepth = 0.14;
	constexpr double CrouchSeconds = 0.1;
	/** Se pone de pie con un muelle amortiguado (rebote de un 4 %): amortiguación (1/s) y frecuencia (rad/s). */
	constexpr double StandDamping = 7.0;
	constexpr double StandFrequency = 17.0;
	/** Saltito del cuerpo al estirarse (cm) y cuánto dura (s). */
	constexpr double StandLift = 9.0;
	constexpr double StandLiftSeconds = 0.3;
	/** Sacudida: cuándo empieza y acaba, vaivenes por segundo, giro del cuerpo sobre sí mismo y balanceo de lado (grados). */
	constexpr double ShakeStart = 0.42;
	constexpr double ShakeEnd = 0.86;
	constexpr double ShakeHz = 8.0;
	constexpr double ShakeTwistDeg = 22.0;
	constexpr double ShakeRollDeg = 7.0;
	/** Se encoge para el salto en los últimos segundos antes del lanzamiento. */
	constexpr double PreLaunchSeconds = 0.14;
	constexpr double PreLaunchDepth = 0.08;
	/** Giro hacia el frente: cuándo empieza y cuándo ya mira al frente. */
	constexpr double TurnStart = 0.25;
	constexpr double TurnEnd = 0.8;

	// ── Trocitos de cáscara ──
	/** Tandas: cuándo salen (s desde la rotura), cuántos y con qué fuerza hacia fuera (cm/s). */
	struct FBitBurst
	{
		double At;
		int32 Count;
		float Speed;
	};
	constexpr FBitBurst Bursts[] = { { 0.0, 5, 220.f }, { 0.44, 10, 330.f }, { 0.62, 5, 260.f } };
	constexpr int32 NumBursts = static_cast<int32>(UE_ARRAY_COUNT(Bursts));
	/** Una tanda que esta máquina ve con más retraso que esto (s) ya no sale. */
	constexpr double BitLateSeconds = 0.25;
	constexpr int32 MaxBits = 24;
	constexpr float BitGravity = -980.f;
	/** Vida de cada trocito (s) y lo que tarda en encoger al final. */
	constexpr float BitLifeMin = 1.1f;
	constexpr float BitLifeMax = 1.5f;
	constexpr float BitShrinkSeconds = 0.35f;
	/** Pasado esto (s) desde el lanzamiento, la pausa se borra aunque quede algo. */
	constexpr double MaxLingerSeconds = 3.0;

	double SmoothStep(double X)
	{
		const double C = FMath::Clamp(X, 0.0, 1.0);
		return C * C * (3.0 - 2.0 * C);
	}

	/**
	 * Trocito de cáscara (~10 cm, dos caras y algo abombado): crema por fuera con una mota del color del huevo y algo más
	 * oscuro por dentro. Una malla por color, compartida por todas las pausas.
	 */
	UStaticMesh* BitMesh(uint32 AccentHex)
	{
		static TMap<uint32, TWeakObjectPtr<UStaticMesh>> Cache;
		if (const TWeakObjectPtr<UStaticMesh>* Found = Cache.Find(AccentHex))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		UMaterialInterface* Mat = TNCastleKit::VertexColorMaterial();
		if (!Mat)
		{
			return nullptr;
		}
		const FLinearColor Outside = TNCastleKit::Pal(0xFFF3DC);
		const FLinearColor Inside = TNCastleKit::Pal(0xE6D7BA);
		const FLinearColor Speck = TNCastleKit::Pal(AccentHex);
		const FVector Crown(0.0, 0.0, 1.6);
		const FVector Rim[6] = { FVector(5.2, 0.4, 0.0), FVector(2.4, 4.6, 0.3), FVector(-2.8, 4.0, 0.0), FVector(-5.0, -0.6, 0.4),
			FVector(-1.6, -4.4, 0.0), FVector(3.6, -3.8, 0.2) };
		TNProcMesh::FTNProcMeshBuffers B;
		for (int32 k = 0; k < 6; ++k)
		{
			const FVector& A = Rim[k];
			const FVector& C = Rim[(k + 1) % 6];
			B.AddTri(Crown, A, C, FVector::UpVector, k == 1 ? Speck : Outside);
			B.AddTri(Crown - FVector(0.0, 0.0, 0.6), A, C, -FVector::UpVector, Inside);
		}
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, Mat);
		if (Mesh)
		{
			// Fuera del recolector, como las mallas compartidas de TNLootGlow.
			Mesh->AddToRoot();
			Cache.Add(AccentHex, Mesh);
		}
		return Mesh;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// TNEggHatch
// ─────────────────────────────────────────────────────────────────────────────

double TNEggHatch::ServerNow(const UWorld* World)
{
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void TNEggHatch::Begin(ACharacter* Turtle, double HatchServerTime, double LaunchServerTime, const FVector& LaunchVelocity,
	float FacingYaw, uint32 AccentHex)
{
	UWorld* World = IsValid(Turtle) ? Turtle->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	// Una pausa a medias de antes (TN.Beach.Egg dos veces seguidas): fuera, sin soltarla; la nueva la sujeta otra vez.
	if (UTN_EggHatchComponent* Previous = Turtle->FindComponentByClass<UTN_EggHatchComponent>())
	{
		Previous->StopHatch(false);
	}
	UTN_EggHatchComponent* Hatch = NewObject<UTN_EggHatchComponent>(Turtle, NAME_None, RF_Transient);
	Hatch->RegisterComponent();
	Hatch->StartHatch(HatchServerTime, LaunchServerTime, LaunchVelocity, FacingYaw, AccentHex);
}

void TNEggHatch::Cancel(ACharacter* Turtle, bool bRelease)
{
	if (UTN_EggHatchComponent* Hatch = IsValid(Turtle) ? Turtle->FindComponentByClass<UTN_EggHatchComponent>() : nullptr)
	{
		Hatch->StopHatch(bRelease);
	}
}

bool TNEggHatch::IsHatching(const ACharacter* Turtle)
{
	const UTN_EggHatchComponent* Hatch = IsValid(Turtle) ? Turtle->FindComponentByClass<UTN_EggHatchComponent>() : nullptr;
	return Hatch && Hatch->IsWaiting();
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_EggHatchComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_EggHatchComponent::UTN_EggHatchComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UTN_EggHatchComponent::StartHatch(double InHatchServerTime, double InLaunchServerTime, const FVector& InLaunchVelocity, float InFacingYaw,
	uint32 InAccentHex)
{
	ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	if (!Turtle)
	{
		DestroyComponent();
		return;
	}
	HatchTime = InHatchServerTime;
	LaunchTime = FMath::Max(InLaunchServerTime, InHatchServerTime);
	LaunchVelocity = InLaunchVelocity;
	FacingYaw = InFacingYaw;
	AccentHex = InAccentHex;
	StartYaw = static_cast<float>(Turtle->GetActorRotation().Yaw);
	// Solo mueve a la tortuga quien la simula: el servidor (a todas) y el cliente que la controla (la suya).
	bMoves = Turtle->HasAuthority() || Turtle->IsLocallyControlled();
	// En su bola de caparazón sigue con su física: ni se sujeta ni se gira (se lanza igual, como antes).
	const ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Turtle);
	bHoldable = !(Tortuga && Tortuga->IsInShell());
	StartViewYaw = StartYaw;
	if (const APlayerController* PC = Cast<APlayerController>(Turtle->GetController()))
	{
		if (PC->IsLocalController())
		{
			StartViewYaw = static_cast<float>(PC->GetControlRotation().Yaw);
		}
	}
	if (CanPose(Turtle))
	{
		const USkeletalMeshComponent* SkelMesh = Turtle->GetMesh();
		BaseMeshLocation = SkelMesh->GetRelativeLocation();
		BaseMeshRotation = SkelMesh->GetRelativeRotation().Quaternion();
		BaseMeshScale = SkelMesh->GetRelativeScale3D();
		bPoseOn = true;
	}
	// El primer paso ya: la sujeta y la pone en su pose (o, si llega tarde, la lanza enseguida).
	Step(0.f);
}

void UTN_EggHatchComponent::StopHatch(bool bRelease)
{
	if (bStopped)
	{
		return;
	}
	bStopped = true;
	RestorePose();
	ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	if (bRelease && bHolding && !bLaunched && Turtle)
	{
		if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
		{
			if (Move->MovementMode == MOVE_None)
			{
				Move->SetMovementMode(MOVE_Falling);
			}
		}
	}
	bHolding = false;
	DestroyComponent();
}

void UTN_EggHatchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Step(DeltaTime);
}

void UTN_EggHatchComponent::Step(float DeltaTime)
{
	using namespace TNEggHatchDetail;
	ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (bStopped || !IsValid(Turtle) || !World)
	{
		if (!bStopped)
		{
			bStopped = true;
			DestroyComponent();
		}
		return;
	}
	const double Now = TNEggHatch::ServerNow(World);
	const double T = Now - HatchTime;
	const bool bLaunchDue = Now >= LaunchTime;

	// Si en la pausa la tumban o se mete en el caparazón, ni pose ni sujetarla: que sigan sus reglas.
	if (const ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Turtle))
	{
		if (Tortuga->IsInShell() || Tortuga->IsKnockedDown())
		{
			bHoldable = false;
		}
	}
	if (bPoseOn && !CanPose(Turtle))
	{
		RestorePose();
	}

	if (bMoves && !bLaunched)
	{
		if (bLaunchDue)
		{
			Launch(Turtle);
		}
		else
		{
			Hold(Turtle);
			TurnToFront(Turtle, T);
		}
	}
	if (bPoseOn)
	{
		if (bLaunchDue)
		{
			RestorePose();
		}
		else
		{
			ApplyPose(Turtle, T, LaunchTime - Now);
		}
	}
	bPauseOver = bLaunchDue;

	if (!bLaunchDue)
	{
		EmitDueBits(Turtle, T);
	}
	const bool bBitsAlive = TickBits(DeltaTime);
	if ((bLaunchDue && !bBitsAlive) || Now - LaunchTime > MaxLingerSeconds)
	{
		bStopped = true;
		DestroyComponent();
	}
}

void UTN_EggHatchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestorePose();
	if (BitsMesh)
	{
		BitsMesh->DestroyComponent();
		BitsMesh = nullptr;
	}
	if (Synth)
	{
		Synth->DestroyComponent();
		Synth = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool UTN_EggHatchComponent::IsVisual(const ACharacter* Turtle) const
{
	const UWorld* World = GetWorld();
	return Turtle && World && World->GetNetMode() != NM_DedicatedServer && !Turtle->IsHidden();
}

bool UTN_EggHatchComponent::CanPose(const ACharacter* Turtle) const
{
	if (!IsVisual(Turtle))
	{
		return false;
	}
	const USkeletalMeshComponent* SkelMesh = Turtle->GetMesh();
	if (!SkelMesh || SkelMesh->IsSimulatingPhysics())
	{
		return false;
	}
	if (const ATortugaCharacter* Tortuga = Cast<ATortugaCharacter>(Turtle))
	{
		return !(Tortuga->IsInShell() || Tortuga->IsKnockedDown() || Tortuga->IsDead() || Tortuga->IsBellyPoseActive());
	}
	return true;
}

void UTN_EggHatchComponent::ApplyPose(ACharacter* Turtle, double T, double ToLaunch)
{
	using namespace TNEggHatchDetail;
	USkeletalMeshComponent* SkelMesh = Turtle->GetMesh();
	if (!SkelMesh)
	{
		return;
	}
	// De pie: se agacha un instante y se estira hasta su alto con un rebote (muelle amortiguado).
	double Height = 1.0;
	if (T < CrouchSeconds)
	{
		Height = 1.0 - CrouchDepth * SmoothStep(T / CrouchSeconds);
	}
	else
	{
		const double Since = T - CrouchSeconds;
		Height = 1.0 - CrouchDepth * FMath::Exp(-StandDamping * Since) * FMath::Cos(StandFrequency * Since);
	}
	// Se encoge para el salto justo antes del lanzamiento.
	if (ToLaunch < PreLaunchSeconds)
	{
		Height *= 1.0 - PreLaunchDepth * (1.0 - FMath::Max(0.0, ToLaunch) / PreLaunchSeconds);
	}
	const double Lift = T > CrouchSeconds ? StandLift * FMath::Sin(UE_DOUBLE_PI * FMath::Clamp((T - CrouchSeconds) / StandLiftSeconds, 0.0, 1.0)) : 0.0;

	// Se sacude la cáscara: vaivén rápido del cuerpo sobre sí mismo (y algo de lado) que crece y se apaga.
	double Twist = 0.0;
	double Roll = 0.0;
	if (T > ShakeStart && T < ShakeEnd)
	{
		const double U = (T - ShakeStart) / (ShakeEnd - ShakeStart);
		const double Envelope = FMath::Sin(UE_DOUBLE_PI * U) * (1.0 - 0.35 * U);
		const double Phase = UE_DOUBLE_TWO_PI * ShakeHz * (T - ShakeStart);
		Twist = ShakeTwistDeg * Envelope * FMath::Sin(Phase);
		Roll = ShakeRollDeg * Envelope * FMath::Cos(Phase);
	}

	// Mismo volumen: lo que pierde de alto lo gana de ancho (ejes de la malla: X ancho, Y tripa-espalda, Z alto). Los giros,
	// en el espacio de la cápsula (Z arriba, X hacia delante) y sobre los pies (el origen de la malla).
	const double Width = 1.0 / FMath::Sqrt(FMath::Max(0.5, Height));
	const FQuat Shake = FQuat(FVector::UpVector, FMath::DegreesToRadians(Twist)) * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll));
	SkelMesh->SetRelativeLocationAndRotation(BaseMeshLocation + FVector(0.0, 0.0, Lift), Shake * BaseMeshRotation);
	SkelMesh->SetRelativeScale3D(BaseMeshScale * FVector(Width, Width, Height));
}

void UTN_EggHatchComponent::RestorePose()
{
	if (!bPoseOn)
	{
		return;
	}
	bPoseOn = false;
	const ACharacter* Turtle = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* SkelMesh = Turtle ? Turtle->GetMesh() : nullptr;
	// En ragdoll no se toca (se movería un cuerpo simulado): la devuelve quien la levanta.
	if (!SkelMesh || SkelMesh->IsSimulatingPhysics())
	{
		return;
	}
	SkelMesh->SetRelativeLocationAndRotation(BaseMeshLocation, BaseMeshRotation);
	SkelMesh->SetRelativeScale3D(BaseMeshScale);
}

void UTN_EggHatchComponent::Hold(ACharacter* Turtle)
{
	UCharacterMovementComponent* Move = bHoldable ? Turtle->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	// Quieta en el huevo. Si algo la suelta entretanto (devolver el control llega cuando llega), se la vuelve a sujetar.
	if (Move->MovementMode != MOVE_None || !Move->Velocity.IsNearlyZero())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	bHolding = true;
}

void UTN_EggHatchComponent::TurnToFront(ACharacter* Turtle, double T)
{
	using namespace TNEggHatchDetail;
	const double Alpha = SmoothStep((T - TurnStart) / (TurnEnd - TurnStart));
	if (Alpha <= 0.0)
	{
		return;
	}
	// La tortuga (el servidor y su dueño, a la vez y con el mismo reloj; a las demás les llega con su movimiento).
	const double Delta = FMath::FindDeltaAngleDegrees(StartYaw, FacingYaw);
	if (bHoldable && FMath::Abs(Delta) > 2.0)
	{
		Turtle->SetActorRotation(FRotator(0.0, StartYaw + Delta * Alpha, 0.0));
	}
	// Y la cámara de su jugador, en su máquina: solo el giro (la inclinación, la que tenga).
	APlayerController* PC = Cast<APlayerController>(Turtle->GetController());
	if (PC && PC->IsLocalController())
	{
		const double ViewDelta = FMath::FindDeltaAngleDegrees(StartViewYaw, FacingYaw);
		if (FMath::Abs(ViewDelta) > 5.0)
		{
			FRotator View = PC->GetControlRotation();
			View.Yaw = StartViewYaw + ViewDelta * Alpha;
			PC->SetControlRotation(View);
		}
	}
}

void UTN_EggHatchComponent::Launch(ACharacter* Turtle)
{
	bLaunched = true;
	bHolding = false;
	if (UCharacterMovementComponent* Move = Turtle->GetCharacterMovement())
	{
		// Sujeta en la pausa (o recién soltada de la espera) sigue sin modo de movimiento: el lanzamiento necesita uno.
		if (Move->MovementMode == MOVE_None)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	Turtle->LaunchCharacter(LaunchVelocity, true, true);
}

void UTN_EggHatchComponent::EmitDueBits(ACharacter* Turtle, double T)
{
	using namespace TNEggHatchDetail;
	for (int32 i = 0; i < NumBursts; ++i)
	{
		const uint8 Mask = static_cast<uint8>(1u << i);
		if ((BitBursts & Mask) != 0 || T < Bursts[i].At)
		{
			continue;
		}
		BitBursts |= Mask;
		// Si esta máquina llega tarde a la tanda, ya no sale.
		if (T - Bursts[i].At > BitLateSeconds || !IsVisual(Turtle))
		{
			continue;
		}
		EmitBits(Turtle, Bursts[i].Count, Bursts[i].Speed);
		if (i > 0)
		{
			// Crujido de cáscara al sacudirse: el sonido de rebuscar, agudo y flojito.
			if (!Synth)
			{
				Synth = UTN_SearchSynthComponent::AttachTo(Turtle, Turtle->GetActorLocation());
			}
			if (Synth)
			{
				Synth->TriggerSound(ETNSearchSound::Rummage, FMath::FRandRange(1.9f, 2.3f), i == 1 ? 0.55f : 0.35f);
			}
		}
	}
}

void UTN_EggHatchComponent::EmitBits(ACharacter* Turtle, int32 Count, float Speed)
{
	using namespace TNEggHatchDetail;
	if (!BitsMesh)
	{
		UStaticMesh* Shard = BitMesh(AccentHex);
		if (!Shard)
		{
			return;
		}
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Turtle, NAME_None, RF_Transient);
		Comp->SetStaticMesh(Shard);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetCastShadow(false);
		Comp->SetMobility(EComponentMobility::Movable);
		if (USceneComponent* TurtleRoot = Turtle->GetRootComponent())
		{
			Comp->SetupAttachment(TurtleRoot);
		}
		Comp->RegisterComponent();
		// En el mundo: los trocitos no siguen a la tortuga.
		Comp->SetAbsolute(true, true, true);
		Comp->SetWorldTransform(FTransform(Turtle->GetActorLocation()));
		Bits.SetNum(MaxBits);
		BitXf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), MaxBits);
		Comp->AddInstances(BitXf, false, false);
		BitsMesh = Comp;
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const double Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0;
	const FVector Center = Turtle->GetActorLocation();
	// De los hombros hacia fuera y arriba; caen hasta los pies.
	const FVector From = Center + FVector(0.0, 0.0, Half * 0.35);
	int32 Spawned = 0;
	for (FShellBit& Bit : Bits)
	{
		if (Spawned >= Count)
		{
			break;
		}
		if (Bit.bAlive)
		{
			continue;
		}
		const float Around = FMath::FRandRange(0.f, 2.f * UE_PI);
		const FVector Out(FMath::Cos(Around), FMath::Sin(Around), 0.0);
		Bit.Position = From + Out * FMath::FRandRange(10.f, 32.f) + FVector(0.0, 0.0, FMath::FRandRange(-15.f, 25.f));
		Bit.Velocity = Out * (Speed * FMath::FRandRange(0.6f, 1.15f)) + FVector(0.0, 0.0, FMath::FRandRange(160.f, 340.f));
		Bit.SpinAxis = FMath::VRand();
		Bit.SpinSpeed = FMath::FRandRange(400.f, 900.f);
		Bit.Angle = FMath::FRandRange(0.f, 360.f);
		Bit.Size = FMath::FRandRange(1.1f, 1.8f);
		Bit.Age = 0.f;
		Bit.Life = FMath::FRandRange(BitLifeMin, BitLifeMax);
		Bit.FloorZ = Center.Z - Half + 1.0;
		Bit.bGrounded = false;
		Bit.bAlive = true;
		++Spawned;
	}
}

bool UTN_EggHatchComponent::TickBits(float DeltaTime)
{
	using namespace TNEggHatchDetail;
	UInstancedStaticMeshComponent* Comp = BitsMesh;
	if (!Comp || Bits.Num() == 0 || BitXf.Num() != Bits.Num())
	{
		return false;
	}
	bool bAny = false;
	for (int32 i = 0; i < Bits.Num(); ++i)
	{
		FShellBit& Bit = Bits[i];
		if (Bit.bAlive)
		{
			Bit.Age += DeltaTime;
			if (Bit.Age >= Bit.Life)
			{
				Bit.bAlive = false;
			}
		}
		if (!Bit.bAlive)
		{
			BitXf[i].SetScale3D(FVector::ZeroVector);
			continue;
		}
		bAny = true;
		if (!Bit.bGrounded)
		{
			Bit.Velocity.Z += BitGravity * DeltaTime;
			Bit.Position += Bit.Velocity * DeltaTime;
			Bit.Angle += Bit.SpinSpeed * DeltaTime;
			if (Bit.Position.Z <= Bit.FloorZ)
			{
				// En el suelo se queda tumbado donde cae.
				Bit.Position.Z = Bit.FloorZ;
				Bit.bGrounded = true;
			}
		}
		const float Shrink = FMath::Clamp((Bit.Life - Bit.Age) / BitShrinkSeconds, 0.f, 1.f);
		const FQuat Spin = Bit.bGrounded ? FQuat(FVector::UpVector, FMath::DegreesToRadians(Bit.Angle))
			: FQuat(Bit.SpinAxis, FMath::DegreesToRadians(Bit.Angle));
		BitXf[i] = FTransform(Spin, Bit.Position, FVector(Bit.Size * Shrink));
	}
	// Cada fotograma, sin MarkRenderStateDirty: TransformChanged ya actualiza instancias y límites al final del fotograma sin rehacer el proxy (#566).
	Comp->BatchUpdateInstancesTransforms(0, BitXf, true, false, false);
	return bAny;
}
