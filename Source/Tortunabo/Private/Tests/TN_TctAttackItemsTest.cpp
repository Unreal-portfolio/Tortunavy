// Objetos de ataque y control de Todos contra Todos (#714): cocobomba, charco de alga y gaviota ladrona. Reglas puras
// (TN_TctItemRules.h) y, en un mundo de juego sin ventana, el resbalón del charco y el robo de la gaviota en el servidor.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_TctAlgaPuddle.h"
#include "World/TN_TctThiefGull.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctAttackItemsTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctAttackItemsTestWorld"));
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

	ATortugaCharacter* SpawnTurtle(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
	}

	/** Todas las cargas de Kind se gastan de una en una. */
	void CheckCharges(FAutomationTestBase& Test, ETNTctItem Kind, int32 Expected)
	{
		const FTNTctItemSpec& Spec = TNTctItemRules::Spec(Kind);
		Test.TestEqual(FString::Printf(TEXT("%s: cargas"), Spec.Code), Spec.Charges, Expected);
		Test.TestTrue(FString::Printf(TEXT("%s: entra en el sorteo"), Spec.Code), Spec.PadWeight > 0.f);
		Test.TestEqual(FString::Printf(TEXT("%s: de código"), Spec.Code), Spec.Source, ETNTctItemSource::Code);
		Test.TestFalse(FString::Printf(TEXT("%s: con nombre"), Spec.Code), TNTctItems::DisplayName(Kind).IsEmpty());
		FName Id = TNTctItemRules::MakeItemId(Kind, Spec.Charges);
		for (int32 Use = 0; Use < Expected; ++Use)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: queda carga %d"), Spec.Code, Use), Id.IsNone());
			Id = TNTctItemRules::ItemIdAfterUse(Id);
		}
		Test.TestTrue(FString::Printf(TEXT("%s: se gasta"), Spec.Code), Id.IsNone());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsCocobombaTest,
	"Tortunabo.Tct.Items.Cocobomba",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsCocobombaTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemTuning;
	TNTctAttackItemsTest::CheckCharges(*this, ETNTctItem::Cocobomba, 2);
	TestEqual(TEXT("Mecha de 2 s"), CocoFuseSeconds, 2.f);
	TestEqual(TEXT("Radio de 3,5 m"), CocoBlastRadius, 350.f);

	const FVector Center(0.0, 0.0, 0.0);
	FVector Near;
	FVector Far;
	FVector Outside;
	TestTrue(TEXT("A 1 m, la lanza"), TNTctItemRules::CocoBlast(Center, FVector(100.0, 0.0, 0.0), Near));
	TestTrue(TEXT("A 3,4 m, la lanza"), TNTctItemRules::CocoBlast(Center, FVector(0.0, 340.0, 0.0), Far));
	TestFalse(TEXT("A 3,6 m, no"), TNTctItemRules::CocoBlast(Center, FVector(360.0, 0.0, 0.0), Outside));
	TestTrue(TEXT("Hacia fuera de la explosión"), Near.X > 0.0 && FMath::Abs(Near.Y) < 1.0 && Far.Y > 0.0);
	TestTrue(TEXT("Hacia arriba"), Near.Z > 0.0 && Far.Z > 0.0);
	TestTrue(TEXT("Más fuerte cuanto más cerca"), Near.Size() > Far.Size());
	FVector Point;
	TNTctItemRules::CocoBlast(Center, FVector(30.0, 0.0, 0.0), Point);
	TestTrue(TEXT("A quemarropa, más que la pala"), Point.Size2D() > ShovelPush);
	FVector Under;
	TestTrue(TEXT("Justo encima, también (hacia un lado)"), TNTctItemRules::CocoBlast(Center, Center, Under) && Under.Size2D() > 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsAlgaTest,
	"Tortunabo.Tct.Items.Alga",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsAlgaTest::RunTest(const FString& Parameters)
{
	using namespace TNTctItemTuning;
	using namespace TNTctAttackItemsTest;
	CheckCharges(*this, ETNTctItem::Alga, 2);
	TestEqual(TEXT("Charco de 8 s"), AlgaPuddleSeconds, 8.f);
	TestEqual(TEXT("Charco de 3 m de ancho"), AlgaPuddleRadius * 2.f, 300.f);

	const FVector Center(0.0, 0.0, 100.0);
	TestTrue(TEXT("Pies en el centro: dentro"), TNTctItemRules::IsInPuddle(Center, AlgaPuddleRadius, FVector(0.0, 0.0, 100.0)));
	TestTrue(TEXT("Pies en el borde: dentro"), TNTctItemRules::IsInPuddle(Center, AlgaPuddleRadius, FVector(140.0, 0.0, 110.0)));
	TestFalse(TEXT("Fuera del charco"), TNTctItemRules::IsInPuddle(Center, AlgaPuddleRadius, FVector(170.0, 0.0, 100.0)));
	TestFalse(TEXT("Saltando por encima"), TNTctItemRules::IsInPuddle(Center, AlgaPuddleRadius, FVector(0.0, 0.0, 300.0)));

	FTNTctGrip Base;
	Base.GroundFriction = 8.f;
	Base.BrakingDeceleration = 2000.f;
	Base.MaxAcceleration = 1500.f;
	const FTNTctGrip Slip = TNTctItemRules::SlipperyGrip(Base);
	TestTrue(TEXT("Casi sin rozamiento (deriva al girar)"), Slip.GroundFriction <= Base.GroundFriction * 0.1f);
	TestTrue(TEXT("Casi sin frenada"), Slip.BrakingDeceleration <= Base.BrakingDeceleration * 0.1f);
	TestTrue(TEXT("Poca aceleración"), Slip.MaxAcceleration < Base.MaxAcceleration * 0.5f && Slip.MaxAcceleration > 0.f);
	TestTrue(TEXT("El salto se queda corto"), AlgaJumpMultiplier < 0.6f);

	// En el mundo: una tortuga dentro del charco resbala y al salir recupera su agarre.
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle);
	UCharacterMovementComponent* Movement = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Componente de objetos"), Effects) || !TestNotNull(TEXT("Movimiento"), Movement))
	{
		return false;
	}
	const float Friction = Movement->GroundFriction;
	const float Acceleration = Movement->MaxAcceleration;
	const float HalfHeight = Turtle->GetSimpleCollisionHalfHeight();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_TctAlgaPuddle* Puddle = Play.World->SpawnActor<ATN_TctAlgaPuddle>(ATN_TctAlgaPuddle::StaticClass(),
		FTransform(Turtle->GetActorLocation() - FVector(0.0, 0.0, HalfHeight)), Params);
	if (!TestNotNull(TEXT("Charco"), Puddle))
	{
		return false;
	}
	Puddle->Tick(0.1f);
	TestTrue(TEXT("Dentro: resbala"), Effects->IsSlipping());
	TestTrue(TEXT("Dentro: menos rozamiento"), Movement->GroundFriction < Friction * 0.2f);
	TestTrue(TEXT("Dentro: menos aceleración"), Movement->MaxAcceleration < Acceleration);

	Turtle->SetActorLocation(Turtle->GetActorLocation() + FVector(600.0, 0.0, 0.0));
	Puddle->Tick(0.1f);
	TestFalse(TEXT("Fuera: ya no resbala"), Effects->IsSlipping());
	TestEqual(TEXT("Fuera: su rozamiento"), Movement->GroundFriction, Friction);
	TestEqual(TEXT("Fuera: su aceleración"), Movement->MaxAcceleration, Acceleration);

	Turtle->SetActorLocation(Turtle->GetActorLocation() - FVector(600.0, 0.0, 0.0));
	Puddle->Tick(0.1f);
	TestTrue(TEXT("Vuelve a entrar: resbala"), Effects->IsSlipping());
	Puddle->Destroy();
	TestFalse(TEXT("Charco acabado: ya no resbala"), Effects->IsSlipping());
	TestEqual(TEXT("Charco acabado: su rozamiento"), Movement->GroundFriction, Friction);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsThiefGullTest,
	"Tortunabo.Tct.Items.GaviotaLadrona",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsThiefGullTest::RunTest(const FString& Parameters)
{
	using namespace TNTctAttackItemsTest;
	CheckCharges(*this, ETNTctItem::GaviotaLadrona, 1);

	const FVector Origin = FVector::ZeroVector;
	const TArray<FVector> Others = { FVector(1500.0, 0.0, 0.0), FVector(0.0, 900.0, 0.0), FVector(2500.0, 0.0, 0.0) };
	TestEqual(TEXT("La más cercana"), TNTctItemRules::PickThiefVictim(Origin, Others), 1);
	TestEqual(TEXT("A más de 20 m, nadie"), TNTctItemRules::PickThiefVictim(Origin, { FVector(2100.0, 0.0, 0.0) }), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Sin nadie, nadie"), TNTctItemRules::PickThiefVictim(Origin, {}), static_cast<int32>(INDEX_NONE));

	// En el mundo (servidor): la gaviota le quita la pala a la víctima y se la trae a quien la soltó.
	FPlayWorld Play;
	ATortugaCharacter* Thief = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	ATortugaCharacter* Victim = SpawnTurtle(Play.World, FVector(800.0, 0.0, 200.0));
	if (!TestNotNull(TEXT("Ladrona"), Thief) || !TestNotNull(TEXT("Víctima"), Victim))
	{
		return false;
	}
	TestFalse(TEXT("Sola en el mundo, sin víctima: no sale"), ATN_TctThiefGull::ServerLaunch(nullptr));
	TestTrue(TEXT("La víctima tiene la pala"), TNTctItems::GiveItem(Victim, ETNTctItem::Shovel));
	TestTrue(TEXT("Sale la gaviota"), ATN_TctThiefGull::ServerLaunch(Thief));
	ATN_TctThiefGull* Gull = nullptr;
	for (TActorIterator<ATN_TctThiefGull> It(Play.World); It; ++It) { Gull = *It; }
	if (!TestNotNull(TEXT("Gaviota en el mundo"), Gull))
	{
		return false;
	}
	for (int32 Step = 0; Step < 200 && Gull->GetPhase() != ETNTctGullPhase::Leave; ++Step)
	{
		Gull->Tick(0.05f);
	}
	const UTN_InventoryComponent* ThiefBag = Thief->GetInventoryComponent();
	const UTN_InventoryComponent* VictimBag = Victim->GetInventoryComponent();
	TestFalse(TEXT("La víctima se queda sin la pala"), VictimBag && VictimBag->HasEquippedItem());
	TestTrue(TEXT("La ladrona tiene la pala en la mano"), ThiefBag && ThiefBag->HasEquippedItem()
		&& TNTctItems::KindOf(ThiefBag->GetEquippedItem()) == ETNTctItem::Shovel);
	TestEqual(TEXT("Con todas sus cargas"), ThiefBag ? TNTctItems::ChargesOf(ThiefBag->GetEquippedItem()) : 0,
		TNTctItemRules::Spec(ETNTctItem::Shovel).Charges);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsAttackPadsTest,
	"Tortunabo.Tct.Items.AttackInPads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsAttackPadsTest::RunTest(const FString& Parameters)
{
	// Los tres salen en el sorteo de los puntos de objetos (barrido del dado sobre todos los objetos).
	const TArray<ETNTctItem> All = TNTctItemRules::AllKinds();
	TSet<ETNTctItem> Seen;
	for (int32 Step = 0; Step < 1000; ++Step)
	{
		Seen.Add(TNTctItemRules::PickPadItem(All, ETNTctItem::None, Step / 1000.f));
	}
	TestTrue(TEXT("Sale la cocobomba"), Seen.Contains(ETNTctItem::Cocobomba));
	TestTrue(TEXT("Sale el charco de alga"), Seen.Contains(ETNTctItem::Alga));
	TestTrue(TEXT("Sale la gaviota ladrona"), Seen.Contains(ETNTctItem::GaviotaLadrona));
	for (const ETNTctItem Kind : { ETNTctItem::Cocobomba, ETNTctItem::Alga, ETNTctItem::GaviotaLadrona })
	{
		TestTrue(FString::Printf(TEXT("%s se puede dar"), TNTctItemRules::Spec(Kind).Code), TNTctItems::AvailableKinds().Contains(Kind));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
