// Tortugas del escenario de estrés «caos» (TN_StressChaos.h): qué hace cada una y con qué entrada. Todas las acciones pasan por
// las funciones que llaman los Input Actions de ATortugaCharacter (Move, ToggleShell, TryInteract), que en un cliente acaban
// en los RPC de servidor de siempre y en el anfitrión, en sus implementaciones.

#include "Testing/TN_StressChaos.h"

#include "Components/PrimitiveComponent.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachStun.h"

namespace TNChaosDriverDetail
{
	/** Una tarea que pasa de esto se da por terminada (atascos...). */
	constexpr float TaskTimeout = 12.f;
	/** Distancia a la que la portadora pide coger a la compañera en su bola. */
	constexpr double GrabDistance = 220.0;
	/** El cebo se mete en su bola cuando su portadora está a menos de esto. */
	constexpr double BaitShellDistance = 600.0;

	/** Libre para hacer algo: viva, de pie o en su bola, sin derribo, aturdimiento ni que la lleven. */
	bool IsBusy(const ATortugaCharacter* Turtle)
	{
		const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
		return Turtle->IsDead() || Turtle->IsKnockedDown() || TNBeach::IsTurtleStunned(Turtle) || (Carry && Carry->IsBeingCarried());
	}
}

void UTN_StressChaosSubsystem::InputMove(ATortugaCharacter* Turtle, const FVector2D& Value) { Turtle->Move(FInputActionValue(Value)); }
void UTN_StressChaosSubsystem::InputShell(FDriver& Driver, ATortugaCharacter* Turtle)
{
	if (Driver.Shell.TryPress())
	{
		Turtle->ToggleShell();
	}
}
void UTN_StressChaosSubsystem::InputInteract(ATortugaCharacter* Turtle) { Turtle->TryInteract(); Turtle->ReleaseInteract(); }
void UTN_StressChaosSubsystem::InputJump(ATortugaCharacter* Turtle) { Turtle->Jump(); }

void UTN_StressChaosSubsystem::InputSprint(ATortugaCharacter* Turtle, bool bOn)
{
	if (bOn)
	{
		Turtle->StartSprint();
	}
	else
	{
		Turtle->StopSprint();
	}
}

void UTN_StressChaosSubsystem::Aim(FDriver& Driver, float Yaw)
{
	if (APlayerController* PC = Driver.Controller.Get())
	{
		PC->SetControlRotation(FRotator(-5.f, Yaw, 0.f));
	}
}

void UTN_StressChaosSubsystem::AimAt(FDriver& Driver, const ATortugaCharacter* Turtle, const FVector& Target)
{
	const FVector To = Target - Turtle->GetActorLocation();
	Aim(Driver, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X))));
}

void UTN_StressChaosSubsystem::SyncDrivers()
{
	Drivers.RemoveAll([](const FDriver& Driver) { return !Driver.Controller.IsValid(); });
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController() || Drivers.ContainsByPredicate([PC](const FDriver& Driver) { return Driver.Controller.Get() == PC; }))
		{
			continue;
		}
		FDriver& Driver = Drivers.AddDefaulted_GetRef();
		Driver.Controller = PC;
		Driver.Index = Drivers.Num() - 1;
		Driver.WanderYaw = Stream.FRandRange(0.f, 360.f);
	}
}

void UTN_StressChaosSubsystem::TickDrivers(float DeltaTime)
{
	for (FDriver& Driver : Drivers)
	{
		APlayerController* PC = Driver.Controller.Get();
		ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (!Turtle)
		{
			continue;
		}
		TrackTransitions(Driver, Turtle);
		TickDriver(Driver, Turtle, DeltaTime);
	}
}

void UTN_StressChaosSubsystem::TrackTransitions(FDriver& Driver, ATortugaCharacter* Turtle)
{
	const bool bInShell = Turtle->IsInShell();
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	const bool bCarrying = Carry && Carry->IsCarrying();
	FActions& Actions = CurrentActions();
	Actions.BallEntries += bInShell && !Driver.bWasInShell ? 1 : 0;
	Actions.Grabs += bCarrying && !Driver.bWasCarrying ? 1 : 0;
	Actions.Throws += !bCarrying && Driver.bWasCarrying ? 1 : 0;
	Driver.bWasInShell = bInShell;
	Driver.bWasCarrying = bCarrying;
}

UTN_StressChaosSubsystem::FDriver* UTN_StressChaosSubsystem::FindFreePartner(const FDriver& For)
{
	const APlayerController* ForPC = For.Controller.Get();
	const APawn* ForPawn = ForPC ? ForPC->GetPawn() : nullptr;
	if (bClientOnly || !ForPawn)
	{
		return nullptr;
	}
	// La libre más cercana: andando o sin haber empezado a rodar.
	FDriver* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FDriver& Other : Drivers)
	{
		const bool bFree = Other.Task == TNChaos::ETask::Wander || (Other.Task == TNChaos::ETask::Ball && Other.Stage == 0);
		const APlayerController* PC = Other.Controller.Get();
		const ATortugaCharacter* Turtle = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		if (&Other == &For || !bFree || !Turtle || TNChaosDriverDetail::IsBusy(Turtle))
		{
			continue;
		}
		const double Dist = FVector::DistSquared(Turtle->GetActorLocation(), ForPawn->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = &Other;
		}
	}
	return Best;
}

void UTN_StressChaosSubsystem::BeginTask(FDriver& Driver, ATortugaCharacter* Turtle)
{
	const uint8 Mask = Phases.IsValidIndex(CurrentPhase) ? Phases[CurrentPhase].Plan.Tasks : TNChaos::TaskBit(TNChaos::ETask::Wander);
	FDriver* Partner = TNChaos::HasTask(Mask, TNChaos::ETask::Carry) ? FindFreePartner(Driver) : nullptr;
	const TNChaos::ETask Focus = Phases.IsValidIndex(CurrentPhase) ? TNChaos::FocusOf(Phases[CurrentPhase].Plan.Step) : TNChaos::ETask::Count;
	Driver.Task = TNChaos::PickTask(Stream, Mask, Partner != nullptr, Focus);
	Driver.Stage = 0;
	Driver.TaskClock = 0.f;
	Driver.StageClock = 0.f;
	Driver.bBait = false;
	Driver.Partner.Reset();
	Driver.WanderYaw = Stream.FRandRange(0.f, 360.f);
	if (Driver.Task == TNChaos::ETask::Carry && Partner)
	{
		// La compañera deja lo que hacía y se mete en su bola a esperar que la cojan.
		ATortugaCharacter* Bait = Cast<ATortugaCharacter>(Partner->Controller->GetPawn());
		EndTask(*Partner, Bait);
		Partner->Task = TNChaos::ETask::Carry;
		Partner->bBait = true;
		Partner->Stage = 0;
		Partner->TaskClock = 0.f;
		Partner->StageClock = 0.f;
		Partner->Partner = Turtle;
		Driver.Partner = Bait;
	}
	UE_CLOG(bVerbose, LogTortunabo, Log, TEXT("[Estrés] caos: %s empieza «%s»%s."), *GetNameSafe(Turtle), TNChaos::TaskName(Driver.Task),
		Driver.Partner.IsValid() ? *FString::Printf(TEXT(" con %s de cebo"), *GetNameSafe(Driver.Partner.Get())) : TEXT(""));
}

void UTN_StressChaosSubsystem::EndTask(FDriver& Driver, ATortugaCharacter* Turtle)
{
	if (Turtle)
	{
		InputSprint(Turtle, false);
		Turtle->StopJumping();
		// Sale de la bola si se ha quedado dentro (la lanzada sale sola al pararse).
		if (Turtle->IsInShell() && !TNChaosDriverDetail::IsBusy(Turtle))
		{
			InputShell(Driver, Turtle);
		}
	}
	Driver.Task = TNChaos::ETask::Wander;
	Driver.bBait = false;
	Driver.Partner.Reset();
}

void UTN_StressChaosSubsystem::TickDriver(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime)
{
	Driver.TaskClock += DeltaTime;
	Driver.StageClock += DeltaTime;
	Driver.Shell.Tick(DeltaTime);
	if (Turtle->IsDead())
	{
		return;
	}
	// El cebo ya está en manos de su compañera (mientras la llevan no se le da entrada: moverse sería forcejear).
	const UTN_CarryComponent* SelfCarry = Turtle->GetCarryComponent();
	if (Driver.bBait && SelfCarry && SelfCarry->IsBeingCarried())
	{
		Driver.Stage = 1;
	}
	bool bDone = Driver.TaskClock > TNChaosDriverDetail::TaskTimeout;
	if (!bDone && !TNChaosDriverDetail::IsBusy(Turtle))
	{
		switch (Driver.Task)
		{
			case TNChaos::ETask::Carry:    bDone = TickCarry(Driver, Turtle, DeltaTime); break;
			case TNChaos::ETask::Ball:     bDone = TickBall(Driver, Turtle, DeltaTime); break;
			default:                       bDone = TickWander(Driver, Turtle, DeltaTime); break;
		}
	}
	if (bDone)
	{
		EndTask(Driver, Turtle);
		BeginTask(Driver, Turtle);
	}
}

bool UTN_StressChaosSubsystem::TickWander(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime)
{
	if (Turtle->IsInShell())
	{
		InputShell(Driver, Turtle);
		return false;
	}
	// Cambia de rumbo cada ~2 s, corre a ratos y salta de vez en cuando, como el monkey.
	if (Driver.StageClock > 2.f)
	{
		Driver.StageClock = 0.f;
		Driver.WanderYaw += Stream.FRandRange(-120.f, 120.f);
		InputSprint(Turtle, Stream.FRand() < 0.5f);
		if (Stream.FRand() < 0.4f)
		{
			InputJump(Turtle);
		}
	}
	Aim(Driver, Driver.WanderYaw);
	InputMove(Turtle, FVector2D(0.f, 1.f));
	return Driver.TaskClock > 5.f;
}

bool UTN_StressChaosSubsystem::TickBall(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime)
{
	if (Driver.Stage == 0)
	{
		if (!Turtle->IsInShell())
		{
			InputShell(Driver, Turtle);
		}
		Driver.Stage = 1;
		Driver.StageClock = 0.f;
		return false;
	}
	if (!Turtle->IsInShell())
	{
		// No ha podido entrar (o ha salido): se reintenta una vez por segundo.
		if (Driver.StageClock > 1.f)
		{
			Driver.Stage = 0;
		}
		return Driver.TaskClock > 6.f;
	}
	// Rodando: la bola sigue la entrada de movimiento; cambia de rumbo cada 1,5 s.
	if (FMath::Fmod(Driver.StageClock, 1.5f) < DeltaTime)
	{
		Driver.WanderYaw += Stream.FRandRange(-90.f, 90.f);
	}
	Aim(Driver, Driver.WanderYaw);
	InputMove(Turtle, FVector2D(0.f, 1.f));
	return Driver.TaskClock > 6.f;
}

bool UTN_StressChaosSubsystem::TickCarry(FDriver& Driver, ATortugaCharacter* Turtle, float DeltaTime)
{
	ATortugaCharacter* Partner = Driver.Partner.Get();
	UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	if (!Partner || !Carry)
	{
		return true;
	}
	if (Driver.bBait)
	{
		// Cebo: va hacia su portadora y, cerca, se mete en su bola y espera quieta. Termina cuando la han lanzado (la lanzada sale
		// sola de la bola al pararse). Entre dos pulsaciones del caparazón deja 1,5 s (la entrada tarda en verse).
		const UTN_CarryComponent* PartnerCarry = Partner->GetCarryComponent();
		const double ToCarrier = FVector::Dist2D(Partner->GetActorLocation(), Turtle->GetActorLocation());
		if (Driver.Stage == 0 && !Turtle->IsInShell() && ToCarrier > TNChaosDriverDetail::BaitShellDistance)
		{
			AimAt(Driver, Turtle, Partner->GetActorLocation());
			InputSprint(Turtle, true);
			InputMove(Turtle, FVector2D(0.f, 1.f));
		}
		else if (Driver.Stage == 0 && !Turtle->IsInShell() && Driver.StageClock > 1.5f)
		{
			Driver.StageClock = 0.f;
			InputSprint(Turtle, false);
			InputShell(Driver, Turtle);
		}
		if (Driver.Stage == 0 && PartnerCarry && PartnerCarry->GetCarriedTurtle() == Turtle)
		{
			Driver.Stage = 1;
		}
		// Su portadora ya no la busca (ha acabado o se ha rendido): deja de esperar.
		if (Driver.Stage == 0 && Driver.TaskClock > 10.f)
		{
			return true;
		}
		return Driver.Stage == 1 && !Turtle->IsInShell();
	}
	switch (Driver.Stage)
	{
		case 0:
		{
			if (Turtle->IsInShell())
			{
				InputShell(Driver, Turtle);
				return false;
			}
			// Hasta la compañera en su bola, corriendo hasta los últimos metros.
			const double ToBait = FVector::Dist2D(Partner->GetActorLocation(), Turtle->GetActorLocation());
			AimAt(Driver, Turtle, Partner->GetActorLocation());
			InputSprint(Turtle, ToBait > 400.0);
			InputMove(Turtle, FVector2D(0.f, ToBait > 400.0 ? 1.f : 0.6f));
			UE_CLOG(bVerbose && FMath::Fmod(Driver.TaskClock, 1.f) < DeltaTime, LogTortunabo, Log, TEXT("[Estrés] caos: %s va a por %s (bola %d, a %.0f cm)."),
				*GetNameSafe(Turtle), *GetNameSafe(Partner), Partner->IsInShell() ? 1 : 0, FVector::Dist2D(Partner->GetActorLocation(), Turtle->GetActorLocation()));
			// La caja del caparazón no deja pegarse del todo: se pide a menos de GrabRange (240 cm), cada 0,3 s.
			if (Partner->IsInShell() && FVector::Dist2D(Partner->GetActorLocation(), Turtle->GetActorLocation()) < TNChaosDriverDetail::GrabDistance
				&& Driver.StageClock > 0.3f)
			{
				Driver.StageClock = 0.f;
				// Lo mismo que hace TryInteract sin interactuable delante (con uno delante, la E interactuaría con él).
				Carry->TryGrabNearest();
				UE_CLOG(bVerbose, LogTortunabo, Log, TEXT("[Estrés] caos: %s pulsa coger (%s en su bola a %.0f cm); lleva: %d."), *GetNameSafe(Turtle),
					*GetNameSafe(Partner), FVector::Dist2D(Partner->GetActorLocation(), Turtle->GetActorLocation()), Carry->IsCarrying() ? 1 : 0);
				Driver.Stage = 1;
				Driver.StageClock = 0.f;
			}
			return false;
		}
		case 1:
			if (Carry->IsCarrying())
			{
				Driver.Stage = 2;
				Driver.StageClock = 0.f;
			}
			else if (Driver.StageClock > 1.f)
			{
				Driver.Stage = 0;
			}
			return false;
		case 2:
			// La lleva un momento andando y la lanza hacia cualquier lado.
			InputMove(Turtle, FVector2D(0.f, 0.6f));
			if (Driver.StageClock > 0.8f)
			{
				Aim(Driver, Stream.FRandRange(0.f, 360.f));
				InputInteract(Turtle);
				Driver.Stage = 3;
				Driver.StageClock = 0.f;
			}
			return false;
		default:
			return !Carry->IsCarrying() && Driver.StageClock > 1.f;
	}
}

