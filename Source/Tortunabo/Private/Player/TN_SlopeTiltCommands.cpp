// Comandos de consola para probar la inclinación con la pendiente (#586). Solo fuera de Shipping.
//   TN.SlopeTilt.Find [lado|frente] [espera_s] [captura]: pone la tortuga local en la cuesta de 15-30° más cercana
//     (hasta 120 m), de lado o de frente a la pendiente, con la cámara donde se ve la inclinación; con captura = 1,
//     2,5 s después hace HighResShot y escribe el estado. Con espera_s, lo hace pasado ese tiempo (para -ExecCmds).
//   TN.SlopeTilt.Dump [espera_s]: escribe en el registro la inclinación de cada tortuga en esta máquina.
//   TN.SlopeTilt.DiveProbe [abajo|arriba] [espera_s] [captura]: corre hacia la cuesta más cercana, salta y hace el panzazo;
//     escribe por fotograma la malla respecto al suelo (vuelo, aterrizaje y deslizamiento) y el mayor giro en un fotograma.

#include "Player/TN_SlopeTiltComponent.h"
#include "Player/TortugaCharacter.h"

#include "Core/TN_Log.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace TNSlopeTiltCommands
{
	constexpr float SearchRadiusCm = 12000.f;
	constexpr float SearchStepCm = 200.f;
	constexpr float MinSlopeDeg = 15.f;
	constexpr float MaxSlopeDeg = 30.f;
	constexpr float NeighbourOffsetCm = 60.f;
	constexpr float NeighbourMaxDeg = 4.f;
	constexpr float ShotDelaySeconds = 2.5f;

	float SlopeDeg(const FVector& Normal)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)));
	}

	bool TraceGround(UWorld& World, const ACharacter& Character, const FVector& At, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNSlopeTiltFind), false, &Character);
		const FVector Start = At + FVector(0.f, 0.f, 5000.f);
		const FVector End = At - FVector(0.f, 0.f, 5000.f);
		return World.LineTraceSingleByChannel(OutHit, Start, End, ECC_Pawn, Params);
	}

	/** Cuesta andable y uniforme (los cuatro vecinos con casi la misma normal) en At. */
	bool IsGoodSlope(UWorld& World, const ACharacter& Character, const FVector& At, FHitResult& OutHit)
	{
		const UCharacterMovementComponent* Move = Character.GetCharacterMovement();
		if (!TraceGround(World, Character, At, OutHit) || !Move || !Move->IsWalkable(OutHit))
		{
			return false;
		}
		const float Deg = SlopeDeg(OutHit.ImpactNormal);
		if (Deg < MinSlopeDeg || Deg > MaxSlopeDeg)
		{
			return false;
		}
		const FVector Offsets[] = { {NeighbourOffsetCm, 0.f, 0.f}, {-NeighbourOffsetCm, 0.f, 0.f}, {0.f, NeighbourOffsetCm, 0.f}, {0.f, -NeighbourOffsetCm, 0.f} };
		for (const FVector& Offset : Offsets)
		{
			FHitResult Near;
			if (!TraceGround(World, Character, OutHit.ImpactPoint + Offset, Near)
				|| FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Near.ImpactNormal, OutHit.ImpactNormal), -1.0, 1.0))) > NeighbourMaxDeg)
			{
				return false;
			}
		}
		return true;
	}

	bool FindSlope(UWorld& World, const ACharacter& Character, FHitResult& OutHit)
	{
		const FVector Origin = Character.GetActorLocation();
		for (float Radius = 0.f; Radius <= SearchRadiusCm; Radius += SearchStepCm)
		{
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(2.f * UE_PI * Radius / SearchStepCm));
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float Angle = 2.f * UE_PI * Step / Steps;
				const FVector At = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
				if (IsGoodSlope(World, Character, At, OutHit))
				{
					return true;
				}
			}
		}
		return false;
	}

	void Dump(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			const UTN_SlopeTiltComponent* Tilt = It->FindComponentByClass<UTN_SlopeTiltComponent>();
			const USkeletalMeshComponent* Mesh = It->GetMesh();
			if (!Tilt || !Mesh)
			{
				continue;
			}
			// Ángulo real entre la vertical y el eje Z de la malla (lo que se ve; de pie, sin panzazo).
			const float VisibleDeg = SlopeDeg(Mesh->GetUpVector());
			UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt] %s rol=%s local=%s inclinación=(cabeceo %.1f, alabeo %.1f) objetivo=(%.1f, %.1f) malla=%.1f°"),
				*It->GetName(), *UEnum::GetValueAsString(It->GetLocalRole()), It->IsLocallyControlled() ? TEXT("sí") : TEXT("no"),
				Tilt->GetCurrentTilt().Pitch, Tilt->GetCurrentTilt().Roll, Tilt->GetTargetTilt().Pitch, Tilt->GetTargetTilt().Roll, VisibleDeg);
		}
	}

	void Place(UWorld* World, bool bSideways, bool bShot)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		FHitResult Hit;
		if (!Character || !FindSlope(*World, *Character, Hit))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[SlopeTilt] Sin tortuga local o sin cuesta de %.0f-%.0f° a menos de %.0f m."), MinSlopeDeg, MaxSlopeDeg, SearchRadiusCm / 100.f);
			return;
		}

		// La normal apunta, en horizontal, cuesta abajo: de frente mira cuesta arriba; de lado, la cuesta sube por la izquierda.
		const FVector2D Downhill = FVector2D(Hit.ImpactNormal.X, Hit.ImpactNormal.Y).GetSafeNormal();
		const float UphillYaw = FMath::RadiansToDegrees(FMath::Atan2(-Downhill.Y, -Downhill.X));
		const float Yaw = bSideways ? UphillYaw + 90.f : UphillYaw;
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(Hit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 10.f), FRotator(0.f, Yaw, 0.f));
		Character->GetCharacterMovement()->StopMovementImmediately();
		// De lado se ve el alabeo desde detrás; de frente, el cabeceo desde un costado.
		PC->SetControlRotation(FRotator(-12.f, bSideways ? Yaw : Yaw - 90.f, 0.f));
		UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt] Tortuga en una cuesta de %.1f° (%s) en %s, rumbo %.0f°."),
			SlopeDeg(Hit.ImpactNormal), bSideways ? TEXT("de lado") : TEXT("de frente"), *Hit.ImpactPoint.ToCompactString(), Yaw);

		if (!bShot)
		{
			return;
		}
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TWeakObjectPtr<APlayerController> WeakPC(PC);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, WeakPC]()
		{
			UWorld* ShotWorld = WeakWorld.Get();
			APlayerController* ShotPC = WeakPC.Get();
			if (ShotWorld && ShotPC)
			{
				Dump(ShotWorld);
				// Por la consola del jugador: HighResShot lo atiende el viewport, no UEngine::Exec.
				ShotPC->ConsoleCommand(TEXT("HighResShot 1"));
				UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt] Captura pedida."));
			}
		}), ShotDelaySeconds, false);
	}

	/** El mundo de juego de ahora: el de la consola si sigue vivo; si no (se ha viajado de mapa), el primero de juego. */
	UWorld* CurrentGameWorld(const TWeakObjectPtr<UWorld>& Preferred)
	{
		if (UWorld* World = Preferred.Get())
		{
			return World;
		}
		for (const FWorldContext& Context : GEngine ? GEngine->GetWorldContexts() : TIndirectArray<FWorldContext>())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Ejecuta Action pasados DelaySeconds de tiempo real, aunque entretanto se viaje a otro mapa (un cliente al unirse). */
	void RunLater(UWorld* World, float DelaySeconds, TFunction<void(UWorld*)> Action)
	{
		if (DelaySeconds <= 0.f)
		{
			if (World)
			{
				Action(World);
			}
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Action](float)
		{
			if (UWorld* LaterWorld = CurrentGameWorld(WeakWorld))
			{
				Action(LaterWorld);
			}
			return false;
		}), DelaySeconds);
	}

	void FindFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		const bool bSideways = Args.Num() < 1 || !Args[0].Equals(TEXT("frente"), ESearchCase::IgnoreCase);
		const float Delay = Args.Num() >= 2 ? FCString::Atof(*Args[1]) : 0.f;
		const bool bShot = Args.Num() >= 3 && FCString::Atoi(*Args[2]) != 0;
		RunLater(World, Delay, [bSideways, bShot](UWorld* LaterWorld) { Place(LaterWorld, bSideways, bShot); });
	}

	void DumpFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		const float Delay = Args.Num() >= 1 ? FCString::Atof(*Args[0]) : 0.f;
		RunLater(World, Delay, [](UWorld* LaterWorld) { Dump(LaterWorld); });
	}

	FAutoConsoleCommandWithWorldAndArgs FindCommand(
		TEXT("TN.SlopeTilt.Find"),
		TEXT("TN.SlopeTilt.Find [lado|frente] [espera_s] [captura 0|1]: pone la tortuga local en la cuesta de 15-30° más cercana ")
		TEXT("(hasta 120 m), de lado (por defecto) o de frente, con la cámara donde se ve la inclinación; con captura = 1, a los ")
		TEXT("2,5 s escribe el estado y hace HighResShot. En el anfitrión o sin red."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FindFromConsole));

	FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("TN.SlopeTilt.Dump"),
		TEXT("TN.SlopeTilt.Dump [espera_s]: escribe en el registro la inclinación (actual, objetivo y la de la malla) de cada tortuga en esta máquina."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpFromConsole));

	/** Estado por fotograma del panzazo de prueba (TN.SlopeTilt.DiveProbe). */
	struct FDiveProbeState
	{
		TWeakObjectPtr<ATortugaCharacter> Character;
		FVector LongLocal = FVector::UpVector;
		FVector SideLocal = FVector::RightVector;
		FQuat LastRelative = FQuat::Identity;
		FRotator LastTilt = FRotator::ZeroRotator;
		FVector Dir = FVector::ForwardVector;
		double StartSeconds = 0.0;
		double DiveSeconds = -1.0;
		double LandSeconds = -1.0;
		float MaxStepDeg = 0.f;
		float MaxStepAfterLandDeg = 0.f;
		float MaxTiltStepDeg = 0.f;
		int32 Frame = 0;
		bool bFlightShot = false;
		bool bSlopeShot = false;
		bool bShots = false;
	};

	/** Ángulo (grados, con signo) entre el eje Axis y el plano de normal Normal: 0 = paralelo. */
	float AxisToPlaneDeg(const FVector& Axis, const FVector& Normal)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Axis.GetSafeNormal(), Normal.GetSafeNormal()), -1.0, 1.0)));
	}

	/** Un fotograma del panzazo de prueba: escribe la malla respecto al suelo y hace las capturas. False al acabar. */
	bool TickDiveProbe(const TSharedRef<FDiveProbeState>& State)
	{
		ATortugaCharacter* Character = State->Character.Get();
		UWorld* World = Character ? Character->GetWorld() : nullptr;
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
		const UTN_SlopeTiltComponent* Tilt = Character ? Character->FindComponentByClass<UTN_SlopeTiltComponent>() : nullptr;
		if (!World || !Mesh || !Move || !Tilt)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds() - State->StartSeconds;
		if (Now > 4.5)
		{
			UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt][DiveProbe] Fin: salto máximo por fotograma %.2f° (tras aterrizar %.2f°); de la inclinación, %.2f°."),
				State->MaxStepDeg, State->MaxStepAfterLandDeg, State->MaxTiltStepDeg);
			return false;
		}

		FHitResult Ground;
		const bool bGround = TraceGround(*World, *Character, Character->GetActorLocation(), Ground);
		const FVector FloorNormal = bGround ? FVector(Ground.ImpactNormal) : FVector::UpVector;
		const FQuat MeshWorld = Mesh->GetComponentQuat();
		const FVector LongWorld = MeshWorld.RotateVector(State->LongLocal);
		const FVector SideWorld = MeshWorld.RotateVector(State->SideLocal);
		const FQuat Relative = Mesh->GetRelativeRotation().Quaternion();
		const float StepDeg = State->Frame == 0 ? 0.f : static_cast<float>(FMath::RadiansToDegrees(State->LastRelative.AngularDistance(Relative)));
		State->LastRelative = Relative;
		// Lo que mueve la inclinación este fotograma (sin el giro propio del panzazo).
		const FRotator CurrentTilt = Tilt->GetCurrentTilt();
		const float TiltStepDeg = State->Frame == 0 ? 0.f : static_cast<float>(FMath::RadiansToDegrees(State->LastTilt.Quaternion().AngularDistance(CurrentTilt.Quaternion())));
		State->LastTilt = CurrentTilt;
		++State->Frame;

		// Corre hacia la cuesta hasta el panzazo: el panzazo sigue la última dirección pulsada (la cámara va de costado).
		if (State->DiveSeconds < 0.0 && Now > 0.5)
		{
			Character->AddMovementInput(State->Dir, 1.f);
		}
		const bool bOnGround = Move->IsMovingOnGround();
		const bool bDiving = Character->IsDiving();
		if (bDiving && State->DiveSeconds < 0.0)
		{
			State->DiveSeconds = Now;
		}
		const bool bDived = State->DiveSeconds >= 0.0;
		if (bDived && bOnGround && State->LandSeconds < 0.0)
		{
			State->LandSeconds = Now;
		}
		// El giro propio de la entrada al panzazo (0,15 s) no cuenta: lo que se mide es el aterrizaje y el deslizamiento.
		if (bDived && Now - State->DiveSeconds > 0.15)
		{
			State->MaxStepDeg = FMath::Max(State->MaxStepDeg, StepDeg);
		}
		if (State->LandSeconds >= 0.0 && bDiving)
		{
			State->MaxStepAfterLandDeg = FMath::Max(State->MaxStepAfterLandDeg, StepDeg);
			State->MaxTiltStepDeg = FMath::Max(State->MaxTiltStepDeg, TiltStepDeg);
		}

		UE_LOG(LogTortunabo, Log,
			TEXT("[SlopeTilt][DiveProbe] t=%.3f %s panzazo=%d suelo=%.1f° largo/horizontal=%.1f° largo/suelo=%.1f° lado/suelo=%.1f° inclinación=(%.1f, %.1f) objetivo=(%.1f, %.1f) paso=%.2f° (inclinación %.2f°) v=%.0f"),
			Now, bOnGround ? TEXT("suelo") : (Move->IsFalling() ? TEXT("aire ") : TEXT("otro ")), bDiving ? 1 : 0,
			SlopeDeg(FloorNormal), AxisToPlaneDeg(LongWorld, FVector::UpVector), AxisToPlaneDeg(LongWorld, FloorNormal),
			AxisToPlaneDeg(SideWorld, FloorNormal), Tilt->GetCurrentTilt().Pitch, Tilt->GetCurrentTilt().Roll,
			Tilt->GetTargetTilt().Pitch, Tilt->GetTargetTilt().Roll, StepDeg, TiltStepDeg, Move->Velocity.Size());

		APlayerController* PC = Cast<APlayerController>(Character->GetController());
		if (State->bShots && PC && bDiving)
		{
			if (!State->bFlightShot && !bOnGround && Now - State->DiveSeconds > 0.12)
			{
				State->bFlightShot = true;
				PC->ConsoleCommand(TEXT("HighResShot 1"));
				UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt][DiveProbe] Captura de vuelo pedida (t=%.3f)."), Now);
			}
			if (!State->bSlopeShot && State->LandSeconds >= 0.0 && Now - State->LandSeconds > 0.35)
			{
				State->bSlopeShot = true;
				PC->ConsoleCommand(TEXT("HighResShot 1"));
				UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt][DiveProbe] Captura en la cuesta pedida (t=%.3f)."), Now);
			}
		}
		return true;
	}

	/**
	 * Panzazo de prueba hacia una cuesta: pone la tortuga 4 m antes de la cuesta más cercana, mirando hacia ella (cuesta
	 * abajo o arriba), salta, hace el panzazo (doble salto) y escribe por fotograma la malla respecto al suelo.
	 */
	void DiveProbe(UWorld* World, bool bDownhill, bool bShots)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		ATortugaCharacter* Character = PC ? Cast<ATortugaCharacter>(PC->GetPawn()) : nullptr;
		FHitResult Hit;
		if (!Character || !FindSlope(*World, *Character, Hit))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[SlopeTilt][DiveProbe] Sin tortuga local o sin cuesta cerca."));
			return;
		}
		const FVector Downhill = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		const FVector Dir = bDownhill ? Downhill : -Downhill;
		FHitResult StartHit;
		if (!TraceGround(*World, *Character, Hit.ImpactPoint - Dir * 400.f, StartHit))
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[SlopeTilt][DiveProbe] Sin suelo en el punto de salida."));
			return;
		}
		const float Yaw = Dir.Rotation().Yaw;
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->TeleportTo(StartHit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f), FRotator(0.f, Yaw, 0.f));
		Character->GetCharacterMovement()->StopMovementImmediately();
		// De costado, para ver el cabeceo en las capturas.
		PC->SetControlRotation(FRotator(-5.f, Yaw - 90.f, 0.f));
		UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt][DiveProbe] Cuesta de %.1f° en %s; panzazo %s desde %s, rumbo %.0f°."),
			SlopeDeg(Hit.ImpactNormal), *Hit.ImpactPoint.ToCompactString(), bDownhill ? TEXT("cuesta abajo") : TEXT("cuesta arriba"),
			*StartHit.ImpactPoint.ToCompactString(), Yaw);

		// Ejes de la malla de pie (giro relativo del objeto por defecto): el largo (pies → cabeza) y el de lado.
		const ACharacter* Default = Character->GetClass()->GetDefaultObject<ACharacter>();
		const FQuat RestRot = Default && Default->GetMesh() ? Default->GetMesh()->GetRelativeRotation().Quaternion() : FQuat::Identity;
		TSharedRef<FDiveProbeState> State = MakeShared<FDiveProbeState>();
		State->Character = Character;
		State->LongLocal = RestRot.Inverse().RotateVector(FVector::UpVector);
		State->SideLocal = RestRot.Inverse().RotateVector(FVector::RightVector);
		State->StartSeconds = World->GetTimeSeconds();
		State->bShots = bShots;
		State->Dir = Dir;

		TWeakObjectPtr<ATortugaCharacter> WeakCharacter(Character);
		FTimerHandle JumpHandle;
		World->GetTimerManager().SetTimer(JumpHandle, FTimerDelegate::CreateLambda([WeakCharacter]()
		{
			if (ATortugaCharacter* C = WeakCharacter.Get())
			{
				static_cast<ACharacter*>(C)->Jump();
			}
		}), 1.f, false);
		FTimerHandle DiveHandle;
		World->GetTimerManager().SetTimer(DiveHandle, FTimerDelegate::CreateLambda([WeakCharacter]()
		{
			ATortugaCharacter* C = WeakCharacter.Get();
			if (!C)
			{
				return;
			}
			C->StopJumping();
			// Por la clase base: el Jump de la tortuga es protegido (el doble salto llama a TryDive).
			static_cast<ACharacter*>(C)->Jump();
		}), 1.25f, false);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return TickDiveProbe(State);
		}));
	}

	void DiveProbeFromConsole(const TArray<FString>& Args, UWorld* World)
	{
		const bool bDownhill = Args.Num() < 1 || !Args[0].Equals(TEXT("arriba"), ESearchCase::IgnoreCase);
		const float Delay = Args.Num() >= 2 ? FCString::Atof(*Args[1]) : 0.f;
		const bool bShots = Args.Num() >= 3 && FCString::Atoi(*Args[2]) != 0;
		RunLater(World, Delay, [bDownhill, bShots](UWorld* LaterWorld) { DiveProbe(LaterWorld, bDownhill, bShots); });
	}

	FAutoConsoleCommandWithWorldAndArgs DiveProbeCommand(
		TEXT("TN.SlopeTilt.DiveProbe"),
		TEXT("TN.SlopeTilt.DiveProbe [abajo|arriba] [espera_s] [captura 0|1]: panzazo de prueba hacia la cuesta de 15-30° más ")
		TEXT("cercana (cuesta abajo por defecto) que escribe por fotograma la malla respecto al suelo; con captura = 1, una en el ")
		TEXT("vuelo y otra deslizando. En el anfitrión o sin red."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DiveProbeFromConsole));
}

#endif // !UE_BUILD_SHIPPING
