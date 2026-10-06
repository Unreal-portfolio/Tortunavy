// Brinco desde el agua predicho (#573), reconciliación con el servidor: la corrección del servidor lleva su espera del
// brinco tras el movimiento corregido (FTNTurtleMoveResponseDataContainer) y, al repetir los movimientos siguientes, el
// dueño la recalcula desde ella en vez de restaurar la que predijo (FTNSavedMove_Turtle::PrepMoveFor). Hallazgo de la
// revisión de la PR #803: la repetición restauraba la espera antigua y el servidor no la mandaba, así que una espera
// equivocada en el dueño sobrevivía a las correcciones.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Movement.SwimHop; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_SwimHopRules.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSwimHopNetTest
{
	/** Mundo de juego con BeginPlay ya hecho. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNSwimHopNetTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSwimHopCorrectionDataTest,
	"Tortunabo.Movement.SwimHop.CorrectionData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSwimHopCorrectionDataTest::RunTest(const FString& Parameters)
{
	UTN_TurtleMovementComponent& Movement = *GetMutableDefault<UTN_TurtleMovementComponent>();

	FTNTurtleMoveResponseDataContainer Sent;
	Sent.ClientAdjustment.bAckGoodMove = false;
	Sent.ClientAdjustment.TimeStamp = 12.5f;
	Sent.ClientAdjustment.NewLoc = FVector(100.0, 200.0, 50.0);
	Sent.ClientAdjustment.MovementMode = MOVE_Swimming;
	Sent.DiveState.Serial = 3;
	Sent.DiveState.CapsuleHalfHeight = 44.f;
	Sent.DiveState.SwimHopCooldown = 0.37f;

	FBitWriter Writer(0, true);
	TestTrue(TEXT("La corrección se escribe"), Sent.Serialize(Movement, Writer, nullptr));

	FTNTurtleMoveResponseDataContainer Received;
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	TestTrue(TEXT("La corrección se lee"), Received.Serialize(Movement, Reader, nullptr));
	TestTrue(TEXT("Es una corrección"), Received.IsCorrection());
	TestEqual(TEXT("Lleva la espera del brinco del servidor"), Received.DiveState.SwimHopCooldown, 0.37f);
	TestEqual(TEXT("Y el panzazo, como antes"), Received.DiveState.Serial, static_cast<uint8>(3));
	TestEqual(TEXT("Con la marca de tiempo del movimiento corregido"), Received.DiveState.TimeStamp, 12.5f);

	// Un movimiento bueno no la lleva (solo las correcciones pagan esos bytes).
	FTNTurtleMoveResponseDataContainer Ack;
	Ack.ClientAdjustment.bAckGoodMove = true;
	Ack.ClientAdjustment.TimeStamp = 13.f;
	Ack.DiveState.SwimHopCooldown = 0.5f;
	FBitWriter AckWriter(0, true);
	Ack.Serialize(Movement, AckWriter, nullptr);
	FBitWriter CorrectionWriter(0, true);
	FTNTurtleMoveResponseDataContainer Correction;
	Correction.ClientAdjustment.bAckGoodMove = false;
	Correction.ClientAdjustment.TimeStamp = 13.f;
	Correction.DiveState.SwimHopCooldown = 0.5f;
	Correction.Serialize(Movement, CorrectionWriter, nullptr);
	TestTrue(TEXT("El acuse de un movimiento bueno es más corto"), AckWriter.GetNumBits() < CorrectionWriter.GetNumBits());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSwimHopReplayTest,
	"Tortunabo.Movement.SwimHop.Replay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSwimHopReplayTest::RunTest(const FString& Parameters)
{
	TNSwimHopNetTest::FPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), FVector(0.0, 0.0, 200.0),
		FRotator::ZeroRotator, Params);
	UTN_TurtleMovementComponent* Movement = Turtle ? Cast<UTN_TurtleMovementComponent>(Turtle->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("Tortuga con su movimiento"), Movement))
	{
		return false;
	}
	FNetworkPredictionData_Client_Character* ClientData = static_cast<FNetworkPredictionData_Client_Character*>(Movement->GetPredictionData_Client());
	if (!TestNotNull(TEXT("Datos de predicción del dueño"), ClientData))
	{
		return false;
	}

	// El dueño predijo un brinco que el servidor no hizo: guarda el movimiento siguiente con la espera de 0,6 s.
	Movement->RestoreSwimHopCooldown(TNSwimHop::CooldownSeconds);
	const FSavedMovePtr Predicted = ClientData->CreateSavedMove();
	Predicted->SetMoveFor(Turtle, 1.f / 60.f, FVector::ZeroVector, *ClientData);

	// Llega la corrección: tras el movimiento corregido, el servidor no tiene espera (ClientHandleMoveResponse la pone).
	Movement->RestoreSwimHopCooldown(0.f);

	// Repetición del movimiento guardado: sigue la espera de la corrección, no la que se predijo.
	Turtle->bClientUpdating = true;
	Predicted->PrepMoveFor(Turtle);
	Turtle->bClientUpdating = false;
	TestEqual(TEXT("Al repetir, la espera es la del servidor"), Movement->GetSwimHopCooldown(), 0.f);
	TestTrue(TEXT("Y con ella el brinco del movimiento repetido sale, como en el servidor"),
		TNSwimHop::Step(Movement->GetSwimHopCooldown(), true, true, 1.f / 60.f).bHop);

	// El movimiento repetido se queda con la espera recalculada: si después se combina con otro, empieza desde ella.
	Movement->RestoreSwimHopCooldown(0.25f);
	const FSavedMovePtr Next = ClientData->CreateSavedMove();
	Next->SetMoveFor(Turtle, 1.f / 60.f, FVector::ZeroVector, *ClientData);
	Next->CombineWith(Predicted.Get(), Turtle, nullptr, Turtle->GetActorLocation());
	TestEqual(TEXT("Al combinar, la espera del principio es la recalculada"), Movement->GetSwimHopCooldown(), 0.f);
	return true;
}

#endif
