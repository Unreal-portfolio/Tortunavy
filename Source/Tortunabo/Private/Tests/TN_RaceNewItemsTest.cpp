// Objetos de carrera de la issue #786 (tabla de surf, caña de pescar, remolino y cohete de feria): catálogo, reparto por
// puesto y origen, y las reglas puras de cada uno (TNRaceItemRules, TN_RaceItemRules.h). Correr desde Session Frontend
// (categoría "Tortunabo.Race.Items") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Race.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_RaceItemRules.h"
#include "World/Beach/TN_RaceItems.h"
#include "World/TN_PickupInteractableBase.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRaceNewItemsTest
{
	const ETNRaceItem NewItems[] = { ETNRaceItem::TablaSurf, ETNRaceItem::CanaPescar, ETNRaceItem::Remolino, ETNRaceItem::CoheteFeria };

	/** Peso para quien va en Norm con 4 en carrera desde la caja. */
	float BoxWeight(ETNRaceItem Item, float Norm)
	{
		return TNRaceItems::PositionWeight(Item, Norm, 4, ETNRaceLootSource::Box);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceNewItemsCatalogTest,
	"Tortunabo.Race.Items.Catalog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceNewItemsCatalogTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceNewItemsTest;
	// Antes de Count y después de los de siempre (no cambia el valor de ninguno de los anteriores).
	TestTrue(TEXT("El silbato sigue siendo el último de los de antes"), static_cast<int32>(ETNRaceItem::Whistle) == 14);
	for (const ETNRaceItem Item : NewItems)
	{
		const FString Code = TNRaceItems::CodeName(Item);
		TestTrue(FString::Printf(TEXT("%s antes de Count"), *Code), Item > ETNRaceItem::Whistle && Item < ETNRaceItem::Count);
		TestTrue(FString::Printf(TEXT("%s: ItemId"), *Code), TNRaceItems::IdOf(Item) == FName(*FString::Printf(TEXT("Race_%s"), *Code)));
		TestTrue(FString::Printf(TEXT("%s: vuelve de su ItemId"), *Code), TNRaceItems::KindOfId(TNRaceItems::IdOf(Item)) == Item);
		const FTN_InventoryItem Row = TNRaceItems::MakeItem(Item);
		TestTrue(FString::Printf(TEXT("%s: es de carrera"), *Code), TNRaceItems::KindOf(Row) == Item);
		TestTrue(FString::Printf(TEXT("%s: tiene pickup"), *Code), Row.PickupActorClass != nullptr);
		TestFalse(FString::Printf(TEXT("%s: nombre propio"), *Code),
			TNRaceItems::DisplayName(Item).EqualTo(TNRaceItems::DisplayName(ETNRaceItem::None)));
	}
	TestEqual(TEXT("Códigos"), TNRaceItems::CodeName(ETNRaceItem::TablaSurf), FString(TEXT("TablaSurf")));
	TestEqual(TEXT("Códigos"), TNRaceItems::CodeName(ETNRaceItem::CoheteFeria), FString(TEXT("CoheteFeria")));
	// La consola los encuentra por su nombre en inglés, en español y por alias.
	const TPair<const TCHAR*, ETNRaceItem> Names[] = {
		{ TEXT("TablaSurf"), ETNRaceItem::TablaSurf }, { TEXT("tabla"), ETNRaceItem::TablaSurf }, { TEXT("Tabla de surf"), ETNRaceItem::TablaSurf },
		{ TEXT("cana"), ETNRaceItem::CanaPescar }, { TEXT("pescar"), ETNRaceItem::CanaPescar },
		{ TEXT("remolino"), ETNRaceItem::Remolino }, { TEXT("cohete"), ETNRaceItem::CoheteFeria }, { TEXT("Cohete de feria"), ETNRaceItem::CoheteFeria } };
	for (const TPair<const TCHAR*, ETNRaceItem>& Name : Names)
	{
		ETNRaceItem Kind = ETNRaceItem::None;
		TestTrue(FString::Printf(TEXT("Consola: %s"), Name.Key), TNRaceItems::ParseKind(Name.Key, Kind) && Kind == Name.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceNewItemsWeightsTest,
	"Tortunabo.Race.Items.Weights",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceNewItemsWeightsTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceNewItemsTest;
	using namespace TNRaceItems;
	// La primera no saca la caña (no tiene a nadie delante); a las de atrás les sale más.
	TestEqual(TEXT("Caña: la primera, nunca"), BoxWeight(ETNRaceItem::CanaPescar, 0.f), 0.f);
	TestEqual(TEXT("Caña: la primera, nunca (cofre)"), PositionWeight(ETNRaceItem::CanaPescar, 0.f, 4, ETNRaceLootSource::Chest), 0.f);
	TestEqual(TEXT("Caña: la primera, nunca (rebuscar)"), PositionWeight(ETNRaceItem::CanaPescar, 0.f, 4, ETNRaceLootSource::Search), 0.f);
	TestTrue(TEXT("Caña: más a la última que a medias"), BoxWeight(ETNRaceItem::CanaPescar, 1.f) > BoxWeight(ETNRaceItem::CanaPescar, 0.5f));
	TestTrue(TEXT("Caña: a medias sí sale"), BoxWeight(ETNRaceItem::CanaPescar, 0.5f) > 0.f);
	TestEqual(TEXT("Caña: sola en la carrera no sale"), PositionWeight(ETNRaceItem::CanaPescar, 0.5f, 1, ETNRaceLootSource::Box), 0.f);

	// Tabla: más a medias y al final que a la primera.
	TestTrue(TEXT("Tabla: a medias más que la primera"), BoxWeight(ETNRaceItem::TablaSurf, 0.5f) > BoxWeight(ETNRaceItem::TablaSurf, 0.f));
	TestTrue(TEXT("Tabla: al final más que la primera"), BoxWeight(ETNRaceItem::TablaSurf, 1.f) > BoxWeight(ETNRaceItem::TablaSurf, 0.f));
	TestTrue(TEXT("Tabla: sola en la carrera sí sale"), PositionWeight(ETNRaceItem::TablaSurf, 0.5f, 1, ETNRaceLootSource::Box) > 0.f);

	// Remolino: más a las de delante (se deja detrás).
	TestTrue(TEXT("Remolino: la primera más que la última"), BoxWeight(ETNRaceItem::Remolino, 0.f) > BoxWeight(ETNRaceItem::Remolino, 1.f));
	TestTrue(TEXT("Remolino: la primera más que a medias"), BoxWeight(ETNRaceItem::Remolino, 0.f) > BoxWeight(ETNRaceItem::Remolino, 0.5f));

	// Cohete: más a las últimas; nunca a la primera.
	TestEqual(TEXT("Cohete: la primera, nunca"), BoxWeight(ETNRaceItem::CoheteFeria, 0.f), 0.f);
	TestTrue(TEXT("Cohete: la última más que a medias"), BoxWeight(ETNRaceItem::CoheteFeria, 1.f) > BoxWeight(ETNRaceItem::CoheteFeria, 0.5f));
	TestTrue(TEXT("Cohete: la última, lo que más le sale de los nuevos"),
		BoxWeight(ETNRaceItem::CoheteFeria, 1.f) >= BoxWeight(ETNRaceItem::TablaSurf, 1.f) && BoxWeight(ETNRaceItem::CoheteFeria, 1.f) > BoxWeight(ETNRaceItem::Remolino, 1.f));

	// Por origen: rebuscar y caja pesan igual; el cofre multiplica por su factor (cada uno el suyo).
	for (const ETNRaceItem Item : NewItems)
	{
		for (const float Norm : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
		{
			const float Search = PositionWeight(Item, Norm, 4, ETNRaceLootSource::Search);
			const float Box = PositionWeight(Item, Norm, 4, ETNRaceLootSource::Box);
			const float Chest = PositionWeight(Item, Norm, 4, ETNRaceLootSource::Chest);
			TestEqual(FString::Printf(TEXT("%s %.2f: rebuscar = caja"), *CodeName(Item), Norm), Search, Box);
			TestTrue(FString::Printf(TEXT("%s %.2f: nunca negativo"), *CodeName(Item), Norm), Box >= 0.f && Chest >= 0.f);
			if (Box > 0.f)
			{
				TestTrue(FString::Printf(TEXT("%s %.2f: el cofre aplica su factor"), *CodeName(Item), Norm), Chest > 0.f);
			}
		}
	}
	TestTrue(TEXT("Cofre: sube el cohete"), PositionWeight(ETNRaceItem::CoheteFeria, 1.f, 4, ETNRaceLootSource::Chest) > BoxWeight(ETNRaceItem::CoheteFeria, 1.f));
	TestTrue(TEXT("Cofre: baja el remolino"), PositionWeight(ETNRaceItem::Remolino, 0.f, 4, ETNRaceLootSource::Chest) < BoxWeight(ETNRaceItem::Remolino, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceNewItemsMoveTest,
	"Tortunabo.Race.Items.Move",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceNewItemsMoveTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceItemRules;
	// El estilo viaja en el multiplicador: la ola y el cohete se reconocen; el turbo, el protector y su suma, no.
	TestTrue(TEXT("Estilo: ola"), MoveStyleOf(SurfMultiplier) == EMoveStyle::Surf);
	TestTrue(TEXT("Estilo: cohete"), MoveStyleOf(RocketMultiplier) == EMoveStyle::Rocket);
	for (const float Normal : { 1.f, 1.25f, 2.f, 2.4f, 3.f })
	{
		TestTrue(FString::Printf(TEXT("Estilo normal con x%.2f"), Normal), MoveStyleOf(Normal) == EMoveStyle::Normal);
		TestEqual(FString::Printf(TEXT("x%.2f no se toca"), Normal), AvoidStyleValues(Normal), Normal);
	}
	TestTrue(TEXT("Un turbo justo en el valor de la ola se aparta"), MoveStyleOf(AvoidStyleValues(SurfMultiplier)) == EMoveStyle::Normal);
	TestTrue(TEXT("El cohete corre más que la ola, y la ola más que correr"), RocketMultiplier > SurfMultiplier && SurfMultiplier > 1.f);

	// La ola va siempre hacia el mar; las teclas solo la apartan un poco a los lados.
	const FVector Course(1.0, 0.0, 0.0);
	TestTrue(TEXT("Ola sin teclas: hacia el mar"), SurfHeading(Course, FVector::ZeroVector, SurfSteerShare).Equals(Course, 1.0e-4));
	TestTrue(TEXT("Ola hacia atrás: sigue hacia el mar"), SurfHeading(Course, FVector(-1.0, 0.0, 0.0), SurfSteerShare).Equals(Course, 1.0e-4));
	const FVector Right = SurfHeading(Course, FVector(0.0, 1.0, 0.0), SurfSteerShare);
	TestTrue(TEXT("Ola a la derecha: avanza y se aparta"), Right.X > 0.8 && Right.Y > 0.3);
	TestEqual(TEXT("Ola a la derecha: el ángulo del reparto"), FMath::Atan2(Right.Y, Right.X), FMath::Atan2(static_cast<double>(SurfSteerShare), 1.0), 1.0e-4);
	TestTrue(TEXT("Ola: unitario"), FMath::IsNearlyEqual(Right.Size(), 1.0, 1.0e-4));

	// El cohete gira como mucho lo que le toca en el paso.
	const float Step = FMath::DegreesToRadians(RocketTurnRateDeg) * 0.1f;
	const FVector Turned = RocketHeading(FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), Step);
	TestEqual(TEXT("Cohete: 4,5 grados en 0,1 s"), FMath::RadiansToDegrees(FMath::Atan2(Turned.Y, Turned.X)), static_cast<double>(RocketTurnRateDeg) * 0.1, 1.0e-3);
	const FVector Back = RocketHeading(FVector(1.0, 0.0, 0.0), FVector(-1.0, -0.01, 0.0), Step);
	TestTrue(TEXT("Cohete: dar la vuelta, poco a poco"), Back.X > 0.99);
	TestTrue(TEXT("Cohete sin teclas: recto"), RocketHeading(FVector(0.0, 2.0, 0.0), FVector::ZeroVector, Step).Equals(FVector(0.0, 1.0, 0.0), 1.0e-4));
	TestTrue(TEXT("Cohete dentro del giro: lo pedido"), RocketHeading(FVector(1.0, 0.0, 0.0), FVector(1.0, 0.01, 0.0), Step).Equals(FVector(1.0, 0.01, 0.0).GetSafeNormal(), 1.0e-4));

	// Pared de frente: se acaba la ola; de lado, una rampa o el suelo, no.
	TestTrue(TEXT("Pared de frente"), IsHeadOnWall(FVector(-1.0, 0.0, 0.0), Course, WalkableNormalZ, SurfHeadOnCos));
	TestTrue(TEXT("Pared algo torcida"), IsHeadOnWall(FVector(-0.9, 0.3, 0.1).GetSafeNormal(), Course, WalkableNormalZ, SurfHeadOnCos));
	TestFalse(TEXT("Pared de lado"), IsHeadOnWall(FVector(0.0, -1.0, 0.0), Course, WalkableNormalZ, SurfHeadOnCos));
	TestFalse(TEXT("Pared rozada"), IsHeadOnWall(FVector(-0.4, -0.9, 0.0).GetSafeNormal(), Course, WalkableNormalZ, SurfHeadOnCos));
	TestFalse(TEXT("Rampa que se sube"), IsHeadOnWall(FVector(-0.5, 0.0, 0.85).GetSafeNormal(), Course, WalkableNormalZ, SurfHeadOnCos));
	TestFalse(TEXT("Suelo"), IsHeadOnWall(FVector::UpVector, Course, WalkableNormalZ, SurfHeadOnCos));
	TestFalse(TEXT("Pared detrás"), IsHeadOnWall(FVector(1.0, 0.0, 0.0), Course, WalkableNormalZ, SurfHeadOnCos));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceNewItemsRodTest,
	"Tortunabo.Race.Items.Rod",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceNewItemsRodTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceItemRules;
	// A quién engancha: la más cercana de las de delante a 25 m o menos.
	TArray<FRodCandidate> Candidates;
	Candidates.Add({ 0.30f, 500.f });   // detrás: no
	Candidates.Add({ 0.60f, 2400.f });  // delante, a 24 m
	Candidates.Add({ 0.70f, 1200.f });  // delante, a 12 m: esta
	Candidates.Add({ 0.80f, 3000.f });  // delante, pero a 30 m: no
	TestEqual(TEXT("Caña: la más cercana de delante"), PickRodTarget(0.5f, Candidates, RodRange), 2);
	TestEqual(TEXT("Caña: la primera no tiene a nadie"), PickRodTarget(0.9f, Candidates, RodRange), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Caña: nadie a su alcance"), PickRodTarget(0.75f, Candidates, RodRange), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Caña: justo a 25 m, sí"), PickRodTarget(0.5f, TArray<FRodCandidate>({ { 0.6f, RodRange } }), RodRange), 0);
	TestEqual(TEXT("Caña: a la par, no"), PickRodTarget(0.5f, TArray<FRodCandidate>({ { 0.5f, 100.f } }), RodRange), static_cast<int32>(INDEX_NONE));

	// Dónde la deja: un poco por delante de donde estará la enganchada y a su lado.
	const FVector Course(1.0, 0.0, 0.0);
	const FVector Still = TowLanding(FVector(1000.0, 0.0, 0.0), FVector::ZeroVector, Course, 1.f, TowSeconds, TowOvertake, TowSide);
	TestTrue(TEXT("Remolque: por delante de la quieta"), FMath::IsNearlyEqual(Still.X, 1000.0 + TowOvertake, 0.01));
	TestTrue(TEXT("Remolque: a su lado"), FMath::IsNearlyEqual(Still.Y, static_cast<double>(TowSide), 0.01));
	const FVector Left = TowLanding(FVector(1000.0, 0.0, 0.0), FVector::ZeroVector, Course, -1.f, TowSeconds, TowOvertake, TowSide);
	TestTrue(TEXT("Remolque: al otro lado"), FMath::IsNearlyEqual(Left.Y, -static_cast<double>(TowSide), 0.01));
	const FVector Running = TowLanding(FVector(1000.0, 0.0, 0.0), FVector(800.0, 0.0, 300.0), Course, 1.f, TowSeconds, TowOvertake, TowSide);
	TestTrue(TEXT("Remolque: adelanta a la que corre"), FMath::IsNearlyEqual(Running.X, 1000.0 + 800.0 * TowSeconds + TowOvertake, 0.01));

	// El lanzamiento cae donde toca a los 1,5 s (con la gravedad de la tortuga).
	const FVector From(0.0, 0.0, 100.0);
	const FVector To(1800.0, 300.0, 160.0);
	constexpr float Gravity = -980.f;
	const FVector V = BallisticVelocity(From, To, TowSeconds, Gravity, TowMaxSpeed);
	const double T = TowSeconds;
	const FVector Landed = From + V * T + FVector(0.0, 0.0, 0.5 * Gravity * T * T);
	TestTrue(TEXT("Remolque: cae en el sitio"), Landed.Equals(To, 0.5));
	TestTrue(TEXT("Remolque: sube (va por el aire)"), V.Z > 0.0);
	const FVector Far = BallisticVelocity(From, FVector(10000.0, 0.0, 100.0), TowSeconds, Gravity, TowMaxSpeed);
	TestTrue(TEXT("Remolque: con tope de velocidad"), Far.Size2D() <= TowMaxSpeed + 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRaceNewItemsWhirlpoolTest,
	"Tortunabo.Race.Items.Whirlpool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRaceNewItemsWhirlpoolTest::RunTest(const FString& Parameters)
{
	using namespace TNRaceItemRules;
	// La espiral: empieza donde entró, acaba en el centro y se acerca sin alejarse nunca.
	const FVector Entry(200.0, 100.0, 0.0);
	TestTrue(TEXT("Espiral: empieza donde entró"), WhirlOffset(Entry, 0.f, WhirlPullSeconds, WhirlTurnsPerSecond).Equals(Entry, 0.01));
	TestTrue(TEXT("Espiral: acaba en el centro"), WhirlOffset(Entry, WhirlPullSeconds, WhirlPullSeconds, WhirlTurnsPerSecond).IsNearlyZero(0.01));
	double Last = Entry.Size2D();
	bool bCloser = true;
	bool bTurns = false;
	for (int32 Step = 1; Step <= 15; ++Step)
	{
		const float T = WhirlPullSeconds * Step / 15.f;
		const FVector Offset = WhirlOffset(Entry, T, WhirlPullSeconds, WhirlTurnsPerSecond);
		bCloser &= Offset.Size2D() <= Last + 0.01;
		bTurns |= FVector::DotProduct(Offset.GetSafeNormal2D(), Entry.GetSafeNormal2D()) < 0.0;
		Last = Offset.Size2D();
	}
	TestTrue(TEXT("Espiral: siempre más cerca"), bCloser);
	TestTrue(TEXT("Espiral: da vueltas"), bTurns);
	TestTrue(TEXT("Giro: da vueltas sobre sí misma"), !FMath::IsNearlyEqual(WhirlSpinYaw(0.f, 0.2f, WhirlPullSeconds, WhirlTurnsPerSecond), 0.f, 1.f));

	// A quién atrapa.
	FWhirlCatchView Base;
	Base.Distance2D = 100.f;
	Base.WhirlAge = 5.f;
	TestTrue(TEXT("Atrapa a la que entra"), CanWhirlCatch(Base));
	FWhirlCatchView View = Base;
	View.Distance2D = WhirlRadius + 1.f;
	TestFalse(TEXT("Fuera del radio, no"), CanWhirlCatch(View));
	View = Base;
	View.HeightGap = 400.f;
	TestFalse(TEXT("Saltando muy por encima, no"), CanWhirlCatch(View));
	View = Base;
	View.bIsOwner = true;
	View.WhirlAge = WhirlOwnerImmunity - 0.1f;
	TestFalse(TEXT("Quien lo suelta es inmune 2 s"), CanWhirlCatch(View));
	View.WhirlAge = WhirlOwnerImmunity + 0.1f;
	TestTrue(TEXT("Quien lo suelta, pasados 2 s, sí"), CanWhirlCatch(View));
	View = Base;
	View.SinceReleased = 1.f;
	TestFalse(TEXT("La que acaba de salir, no"), CanWhirlCatch(View));
	View.SinceReleased = WhirlRecatchSeconds + 0.1f;
	TestTrue(TEXT("La que salió hace rato, sí"), CanWhirlCatch(View));
	View = Base;
	View.bCanBeHit = false;
	TestFalse(TEXT("Con el protector solar (o aturdida), no"), CanWhirlCatch(View));
	View = Base;
	View.bBusy = true;
	TestFalse(TEXT("Ocupado con otra, no"), CanWhirlCatch(View));
	View = Base;
	View.bRaceLive = false;
	TestFalse(TEXT("Carrera parada, no"), CanWhirlCatch(View));

	// Lo que dura y lo que marea, como pide la issue.
	TestEqual(TEXT("Dura 12 s"), WhirlLifeSeconds, 12.f);
	TestEqual(TEXT("Atrae 1,5 s"), WhirlPullSeconds, 1.5f);
	TestEqual(TEXT("Marea 1 s"), WhirlDizzySeconds, 1.f);
	TestEqual(TEXT("Ola de 3 s"), SurfSeconds, 3.f);
	TestEqual(TEXT("Cohete de 2 s"), RocketSeconds, 2.f);
	TestEqual(TEXT("Caña a 25 m"), RodRange, 2500.f);
	TestEqual(TEXT("Remolque de 1,5 s"), TowSeconds, 1.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
