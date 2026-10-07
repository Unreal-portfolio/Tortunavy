// Guardado de los puntos de final de partida (#873): el saldo de la tienda (UTN_CosmeticSaveGame::ShopPoints) sobrevive al
// guardado, la migración de perfiles v1 a v2 lo rellena con lo ya ganado, y en Results cada máquina suma a su perfil los
// puntos de su jugadora una sola vez aunque CoopScore y MatchFlowState lleguen en cualquier orden (TN_ScoreDecisions.h, como
// ATN_CoopGameState::PersistLocalPlayerScoreIfResults).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Score.Points; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Core/TN_ScoreDecisions.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Multiplayer/TN_SaveGameDecisions.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPointsSaveTest
{
	/** Una máquina en Results vista desde su jugadora local: saldo del perfil y lo ya guardado de esta partida. */
	struct FWallet
	{
		int32 ShopPoints = 0;
		int32 Earned = 0;
		int32 Persisted = 0;
		bool bResults = false;
		bool bScoreValid = false;
		int32 Total = 0;

		/** PersistLocalPlayerScoreIfResults → UMP_GameInstance::AddCoopScore. */
		void PersistIfResults()
		{
			if (!bResults || !bScoreValid) { return; }
			const int32 Delta = TNScoreLogic::ComputePersistDelta(Total, Persisted);
			if (Delta > 0)
			{
				ShopPoints += Delta;
				Earned += Delta;
				Persisted = Total;
			}
		}

		void SetFlow(bool bIsResults)
		{
			bResults = bIsResults;
			Persisted = TNScoreLogic::PersistedAfterFlowChange(Persisted, bIsResults);
			PersistIfResults();
		}

		void SetScore(int32 InTotal)
		{
			bScoreValid = true;
			Total = InTotal;
			PersistIfResults();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPointsSaveRoundTripTest,
	"Tortunabo.Score.Points.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPointsSaveRoundTripTest::RunTest(const FString& Parameters)
{
	UTN_CosmeticSaveGame* Profile = NewObject<UTN_CosmeticSaveGame>();
	Profile->ShopPoints = 420;
	Profile->AccumulatedCoopScore = 900;
	Profile->UnlockedSkinIds = { TEXT("Shell_Gold") };
	Profile->StampCurrentVersion();
	TestEqual(TEXT("versión 2"), Profile->SaveVersion, 2);

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("se guarda en memoria"), UGameplayStatics::SaveGameToMemory(Profile, Bytes))) { return false; }
	const UTN_CosmeticSaveGame* Loaded = Cast<UTN_CosmeticSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("se vuelve a leer"), Loaded)) { return false; }
	TestEqual(TEXT("saldo"), Loaded->ShopPoints, 420);
	TestEqual(TEXT("ganados en total"), Loaded->AccumulatedCoopScore, 900);
	TestTrue(TEXT("lo desbloqueado"), Loaded->UnlockedSkinIds.Contains(TEXT("Shell_Gold")));
	TestTrue(TEXT("entero (marca de fin)"), Loaded->IsIntact());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPointsSaveMigrationTest,
	"Tortunabo.Score.Points.Migration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPointsSaveMigrationTest::RunTest(const FString& Parameters)
{
	using namespace TNSaveLogic;
	TestTrue(TEXT("v1 → v2: migrar"), DecideMigration(1, COSMETIC_SAVE_VERSION) == EMigration::Upgrade);
	TestEqual(TEXT("v1: el saldo empieza con lo ya ganado"), MigratedShopPoints(1, 0, 900), 900);
	TestEqual(TEXT("v0: igual"), MigratedShopPoints(0, 0, 250), 250);
	TestEqual(TEXT("v2: se queda el saldo que había"), MigratedShopPoints(2, 300, 900), 300);
	TestEqual(TEXT("nunca negativo"), MigratedShopPoints(1, 0, -10), 0);
	TestEqual(TEXT("nunca negativo (v2)"), MigratedShopPoints(2, -5, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPointsSavePersistTest,
	"Tortunabo.Score.Points.PersistOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNPointsSavePersistTest::RunTest(const FString& Parameters)
{
	using TNPointsSaveTest::FWallet;

	// Anfitrión: los puntos se calculan y luego se anuncia Results.
	FWallet Host;
	Host.SetScore(283);
	TestEqual(TEXT("fuera de Results no se guarda"), Host.ShopPoints, 0);
	Host.SetFlow(true);
	TestEqual(TEXT("anfitrión: al saldo"), Host.ShopPoints, 283);
	Host.PersistIfResults();
	Host.SetScore(283);
	TestEqual(TEXT("repetir no duplica"), Host.ShopPoints, 283);

	// Cliente: Results llega antes que CoopScore.
	FWallet Client;
	Client.ShopPoints = 100;
	Client.SetFlow(true);
	TestEqual(TEXT("sin puntos aún: nada"), Client.ShopPoints, 100);
	Client.SetScore(150);
	TestEqual(TEXT("cliente: se suma al llegar"), Client.ShopPoints, 250);
	TestEqual(TEXT("cliente: ganados"), Client.Earned, 150);

	// Partida siguiente: sale de Results y se gana menos que la anterior; se guarda entera.
	Client.SetFlow(false);
	Client.bScoreValid = false;
	Client.SetScore(90);
	Client.SetFlow(true);
	TestEqual(TEXT("segunda partida entera"), Client.ShopPoints, 340);

	// Quien no llega también guarda lo suyo (muñecos, objetos, títulos); con 0 no cambia nada.
	FWallet Zero;
	Zero.SetFlow(true);
	Zero.SetScore(0);
	TestEqual(TEXT("0 puntos: el saldo no cambia"), Zero.ShopPoints, 0);
	return true;
}

#endif
