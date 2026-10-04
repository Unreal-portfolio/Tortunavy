// Parrilla y reaparición de la carrera del Rally (#103, #289): carril libre, fantasma frente al bloqueo y altura de aparición.
// Las reglas puras están en TNRallyRace (Rally/TN_RallyTrack.h); RespawnTrackPath recorre el camino real de
// ATN_RallyGameMode::RespawnTeam (ATN_RallyTrack::FindFreeRespawnTransform) con una pista construida en un mundo de prueba.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Race; Quit" -nullrhi -unattended

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyGameMode.h"
#include "Rally/TN_RallyTrack.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRaceRespawnLaneTest, "Tortunabo.Rally.Race.RespawnLanes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRaceRespawnLaneTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRace;
	TestEqual(TEXT("Carril 0 en el eje"), RespawnLaneLateralCm(0, 350.0), 0.0);
	TestEqual(TEXT("Carril 1 a la izquierda"), RespawnLaneLateralCm(1, 350.0), -350.0);
	TestEqual(TEXT("Carril 2 a la derecha"), RespawnLaneLateralCm(2, 350.0), 350.0);
	TestEqual(TEXT("El carril da la vuelta (3 = 0)"), RespawnLaneLateralCm(3, 350.0), 0.0);

	const TArray<FVector> Lanes = { FVector(0.0, 0.0, 0.0), FVector(0.0, -350.0, 0.0), FVector(0.0, 350.0, 0.0) };
	TestEqual(TEXT("Sin nadie cerca, el centro"), PickFreeRespawnLane(Lanes, {}, 300.0), 0);
	TestEqual(TEXT("Buggy parado en el centro: izquierda"), PickFreeRespawnLane(Lanes, { FVector(50.0, 0.0, 0.0) }, 300.0), 1);
	TestEqual(TEXT("Centro e izquierda ocupados: derecha"),
		PickFreeRespawnLane(Lanes, { FVector(0.0, 0.0, 0.0), FVector(0.0, -350.0, 0.0) }, 300.0), 2);
	TestEqual(TEXT("Un buggy lejos no ocupa el carril"), PickFreeRespawnLane(Lanes, { FVector(5000.0, 0.0, 0.0) }, 300.0), 0);
	TestEqual(TEXT("Justo en el radio cuenta como libre"), PickFreeRespawnLane(Lanes, { FVector(300.0, 0.0, 0.0) }, 300.0), 0);

	// Todos ocupados: el de más holgura, nunca el carril de la puerta por defecto (el fallo de #103).
	const TArray<FVector> Crowd = { FVector(0.0, 0.0, 0.0), FVector(0.0, -350.0, 0.0), FVector(0.0, 450.0, 0.0) };
	TestEqual(TEXT("Todo ocupado: el carril con el buggy más lejano"), PickFreeRespawnLane(Lanes, Crowd, 300.0), 2);
	TestEqual(TEXT("Sin carriles no hay carril"), PickFreeRespawnLane({}, Crowd, 300.0), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRaceResolveRespawnTest, "Tortunabo.Rally.Race.ResolveRespawn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRaceResolveRespawnTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRace;
	FRespawnSignals Destroyed;
	Destroyed.bDestroyed = true;
	FRespawnSignals Request;
	Request.bRequested = true;
	FRespawnSignals Fell;
	Fell.bFellOutOfWorld = true;

	// Reventar reaparece siempre (#296): antes iba por el camino de la petición y se perdía fuera de carrera o con inmunidad.
	TestEqual(TEXT("Reventado en carrera"), static_cast<int32>(ResolveRespawn(Destroyed, true, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::Destroyed));
	TestEqual(TEXT("Reventado antes del verde"), static_cast<int32>(ResolveRespawn(Destroyed, false, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::Destroyed));
	TestEqual(TEXT("Reventado con inmunidad"), static_cast<int32>(ResolveRespawn(Destroyed, true, false, false, true)), static_cast<int32>(ETNRallyRespawnReason::Destroyed));
	TestEqual(TEXT("Reventado tras llegar"), static_cast<int32>(ResolveRespawn(Destroyed, true, true, false, false)), static_cast<int32>(ETNRallyRespawnReason::Destroyed));
	TestEqual(TEXT("Un buggy retirado no reaparece al reventar"), static_cast<int32>(ResolveRespawn(Destroyed, true, false, true, false)), static_cast<int32>(ETNRallyRespawnReason::None));

	TestEqual(TEXT("Petición en carrera"), static_cast<int32>(ResolveRespawn(Request, true, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::Request));
	TestEqual(TEXT("Petición antes del verde: nada"), static_cast<int32>(ResolveRespawn(Request, false, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::None));
	TestEqual(TEXT("Petición con inmunidad: nada"), static_cast<int32>(ResolveRespawn(Request, true, false, false, true)), static_cast<int32>(ETNRallyRespawnReason::None));
	TestEqual(TEXT("Petición tras llegar: nada"), static_cast<int32>(ResolveRespawn(Request, true, true, false, false)), static_cast<int32>(ETNRallyRespawnReason::None));
	TestEqual(TEXT("Petición de un retirado: nada"), static_cast<int32>(ResolveRespawn(Request, true, false, true, false)), static_cast<int32>(ETNRallyRespawnReason::None));

	TestEqual(TEXT("Bajo el KillZ, en cualquier fase"), static_cast<int32>(ResolveRespawn(Fell, false, true, true, true)), static_cast<int32>(ETNRallyRespawnReason::Hazard));
	FRespawnSignals All;
	All.bFellOutOfWorld = All.bDestroyed = All.bRequested = true;
	TestEqual(TEXT("El KillZ manda sobre el reventón y la petición"), static_cast<int32>(ResolveRespawn(All, true, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::Hazard));
	TestEqual(TEXT("Sin señales, nada"), static_cast<int32>(ResolveRespawn(FRespawnSignals(), true, false, false, false)), static_cast<int32>(ETNRallyRespawnReason::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRaceGhostTest, "Tortunabo.Rally.Race.GhostCoversLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRaceGhostTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRace;
	// El fallo de #103: 2 s de fantasma con 3 s de bloqueo.
	TestTrue(TEXT("Un fantasma corto se alarga hasta el bloqueo + 0,5 s"), EffectiveGhostSeconds(3.f, 2.f) >= 3.5f);
	TestEqual(TEXT("Un fantasma largo se respeta"), EffectiveGhostSeconds(3.f, 5.f), 5.f);
	TestEqual(TEXT("Sin bloqueo, el margen"), EffectiveGhostSeconds(0.f, 0.f), GhostAfterLockSeconds);

	const ATN_RallyGameMode* Defaults = GetDefault<ATN_RallyGameMode>();
	TestTrue(TEXT("Los valores por defecto ya cumplen fantasma >= bloqueo + 0,5 s"),
		Defaults->RespawnGhostSeconds >= Defaults->RespawnLockSeconds + GhostAfterLockSeconds);
	const float Ghost = EffectiveGhostSeconds(Defaults->RespawnLockSeconds, Defaults->RespawnGhostSeconds);
	TestTrue(TEXT("El fantasma efectivo de RespawnTeam cubre el bloqueo"), Ghost >= Defaults->RespawnLockSeconds + GhostAfterLockSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRaceSpawnHeightTest, "Tortunabo.Rally.Race.SpawnHeight",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRaceSpawnHeightTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyRace;
	const double Fallback = FallbackOriginAboveBottomCm + SpawnClearanceCm;
	TestEqual(TEXT("Origen a 51 cm de la base de las ruedas: apoyado + 5 cm"), RestingLiftCm(51.0), 56.0);
	TestEqual(TEXT("Origen en la base: solo la holgura"), RestingLiftCm(0.0), SpawnClearanceCm);
	TestEqual(TEXT("Medida negativa: la de reserva"), RestingLiftCm(-10.0), Fallback);
	TestEqual(TEXT("Medida absurda: la de reserva"), RestingLiftCm(MaxOriginAboveBottomCm + 1.0), Fallback);
	TestEqual(TEXT("Sin medida (NaN): la de reserva"), RestingLiftCm(std::numeric_limits<double>::quiet_NaN()), Fallback);
	TestEqual(TEXT("Holgura negativa no hunde el buggy"), RestingLiftCm(51.0, -20.0), 51.0);
	TestTrue(TEXT("La aparición apoyada queda por debajo de los 80 cm de antes"), RestingLiftCm(51.0) < 80.0);
	return true;
}

namespace TNRallyRaceTestHelpers
{
	/** Mundo de juego vacío (sin suelo: las trazas fallan y la pista se queda en la cota del eje). */
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyRaceTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	TArray<TNRally::FGateDef> StraightGates()
	{
		TArray<TNRally::FGateDef> Gates;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TNRally::FGateDef& Gate = Gates.AddDefaulted_GetRef();
			Gate.Location = FVector(5000.0 * Index, 0.0, 0.0);
			Gate.YawDeg = 0.0;
		}
		return Gates;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyRaceRespawnTrackTest, "Tortunabo.Rally.Race.RespawnTrackPath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyRaceRespawnTrackTest::RunTest(const FString& Parameters)
{
	TNRallyRaceTestHelpers::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	ATN_RallyTrack* Track = Scoped.World->SpawnActor<ATN_RallyTrack>();
	if (!TestNotNull(TEXT("Pista"), Track) || !TestTrue(TEXT("La pista recta se construye"),
		Track->BuildFromGates(TNRallyRaceTestHelpers::StraightGates(), false)))
	{
		return false;
	}
	const double Lift = TNRallyRace::RestingLiftCm(51.0);
	const FVector Center = Track->GetRespawnTransform(1, 0, Lift).GetLocation();
	const FVector Left = Track->GetRespawnTransform(1, 1, Lift).GetLocation();
	const FVector Right = Track->GetRespawnTransform(1, 2, Lift).GetLocation();
	TestEqual(TEXT("Los carriles laterales están a 3,5 m del centro"), FVector::Dist(Center, Left), TNRallyRace::RespawnLaneSpacingCm, 1.0);
	TestEqual(TEXT("Sin suelo, el punto queda a la cota del eje + la altura apoyada"), Center.Z, Lift, 1.0);

	int32 Lane = INDEX_NONE;
	FVector Picked = Track->FindFreeRespawnTransform(1, {}, Lift, &Lane).GetLocation();
	TestEqual(TEXT("Puerta libre: carril central"), Lane, 0);
	TestTrue(TEXT("Puerta libre: el punto del centro"), Picked.Equals(Center, 1.0));

	// Un buggy parado tras la puerta (el caso de #103): el siguiente no reaparece encima.
	const FVector Stopped = Center + FVector(80.0, 0.0, 0.0);
	Picked = Track->FindFreeRespawnTransform(1, { Stopped }, Lift, &Lane).GetLocation();
	TestEqual(TEXT("Con un buggy en el centro, carril izquierdo"), Lane, 1);
	TestTrue(TEXT("Lejos del buggy parado"), FVector::Dist(Picked, Stopped) >= TNRallyRace::RespawnClearRadiusCm);

	Picked = Track->FindFreeRespawnTransform(1, { Stopped, Left }, Lift, &Lane).GetLocation();
	TestEqual(TEXT("Centro e izquierda ocupados: carril derecho"), Lane, 2);
	TestTrue(TEXT("El derecho es el punto del carril 2"), Picked.Equals(Right, 1.0));

	// Parrilla: el hueco apoyado sale de la misma traza con la altura medida (sin los 80 cm de antes).
	TestEqual(TEXT("Hueco de parrilla apoyado"), Track->GetGridSlotTransform(0, Lift).GetLocation().Z, Lift, 1.0);
	Track->ClearTrack();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
