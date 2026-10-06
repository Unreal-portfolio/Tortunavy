// Lógica pura del combate del Rally (TN_RallyCombatLogic.h) y de la munición seleccionada y el retroceso
// (TN_RallyTurretLogic.h). Sin mundo ni actores. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Combat; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyTurretLogic.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyCombatTest
{
	constexpr float HalfLength = 190.f;
	constexpr float HalfWidth = 95.f;

	/** Componente Z del par de un impulso J aplicado en R (respecto al origen): distinto de 0 = hace girar. */
	double YawTorque(const FVector& R, const FVector& J)
	{
		return R.X * J.Y - R.Y * J.X;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatAmmoCycleTest,
	"Tortunabo.Rally.Combat.AmmoCycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatAmmoCycleTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FSpecial Empty;
	const FSpecial Anchor = Give(ETNRallyAmmo::Ancla, 2);

	TestEqual(TEXT("sin especial solo hay coco"), AvailableAmmo(Empty).Num(), 1);
	TestTrue(TEXT("con ancla: coco y ancla"), AvailableAmmo(Anchor).Num() == 2 && AvailableAmmo(Anchor)[1] == ETNRallyAmmo::Ancla);
	TestTrue(TEXT("sin especial, el ciclo se queda en coco"), CycleAmmo(ETNRallyAmmo::Coco, Empty, 1) == ETNRallyAmmo::Coco);
	TestTrue(TEXT("adelante: coco → ancla"), CycleAmmo(ETNRallyAmmo::Coco, Anchor, 1) == ETNRallyAmmo::Ancla);
	TestTrue(TEXT("adelante desde la última vuelve al coco"), CycleAmmo(ETNRallyAmmo::Ancla, Anchor, 1) == ETNRallyAmmo::Coco);
	TestTrue(TEXT("atrás desde el coco da la última"), CycleAmmo(ETNRallyAmmo::Coco, Anchor, -1) == ETNRallyAmmo::Ancla);
	TestTrue(TEXT("dos pasos dan la vuelta"), CycleAmmo(ETNRallyAmmo::Coco, Anchor, 2) == ETNRallyAmmo::Coco);
	TestTrue(TEXT("dirección 0 no cambia"), CycleAmmo(ETNRallyAmmo::Ancla, Anchor, 0) == ETNRallyAmmo::Ancla);

	TestTrue(TEXT("el coco seleccionado se queda"), ResolveSelection(ETNRallyAmmo::Coco, Anchor) == ETNRallyAmmo::Coco);
	TestTrue(TEXT("sin cargas, la selección vuelve al coco"), ResolveSelection(ETNRallyAmmo::Ancla, AfterSpecialShot(Give(ETNRallyAmmo::Ancla, 1))) == ETNRallyAmmo::Coco);
	TestTrue(TEXT("una caja nueva sustituye la especial seleccionada"), ResolveSelection(ETNRallyAmmo::Ancla, Give(ETNRallyAmmo::Tinta, 2)) == ETNRallyAmmo::Tinta);
	TestTrue(TEXT("None no es una selección válida"), ResolveSelection(ETNRallyAmmo::None, Anchor) == ETNRallyAmmo::Coco);

	TestTrue(TEXT("el ancla es especial"), IsSpecial(ETNRallyAmmo::Ancla));
	TestEqual(TEXT("el ancla da 2 cargas"), SpecFor(ETNRallyAmmo::Ancla).BoxCharges, 2);
	TestTrue(TEXT("el ancla vuela"), SpecFor(ETNRallyAmmo::Ancla).SpeedCms > 0.f && SpecFor(ETNRallyAmmo::Ancla).LifeSeconds > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatImpactPushTest,
	"Tortunabo.Rally.Combat.ImpactPush",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatImpactPushTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	using TNRallyCombatTest::HalfLength;
	using TNRallyCombatTest::HalfWidth;

	TestTrue(TEXT("punto delante: morro"), ClassifyHitZone(FVector(180.f, 20.f, 0.f), HalfLength, HalfWidth) == EHitZone::Front);
	TestTrue(TEXT("punto detrás: trasera"), ClassifyHitZone(FVector(-170.f, -10.f, 0.f), HalfLength, HalfWidth) == EHitZone::Rear);
	TestTrue(TEXT("punto al costado: lateral"), ClassifyHitZone(FVector(30.f, 90.f, 0.f), HalfLength, HalfWidth) == EHitZone::Side);

	// Coco de frente contra el morro: frena (impulso hacia atrás) y levanta (hacia arriba en el morro).
	const FImpactPush Nose = ComputeImpactPush(ETNRallyAmmo::Coco, FVector(185.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), HalfLength, HalfWidth);
	TestTrue(TEXT("morro: zona"), Nose.Zone == EHitZone::Front);
	TestTrue(TEXT("morro: frena"), Nose.LocalImpulseCms.X < 0.f);
	TestTrue(TEXT("morro: levanta"), Nose.LocalImpulseCms.Z > 0.f && Nose.LocalPoint.X > 0.f);

	// Coco de lado contra el centro del costado derecho: empuja a la izquierda y, aplicado lejos del centro, gira.
	const FImpactPush Side = ComputeImpactPush(ETNRallyAmmo::Coco, FVector(0.f, 95.f, 0.f), FVector(0.f, -1.f, 0.f), HalfLength, HalfWidth);
	TestTrue(TEXT("lateral: zona"), Side.Zone == EHitZone::Side);
	TestTrue(TEXT("lateral: empuja hacia fuera del impacto"), Side.LocalImpulseCms.Y < 0.f);
	TestTrue(TEXT("lateral: brazo de palanca"), FMath::Abs(Side.LocalPoint.X) >= HalfLength * SideLeverFraction - KINDA_SMALL_NUMBER);
	TestTrue(TEXT("lateral: hace girar"), FMath::Abs(TNRallyCombatTest::YawTorque(Side.LocalPoint, Side.LocalImpulseCms)) > 1.0);
	TestEqual(TEXT("lateral: no levanta"), static_cast<float>(Side.LocalImpulseCms.Z), 0.f);

	// Coco desde atrás contra la trasera: empuja hacia delante.
	const FImpactPush Rear = ComputeImpactPush(ETNRallyAmmo::Coco, FVector(-185.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), HalfLength, HalfWidth);
	TestTrue(TEXT("trasera: zona"), Rear.Zone == EHitZone::Rear);
	TestTrue(TEXT("trasera: empuja hacia delante"), Rear.LocalImpulseCms.X > 0.f && Rear.LocalPoint.X < 0.f);

	const FImpactPush NoDir = ComputeImpactPush(ETNRallyAmmo::Coco, FVector(0.f, 95.f, 0.f), FVector::ZeroVector, HalfLength, HalfWidth);
	TestTrue(TEXT("sin dirección: empuja del punto hacia el centro"), NoDir.LocalImpulseCms.Y < 0.f);
	TestTrue(TEXT("la burbuja no empuja"), ComputeImpactPush(ETNRallyAmmo::Burbuja, FVector(185.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), HalfLength, HalfWidth).LocalImpulseCms.IsZero());
	TestEqual(TEXT("el coco empuja 350 cm/s en horizontal"), static_cast<float>(FVector(Side.LocalImpulseCms.X, Side.LocalImpulseCms.Y, 0.f).Size()), ImpactFor(ETNRallyAmmo::Coco).PushCms, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatRecoilTest,
	"Tortunabo.Rally.Combat.Recoil",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatRecoilTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	constexpr float HalfLength = TNRallyCombatTest::HalfLength;

	TestTrue(TEXT("hacia delante frena"), RecoilVelocity(FVector(1.f, 0.f, 0.f), 120.f).X < 0.f);
	TestTrue(TEXT("hacia atrás acelera"), RecoilVelocity(FVector(-1.f, 0.f, 0.f), 120.f).X > 0.f);

	const FRecoilLift Forward = RecoilLift(FVector(1.f, 0.f, 0.f), 200.f, HalfLength);
	TestTrue(TEXT("hacia delante levanta el morro"), Forward.LiftCms > 0.f && Forward.LocalPoint.X > 0.f);
	TestEqual(TEXT("levantamiento a tope"), Forward.LiftCms, 200.f * RecoilLiftRatio, 0.01f);

	const FRecoilLift Backward = RecoilLift(FVector(-1.f, 0.f, 0.f), 200.f, HalfLength);
	TestTrue(TEXT("hacia atrás levanta la trasera"), Backward.LiftCms > 0.f && Backward.LocalPoint.X < 0.f);

	TestEqual(TEXT("de lado no levanta"), RecoilLift(FVector(0.f, 1.f, 0.f), 200.f, HalfLength).LiftCms, 0.f);
	TestEqual(TEXT("sin retroceso no levanta"), RecoilLift(FVector(1.f, 0.f, 0.f), 0.f, HalfLength).LiftCms, 0.f);
	const FRecoilLift Diagonal = RecoilLift(FVector(1.f, 1.f, 0.f).GetSafeNormal(), 200.f, HalfLength);
	TestTrue(TEXT("en diagonal levanta menos"), Diagonal.LiftCms > 0.f && Diagonal.LiftCms < Forward.LiftCms);

	// Sin tope (#695, Decisión del 06-10 en #775): cada munición levanta lo que da su retroceso; el mortero, 560 cm/s el morro.
	const float MortarRecoil = SpecFor(ETNRallyAmmo::Mortero).RecoilCms;
	TestEqual(TEXT("el mortero levanta todo su retroceso"), RecoilLift(FVector(1.f, 0.f, 0.f), MortarRecoil, HalfLength).LiftCms,
		MortarRecoil * RecoilLiftRatio, 0.01f);
	TestEqual(TEXT("hacia atrás, el mortero levanta lo mismo"), RecoilLift(FVector(-1.f, 0.f, 0.f), MortarRecoil, HalfLength).LiftCms,
		MortarRecoil * RecoilLiftRatio, 0.01f);
	TestTrue(TEXT("el mortero levanta más que la concha"), RecoilLift(FVector(1.f, 0.f, 0.f), MortarRecoil, HalfLength).LiftCms
		> RecoilLift(FVector(1.f, 0.f, 0.f), SpecFor(ETNRallyAmmo::Concha).RecoilCms, HalfLength).LiftCms);
	TestEqual(TEXT("el frenazo horizontal del mortero no cambia"), static_cast<float>(-RecoilVelocity(FVector(1.f, 0.f, 0.f), MortarRecoil).X),
		MortarRecoil, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatDamageTest,
	"Tortunabo.Rally.Combat.Damage",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatDamageTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	TestTrue(TEXT("el coco quita vida"), ImpactFor(ETNRallyAmmo::Coco).Damage > 0.f);
	TestTrue(TEXT("el mortero quita más que el coco"), ImpactFor(ETNRallyAmmo::Mortero).Damage > ImpactFor(ETNRallyAmmo::Coco).Damage);
	TestTrue(TEXT("el ancla quita vida"), ImpactFor(ETNRallyAmmo::Ancla).Damage > 0.f);
	TestEqual(TEXT("la burbuja no quita vida"), ImpactFor(ETNRallyAmmo::Burbuja).Damage, 0.f);

	TestEqual(TEXT("daño normal"), ApplyDamage(100.f, 10.f, 100.f), 90.f);
	TestEqual(TEXT("no baja de 0"), ApplyDamage(5.f, 10.f, 100.f), 0.f);
	TestEqual(TEXT("un daño negativo no cura"), ApplyDamage(50.f, -20.f, 100.f), 50.f);

	TestFalse(TEXT("con vida llena no echa humo"), IsSmoking(100.f, 100.f));
	TestTrue(TEXT("a media vida echa humo"), IsSmoking(50.f, 100.f));
	TestFalse(TEXT("reventado no echa humo (es la nube)"), IsSmoking(0.f, 100.f));
	TestTrue(TEXT("con 0 revienta"), IsDestroyed(0.f));
	TestFalse(TEXT("con 1 no revienta"), IsDestroyed(1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatCrashTest,
	"Tortunabo.Rally.Combat.Crash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatCrashTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	TestEqual(TEXT("un roce no hace daño"), CrashDamage(300.f, 0.f), 0.f);
	TestTrue(TEXT("en el umbral ya hace algo"), CrashDamage(CrashMinDeltaVCms, 0.f) > 0.f);
	TestEqual(TEXT("a tope, el máximo"), CrashDamage(CrashMaxDeltaVCms * 2.f, 0.f), CrashMaxDamage);
	TestTrue(TEXT("más fuerte, más daño"), CrashDamage(2000.f, 0.f) > CrashDamage(1000.f, 0.f));
	TestEqual(TEXT("aterrizar no es un choque"), CrashDamage(2000.f, 1.f), 0.f);
	TestEqual(TEXT("algo encima tampoco"), CrashDamage(2000.f, -1.f), 0.f);
	TestEqual(TEXT("un valor no finito no hace daño"), CrashDamage(std::numeric_limits<float>::quiet_NaN(), 0.f), 0.f);

	TestEqual(TEXT("golpe por detrás: empujón"), RearBumpCmsFor(-150.f, 190.f, 500.f), RearBumpCms);
	TestEqual(TEXT("golpe en el morro: sin empujón"), RearBumpCmsFor(150.f, 190.f, 500.f), 0.f);
	TestEqual(TEXT("golpe por detrás lento: sin empujón"), RearBumpCmsFor(-150.f, 190.f, 100.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatAnchorTest,
	"Tortunabo.Rally.Combat.Anchor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatAnchorTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	const FVector Velocity(2000.f, 500.f, 300.f);
	const FVector Drag = AnchorDragAccel(Velocity);
	TestTrue(TEXT("frena: opuesta a la velocidad"), FVector::DotProduct(Drag, Velocity) < 0.f);
	TestEqual(TEXT("solo en horizontal"), static_cast<float>(Drag.Z), 0.f);
	TestEqual(TEXT("rápido: deceleración máxima"), static_cast<float>(Drag.Size()), AnchorMaxDecelCms2, 0.5f);
	TestTrue(TEXT("parado: nada"), AnchorDragAccel(FVector::ZeroVector).IsZero());
	TestTrue(TEXT("despacio: menos"), AnchorDragAccel(FVector(100.f, 0.f, 0.f)).Size() < AnchorMaxDecelCms2);

	const FVector Hook(1000.f, 0.f, 0.f);
	TestTrue(TEXT("cuerda floja: el ancla no se mueve"), DragAnchor(FVector(800.f, 0.f, 0.f), Hook, 500.f).Equals(FVector(800.f, 0.f, 0.f)));
	const FVector Dragged = DragAnchor(FVector::ZeroVector, Hook, 500.f);
	TestEqual(TEXT("cuerda tensa: el ancla va a su largo"), static_cast<float>(FVector::Dist(Dragged, Hook)), 500.f, 0.01f);
	TestTrue(TEXT("arrastrada detrás del gancho"), Dragged.X < Hook.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatTurtleLaunchTest,
	"Tortunabo.Rally.Combat.TurtleLaunch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatTurtleLaunchTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	TestTrue(TEXT("despacio no lanza"), TurtleLaunchVelocity(FVector(200.f, 0.f, 0.f), FVector(100.f, 0.f, 0.f)).IsZero());

	const FVector Launch = TurtleLaunchVelocity(FVector(2000.f, 0.f, 0.f), FVector(100.f, 50.f, 0.f));
	TestTrue(TEXT("sale hacia arriba"), Launch.Z >= RunOverBaseUpCms);
	TestTrue(TEXT("sale en la dirección del buggy"), Launch.X > 0.f);
	TestTrue(TEXT("y hacia su lado"), Launch.Y > 0.f);
	TestTrue(TEXT("con tope horizontal"), FVector(Launch.X, Launch.Y, 0.f).Size() <= RunOverMaxHorizontalCms + 0.5f);
	TestTrue(TEXT("con tope vertical"), TurtleLaunchVelocity(FVector(9000.f, 0.f, 0.f), FVector(100.f, 0.f, 0.f)).Z <= RunOverMaxUpCms + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyCombatClientAimTest,
	"Tortunabo.Rally.Combat.ClientAimTolerance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyCombatClientAimTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyTurret;
	const FVector Server(1.f, 0.f, 0.f);
	const auto Yawed = [](float Deg) { return FRotator(0.f, Deg, 0.f).Vector(); };
	const auto Pitched = [](float Deg) { return FRotator(Deg, 0.f, 0.f).Vector(); };

	// El caso del ping: 9° de giro entre lo que vio el cliente y lo que aplica el servidor.
	TestTrue(TEXT("9° de guiñada: manda la del cliente"), ResolveClientFireDirection(Server, Yawed(9.f)).Equals(Yawed(9.f), 1e-4f));
	TestTrue(TEXT("11,9° de cabeceo: manda la del cliente"), ResolveClientFireDirection(Server, Pitched(11.9f)).Equals(Pitched(11.9f), 1e-4f));
	TestTrue(TEXT("12,5°: se rechaza y manda la del servidor"), ResolveClientFireDirection(Server, Yawed(12.5f)).Equals(Server, 1e-4f));
	TestTrue(TEXT("hacia atrás: se rechaza"), ResolveClientFireDirection(Server, -Server).Equals(Server, 1e-4f));
	TestTrue(TEXT("sin dirección del cliente: la del servidor"), ResolveClientFireDirection(Server, FVector::ZeroVector).Equals(Server, 1e-4f));
	const FVector NaNDir(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f);
	TestTrue(TEXT("NaN: la del servidor"), ResolveClientFireDirection(Server, NaNDir).Equals(Server, 1e-4f));
	TestTrue(TEXT("sale unitaria aunque llegue larga"), FMath::IsNearlyEqual(ResolveClientFireDirection(Server, Yawed(5.f) * 37.f).Size(), 1.0, 1e-4));
	TestTrue(TEXT("tolerancia propia más estrecha"), ResolveClientFireDirection(Server, Yawed(9.f), 5.f).Equals(Server, 1e-4f));
	TestTrue(TEXT("la tolerancia por defecto cubre el desvío medido (9°)"), MaxClientAimErrorDeg >= 9.f && MaxClientAimErrorDeg <= 15.f);
	return true;
}

#endif
