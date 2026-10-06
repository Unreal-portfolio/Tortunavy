// Objetos de movilidad de Todos contra Todos (#777): flotador y medusa trampolín. Reglas puras (TN_TctRules.h y
// TN_TctItemRules.h) y, en un mundo de juego sin ventana, el flotador en el componente de la tortuga y el bote de la medusa.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Tct.Items; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/TN_TctItemComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctItems.h"
#include "Game/TN_TctRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_TctJellyPad.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTctMobilityItemsTest
{
	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTctMobilityItemsTestWorld"));
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

	/** Lo más alto que llega un salto de VelocityZ con GravityZ, integrado como UCharacterMovementComponent (punto medio, 60 Hz). */
	double SimulatedApex(double VelocityZ, double GravityZ)
	{
		constexpr double Dt = 1.0 / 60.0;
		double Z = 0.0;
		double V = VelocityZ;
		double Top = 0.0;
		for (int32 Step = 0; Step < 600 && V > 0.0; ++Step)
		{
			const double Next = V + GravityZ * Dt;
			Z += 0.5 * (V + Next) * Dt;
			V = Next;
			Top = FMath::Max(Top, Z);
		}
		return Top;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsFloatRuleTest,
	"Tortunabo.Tct.Items.FlotadorRule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsFloatRuleTest::RunTest(const FString& Parameters)
{
	using namespace TNTctRules;
	const FTNTctItemSpec& Spec = TNTctItemRules::Spec(ETNTctItem::Flotador);
	TestEqual(TEXT("Una carga"), Spec.Charges, 1);
	TestTrue(TEXT("Entra en el sorteo"), Spec.PadWeight > 0.f);
	TestEqual(TEXT("Flota 4 s"), TNTctItemTuning::FloatSeconds, 4.f);

	// Por qué cae: el agua (lo que salva el flotador) o fuera de la arena.
	FTNTctArenaBounds Bounds;
	Bounds.Min = FVector(-1000.0, -1000.0, -200.0);
	Bounds.Max = FVector(1000.0, 1000.0, 400.0);
	FTNTctBody Body;
	Body.Location = FVector(0.0, 0.0, 90.0);
	TestEqual(TEXT("De pie y seca: no cae"), FallCause(Body, Bounds, -400.f), ETNTctFall::None);
	TestEqual(TEXT("El agua le pasa de los pies: agua"), FallCause(Body, Bounds, 60.f), ETNTctFall::Water);
	Body.Location = FVector(9000.0, 0.0, 90.0);
	TestEqual(TEXT("Lanzada lejos: fuera de la arena"), FallCause(Body, Bounds, -400.f), ETNTctFall::OutOfArena);

	FTNTctFloatState None;
	TestTrue(TEXT("Sin flotador, el agua elimina"), ResolveFall(ETNTctFall::Water, None, 10.0, 4.f, 1.2f));

	FTNTctFloatState State;
	State.bHasFloat = true;
	TestFalse(TEXT("Seca: nada"), ResolveFall(ETNTctFall::None, State, 5.0, 4.f, 1.2f));
	TestTrue(TEXT("Seca: el flotador sigue"), State.bHasFloat);
	TestFalse(TEXT("Con flotador, el agua no elimina"), ResolveFall(ETNTctFall::Water, State, 10.0, 4.f, 1.2f));
	TestFalse(TEXT("El flotador se gasta"), State.bHasFloat);
	TestEqual(TEXT("Flota hasta los 14 s"), State.FloatEnd, 14.0);
	TestFalse(TEXT("Flotando, el agua no elimina"), ResolveFall(ETNTctFall::Water, State, 12.0, 4.f, 1.2f));
	TestFalse(TEXT("Recién lanzada a tierra, aún a salvo"), ResolveFall(ETNTctFall::Water, State, 15.0, 4.f, 1.2f));
	TestTrue(TEXT("Pasado el respiro, el agua vuelve a eliminar"), ResolveFall(ETNTctFall::Water, State, 15.3, 4.f, 1.2f));
	FTNTctFloatState Floating = State;
	Floating.SafeUntil = 100.0;
	TestTrue(TEXT("Fuera de la arena elimina aunque flote"), ResolveFall(ETNTctFall::OutOfArena, Floating, 20.0, 4.f, 1.2f));

	// El punto seco más cercano y el lanzamiento hasta él.
	const TArray<FVector> Ground = { FVector(500.0, 0.0, 50.0), FVector(900.0, 0.0, 300.0), FVector(-2000.0, 0.0, 600.0) };
	FVector Dry;
	TestTrue(TEXT("Hay punto seco"), TNTctItemRules::NearestDryPoint(Ground, FVector::ZeroVector, 0.f, Dry));
	TestEqual(TEXT("El más cercano por encima del agua"), Dry, FVector(900.0, 0.0, 300.0));
	TestTrue(TEXT("Con el agua más alta, el siguiente"), TNTctItemRules::NearestDryPoint(Ground, FVector::ZeroVector, 350.f, Dry)
		&& Dry.Equals(FVector(-2000.0, 0.0, 600.0)));
	TestTrue(TEXT("Todo cubierto: el más alto"), TNTctItemRules::NearestDryPoint(Ground, FVector::ZeroVector, 5000.f, Dry)
		&& Dry.Equals(FVector(-2000.0, 0.0, 600.0)));
	TestFalse(TEXT("Sin suelo, nada"), TNTctItemRules::NearestDryPoint({}, FVector::ZeroVector, 0.f, Dry));

	const FVector From(0.0, 0.0, -50.0);
	const FVector To(900.0, 300.0, 300.0);
	const double GravityZ = -1960.0;
	const FVector Launch = TNTctItemRules::RescueLaunch(From, To, static_cast<float>(GravityZ));
	const double Seconds = FMath::Clamp(FVector::Dist2D(From, To) / TNTctItemTuning::RescueFlatSpeed,
		static_cast<double>(TNTctItemTuning::RescueMinSeconds), static_cast<double>(TNTctItemTuning::RescueMaxSeconds));
	FVector Where = From;
	FVector Velocity = Launch;
	constexpr double Dt = 1.0 / 120.0;
	for (double Time = 0.0; Time < Seconds - 1.e-6; Time += Dt)
	{
		const FVector Next = Velocity + FVector(0.0, 0.0, GravityZ * Dt);
		Where += 0.5 * (Velocity + Next) * Dt;
		Velocity = Next;
	}
	AddInfo(FString::Printf(TEXT("Rescate: llega a %.0f uu del punto seco"), FVector::Dist(Where, To)));
	TestTrue(TEXT("El lanzamiento la deja en el punto seco"), FVector::Dist(Where, To) < 30.0);
	TestTrue(TEXT("Sale hacia arriba"), Launch.Z > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsFloatWorldTest,
	"Tortunabo.Tct.Items.FlotadorWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsFloatWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNTctMobilityItemsTest;
	FPlayWorld Play;
	ATortugaCharacter* Turtle = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	UTN_TctItemComponent* Effects = UTN_TctItemComponent::FindOrAddOn(Turtle);
	UCharacterMovementComponent* Movement = Turtle ? Turtle->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Tortuga"), Turtle) || !TestNotNull(TEXT("Componente"), Effects) || !TestNotNull(TEXT("Movimiento"), Movement))
	{
		return false;
	}
	const UTN_InventoryComponent* Bag = Turtle->GetInventoryComponent();
	TestTrue(TEXT("Con la pala en la mano"), TNTctItems::GiveItem(Turtle, ETNTctItem::Shovel));
	TestTrue(TEXT("Coge el flotador"), TNTctItems::GiveItem(Turtle, ETNTctItem::Flotador));
	TestTrue(TEXT("Lo lleva"), Effects->HasFloat());
	TestTrue(TEXT("No ocupa la mano: sigue con la pala"), Bag && Bag->HasEquippedItem()
		&& TNTctItems::KindOf(Bag->GetEquippedItem()) == ETNTctItem::Shovel);
	TestFalse(TEXT("Ni el caparazón del inventario"), Bag && Bag->HasStoredItem());
	TestFalse(TEXT("Solo uno a la vez"), TNTctItems::GiveItem(Turtle, ETNTctItem::Flotador));

	const float Gravity = Movement->GravityScale;
	TestFalse(TEXT("Cae al agua: no la elimina"), Effects->ServerResolveFall(ETNTctFall::Water));
	TestFalse(TEXT("El flotador se gasta"), Effects->HasFloat());
	TestTrue(TEXT("Flota"), Effects->IsFloating());
	TestEqual(TEXT("Flotando, sin gravedad"), Movement->GravityScale, 0.f);
	TestFalse(TEXT("Flotando, aún no toca el rescate"), Effects->ServerTakeRescue());
	TestFalse(TEXT("Flotando, el agua no la elimina"), Effects->ServerResolveFall(ETNTctFall::Water));
	TestTrue(TEXT("Fuera de la arena, sí"), Effects->ServerResolveFall(ETNTctFall::OutOfArena));

	Effects->ClearEffects();
	TestFalse(TEXT("Ronda nueva: ni flota"), Effects->IsFloating());
	TestEqual(TEXT("Ronda nueva: su gravedad"), Movement->GravityScale, Gravity);
	TestTrue(TEXT("Ronda nueva: sin flotador, el agua elimina"), Effects->ServerResolveFall(ETNTctFall::Water));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTctItemsJellyTest,
	"Tortunabo.Tct.Items.MedusaTrampolin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTctItemsJellyTest::RunTest(const FString& Parameters)
{
	using namespace TNTctMobilityItemsTest;
	const FTNTctItemSpec& Spec = TNTctItemRules::Spec(ETNTctItem::MedusaTrampolin);
	TestEqual(TEXT("Una carga"), Spec.Charges, 1);
	TestTrue(TEXT("Entra en el sorteo"), Spec.PadWeight > 0.f);
	TestEqual(TEXT("Dura 15 s"), TNTctItemTuning::JellyLifeSeconds, 15.f);

	// Altura del bote con la gravedad de la tortuga de verdad (BP_TortugaCharacter): 6 m ± 1 m.
	double GravityZ = UPhysicsSettings::Get()->DefaultGravityZ;
	if (const UClass* TurtleClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Blueprints/Characters/BP_TortugaCharacter.BP_TortugaCharacter_C")))
	{
		const ACharacter* Default = TurtleClass->GetDefaultObject<ACharacter>();
		GravityZ *= Default && Default->GetCharacterMovement() ? Default->GetCharacterMovement()->GravityScale : 1.f;
	}
	else
	{
		AddWarning(TEXT("Sin BP_TortugaCharacter: se mide con la gravedad del motor."));
	}
	const double Apex = SimulatedApex(TNTctItemRules::BounceSpeed(TNTctItemTuning::JellyBounceHeight, static_cast<float>(GravityZ)), GravityZ);
	AddInfo(FString::Printf(TEXT("Gravedad de la tortuga %.0f cm/s²: bota %.0f uu"), GravityZ, Apex));
	TestTrue(TEXT("Bota 6 m ± 1 m"), Apex >= 500.0 && Apex <= 700.0);
	TestTrue(TEXT("Con la gravedad del motor, también"),
		FMath::Abs(SimulatedApex(TNTctItemRules::BounceSpeed(600.f, -980.f), -980.0) - 600.0) <= 100.0);

	const FVector Base(0.0, 0.0, 0.0);
	TestTrue(TEXT("Pisándola: bota"), TNTctItemRules::JellyTouches(Base, FVector(50.0, 0.0, 10.0), 0.f));
	TestTrue(TEXT("Cayendo encima: bota"), TNTctItemRules::JellyTouches(Base, FVector(0.0, 60.0, 40.0), -600.f));
	TestFalse(TEXT("Ya subiendo: no vuelve a botar"), TNTctItemRules::JellyTouches(Base, FVector(0.0, 0.0, 10.0), 900.f));
	TestFalse(TEXT("Al lado: no"), TNTctItemRules::JellyTouches(Base, FVector(150.0, 0.0, 0.0), 0.f));
	TestFalse(TEXT("Muy por encima: no"), TNTctItemRules::JellyTouches(Base, FVector(0.0, 0.0, 200.0), -100.f));

	// En el mundo (servidor): bota a quien la pisa, también a quien la plantó, una vez por pisada.
	FPlayWorld Play;
	ATortugaCharacter* Planter = SpawnTurtle(Play.World, FVector(0.0, 0.0, 200.0));
	if (!TestNotNull(TEXT("Tortuga"), Planter))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.Instigator = Planter;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = Planter->GetActorLocation() - FVector(0.0, 0.0, Planter->GetSimpleCollisionHalfHeight());
	ATN_TctJellyPad* Jelly = Play.World->SpawnActor<ATN_TctJellyPad>(ATN_TctJellyPad::StaticClass(), FTransform(Feet), Params);
	if (!TestNotNull(TEXT("Medusa"), Jelly))
	{
		return false;
	}
	// De pie en el suelo (en este mundo sin fotogramas, el movimiento aún no tiene modo).
	UCharacterMovementComponent* Movement = Planter->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	TestEqual(TEXT("Bota a quien la plantó"), Jelly->ServerBounceTurtles(), 1);
	const float Expected = TNTctItemRules::BounceSpeed(TNTctItemTuning::JellyBounceHeight, Movement->GetGravityZ());
	AddInfo(FString::Printf(TEXT("Lanzamiento pendiente: %.0f cm/s hacia arriba (esperado %.0f)"), Movement->PendingLaunchVelocity.Z, Expected));
	TestTrue(TEXT("Hacia arriba con la velocidad del bote"), FMath::IsNearlyEqual(Movement->PendingLaunchVelocity.Z, static_cast<double>(Expected), 1.0));
	TestEqual(TEXT("No la vuelve a botar en la misma pisada"), Jelly->ServerBounceTurtles(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
