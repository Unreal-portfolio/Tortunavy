// Aspecto de los objetos de siempre (#787): cada fila de DT_Items sale con un icono dibujado en código (nunca un recurso del
// motor ni de VREditor) y, si traía una forma básica del motor, con una malla construida en código. También el recambio de la
// tinta en pantalla (TNInkScreen).
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Items.CatalogVisuals; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_CatalogItemVisuals.h"
#include "../World/TN_CatalogItemArt.h"
#include "../Player/TN_InkScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCatalogVisualsTest
{
	/** true si la fila sale en algún modo: los sorteos y el tutorial solo cogen filas con uso; Score es de relleno. */
	bool IsUsedRow(const FTN_InventoryItem& Row)
	{
		return Row.IsValid() && Row.UseType != ETN_ItemUseType::None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCatalogVisualsRowsTest,
	"Tortunabo.Items.CatalogVisuals.Rows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCatalogVisualsRowsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Catalog = LoadObject<UDataTable>(nullptr, TNRaceItems::CatalogPath());
	if (!TestNotNull(TEXT("DT_Items se carga"), Catalog))
	{
		return false;
	}
	int32 Used = 0;
	TSet<UTexture2D*> Icons;
	Catalog->ForeachRow<FTN_InventoryItem>(TEXT("CatalogVisualsTest"), [this, &Used, &Icons](const FName& RowName, const FTN_InventoryItem& Row)
	{
		const FString Name = RowName.ToString();
		const ETNCatalogLook Look = TNCatalogItemVisuals::LookOf(Row);
		if (!TNCatalogVisualsTest::IsUsedRow(Row) && Look == ETNCatalogLook::None)
		{
			return;
		}
		Used += TNCatalogVisualsTest::IsUsedRow(Row) ? 1 : 0;
		TestNotEqual(FString::Printf(TEXT("%s tiene aspecto propio"), *Name), Look, ETNCatalogLook::None);

		FTN_InventoryItem Item = Row;
		TestTrue(FString::Printf(TEXT("%s se resuelve"), *Name), TNCatalogItemVisuals::ResolveVisualsHeadless(Item));
		UTexture2D* Icon = Item.ItemIcon.Get();
		if (TestNotNull(FString::Printf(TEXT("%s con icono"), *Name), Icon))
		{
			TestFalse(FString::Printf(TEXT("%s: icono sin recursos del motor (%s)"), *Name, *Icon->GetPathName()),
				TNCatalogItemVisuals::IsEnginePlaceholder(Icon));
			TestEqual(FString::Printf(TEXT("%s: icono de 128 px"), *Name), Icon->GetSizeX(), 128);
			TestFalse(FString::Printf(TEXT("%s: icono propio, no el de otra fila"), *Name), Icons.Contains(Icon));
			Icons.Add(Icon);
		}
		UStaticMesh* Mesh = Item.EquippedMesh.Get();
		if (TestNotNull(FString::Printf(TEXT("%s con malla"), *Name), Mesh))
		{
			TestFalse(FString::Printf(TEXT("%s: malla sin formas del motor (%s)"), *Name, *Mesh->GetPathName()),
				TNCatalogItemVisuals::IsEnginePlaceholder(Mesh));
		}
		// Idempotente: el inventario lo vuelve a resolver en cada réplica.
		FTN_InventoryItem Again = Item;
		TNCatalogItemVisuals::ResolveVisualsHeadless(Again);
		TestEqual(FString::Printf(TEXT("%s: misma malla al repetir"), *Name), Again.EquippedMesh.Get(), Mesh);
		TestEqual(FString::Printf(TEXT("%s: mismo icono al repetir"), *Name), Again.ItemIcon.Get(), Icon);
	});
	// Barrita, bola, cabezota, concha, tinta y tótem.
	TestTrue(FString::Printf(TEXT("Al menos seis filas usadas (%d)"), Used), Used >= 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCatalogVisualsRulesTest,
	"Tortunabo.Items.CatalogVisuals.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCatalogVisualsRulesTest::RunTest(const FString& Parameters)
{
	// Los de carrera y de Todos contra Todos no son de DT_Items: no los toca.
	const FTN_InventoryItem Race = TNRaceItems::MakeItem(ETNRaceItem::Coconut);
	TestEqual(TEXT("Coco turbo: no es de DT_Items"), TNCatalogItemVisuals::LookOf(Race), ETNCatalogLook::None);
	FTN_InventoryItem Unknown;
	Unknown.ItemId = TEXT("SinUso");
	TestEqual(TEXT("Fila sin uso desconocida: sin aspecto"), TNCatalogItemVisuals::LookOf(Unknown), ETNCatalogLook::None);

	// Recursos del motor y de VREditor: sí; lo construido en ejecución (/Engine/Transient): no.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (TestNotNull(TEXT("Cubo del motor"), Cube))
	{
		TestTrue(TEXT("El cubo es de relleno"), TNCatalogItemVisuals::IsEnginePlaceholder(Cube));
	}
	TestFalse(TEXT("Nulo no es del motor"), TNCatalogItemVisuals::IsEnginePlaceholder(nullptr));
	UTexture2D* Painted = TNCatalogItemArt::GetIcon(ETNCatalogLook::BigHead, true);
	if (TestNotNull(TEXT("Icono de la cabezota pintado"), Painted))
	{
		TestFalse(TEXT("Lo pintado en ejecución no cuenta como del motor"), TNCatalogItemVisuals::IsEnginePlaceholder(Painted));
	}

	// Un lanzable conserva su malla aunque sea del motor (viaja por un multicast); los demás la cambian.
	FTN_InventoryItem Throwable;
	Throwable.ItemId = TEXT("Prueba");
	Throwable.UseType = ETN_ItemUseType::Throwable;
	Throwable.EquippedMesh = Cube;
	TestFalse(TEXT("Lanzable: conserva la malla"), TNCatalogItemVisuals::ShouldReplaceMesh(Throwable));
	FTN_InventoryItem Boost = Throwable;
	Boost.UseType = ETN_ItemUseType::SelfStaminaBoost;
	Boost.EquippedMeshScale = FVector(0.25f);
	TestTrue(TEXT("Barrita con el cubo: se cambia"), TNCatalogItemVisuals::ShouldReplaceMesh(Boost));
	TNCatalogItemVisuals::ResolveVisualsHeadless(Boost);
	TestTrue(TEXT("Barrita: escala de la malla de código"), Boost.EquippedMeshScale.Equals(FVector::OneVector));
	TestTrue(TEXT("Barrita: malla de código"), Boost.EquippedMesh != Cube && Boost.EquippedMesh != nullptr);
	if (Boost.EquippedMesh)
	{
		const FVector Size = Boost.EquippedMesh->GetBoundingBox().GetSize();
		TestTrue(FString::Printf(TEXT("Barrita entre 20 y 35 cm (%.1f)"), Size.GetMax()), Size.GetMax() >= 20.0 && Size.GetMax() <= 35.0);
	}

	// Todos los aspectos con malla de código la construyen; todos tienen icono.
	for (int32 Index = static_cast<int32>(ETNCatalogLook::None) + 1; Index < static_cast<int32>(ETNCatalogLook::Count); ++Index)
	{
		const ETNCatalogLook Look = static_cast<ETNCatalogLook>(Index);
		const FString Code = TNCatalogItemVisuals::CodeName(Look);
		TestNotNull(FString::Printf(TEXT("%s: icono"), *Code), TNCatalogItemArt::GetIcon(Look, true));
		TNCatalogItemArt::FHeldLook Held;
		TestEqual(FString::Printf(TEXT("%s: malla de código si la necesita"), *Code), TNCatalogItemArt::GetHeldLook(Look, Held, true),
			TNCatalogItemArt::HasCodeMesh(Look));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNInkScreenTest,
	"Tortunabo.Items.CatalogVisuals.InkScreen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNInkScreenTest::RunTest(const FString& Parameters)
{
	// El material de BP_TortugaCharacter es el DefaultPostProcessMaterial del motor: no tapa nada, va el recambio.
	const UMaterialInterface* EngineDefault = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultPostProcessMaterial.DefaultPostProcessMaterial"));
	if (TestNotNull(TEXT("Material de post-proceso del motor"), EngineDefault))
	{
		TestTrue(TEXT("El del motor necesita recambio"), TNInkScreen::NeedsFallback(EngineDefault));
	}
	TestTrue(TEXT("Sin material, recambio"), TNInkScreen::NeedsFallback(nullptr));

	UTexture2D* Splats = TNInkScreen::SplatTexture(true);
	if (TestNotNull(TEXT("Manchas pintadas"), Splats))
	{
		TestEqual(TEXT("Manchas en 16:9"), Splats->GetSizeX() * 9, Splats->GetSizeY() * 16);
	}
	TestEqual(TEXT("Entera con tiempo por delante"), TNInkScreen::OpacityAt(3.0), 1.f);
	TestEqual(TEXT("Apagada al acabar"), TNInkScreen::OpacityAt(0.0), 0.f);
	TestEqual(TEXT("Apagada pasado el final"), TNInkScreen::OpacityAt(-1.0), 0.f);
	TestTrue(TEXT("Desvaneciéndose al final"), TNInkScreen::OpacityAt(0.4) > 0.f && TNInkScreen::OpacityAt(0.4) < 1.f);
	return true;
}

#endif
