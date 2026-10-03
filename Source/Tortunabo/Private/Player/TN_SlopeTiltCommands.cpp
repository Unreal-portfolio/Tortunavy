// Comandos de consola para probar la inclinación con la pendiente (#586). Solo fuera de Shipping.
//   TN.SlopeTilt.Find [lado|frente] [espera_s] [captura]: pone la tortuga local en la cuesta de 15-30° más cercana
//     (hasta 120 m), de lado o de frente a la pendiente, con la cámara donde se ve la inclinación; con captura = 1,
//     2,5 s después hace HighResShot y escribe el estado. Con espera_s, lo hace pasado ese tiempo (para -ExecCmds).
//   TN.SlopeTilt.Dump [espera_s]: escribe en el registro la inclinación de cada tortuga en esta máquina.

#include "Player/TN_SlopeTiltComponent.h"

#include "Core/TN_Log.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
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
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* ShotWorld = WeakWorld.Get())
			{
				Dump(ShotWorld);
				GEngine->Exec(ShotWorld, TEXT("HighResShot 1"));
				UE_LOG(LogTortunabo, Log, TEXT("[SlopeTilt] Captura pedida."));
			}
		}), ShotDelaySeconds, false);
	}

	void RunLater(UWorld* World, float DelaySeconds, TFunction<void(UWorld*)> Action)
	{
		if (!World)
		{
			return;
		}
		if (DelaySeconds <= 0.f)
		{
			Action(World);
			return;
		}
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Action]()
		{
			if (UWorld* LaterWorld = WeakWorld.Get())
			{
				Action(LaterWorld);
			}
		}), DelaySeconds, false);
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
}

#endif // !UE_BUILD_SHIPPING
