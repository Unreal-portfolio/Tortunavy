// Red de la tortuga de un cliente: lanzamientos que decide el servidor y estrena el dueño en su movimiento
// (FTNServerLaunch) y pasos que no se corrigen mientras el servidor la mueve en su bola (#18).

#include "Player/TN_TurtleMovementComponent.h"
#include "Core/TN_Log.h"
#include "Player/TN_MovementLimits.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace TNServerLaunchCVars
{
	static int32 GPredicted = 1;
	static FAutoConsoleVariableRef CVarPredicted(
		TEXT("TN.Net.ServerLaunch"),
		GPredicted,
		TEXT("1 = los lanzamientos que decide el servidor (empujón de la mina) los estrena el dueño en su movimiento, sin corrección; 0 = LaunchCharacter del servidor, como antes (para comparar con p.NetShowCorrections 1). En el servidor."),
		ECVF_Cheat);
}

// ─────────────────────────────────────────────────────────────────────────────
// Datos de los movimientos del cliente al servidor
// ─────────────────────────────────────────────────────────────────────────────

FTNTurtleNetworkMoveDataContainer::FTNTurtleNetworkMoveDataContainer()
{
	NewMoveData = &TurtleMoveData[0];
	PendingMoveData = &TurtleMoveData[1];
	OldMoveData = &TurtleMoveData[2];
}

const FTNTurtleNetworkMoveData* FTNTurtleNetworkMoveDataContainer::FindTurtleData(const FCharacterNetworkMoveData* Data) const
{
	for (const FTNTurtleNetworkMoveData& MoveData : TurtleMoveData)
	{
		if (Data == &MoveData)
		{
			return &MoveData;
		}
	}
	return nullptr;
}

uint8 FTNTurtleNetworkMoveDataContainer::GetLaunchId(const FCharacterNetworkMoveData* Data) const
{
	const FTNTurtleNetworkMoveData* MoveData = FindTurtleData(Data);
	return MoveData ? MoveData->LaunchId : 0;
}

uint16 FTNTurtleNetworkMoveDataContainer::GetDiveYaw(const FCharacterNetworkMoveData* Data) const
{
	const FTNTurtleNetworkMoveData* MoveData = FindTurtleData(Data);
	return MoveData ? MoveData->DiveYaw : 0;
}

uint8 FTNTurtleNetworkMoveDataContainer::GetPredictedCaps(const FCharacterNetworkMoveData* Data) const
{
	const FTNTurtleNetworkMoveData* MoveData = FindTurtleData(Data);
	return MoveData ? MoveData->PredictedCaps : 0;
}

void FTNTurtleNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
	const ACharacter* Owner = ClientMove.CharacterOwner;
	const UTN_TurtleMovementComponent* TurtleMove = Owner ? Cast<UTN_TurtleMovementComponent>(Owner->GetCharacterMovement()) : nullptr;
	LaunchId = TurtleMove ? TurtleMove->GetServerLaunch().GetIdForMove(ClientMove.TimeStamp) : 0;
	// El panzazo que pide este movimiento (su marca ya va en CompressedMoveFlags): la dirección (#24).
	DiveYaw = (CompressedMoveFlags & TNDiveLogic::DiveRequestFlag) != 0 ? UTN_TurtleMovementComponent::GetSavedMoveDiveYaw(ClientMove) : 0;
	// Los topes predichos con que el dueño ha hecho este movimiento (#575, #574).
	PredictedCaps = UTN_TurtleMovementComponent::GetSavedMovePredictedCaps(ClientMove) & TNMovementLimits::PredictedCapAllBits;
}

bool FTNTurtleNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bBaseOk = FCharacterNetworkMoveData::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	uint8 bHasLaunch = LaunchId != 0 ? 1 : 0;
	Ar.SerializeBits(&bHasLaunch, 1);
	if (bHasLaunch != 0)
	{
		Ar << LaunchId;
	}
	else
	{
		LaunchId = 0;
	}
	// Con la marca del panzazo (ya leída con lo de serie), su giro: 16 bits; sin ella, nada.
	TNDiveLogic::SerializeDiveRequest(Ar, CompressedMoveFlags, DiveYaw);
	// Los topes predichos: un bit cada uno.
	uint8 Caps = PredictedCaps & TNMovementLimits::PredictedCapAllBits;
	Ar.SerializeBits(&Caps, TNMovementLimits::NumPredictedCaps);
	PredictedCaps = Caps & TNMovementLimits::PredictedCapAllBits;
	return bBaseOk && !Ar.IsError();
}

// ─────────────────────────────────────────────────────────────────────────────
// Respuesta del servidor: en las correcciones, su panzazo (#24)
// ─────────────────────────────────────────────────────────────────────────────

void FTNTurtleMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	DiveState = FTNDiveNetState();
	if (PendingAdjustment.bAckGoodMove)
	{
		return;
	}
	const UTN_TurtleMovementComponent* TurtleMove = Cast<const UTN_TurtleMovementComponent>(&CharacterMovement);
	if (TurtleMove && TurtleMove->GetCorrectionDiveState().TimeStamp == PendingAdjustment.TimeStamp)
	{
		DiveState = TurtleMove->GetCorrectionDiveState();
		return;
	}
	// Por si acaso (no debería): el de ahora.
	const ATortugaCharacter* Turtle = Cast<const ATortugaCharacter>(CharacterMovement.GetCharacterOwner());
	const UCapsuleComponent* Capsule = Turtle ? Turtle->GetCapsuleComponent() : nullptr;
	DiveState.bDiving = Turtle && Turtle->IsDiving();
	DiveState.Serial = Turtle ? Turtle->GetDiveSerial() : 0;
	DiveState.CapsuleHalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
	DiveState.SwimHopCooldown = TurtleMove ? TurtleMove->GetSwimHopCooldown() : 0.f;
	DiveState.TimeStamp = PendingAdjustment.TimeStamp;
}

bool FTNTurtleMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	const bool bBaseOk = FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap);
	if (IsCorrection())
	{
		uint8 bDiving = DiveState.bDiving ? 1 : 0;
		Ar.SerializeBits(&bDiving, 1);
		DiveState.bDiving = bDiving != 0;
		Ar << DiveState.Serial;
		Ar << DiveState.CapsuleHalfHeight;
		Ar << DiveState.SwimHopCooldown;
		DiveState.SwimHopCooldown = FMath::Max(0.f, DiveState.SwimHopCooldown);
		DiveState.TimeStamp = ClientAdjustment.TimeStamp;
	}
	return bBaseOk && !Ar.IsError();
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: conceder
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::LaunchFromServer(ACharacter* Character, const FVector& LaunchVelocity)
{
	if (!Character || !Character->HasAuthority())
	{
		return;
	}
	UTN_TurtleMovementComponent* TurtleMove = Cast<UTN_TurtleMovementComponent>(Character->GetCharacterMovement());
	const UWorld* World = Character->GetWorld();
	// Solo la tortuga que mueve un cliente remoto (el que predice su movimiento) espera a su dueño; el resto, ya.
	const bool bRemoteOwner = TNServerLaunchCVars::GPredicted != 0 && TurtleMove && World && TurtleMove->GetIsReplicated()
		&& !Character->IsLocallyControlled() && Character->GetRemoteRole() == ROLE_AutonomousProxy;
	if (!bRemoteOwner)
	{
		Character->LaunchCharacter(LaunchVelocity, true, true);
		return;
	}
	FVector SentVelocity = FVector::ZeroVector;
	const uint8 Id = TurtleMove->ServerLaunch.Grant(LaunchVelocity, World->GetTimeSeconds(), SentVelocity);
	TurtleMove->ClientReceiveServerLaunch(Id, SentVelocity);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Lanzamiento] %s: concedido %d (%s) a su dueño."), *Character->GetName(), static_cast<int32>(Id), *SentVelocity.ToString());
}

void UTN_TurtleMovementComponent::ClientReceiveServerLaunch_Implementation(uint8 Id, FVector_NetQuantize LaunchVelocity)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ServerLaunch.Receive(Id, LaunchVelocity, World->GetTimeSeconds());
}

// ─────────────────────────────────────────────────────────────────────────────
// En el movimiento: el dueño lo estrena, el servidor lo aplica en el mismo
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleMovementComponent::ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration)
{
	// Antes de guardar el movimiento: con el lanzamiento pendiente el motor no lo junta con otro (bForceNoCombine).
	FVector LaunchVelocity = FVector::ZeroVector;
	const UWorld* World = GetWorld();
	const bool bLaunching = World && MovementMode != MOVE_None && ServerLaunch.BeginMove(World->GetTimeSeconds(), LaunchVelocity);
	if (bLaunching)
	{
		Launch(LaunchVelocity);
	}
	Super::ReplicateMoveToServer(DeltaTime, NewAcceleration);
	if (bLaunching && ServerLaunch.IsStarting())
	{
		// El motor no ha llegado a hacer el movimiento: al siguiente, sin dejarlo pendiente para una repetición.
		ServerLaunch.RetryMove();
		PendingLaunchVelocity = FVector::ZeroVector;
	}
}

bool UTN_TurtleMovementComponent::HandlePendingLaunch()
{
	if (ServerLaunch.IsStarting() && CharacterOwner && !CharacterOwner->bClientUpdating && GetOwnerRole() == ROLE_AutonomousProxy)
	{
		if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
		{
			ServerLaunch.ConfirmMove(ClientData->CurrentTimeStamp, PendingLaunchVelocity);
		}
	}
	return Super::HandlePendingLaunch();
}

void UTN_TurtleMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	FVector LaunchVelocity = FVector::ZeroVector;
	if (FindServerLaunchForMove(ClientTimeStamp, LaunchVelocity))
	{
		Launch(LaunchVelocity);
	}
	// Topes predichos (#575, #574). Servidor, movimiento validado de un cliente: los que pide, si caen en la ventana del
	// último cambio, medida con el tiempo de sus movimientos (este DeltaTime), no con la hora de llegada. Al repetir en el
	// dueño, ya los puso PrepMoveFor.
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority && !CharacterOwner->IsLocallyControlled())
	{
		const ATortugaCharacter* Turtle = GetTurtle();
		UTN_StaminaComponent* Stamina = Turtle ? Turtle->GetStaminaComponent() : nullptr;
		const uint8 Claimed = TurtleNetworkMoveData.GetPredictedCaps(GetCurrentNetworkMoveData());
		MovePredictedCaps = Stamina ? Stamina->ConsumeClientPredictedCaps(Claimed, DeltaTime) : 0;
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

bool UTN_TurtleMovementComponent::FindServerLaunchForMove(float ClientTimeStamp, FVector& OutVelocity)
{
	if (!CharacterOwner)
	{
		return false;
	}
	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		return CharacterOwner->bClientUpdating && ServerLaunch.GetForMove(ClientTimeStamp, OutVelocity);
	}
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}
	const uint8 Id = TurtleNetworkMoveData.GetLaunchId(GetCurrentNetworkMoveData());
	if (ServerLaunch.Take(Id, OutVelocity))
	{
		return true;
	}
	const UWorld* World = GetWorld();
	if (World && ServerLaunch.TakeExpired(World->GetTimeSeconds(), OutVelocity))
	{
		UE_LOG(LogTortunabo, Verbose, TEXT("[Lanzamiento] %s: su dueño no lo ha estrenado a tiempo; lo aplica el servidor."), *CharacterOwner->GetName());
		return true;
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor: en su bola no se corrige
// ─────────────────────────────────────────────────────────────────────────────

bool UTN_TurtleMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& ClientWorldLocation, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName,
	uint8 ClientMovementMode)
{
	// La mueve su caja del caparazón (bola de un aturdimiento, caparazón, en brazos): la cápsula del servidor no simula los
	// pasos del dueño. Los que aún manda andando, hasta que le llega la bola, se corregían todos (de 55 a 175 cm al pisar una
	// mina) contra la caja; ahora no: la bola replicada lo coloca en ella y, mientras dura, el dueño no manda pasos.
	const ATortugaCharacter* Turtle = GetTurtle();
	const UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
	if (Shell && Shell->HasLocalBody() && MovementMode == MOVE_None)
	{
		return false;
	}
	return Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBase,
		ClientBaseBoneName, ClientMovementMode);
}
