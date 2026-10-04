#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "TN_RayTracingQualitySubsystem.generated.h"

class IConsoleVariable;

/** Regla de ray tracing por calidad de iluminación global (#562), como lógica pura (tests en Tortunabo.Render.RayTracingQuality). */
namespace TNRayTracingQuality
{
	/** Calidad de iluminación global (sg.GlobalIlluminationQuality) mínima con ray tracing: Épico (3) y Cine (4). */
	constexpr int32 MinQualityWithRayTracing = 3;

	/** Bajo, Medio y Alto sin ray tracing (Lumen por software o sin Lumen); Épico y Cine, con él. */
	bool ShouldEnable(int32 GlobalIlluminationQuality);
}

/**
 * @brief Enciende o apaga el ray tracing (r.RayTracing.Enable) según la calidad de iluminación global (#562).
 *
 * El proyecto compila el ray tracing (r.RayTracing=True) y en UE 5.6 se puede apagar en caliente
 * (r.RayTracing.EnableOnDemand=1 de serie). Con él apagado no se crea el BLAS de cada sección de ProcMesh (el terreno), el
 * hilo de render no recoge instancias de ray tracing y Lumen pasa a trazado por software. DefaultScalability.ini no puede
 * hacerlo: r.RayTracing.Enable no es ECVF_Scalability y el motor ignora la línea (con un ensure).
 *
 * Escucha sg.GlobalIlluminationQuality (al arrancar y en cada cambio de calidad, sea desde el menú o por consola) y fija
 * r.RayTracing.Enable con prioridad de escalabilidad: un valor puesto a mano en consola o en Engine.ini manda sobre este.
 * Solo toca la cvar si su valor cambia (cambiarla recrea el estado de render de todos los componentes).
 */
UCLASS()
class TORTUNABO_API UTN_RayTracingQualitySubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleGlobalIlluminationQualityChanged(IConsoleVariable* Variable);

	FDelegateHandle QualityChangedHandle;
};
