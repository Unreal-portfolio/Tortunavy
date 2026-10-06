// Tabla de intensidad del cooperativo (#788): fichero versionado, reglas de rondas y tramos, módulos de diseño y filtro de
// enemigos. Correr desde Session Frontend (categoría "Tortunabo.ProcMap.CoopIntensity") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.CoopIntensity; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "World/ProcMap/TN_CoopIntensity.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace TNCoopIntensity;

	bool LoadVersionedTable(FAutomationTestBase& Test, FTable& Out)
	{
		FString Text;
		FString Error;
		if (!Test.TestTrue(TEXT("Existe Content/Data/Coop/IntensityTable.json"), FFileHelper::LoadFileToString(Text, *DefaultTablePath())))
		{
			return false;
		}
		const bool bOk = ParseTable(Text, Out, Error);
		Test.TestTrue(FString::Printf(TEXT("La tabla versionada se entiende (%s)"), *Error), bOk);
		return bOk;
	}

	/** Tabla mínima: una ronda de dos tramos, un módulo fácil con un enemigo que existe y otro que no. */
	const TCHAR* SmallTableJson()
	{
		return TEXT("{\"version\":1,\"tramos_por_ronda\":2,")
			TEXT("\"rondas\":[{\"ronda\":1,\"tramos\":[\"Facil\",\"PUZLE\"]}],")
			TEXT("\"modulos\":[")
			TEXT("{\"id\":1,\"dificultad\":\"Fácil\",\"intensidad\":40,\"enemigos\":[{\"nombre\":\"Algas\",\"cantidad\":2},{\"nombre\":\"Pulpo gigante\",\"cantidad\":1}]},")
			TEXT("{\"id\":2,\"dificultad\":\"Puzle\",\"intensidad\":0,\"enemigos\":[{\"nombre\":\"Algas\",\"cantidad\":3}]}")
			TEXT("]}");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopIntensityFileTest,
	"Tortunabo.ProcMap.CoopIntensity.FicheroVersionado",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopIntensityFileTest::RunTest(const FString& Parameters)
{
	FTable Table;
	if (!LoadVersionedTable(*this, Table))
	{
		return false;
	}
	TestEqual(TEXT("Cinco rondas"), Table.Rounds.Num(), 5);
	TestEqual(TEXT("Cinco tramos por ronda"), Table.NumTramos, 5);
	TestEqual(TEXT("Cuarenta módulos de diseño"), Table.Modules.Num(), 40);
	TestTrue(TEXT("Ronda 5, tramo 3 = Puzle"), DifficultyFor(Table, 5, 2) == EDifficulty::Puzzle);
	TestTrue(TEXT("Ronda 1, tramo 4 = Puzle"), DifficultyFor(Table, 1, 3) == EDifficulty::Puzzle);
	TestTrue(TEXT("Ronda 4, tramo 5 = Difícil (en el Excel, «Dificil» sin tilde)"), DifficultyFor(Table, 4, 4) == EDifficulty::Hard);
	for (const FModuleRow& Row : Table.Modules)
	{
		const EDifficulty Expected = Row.Id <= 10 ? EDifficulty::Easy : Row.Id <= 20 ? EDifficulty::Medium
			: Row.Id <= 30 ? EDifficulty::Hard : EDifficulty::Puzzle;
		TestTrue(FString::Printf(TEXT("Módulo %d con la dificultad de su decena"), Row.Id), Row.Difficulty == Expected);
		if (Row.Id >= 31)
		{
			TestEqual(FString::Printf(TEXT("El módulo de puzle %d no lleva enemigos"), Row.Id), Row.Enemies.Num(), 0);
		}
	}
	TestNotNull(TEXT("El juego carga la tabla versionada"), GetDefaultTable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopIntensityRulesTest,
	"Tortunabo.ProcMap.CoopIntensity.Reglas",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopIntensityRulesTest::RunTest(const FString& Parameters)
{
	FTable Table;
	if (!LoadVersionedTable(*this, Table))
	{
		return false;
	}

	// Tramos iguales del recorrido.
	TestEqual(TEXT("Primer paso, primer tramo"), TramoOfStep(0, 20, 5), 0);
	TestEqual(TEXT("Paso 4 de 20, primer tramo"), TramoOfStep(3, 20, 5), 0);
	TestEqual(TEXT("Paso 5 de 20, segundo tramo"), TramoOfStep(4, 20, 5), 1);
	TestEqual(TEXT("Último paso, último tramo"), TramoOfStep(19, 20, 5), 4);
	TestEqual(TEXT("Recorrido más corto que los tramos: el último paso va al último"), TramoOfStep(2, 3, 5), 3);
	TestEqual(TEXT("Paso fuera de rango: el último tramo"), TramoOfStep(99, 20, 5), 4);

	// Rondas fuera de la tabla.
	TestTrue(TEXT("Ronda 0 juega como la 1"), DifficultyFor(Table, 0, 3) == DifficultyFor(Table, 1, 3));
	TestTrue(TEXT("Ronda 9 juega como la 5"), DifficultyFor(Table, 9, 2) == EDifficulty::Puzzle);

	// Módulos de diseño de la dificultad del tramo, siempre los mismos con la misma semilla.
	for (uint32 Seed = 1; Seed < 40; ++Seed)
	{
		const FModuleRow* Hard = PickModule(Table, EDifficulty::Hard, Seed);
		if (TestNotNull(TEXT("Hay módulo difícil"), Hard))
		{
			TestTrue(TEXT("Módulo difícil entre el 21 y el 30"), Hard->Id >= 21 && Hard->Id <= 30);
			TestTrue(TEXT("Misma semilla, mismo módulo"), PickModule(Table, EDifficulty::Hard, Seed) == Hard);
		}
		const TArray<FTramoPlan> Plan = PlanRound(Table, 5, Seed * 977u);
		if (!TestEqual(TEXT("Un plan por tramo"), Plan.Num(), 5))
		{
			break;
		}
		TestTrue(TEXT("Ronda 5: el tramo 3 es de puzle"), Plan[2].Difficulty == EDifficulty::Puzzle);
		TestTrue(TEXT("Ronda 5: el tramo 3 usa un módulo del 31 al 40"), Plan[2].ModuleId >= 31 && Plan[2].ModuleId <= 40);
		TestEqual(TEXT("Ronda 5: el tramo 3 no lleva enemigos"), Plan[2].Enemies.Num(), 0);
		TestTrue(TEXT("Ronda 5: el tramo 1 usa un módulo medio"), Plan[0].ModuleId >= 11 && Plan[0].ModuleId <= 20);
		TestEqual(TEXT("Ronda 5: el tramo 1 lleva los cinco enemigos de su módulo"), Plan[0].Enemies.Num(), 5);
	}

	// Filtro de los peligros por bioma.
	TestFalse(TEXT("Puzle: sin enemigos"), AllowsHazard(EDifficulty::Puzzle, true, 0));
	TestTrue(TEXT("Puzle: lo que no es enemigo va"), AllowsHazard(EDifficulty::Puzzle, false, 2));
	TestTrue(TEXT("Fácil: enemigos de dificultad fácil"), AllowsHazard(EDifficulty::Easy, true, 0));
	TestFalse(TEXT("Fácil: sin los de normal"), AllowsHazard(EDifficulty::Easy, true, 1));
	TestTrue(TEXT("Medio: los de normal"), AllowsHazard(EDifficulty::Medium, true, 1));
	TestFalse(TEXT("Medio: sin los de difícil"), AllowsHazard(EDifficulty::Medium, true, 2));
	TestTrue(TEXT("Difícil: todos"), AllowsHazard(EDifficulty::Hard, true, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCoopIntensityEnemiesTest,
	"Tortunabo.ProcMap.CoopIntensity.EnemigosDesconocidos",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCoopIntensityEnemiesTest::RunTest(const FString& Parameters)
{
	FTable Table;
	FString Error;
	if (!TestTrue(TEXT("La tabla pequeña se entiende"), ParseTable(SmallTableJson(), Table, Error)))
	{
		AddInfo(Error);
		return false;
	}
	TestTrue(TEXT("«Algas» es un enemigo del juego"), ResolveEnemy(TEXT("Algas")) == EKnownEnemy::Seaweed);
	TestTrue(TEXT("«Pulpo gigante» no existe"), ResolveEnemy(TEXT("Pulpo gigante")) == EKnownEnemy::None);

	const TArray<FTramoPlan> Plan = PlanRound(Table, 1, 7u);
	const TArray<FString> Unknown = UnknownEnemies(Plan);
	TestEqual(TEXT("Un enemigo que no existe"), Unknown.Num(), 1);
	TestTrue(TEXT("Es el pulpo"), Unknown.Contains(TEXT("Pulpo gigante")));
	TestEqual(TEXT("El módulo de puzle no lleva enemigos aunque el fichero los nombre"), Plan.IsValidIndex(1) ? Plan[1].Enemies.Num() : -1, 0);

	// Ficheros mal escritos: no se aceptan a medias.
	FTable Bad;
	TestFalse(TEXT("Dificultad desconocida"), ParseTable(TEXT("{\"tramos_por_ronda\":1,\"rondas\":[{\"tramos\":[\"Imposible\"]}],\"modulos\":[{\"id\":1,\"dificultad\":\"Facil\"}]}"), Bad, Error));
	TestFalse(TEXT("Una ronda con tramos de menos"), ParseTable(TEXT("{\"tramos_por_ronda\":3,\"rondas\":[{\"tramos\":[\"Facil\"]}],\"modulos\":[{\"id\":1,\"dificultad\":\"Facil\"}]}"), Bad, Error));
	TestFalse(TEXT("Sin módulos"), ParseTable(TEXT("{\"tramos_por_ronda\":1,\"rondas\":[{\"tramos\":[\"Facil\"]}]}"), Bad, Error));
	TestFalse(TEXT("La tabla rechazada queda vacía"), Bad.IsValid());
	return true;
}

#endif
