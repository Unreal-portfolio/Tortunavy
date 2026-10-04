// Aspecto del buggy del Rally (#114, #115): catálogo de modelos y pinturas, validación del look en el servidor y guardado
// en el perfil cosmético. Sin mundo ni assets:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Buggy; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Multiplayer/TN_CosmeticSaveGame.h"
#include "Player/TN_CosmeticsSync.h"
#include "Vehicles/TN_BuggyCosmetics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBuggyLookTest
{
	bool InUnit(const FLinearColor& C)
	{
		return C.R >= 0.f && C.R <= 1.f && C.G >= 0.f && C.G <= 1.f && C.B >= 0.f && C.B <= 1.f && FMath::IsNearlyEqual(C.A, 1.f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBuggyLookCatalogTest,
	"Tortunabo.Rally.Buggy.Look.Catalog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBuggyLookCatalogTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyCosmetics;
	const TArray<FTNBuggyModelInfo>& ModelList = Models();
	const TArray<FTNBuggyPaintInfo>& PaintList = Paints();
	TestTrue(TEXT("El de serie y las tres tortugas"), ModelList.Num() >= 4);
	TestTrue(TEXT("Un buen montón de pinturas"), PaintList.Num() >= 15);
	TestEqual(TEXT("El primer modelo es el de serie"), ModelList[0].Id, FName(NAME_None));
	TestEqual(TEXT("La primera pintura es la de serie"), PaintList[0].Id, FName(NAME_None));
	TestEqual(TEXT("El de serie es gratis"), ModelList[0].Price, 0);
	TestTrue(TEXT("El de serie es el buggy de Art/Source"), ModelList[0].Style == ETNBuggyBodyStyle::Stock);
	TestTrue(TEXT("Resolver NAME_None da el de Art/Source"), ResolveModel(NAME_None).Style == ETNBuggyBodyStyle::Stock);
	TestEqual(TEXT("La de serie es gratis"), PaintList[0].Price, 0);

	TSet<FName> Ids;
	TSet<FString> Names;
	TSet<ETNBuggyBodyStyle> StylesSeen;
	for (int32 i = 0; i < ModelList.Num(); ++i)
	{
		const FTNBuggyModelInfo& M = ModelList[i];
		TestFalse(FString::Printf(TEXT("Modelo %d: nombre"), i), M.Name.IsEmpty());
		TestFalse(FString::Printf(TEXT("Modelo %d: frase del tendero"), i), M.Description.IsEmpty());
		TestFalse(FString::Printf(TEXT("Modelo %d: nombre repetido"), i), Names.Contains(M.Name.ToString()));
		Names.Add(M.Name.ToString());
		TestFalse(FString::Printf(TEXT("Modelo %d: estilo repetido"), i), StylesSeen.Contains(M.Style));
		StylesSeen.Add(M.Style);
		if (i > 0)
		{
			TestTrue(FString::Printf(TEXT("%s: prefijo BuggyModel_"), *M.Id.ToString()), M.Id.ToString().StartsWith(ModelPrefix()));
			TestTrue(FString::Printf(TEXT("%s: cuesta conchas"), *M.Id.ToString()), M.Price > 0);
			TestFalse(FString::Printf(TEXT("%s: Id repetido"), *M.Id.ToString()), Ids.Contains(M.Id));
			Ids.Add(M.Id);
		}
	}
	for (int32 i = 0; i < PaintList.Num(); ++i)
	{
		const FTNBuggyPaintInfo& P = PaintList[i];
		const FString Who = P.Id.IsNone() ? FString(TEXT("Pintura de serie")) : P.Id.ToString();
		TestFalse(Who + TEXT(": nombre"), P.Name.IsEmpty());
		TestFalse(Who + TEXT(": frase del tendero"), P.Description.IsEmpty());
		TestFalse(Who + TEXT(": nombre repetido"), Names.Contains(P.Name.ToString()));
		Names.Add(P.Name.ToString());
		TestTrue(Who + TEXT(": colores lineales en [0, 1]"), TNBuggyLookTest::InUnit(P.Base) && TNBuggyLookTest::InUnit(P.Plates)
			&& TNBuggyLookTest::InUnit(P.Accent) && TNBuggyLookTest::InUnit(P.PatternColor));
		TestTrue(Who + TEXT(": dibujo válido"), static_cast<uint8>(P.Pattern) < static_cast<uint8>(ETNBuggyPattern::Count));
		TestTrue(Who + TEXT(": tamaño del dibujo"), P.PatternScale >= 0.2f && P.PatternScale <= 4.f);
		TestTrue(Who + TEXT(": brillo"), P.Shine >= 0.f && P.Shine <= 1.f);
		TestTrue(Who + TEXT(": luz propia"), P.Glow >= 0.f && P.Glow <= 20.f);
		if (i > 0)
		{
			TestTrue(Who + TEXT(": prefijo BuggyPaint_"), P.Id.ToString().StartsWith(PaintPrefix()));
			TestTrue(Who + TEXT(": cuesta conchas"), P.Price > 0);
			TestFalse(Who + TEXT(": Id repetido (también con los modelos)"), Ids.Contains(P.Id));
			Ids.Add(P.Id);
		}
	}
	TestEqual(TEXT("Catálogo de modelos de la tienda (sin el de serie)"), CatalogIds(ETNCosmeticCategory::BuggyModel).Num(), ModelList.Num() - 1);
	TestEqual(TEXT("Catálogo de pinturas de la tienda (sin la de serie)"), CatalogIds(ETNCosmeticCategory::BuggyPaint).Num(), PaintList.Num() - 1);
	TestEqual(TEXT("Las categorías de la tortuga no tienen buggies"), CatalogIds(ETNCosmeticCategory::Helmet).Num(), 0);
	// Los ocho primeros dibujos comparten el HLSL del caparazón de la tortuga (ETNShellPattern).
	TestEqual(TEXT("Sandía en el mismo índice que el caparazón"), static_cast<int32>(ETNBuggyPattern::Melon), static_cast<int32>(ETNShellPattern::Melon));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBuggyLookValidationTest,
	"Tortunabo.Rally.Buggy.Look.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBuggyLookValidationTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyCosmetics;
	const FName Caiman(TEXT("BuggyModel_Caiman"));
	const FName Lava(TEXT("BuggyPaint_Lava"));
	const FName Fake(TEXT("BuggyPaint_Inventada"));
	TestNotNull(TEXT("El Caimán existe"), FindModel(Caiman));
	TestNotNull(TEXT("La lava existe"), FindPaint(Lava));
	TestNull(TEXT("Un Id inventado no existe"), FindPaint(Fake));
	TestNull(TEXT("Un modelo no es una pintura"), FindPaint(Caiman));
	TestEqual(TEXT("Resolver lo inventado da el de serie"), ResolvePaint(Fake).Id, FName(NAME_None));
	TestTrue(TEXT("Precio del Caimán"), PriceOf(ETNCosmeticCategory::BuggyModel, Caiman) > 0);
	TestEqual(TEXT("Precio en la categoría equivocada"), PriceOf(ETNCosmeticCategory::BuggyPaint, Caiman), 0);
	ETNCosmeticCategory Category = ETNCosmeticCategory::Helmet;
	TestTrue(TEXT("La lava es una pintura"), CategoryOf(Lava, Category) && Category == ETNCosmeticCategory::BuggyPaint);

	FTN_BuggyLook Look;
	TestTrue(TEXT("El de serie siempre se puede poner"), CanEquip(Look, {}));
	Look.ModelId = Caiman;
	TestFalse(TEXT("Sin comprar no se puede poner"), CanEquip(Look, {}));
	TestTrue(TEXT("Comprado sí"), CanEquip(Look, { Caiman }));
	Look.PaintId = Lava;
	TestFalse(TEXT("Modelo comprado y pintura no: no"), CanEquip(Look, { Caiman }));
	TestTrue(TEXT("Los dos comprados: sí"), CanEquip(Look, { Caiman, Lava }));
	Look.PaintId = Fake;
	TestFalse(TEXT("Una pintura inventada nunca, aunque venga en la lista"), CanEquip(Look, { Caiman, Fake }));
	Look.ModelId = Lava;
	Look.PaintId = NAME_None;
	TestFalse(TEXT("Una pintura en el hueco del modelo: no"), CanEquip(Look, { Lava }));

	FTN_BuggyLook Dirty;
	Dirty.ModelId = FName(TEXT("BuggyModel_DeOtraVersion"));
	Dirty.PaintId = Lava;
	const FTN_BuggyLook Clean = Sanitize(Dirty);
	TestEqual(TEXT("Saneado: el modelo desconocido pasa a ser el de serie"), Clean.ModelId, FName(NAME_None));
	TestEqual(TEXT("Saneado: la pintura buena se queda"), Clean.PaintId, Lava);

	TSet<FName> Known = { FName(TEXT("Previo")) };
	TArray<FName> TooMany;
	TooMany.Init(Lava, MaxUnlocked + 1);
	TestFalse(TEXT("Lista demasiado larga: se rechaza"), FilterKnownIds(TooMany, MaxUnlocked, Known));
	TestTrue(TEXT("Rechazada: no toca lo anterior"), Known.Contains(FName(TEXT("Previo"))));
	TestTrue(TEXT("Lista válida"), FilterKnownIds({ Caiman, Fake, NAME_None, Lava, FName(TEXT("Helmet_Gold")) }, MaxUnlocked, Known));
	TestEqual(TEXT("Solo quedan los del catálogo"), Known.Num(), 2);

	const FName Classic(TEXT("BuggyModel_Clasico"));
	TestTrue(TEXT("La tortuga clásica se vende"), FindModel(Classic) && PriceOf(ETNCosmeticCategory::BuggyModel, Classic) > 0);
	TestNotEqual(TEXT("Clave distinta para cada aspecto"), LookKey(FTN_BuggyLook()), LookKey(Clean));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBuggyLookSyncTest,
	"Tortunabo.Rally.Buggy.Look.Sync",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBuggyLookSyncTest::RunTest(const FString& Parameters)
{
	using namespace TNCosmeticsSync;
	FTNCosmeticLoadout Loadout;
	Loadout.UnlockedBuggyIds.Init(FName(TEXT("BuggyPaint_Lava")), RpcArrayCap);
	TestTrue(TEXT("Buggies desbloqueados justo en la cota"), IsLoadoutWithinRpcCaps(Loadout));
	Loadout.UnlockedBuggyIds.Add(FName(TEXT("BuggyPaint_Lava")));
	TestFalse(TEXT("Uno más: cliente manipulado"), IsLoadoutWithinRpcCaps(Loadout));

	FTN_BuggyLook Look;
	Look.ModelId = FName(TEXT("BuggyModel_Laud"));
	TestFalse(TEXT("El servidor no acepta un buggy sin comprar"), CanEquipBuggyLook(Look, {}));
	TestTrue(TEXT("Comprado, sí"), CanEquipBuggyLook(Look, { Look.ModelId }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBuggyLookSaveTest,
	"Tortunabo.Rally.Buggy.Look.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBuggyLookSaveTest::RunTest(const FString& Parameters)
{
	UTN_CosmeticSaveGame* Original = NewObject<UTN_CosmeticSaveGame>();
	Original->StampCurrentVersion();
	Original->UnlockedBuggyIds = { FName(TEXT("BuggyModel_Caiman")), FName(TEXT("BuggyPaint_Meta")) };
	Original->EquippedBuggyLook.ModelId = FName(TEXT("BuggyModel_Caiman"));
	Original->EquippedBuggyLook.PaintId = FName(TEXT("BuggyPaint_Meta"));
	Original->AccumulatedRaceScore = 321;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serializa el perfil"), UGameplayStatics::SaveGameToMemory(Original, Bytes));
	const UTN_CosmeticSaveGame* Loaded = Cast<UTN_CosmeticSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Se vuelve a leer"), Loaded))
	{
		return false;
	}
	TestTrue(TEXT("Entero (marca de fin)"), Loaded->IsIntact());
	TestEqual(TEXT("Buggies comprados"), Loaded->UnlockedBuggyIds.Num(), 2);
	TestTrue(TEXT("Buggy equipado"), Loaded->EquippedBuggyLook == Original->EquippedBuggyLook);
	TestEqual(TEXT("Y lo de siempre sigue igual"), Loaded->AccumulatedRaceScore, 321);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
