// Punto de mira del dueño en los lanzamientos (#894): el cliente manda el punto del centro de su pantalla y el servidor lo
// acepta si cae en un cono alrededor de la rotación de control (TNThrowAim::IsClientAimPointValid). Antes el servidor lo
// calculaba con su copia de la cámara, sin los ajustes locales, y el lanzamiento de un cliente remoto se desviaba.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Throw.ClientAim; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TN_ThrowAim.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNThrowClientAimTest
{
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNThrowClientAimTestWorld"));
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNThrowClientAimConeTest,
	"Tortunabo.Throw.ClientAim.Cone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNThrowClientAimConeTest::RunTest(const FString& Parameters)
{
	const FVector Origin(100.0, -50.0, 300.0);
	const FVector Forward(1.0, 0.0, 0.0);
	const double Dist = 2000.0;
	auto At = [&](double AngleDeg)
	{
		return Origin + FRotator(0.f, static_cast<float>(AngleDeg), 0.f).Vector() * Dist;
	};

	// Dentro del cono: de frente y cerca del borde (la cámara del cliente no está donde la del servidor).
	TestTrue(TEXT("De frente vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(0.0)));
	TestTrue(TEXT("A 20 grados vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(20.0)));
	TestTrue(TEXT("Justo dentro del borde vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(TNThrowAim::MaxAngleDeg - 0.5)));
	TestTrue(TEXT("Hacia abajo, al suelo cercano, vale"),
		TNThrowAim::IsClientAimPointValid(Origin, FRotator(-30.f, 0.f, 0.f).Vector(), Origin + FRotator(-40.f, 0.f, 0.f).Vector() * 400.0));

	// Fuera del cono: a un lado, detrás.
	TestFalse(TEXT("Justo fuera del borde no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(TNThrowAim::MaxAngleDeg + 0.5)));
	TestFalse(TEXT("A 90 grados no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(90.0)));
	TestFalse(TEXT("Detrás no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, At(180.0)));

	// Fuera de alcance, pegado al origen o con NaN: no vale.
	const double MaxRange = TNThrowAim::AimRangeCm + TNThrowAim::AimRangeSlackCm;
	TestTrue(TEXT("El punto lejano del rayo vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, Origin + Forward * TNThrowAim::AimRangeCm));
	TestFalse(TEXT("Más allá del alcance no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, Origin + Forward * (MaxRange + 10.0)));
	TestFalse(TEXT("En el origen no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, Origin));
	TestFalse(TEXT("Con NaN no vale"), TNThrowAim::IsClientAimPointValid(Origin, Forward, FVector(NAN, 0.0, 0.0)));
	TestFalse(TEXT("Sin dirección no vale"), TNThrowAim::IsClientAimPointValid(Origin, FVector::ZeroVector, At(0.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNThrowClientAimTurtleTest,
	"Tortunabo.Throw.ClientAim.Turtle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNThrowClientAimTurtleTest::RunTest(const FString& Parameters)
{
	TNThrowClientAimTest::FPlayWorld Play;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATortugaCharacter* Turtle = Play.World->SpawnActor<ATortugaCharacter>(ATortugaCharacter::StaticClass(),
		FVector(0.0, 0.0, 200.0), FRotator(0.f, 90.f, 0.f), Params);
	if (!TestNotNull(TEXT("Se crea la tortuga"), Turtle)) { return false; }
	const UCameraComponent* Camera = Turtle->FindComponentByClass<UCameraComponent>();
	if (!TestNotNull(TEXT("La tortuga tiene cámara"), Camera)) { return false; }

	// Sin controlador, la rotación de control es la del actor (mira a +Y).
	const FVector CamLoc = Camera->GetComponentLocation();
	const FVector Ahead = CamLoc + FVector(0.0, 1500.0, -100.0);
	const FVector Behind = CamLoc + FVector(0.0, -1500.0, 0.0);
	const FVector Side = CamLoc + FVector(1500.0, 0.0, 0.0);

	const TOptional<FVector> AheadPoint = Turtle->ValidateClientAimPoint(Ahead, true);
	TestTrue(TEXT("El punto de delante se acepta"), AheadPoint.IsSet());
	TestTrue(TEXT("Y es el que mandó el cliente"), AheadPoint.IsSet() && AheadPoint.GetValue().Equals(Ahead, 0.01));
	TestFalse(TEXT("El de detrás se rechaza"), Turtle->ValidateClientAimPoint(Behind, true).IsSet());
	TestFalse(TEXT("El de un lado se rechaza"), Turtle->ValidateClientAimPoint(Side, true).IsSet());
	TestFalse(TEXT("Sin punto no hay nada que validar"), Turtle->ValidateClientAimPoint(Ahead, false).IsSet());

	// El lanzamiento al punto aceptado va hacia él (en horizontal).
	const FVector Start = Turtle->GetActorLocation();
	const FVector Dir = Turtle->GetThrowDirectionToPoint(Start, Ahead, Turtle->GetTurtleAimRotation(), 1500.f);
	const FVector FlatDir = FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal();
	const FVector FlatToPoint = FVector(Ahead.X - Start.X, Ahead.Y - Start.Y, 0.0).GetSafeNormal();
	TestTrue(TEXT("El lanzamiento sale hacia el punto"), FVector::DotProduct(FlatDir, FlatToPoint) > 0.999);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
