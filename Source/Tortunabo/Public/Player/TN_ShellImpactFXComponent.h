#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Audio/TN_ShellImpactSynth.h"
#include "TN_ShellImpactFXComponent.generated.h"

class ATN_ShellBody;
class UPrimitiveComponent;
struct FHitResult;

namespace TNShellImpactFX
{
	// Emisores, caché de superficie y memoria del golpe anterior (Private/Player/TN_ShellImpactFXComponent.cpp).
	struct FState;
}

/**
 * Golpes de la bola del caparazón: un sonido y un mini efecto visual según contra qué choca (arena, roca, madera, agua,
 * otra tortuga, un enemigo o los trastos de los bañistas), con fuerza según la velocidad del impacto.
 *
 * - Se engancha SOLA desde fuera a la bola de física de la tortuga (ATN_ShellBody, que no se toca): cada fotograma mira
 *   UTN_ShellComponent::GetBody() y, cuando cambia la caja, activa «Simulation Generates Hit Events» en ella
 *   (SetNotifyRigidBodyCollision) y se apunta a su OnComponentHit. Al perder la caja se suelta.
 * - Sin red: el golpe lo detecta cada máquina en SU copia de la bola (en los clientes la caja también simula y la réplica
 *   la corrige), así que todos los jugadores cercanos lo ven y oyen sin un solo byte más y sin multicast. Nada de esto
 *   existe en un servidor dedicado (no se crea el componente ni se activan los eventos de golpe).
 * - Velocidad del impacto: el mayor de la variación de velocidad que da el motor (impulso normal / masa) y lo que la bola
 *   llevaba contra la superficie el fotograma anterior. Por debajo de MinImpactSpeed (2,6 m/s) no suena ni se ve nada
 *   (rodar y botes pequeños no cuentan) y a FullImpactSpeed (18 m/s) es el golpe máximo.
 * - Límites para que nunca sea un «pum, pum, pum»: separación mínima de 0,14 s entre golpes de una bola (hasta 0,42 s si el
 *   golpe es flojo, salvo que el nuevo sea claramente más fuerte que el anterior), como mucho 8 golpes por segundo entre todas
 *   las bolas del mundo (solo pasan los fuertes de ahí) y ningún golpe a más de 60 m de la cámara.
 * - Superficie: por la clase del actor contra el que choca. Caparazón o tortuga, ATN_BeachEnemy, generador de la playa (por
 *   el nombre de la malla: acantilado = roca, bosquecillo = madera, pendiente fuerte = roca, el resto arena), fortalezas y
 *   castillos (arena), catapultas, plataformas y cofres (madera) y el resto de elementos y decorado (trastos de plástico y
 *   lata). Fuera de la playa (mapa procedural, lobby), por TNTurtleSurface, la misma superficie que los pasos y el polvo.
 *   Al entrar al agua a más de 2,6 m/s: chapoteo.
 * - Efecto visual: dos emisores de partículas de TNAmbientFX (la nube y los trocitos: granos, esquirlas, astillas, gotas,
 *   chispas o confeti) por timbre, creados la primera vez que hacen falta y movidos solo con partículas vivas.
 * - Sonido: UTN_ShellImpactSynthComponent (síntesis en código, sin archivos de audio; categoría Efectos).
 *
 * Consola: TN.Shell.Impact 0|1, TN.Shell.Impact.Debug 0|1, TN.Shell.Impact.Volume, TN.Shell.Impact.MinSpeed y
 * TN.Shell.Impact.Test <arena|roca|madera|agua|tortuga|enemigo|trasto|todos> [fuerza] (Docs/Comandos_Prueba.md).
 */
UCLASS(ClassGroup = (Effects), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_ShellImpactFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_ShellImpactFXComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * El componente de InOwner; si no tiene, se lo crea (registrado). Null en servidor dedicado, fuera de un mundo de juego,
	 * sin pantalla ni audio o con el actor destruyéndose.
	 */
	static UTN_ShellImpactFXComponent* FindOrAddTo(AActor* InOwner);

	/** Prueba: un golpe de ese timbre y fuerza delante de la tortuga (sonido y efecto), sin física ni límites. */
	void PlayTestImpact(ETNShellImpactSound InSound, float InStrength);

	/**
	 * Estampado del panzazo contra la pared (#355): el golpe (sonido y polvo) en InWhere, con InWallNormal hacia fuera de la
	 * pared. El timbre sale de lo que hay ahí en esta máquina (una traza corta contra la pared). Cuenta como el último golpe
	 * de la bola: el primer bote de la bola que nace a continuación no lo repite.
	 */
	void PlayWallSplat(const FVector& InWhere, const FVector& InWallNormal, float InStrength);

	/** Velocidad de impacto (cm/s) por debajo de la cual no pasa nada. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "0.0"))
	float MinImpactSpeed = 260.f;

	/** Velocidad de impacto (cm/s) del golpe máximo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "100.0"))
	float FullImpactSpeed = 1800.f;

	/** Separación mínima entre dos golpes de una misma bola (s), con un golpe fuerte; los flojos esperan más. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "0.05"))
	float MinInterval = 0.14f;

	/** Cantidad de partículas (1 = normal; 0 apaga el efecto visual). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float FXAmount = 1.f;

	/** Volumen de los golpes (1 = normal; 0 los apaga). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SoundLoudness = 1.f;

	/** Más lejos de la cámara local (cm) no se crean partículas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ShellImpact", meta = (ClampMin = "500.0"))
	float MaxViewDistance = 6000.f;

private:
	/** OnComponentHit de la caja de la bola (solo se apunta mientras hay caja). */
	UFUNCTION()
	void HandleShellHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void BindBody(ATN_ShellBody* InBody);
	void UnbindBody();

	/** Sonido y efecto de un golpe ya decidido (Where: punto; Normal: hacia fuera de la superficie). */
	void EmitImpact(ETNShellImpactSound InSound, float InStrength, const FVector& InWhere, const FVector& InNormal);

	/** Caja a la que está apuntado el componente. */
	TWeakObjectPtr<ATN_ShellBody> BoundBody;

	/** Emisores, caché y memoria del golpe anterior. */
	TSharedPtr<TNShellImpactFX::FState> State;
};
