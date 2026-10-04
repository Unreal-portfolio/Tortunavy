// Mundo de juego con física para medir el buggy del Rally sin ventana (#294, #606, revisión de la PR #240): se avanza a
// 60 Hz con World->Tick (Chaos y vehículo incluidos), sin PIE ni -game. Lo usan los tests Tortunabo.Rally.Measure.*:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Measure; Quit" -nullrhi -unattended -NoSteam
// Lo que depende de pintar (la pose sentada de las tortugas, UTN_BuggyRiderAnimComponent::ApplyPose) no se puede medir así.
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "Rally/TN_RallyGameState.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyTrack.h"
#include "UObject/UnrealType.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyData.h"

namespace TNRallyPhysicsMeasure
{
	constexpr float StepSeconds = 1.f / 60.f;
	constexpr int32 StepsPerSecond = 60;
	/** Suelo llano: losa de 3 km de lado con la cara de arriba en Z = 0. */
	constexpr double GroundSizeCm = 300000.0;
	constexpr double GroundThicknessCm = 100.0;
	constexpr double SpawnLiftCm = 120.0;
	constexpr float SettleSeconds = 2.f;
	/** Pista de las medidas sobre el trazado: R01, el circuito por defecto del Rally (#692 quitó E01B del repo). */
	inline const TCHAR* MeasureVariant() { return TEXT("R01_circuito_dunas"); }

	/** Mundo de juego con BeginPlay hecho; Step lo avanza un fotograma de 1/60 s con física. */
	struct FPhysicsWorld
	{
		UWorld* World = nullptr;
		uint64 CachedFrameCounter = 0;

		explicit FPhysicsWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			// Fuera de un mapa cargado por el motor (LoadMap lo activa), el mundo no simula: sin esto Chaos no avanza.
			World->bShouldSimulatePhysics = true;
			CachedFrameCounter = GFrameCounter;
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}

		~FPhysicsWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				GFrameCounter = CachedFrameCounter;
			}
		}

		FPhysicsWorld(const FPhysicsWorld&) = delete;
		FPhysicsWorld& operator=(const FPhysicsWorld&) = delete;

		/** Un fotograma. Sube GFrameCounter como FTestWorldWrapper::TickTestWorld: sin eso Chaos solo avanza el primer paso. */
		void Step() const
		{
			World->Tick(LEVELTICK_All, StepSeconds);
			++GFrameCounter;
		}

		void Advance(float Seconds) const
		{
			for (int32 Index = 0; Index < FMath::RoundToInt32(Seconds * StepsPerSecond); ++Index)
			{
				Step();
			}
		}
	};

	/** Recoge del registro las líneas que contienen Marker (los comandos de medida escriben ahí su resultado). */
	class FLineCapture final : public FOutputDevice
	{
	public:
		explicit FLineCapture(const TCHAR* InMarker) : Marker(InMarker) { GLog->AddOutputDevice(this); }
		virtual ~FLineCapture() override { GLog->RemoveOutputDevice(this); }

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Text && FCString::Strstr(Text, *Marker))
			{
				FScopeLock Lock(&Guard);
				Lines.Add(Text);
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		TArray<FString> Take()
		{
			GLog->Flush();
			FScopeLock Lock(&Guard);
			return Lines;
		}

	private:
		FString Marker;
		FCriticalSection Guard;
		TArray<FString> Lines;
	};

	inline AStaticMeshActor* SpawnFlatGround(UWorld& World)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Ground = Cube
			? World.SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, -0.5 * GroundThicknessCm), FRotator::ZeroRotator, Params) : nullptr;
		if (!Ground)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Ground->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		// El cubo básico mide 100 cm de lado.
		Mesh->SetWorldScale3D(FVector(GroundSizeCm, GroundSizeCm, GroundThicknessCm) / 100.0);
		Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		return Ground;
	}

	/** Buggy con el ajuste Data (null = el de por defecto), puesto antes de PostInitializeComponents, que lo lee. */
	inline ATN_Buggy* SpawnBuggy(UWorld& World, const FTransform& Where, UTN_BuggyData* Data = nullptr)
	{
		ATN_Buggy* Buggy = World.SpawnActorDeferred<ATN_Buggy>(ATN_Buggy::StaticClass(), Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Buggy)
		{
			return nullptr;
		}
		// Data es protegido y solo de edición: se pone por reflexión, como haría un Blueprint hijo.
		FObjectProperty* Property = Data ? FindFProperty<FObjectProperty>(ATN_Buggy::StaticClass(), TEXT("Data")) : nullptr;
		if (Property)
		{
			Property->SetObjectPropertyValue_InContainer(Buggy, Data);
		}
		Buggy->FinishSpawning(Where);
		return Buggy;
	}

	/** Pista de Variant con su terreno y la carrera en marcha (sin GameMode: nadie reaparece ni corta motores). */
	inline ATN_RallyTrack* PrepareMeasureTrack(const FPhysicsWorld& Test, FName Variant)
	{
		ATN_RallyGameState* RallyState = Test.World->SpawnActor<ATN_RallyGameState>();
		if (!RallyState)
		{
			return nullptr;
		}
		Test.World->SetGameState(RallyState);
		ATN_RallyTrack* Track = RallyState->PrepareTrack(Variant);
		RallyState->Phase = ETNRallyPhase::Racing;
		return Track && Track->IsBuilt() ? Track : nullptr;
	}

	/** Pista de MeasureVariant (R01). */
	inline ATN_RallyTrack* PrepareMeasureTrack(const FPhysicsWorld& Test)
	{
		return PrepareMeasureTrack(Test, FName(MeasureVariant()));
	}

	inline float Kmh(const ATN_Buggy& Buggy)
	{
		return static_cast<float>(TNRally::CmsToKmh(Buggy.GetForwardSpeedCms()));
	}

	/** Volante que mantiene el morro hacia +X (las pruebas en llano van en línea recta por la losa). */
	inline float HoldHeadingSteer(const ATN_Buggy& Buggy)
	{
		const float YawDeg = static_cast<float>(FRotator::NormalizeAxis(Buggy.GetActorRotation().Yaw));
		return FMath::Clamp(-YawDeg / 10.f, -1.f, 1.f);
	}

	/** Deja caer el buggy y que se asiente parado con el freno de mano. */
	inline void Settle(const FPhysicsWorld& Test, ATN_Buggy& Buggy)
	{
		for (int32 Index = 0; Index < FMath::RoundToInt32(SettleSeconds * StepsPerSecond); ++Index)
		{
			Buggy.SetAIDriveInput(0.f, 0.f, 0.f, true);
			Test.Step();
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
