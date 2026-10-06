// Montículo de la estación «Rebuscar» del tutorial (#791): siempre da la bola, aunque el rebuscable del mapa sortee también
// los objetos del coop. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tutorial.MoundGivesBall; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "Lobby/TN_TutorialPractice.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTutorialMoundTest
{
	/** Abre PickLoot (protegido) para el test, sin crear el actor: se llama sobre el objeto por defecto. */
	struct FAccess : ATN_TutorialSearchSpot
	{
		using ATN_TutorialSearchSpot::PickLoot;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTutorialMoundGivesBallTest,
	"Tortunabo.Tutorial.MoundGivesBall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTutorialMoundGivesBallTest::RunTest(const FString& Parameters)
{
	// El catálogo cargado antes, como en BeginPlay: así el objeto por defecto lo encuentra sin carga síncrona.
	const UDataTable* Items = LoadObject<UDataTable>(nullptr, TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items"));
	if (!TestNotNull(TEXT("DT_Items cargado"), Items))
	{
		return false;
	}
	const ATN_TutorialSearchSpot* Mound = GetDefault<ATN_TutorialSearchSpot>();
	static const FName BallId(TEXT("ThrowableBall"));
	int32 Balls = 0;
	constexpr int32 Tries = 64;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		FTN_InventoryItem Item;
		if ((Mound->*(&TNTutorialMoundTest::FAccess::PickLoot))(Item, nullptr) && Item.ItemId == BallId)
		{
			++Balls;
		}
	}
	TestEqual(TEXT("el montículo da la bola en cada búsqueda"), Balls, Tries);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
