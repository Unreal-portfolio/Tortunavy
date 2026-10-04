#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UWorld;

/**
 * Piezas comunes de las trampas de la playa (ATN_BeachBarbedWire, ATN_BeachSeaweed, ATN_BeachWobblyPlatform,
 * ATN_BeachSpadeRamp, ATN_BeachShellGate...): reloj del servidor suavizado para animar igual en todas las máquinas,
 * estallidos de partículas low-poly (chispas, astillas, arena) y texto emergente de dibujos («¡AY!»). Lo visual es local
 * de cada máquina y no existe en un servidor dedicado. Implementación: Private/World/Beach/TN_BeachTrapCommon.cpp.
 */

/**
 * Hora del servidor (AGameStateBase::GetServerWorldTimeSeconds) suavizada: avanza con el fotograma y se acerca poco a
 * poco a la del servidor, sin saltos cuando esta se corrige (como el puente bamboleante del lobby).
 */
struct FTNTrapClock
{
	double Advance(const UWorld* World, float DeltaSeconds);
	/** Lo mismo con la hora del servidor ya leída (Advance la lee del GameState): sin mundo, para las pruebas. */
	double AdvanceTo(double ServerTime, float DeltaSeconds);
	double Now() const { return Clock; }

private:
	double Clock = 0.0;
	bool bValid = false;
};

/** Forma de las partículas de un estallido (malla de 100 cm con color de vértice). */
enum class ETNTrapBurstShape : uint8
{
	/** Chispa: rombo alargado que vuela en la dirección de su velocidad. */
	Spark,
	/** Astilla o esquirla: lámina plana que da vueltas. */
	Chip,
	/** Pegote redondo (arena, agua). */
	Blob,
};

/**
 * Estallido de partículas: instancias de una malla low-poly movidas a mano (gravedad, rozamiento, tamaño que encoge).
 * Init crea el componente de instancias (nada en servidor dedicado); Burst las lanza desde un punto del mundo; Tick las
 * mueve y devuelve false cuando ya no queda ninguna viva (el dueño puede dejar de llamarlo).
 */
struct FTNTrapBurst
{
	void Init(AActor* InOwner, ETNTrapBurstShape InShape, const FLinearColor& InColor, int32 InMaxParticles);

	/** Movimiento: gravedad (cm/s², negativa hacia abajo), rozamiento (1/s), tamaños (cm) y vida (s). */
	void SetMotion(float InGravity, float InDrag, float InSizeStart, float InSizeEnd, float InLifeMin, float InLifeMax);

	/** Lanza Count partículas desde WorldAt (en un disco de Radius) hacia Dir con Speed y un cono de apertura Spread (0..~1,5). */
	void Burst(const FVector& WorldAt, int32 Count, const FVector& Dir, float Speed, float Spread, float Radius = 0.f);

	bool Tick(float DeltaSeconds);
	bool IsLive() const { return bLive; }

private:
	struct FParticle
	{
		FVector P = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Spin = 0.f;
		bool bAlive = false;
	};

	TWeakObjectPtr<UInstancedStaticMeshComponent> ISM;
	TArray<FParticle> Particles;
	TArray<FTransform> Xf;
	ETNTrapBurstShape Shape = ETNTrapBurstShape::Spark;
	float Gravity = -980.f;
	float Drag = 1.f;
	float SizeStart = 20.f;
	float SizeEnd = 4.f;
	float LifeMin = 0.2f;
	float LifeMax = 0.5f;
	uint32 Rng = 0x9E3779B9u;
	bool bLive = false;
};

/** Texto emergente de dibujos («¡AY!», «¡CRAC!»): sale, crece de golpe, sube, mira a la cámara local y se va. */
struct FTNTrapPopText
{
	void Show(AActor* InOwner, const FText& InText, const FColor& InColor, const FVector& WorldAt, float WorldSize = 120.f);

	/** false cuando ya no se ve. */
	bool Tick(float DeltaSeconds, const UWorld* World);

private:
	TWeakObjectPtr<UTextRenderComponent> Comp;
	FVector Start = FVector::ZeroVector;
	float Age = 10.f;
	float Size = 120.f;
};
