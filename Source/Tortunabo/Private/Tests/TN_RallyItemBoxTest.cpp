// Cajas «?» del Rally (#629): reparto de munición de la torreta por puesto (sin turbos ni estrellas), cargas, retroceso de
// todas las municiones, conchas que corren por el suelo, uso de los bots y guiado de la concha teledirigida. Lógica pura.
// Correr desde Session Frontend (categoría "Tortunabo.Rally.ItemBox") o headless con UnrealEditor-Win64-DebugGame-Cmd
// <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.ItemBox; Quit".

#include "Misc/AutomationTest.h"
#include "Kart/TN_KartShellLogic.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyVehicle.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNItemBoxTest
{
	/** Munición especial que puede salir de una caja «?». */
	const ETNRallyAmmo BoxAmmo[] = { ETNRallyAmmo::Concha, ETNRallyAmmo::ConchaGuiada, ETNRallyAmmo::Alga, ETNRallyAmmo::Tinta,
		ETNRallyAmmo::Burbuja, ETNRallyAmmo::Mortero, ETNRallyAmmo::Ancla, ETNRallyAmmo::Erizos, ETNRallyAmmo::Medusa, ETNRallyAmmo::Arpon };

	/** Veces que sale cada munición con Rolls tiradas uniformes (índice = valor de ETNRallyAmmo). */
	TArray<int32> CountRolls(const TNRally::FAmmoWeights& Weights, int32 Rolls)
	{
		TArray<int32> Counts;
		// Todos los valores del enum (con su _MAX): las municiones nuevas se añaden al final.
		Counts.SetNumZeroed(StaticEnum<ETNRallyAmmo>()->NumEnums());
		for (int32 Index = 0; Index < Rolls; ++Index)
		{
			++Counts[static_cast<int32>(TNRally::PickAmmo(Weights, (Index + 0.5f) / Rolls))];
		}
		return Counts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyItemBoxWeightsTest, "Tortunabo.Rally.ItemBox.WeightsByPlace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyItemBoxWeightsTest::RunTest(const FString& Parameters)
{
	using namespace TNRally;
	using namespace TNItemBoxTest;
	for (int32 Teams = 1; Teams <= 8; ++Teams)
	{
		for (int32 Place = 1; Place <= Teams; ++Place)
		{
			const TArray<int32> Counts = CountRolls(AmmoWeightsForPlace(Place, Teams), 2000);
			const FString Where = FString::Printf(TEXT("%d.º de %d"), Place, Teams);
			TestEqual(*(Where + TEXT(": nunca sale nada")), Counts[static_cast<int32>(ETNRallyAmmo::None)], 0);
			TestEqual(*(Where + TEXT(": el coco no sale de una caja (es el infinito)")), Counts[static_cast<int32>(ETNRallyAmmo::Coco)], 0);
			for (const ETNRallyAmmo Ammo : BoxAmmo)
			{
				TestTrue(*(Where + TEXT(": sale ") + UEnum::GetValueAsString(Ammo)), Counts[static_cast<int32>(Ammo)] > 0);
			}
		}
	}
	for (const ETNRallyAmmo Ammo : BoxAmmo)
	{
		TestTrue(*(TEXT("toda munición de caja es especial: ") + UEnum::GetValueAsString(Ammo)), TNRallyTurret::IsSpecial(Ammo));
	}
	const FAmmoWeights First = AmmoWeightsForPlace(1, 8);
	const FAmmoWeights Last = AmmoWeightsForPlace(8, 8);
	TestTrue(TEXT("Delante, más conchas rectas que detrás"), First.Concha > Last.Concha);
	TestTrue(TEXT("Detrás, más teledirigidas que delante"), Last.ConchaGuiada > First.ConchaGuiada);
	TestTrue(TEXT("Delante, la recta más que la teledirigida"), First.Concha > First.ConchaGuiada);
	TestTrue(TEXT("Detrás, la teledirigida más que la recta"), Last.ConchaGuiada > Last.Concha);
	TestTrue(TEXT("Delante, más alga que detrás"), First.Alga > Last.Alga);
	TestTrue(TEXT("Detrás, más mortero que delante"), Last.Mortero > First.Mortero);
	TestTrue(TEXT("Sin pesos sale alga (nunca nada)"), PickAmmo(FAmmoWeights(), 0.5f) == ETNRallyAmmo::Alga);

	FAmmoWeights Only;
	Only.ConchaGuiada = 1.f;
	TestTrue(TEXT("Una sola munición posible: sale siempre"), PickAmmo(Only, 0.f) == ETNRallyAmmo::ConchaGuiada);
	TestTrue(TEXT("También con la tirada más alta"), PickAmmo(Only, 1.f) == ETNRallyAmmo::ConchaGuiada);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyItemBoxAmmoTest, "Tortunabo.Rally.ItemBox.AmmoUse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyItemBoxAmmoTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	using namespace TNItemBoxTest;
	for (const ETNRallyAmmo Ammo : BoxAmmo)
	{
		const FString Name = UEnum::GetValueAsString(Ammo);
		const FAmmoSpec Spec = SpecFor(Ammo);
		TestTrue(*(Name + TEXT(": la caja da cargas")), TNRally::ChargesFor(Ammo) > 0);
		TestTrue(*(Name + TEXT(": tiene cadencia")), Spec.FireInterval > 0.f);
		// La medusa (#771) no lanza nada: actúa sobre el propio buggy, sin retroceso.
		if (!IsSelfAmmo(Ammo))
		{
			TestTrue(*(Name + TEXT(": el retroceso empuja al buggy")), Spec.RecoilCms > 0.f);
			TestTrue(*(Name + TEXT(": sale de la torreta")), Spec.SpeedCms > 0.f && Spec.LifeSeconds > 0.f);
		}
		// La caja sustituye la especial que llevara y se puede seleccionar con la rueda.
		const FSpecial Given = Give(Ammo, TNRally::ChargesFor(Ammo));
		TestTrue(*(Name + TEXT(": se puede disparar")), CanFireSpecial(Given));
		TestTrue(*(Name + TEXT(": la rueda la selecciona")), CycleAmmo(ETNRallyAmmo::Coco, Given, 1) == Ammo);
	}
	TestTrue(TEXT("el coco también retrocede"), SpecFor(ETNRallyAmmo::Coco).RecoilCms > 0.f);
	TestEqual(TEXT("Concha: 2 cargas"), TNRally::ChargesFor(ETNRallyAmmo::Concha), 2);
	TestEqual(TEXT("Concha teledirigida: 1 carga"), TNRally::ChargesFor(ETNRallyAmmo::ConchaGuiada), 1);
	TestTrue(TEXT("la concha corre por el suelo"), IsGroundShell(ETNRallyAmmo::Concha) && IsGroundShell(ETNRallyAmmo::ConchaGuiada));
	TestFalse(TEXT("el resto vuela"), IsGroundShell(ETNRallyAmmo::Coco) || IsGroundShell(ETNRallyAmmo::Mortero) || IsGroundShell(ETNRallyAmmo::Alga));
	TestEqual(TEXT("las conchas, sin gravedad de vuelo"), SpecFor(ETNRallyAmmo::Concha).GravityScale, 0.f);
	const FVector Recoil = RecoilVelocity(FVector::BackwardVector, SpecFor(ETNRallyAmmo::Concha).RecoilCms);
	TestTrue(TEXT("una concha hacia atrás empuja hacia delante"), Recoil.X > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyItemBoxBotsTest, "Tortunabo.Rally.ItemBox.Bots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyItemBoxBotsTest::RunTest(const FString& Parameters)
{
	using namespace TNRally;
	const auto Is = [this](const TCHAR* What, EBotSpecialShot Got, EBotSpecialShot Expected)
	{
		TestEqual(What, static_cast<int32>(Got), static_cast<int32>(Expected));
	};
	Is(TEXT("Concha con alguien a 30 m delante: al de delante"), ShouldBotFireSpecial(ETNRallyAmmo::Concha, 0.5f, 3000.f, -1.f), EBotSpecialShot::AtAhead);
	Is(TEXT("Teledirigida con alguien delante: al de delante"), ShouldBotFireSpecial(ETNRallyAmmo::ConchaGuiada, 0.5f, 5000.f, 800.f), EBotSpecialShot::AtAhead);
	Is(TEXT("Concha sin nadie cerca: se la guarda"), ShouldBotFireSpecial(ETNRallyAmmo::Concha, 0.5f, 20000.f, -1.f), EBotSpecialShot::Hold);
	Is(TEXT("Alga con alguien detrás: al de detrás"), ShouldBotFireSpecial(ETNRallyAmmo::Alga, 0.5f, 3000.f, 2000.f), EBotSpecialShot::AtBehind);
	Is(TEXT("Alga sin nadie detrás pero sí delante: al de delante"), ShouldBotFireSpecial(ETNRallyAmmo::Alga, 0.5f, 3000.f, -1.f), EBotSpecialShot::AtAhead);
	Is(TEXT("Mortero al de delante"), ShouldBotFireSpecial(ETNRallyAmmo::Mortero, 0.f, 1000.f, -1.f), EBotSpecialShot::AtAhead);
	Is(TEXT("En cabeza, la tinta se espera"), ShouldBotFireSpecial(ETNRallyAmmo::Tinta, 1.f, -1.f, 500.f), EBotSpecialShot::Hold);
	Is(TEXT("La burbuja, nada más cogerla, se espera"), ShouldBotFireSpecial(ETNRallyAmmo::Burbuja, 0.5f, -1.f, -1.f), EBotSpecialShot::Hold);
	Is(TEXT("La burbuja, al rato, hacia delante"), ShouldBotFireSpecial(ETNRallyAmmo::Burbuja, 2.f, -1.f, -1.f), EBotSpecialShot::Free);
	Is(TEXT("Pasados 8 s en cabeza, al de detrás"), ShouldBotFireSpecial(ETNRallyAmmo::Tinta, 8.5f, -1.f, 9000.f), EBotSpecialShot::AtBehind);
	Is(TEXT("Pasados 8 s sin nadie, hacia delante"), ShouldBotFireSpecial(ETNRallyAmmo::Ancla, 8.5f, -1.f, -1.f), EBotSpecialShot::Free);
	Is(TEXT("Sin especial, nada"), ShouldBotFireSpecial(ETNRallyAmmo::None, 20.f, 100.f, 100.f), EBotSpecialShot::Hold);
	Is(TEXT("El coco no es una especial"), ShouldBotFireSpecial(ETNRallyAmmo::Coco, 20.f, 100.f, 100.f), EBotSpecialShot::Hold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyItemBoxShellTest, "Tortunabo.Rally.ItemBox.Shell",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyItemBoxShellTest::RunTest(const FString& Parameters)
{
	using namespace TNKart;
	TestEqual(TEXT("La teledirigida va a por la de delante"), HomingTargetPlace(3), 2);
	TestEqual(TEXT("La primera no tiene a quién perseguir"), HomingTargetPlace(1), INDEX_NONE);
	const FVector Turned = SteerShell(FVector::ForwardVector, FVector(0.0, 1.0, 0.0), 30.f);
	TestEqual(TEXT("Gira 30° hacia el blanco"), FMath::RadiansToDegrees(FMath::Atan2(Turned.Y, Turned.X)), 30.0, 0.01);
	FVector Heading = FVector::ForwardVector;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Heading = SteerShell(Heading, FVector(-1.0, 1.0, 0.0), 30.f);
	}
	TestTrue(TEXT("Al final mira al blanco"), FVector::DotProduct(Heading, FVector(-1.0, 1.0, 0.0).GetSafeNormal()) > 0.999);
	TestEqual(TEXT("Siempre en el plano y unitaria"), SteerShell(FVector(1.0, 0.0, 0.5), FVector(0.0, 0.0, 1.0), 10.f).Size(), 1.0, 0.001);
	return true;
}

#endif
