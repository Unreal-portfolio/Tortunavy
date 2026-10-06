#pragma once

#include "CoreMinimal.h"
#include "World/TN_MapVariantLoader.h"
#include "TN_TctArena.generated.h"

class ATN_TctGameState;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * La arena de Todos contra Todos (#651): una variante inventada de Scripts/terrain_volumes/Variants (por defecto A01_diana,
 * hecha para este modo; con ?Arena=<variante> en la URL, otra, p. ej. P01_plataformas) cargada como ATN_MapVariantLoader, más
 * el mar que sube durante la ronda.
 *
 * - Replicada: el servidor elige la variante (ServerSetArenaVariant) y cada máquina construye la misma malla desde el disco.
 * - El mar es un plano con el material del mar del mapa procedural; en cada máquina se pone a la altura que da el GameState
 *   (ATN_TctGameState::GetWaterZ), sin replicar la altura.
 * - El agua es veneno (#831): el mar tiene un aspecto tóxico (verde) y, en los 5 s antes de cada subida y mientras sube, una
 *   marca (plano translúcido) enseña a qué altura llegará. Sin zonas de muerte del fondo (bSpawnKillZones apagado): tocar el
 *   agua no mata, intoxica (UTN_TctItemComponent::ServerTickWater).
 * - El terreno lleva la arena de la playa (SandMaterialPath: M_GridTerrainWet, la del Rally y del Coop, con grano, rizos y
 *   arena mojada) en vez del material genérico del cargador (#779). Solo aquí: el resto de cargadores no cambia.
 * - Servidor: Survey mide el suelo pisable (alturas para los escalones del agua, caja de la arena y sitios de salida lejos de
 *   los bordes).
 *
 * Como ATN_MapVariantLoader, lee de Scripts/, que no se empaqueta: el modo solo se juega desde el editor (PIE o -game sin
 * cocinar) hasta que las arenas pasen a assets.
 */
UCLASS()
class TORTUNABO_API ATN_TctArena : public ATN_MapVariantLoader
{
	GENERATED_BODY()

public:
	ATN_TctArena();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** La arena del mundo (la primera), o nullptr. */
	static ATN_TctArena* Find(const UWorld* World);

	/** Servidor: carga NewVariant (si cambia) y la replica. Antes de BeginPlay, las zonas de muerte salen de ella. */
	void ServerSetArenaVariant(FName NewVariant);

	FName GetArenaVariant() const { return ArenaVariant; }

	/** true si existe la carpeta de esa variante con su manifest.json. */
	static bool VariantExists(FName VariantName);

	/** Material de arena de playa que lleva el terreno de todas las arenas de TcT. */
	static const TCHAR* SandMaterialPath();

	/**
	 * Servidor: mide el suelo pisable de la arena con trazas cada SampleSpacing (uu). false si no hay suelo (variante que no se
	 * pudo cargar).
	 */
	bool Survey(float SampleSpacing);

	/** Alturas del suelo pisable (una por muestra) de la última medición. */
	const TArray<float>& GetSurveyHeights() const { return SurveyHeights; }

	/** Caja del terreno con colisión (válida tras Survey). */
	const FBox& GetGroundBox() const { return GroundBox; }

	/** Altura del mar de la variante (water_uu del manifest, en el mundo). */
	float GetBaseWaterZ() const { return BaseWaterZ; }

	/**
	 * Count sitios de salida repartidos por el suelo pisable (lejos de los bordes), con la cápsula CapsuleLift por encima del
	 * suelo y mirando al centro. Menos si no hay tantos sitios.
	 */
	TArray<FTransform> PickSpawnTransforms(int32 Count, float CapsuleLift) const;

	/** Puntos de suelo pisable lejos de los bordes (de la última medición): de aquí salen las salidas y los puntos de objetos. */
	const TArray<FVector>& GetSpawnCandidates() const { return SpawnCandidates; }

	/**
	 * Lo cerca del vacío que está cada sitio de GetSpawnCandidates (0-1, mismo orden): 1 con un desnivel de más de 1,5 m
	 * pegado a él, 0 sin ninguno a menos de 15 m (#830). Los puntos de objetos expuestos dan mejores objetos.
	 */
	const TArray<float>& GetSpawnExposure() const { return SpawnExposure; }

	/** La cota del suelo más alto medido (mundo). */
	float GetTopZ() const { return HighestZ; }

protected:
	/** Variante elegida por el servidor (la de Variant del nivel hasta que la cambie). */
	UPROPERTY(ReplicatedUsing = OnRep_ArenaVariant)
	FName ArenaVariant;

	UFUNCTION()
	void OnRep_ArenaVariant();

	/** El mar que sube. */
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> WaterPlane;

	/** La marca del nivel al que llegará el agua en la próxima subida (#831). */
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> MarkerPlane;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMaterial;

	/** Cuánto sobresale el mar por cada lado de la caja de la arena (uu). */
	UPROPERTY(EditAnywhere, Category = "Tct", meta = (ClampMin = "0.0"))
	float WaterPlaneMargin = 40000.f;

private:
	/** Pone la arena de playa como material del terreno (y en los trozos ya construidos). */
	void ApplySandMaterial();
	/** Pone el aspecto tóxico del agua y prepara la marca del nivel (una vez, en máquinas con pantalla). */
	void SetUpToxicLook();
	/** Coloca y pinta la marca del nivel al que llegará el agua según el GameState. */
	void TickMarker(const ATN_TctGameState* State);
	/** Lee water_uu del manifest y ajusta el plano del mar a la caja del terreno. */
	void FitWaterPlane();
	/** Traza vertical en (X, Y) contra este actor: suelo pisable con su altura. */
	bool TraceGround(double X, double Y, double TopZ, double BottomZ, FVector& OutPoint) const;

	TArray<float> SurveyHeights;
	/** Muestras de suelo pisable con sus cuatro vecinas también pisables y a la misma altura (lejos de los bordes). */
	TArray<FVector> SpawnCandidates;
	TArray<float> SpawnExposure;
	float HighestZ = 0.f;
	FBox GroundBox = FBox(ForceInit);
	float BaseWaterZ = 0.f;
};
