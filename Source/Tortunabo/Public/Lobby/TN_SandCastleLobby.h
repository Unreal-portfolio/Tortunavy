#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_SandCastleLobby.generated.h"

class APlayerController;
class UBoxComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * El lobby como castillo de arena redondo (LVL_Lobby). Va colocado en el nivel y se construye en el editor
 * (OnConstruction) y en ejecución, así que se ve y se ajusta sin darle al Play. Solo se replican el estado de la puerta
 * y de los huevos.
 *
 * Plano (cm, centro del círculo en el origen del actor; +Y es la puerta, las 12 del reloj; las 3 quedan a -X):
 * - Muralla redonda de radio interior Radius con almenas, marcas de cubo y conchas, y torres de cubo de alturas
 *   distintas repartidas sin simetría.
 * - Puerta doble (las 12): dos puertas con una sala en medio que sale hacia fuera de la muralla (TNCastleKit::Gatehouse,
 *   la misma estructura con la que se sale en el mapa procedural). La puerta 1 se abre cuando alguien se acerca y se
 *   cierra cuando todos están dentro; la 2 sigue cerrada. Estar en la sala cuenta como listo, igual que un huevo.
 * - Muro interior recto de las 3:40 a las 8:20, algo por debajo del centro (CutY), con adarve por arriba. Separa la
 *   plaza del lobby (arriba) del patio de pruebas (abajo). En su centro, la torre del homenaje (las 6) con un paso por
 *   dentro, una escalera de caracol por fuera y un balcón con almenas que mira a la plaza; del rellano de la escalera
 *   baja otra al adarve de la izquierda, que acaba en un tobogán a la plaza. El de la derecha (se llega botando en las
 *   medusas) tiene un mirador y un tobogán al patio de pruebas. En la azotea, delante del torreón, una tarima de
 *   decoración.
 * - Plaza: pila de ocho huevos en un montículo de dos alturas (EggsCenter): siete en el piso bajo y uno arriba, uno por
 *   jugador de los ocho que caben en la sesión. Meterse en uno marca al jugador como listo
 *   (ATN_HQGameMode::SetPlayerReadyState) y con todos listos empieza la cuenta atrás.
 * - Los puestos (tienda de las 10 a las 11, cuartel de la 12 a la 1, probadores de las 2 a las 3:30 y medusas de las
 *   8:20 a las 10) y las piezas del patio de pruebas son actores propios colocados en el nivel; LayoutSpot() da sus
 *   sitios.
 *
 * Consola: TN.Lobby.Castle 0 esconde el castillo en ejecución (hay que volver a cargar el lobby).
 */
UCLASS()
class TORTUNABO_API ATN_SandCastleLobby : public AActor
{
	GENERATED_BODY()

public:
	ATN_SandCastleLobby();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** false con TN.Lobby.Castle 0. */
	static bool IsEnabled();

	/** Número de huevos de la pila (uno por jugador; ocho, el máximo de la sesión). */
	static constexpr int32 NumEggs = 8;

	/** Radio interior de la muralla (cm). */
	static constexpr double Radius = 2400.0;

	/** Cota (Y local) del muro interior que separa la plaza del patio de pruebas. */
	static constexpr double CutY = -800.0;

	/**
	 * Punto de la plaza a la hora ClockHour del reloj (12 = la puerta, +Y; 3 = -X) y a Dist cm del centro, en
	 * coordenadas locales del castillo; Yaw mira al centro del círculo.
	 */
	static FVector LayoutSpot(double ClockHour, double Dist, float& OutYawToCenter);

	/**
	 * Sitios de salida de los jugadores en la plaza (locales), entre la puerta y la pila de huevos, mirando a la pila: los
	 * cuatro de siempre en una fila y otros cuatro en una segunda fila detrás, hacia la puerta (NumEggs en total).
	 */
	static void GetSpawnSpots(TArray<FTransform>& OutLocalSpots);

	/**
	 * Pinta o no el mar de fuera y la orilla que baja a él (la playa de fuera sigue). El valle del lobby (ATN_LobbyValley)
	 * lo apaga: ocupa ese sitio. Si cambia, rehace el castillo.
	 */
	void SetDrawSea(bool bDraw);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Castle")
	TObjectPtr<USceneComponent> CastleRoot;

	/**
	 * Suelo, murallas, torres, puerta doble, torre del homenaje, escaleras, toboganes y montículo (con colisión).
	 * Las tres mallas generadas son RF_Transient (no se guardan con el nivel) y sus punteros, Transient: si se guardaran,
	 * al cargar LVL_Lobby llegarían a nulo (apuntan a algo que no se guarda) y el castillo no se construiría.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Castle")
	TObjectPtr<UProceduralMeshComponent> CastleMesh;

	/** Adornos sin colisión: conchas, estrellas, banderas, antorchas, bases de los huevos, el mar y la playa de fuera. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Castle")
	TObjectPtr<UProceduralMeshComponent> DecorMesh;

	/** Barreras invisibles (murallas, balcón, adarves y sala de la puerta doble): nadie se cae fuera del castillo. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Castle")
	TObjectPtr<UProceduralMeshComponent> BarrierMesh;

	/** Hojas de la puerta 1 de la puerta doble (bisagra en el origen de cada una). */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UStaticMeshComponent> GateLeafLeft;

	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UStaticMeshComponent> GateLeafRight;

	/** Hojas de la puerta 2 (siempre cerrada en el lobby). */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UStaticMeshComponent> Gate2LeafLeft;

	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UStaticMeshComponent> Gate2LeafRight;

	/** Bloqueo de la puerta 1 (solo cerrada). */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UBoxComponent> GateBlock;

	/** Bloqueo de la puerta 2. */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UBoxComponent> Gate2Block;

	/** Tapas de los huevos. */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TArray<TObjectPtr<UStaticMeshComponent>> EggLids;

	/** Rótulo del cartel de madera sobre la puerta 1, por la cara de la plaza. */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UTextRenderComponent> GateSignText;

	/** Rótulo del cartel de la puerta 2, por fuera (el mismo nombre). */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UTextRenderComponent> Gate2SignText;

	/** Luz de las antorchas de la sala de la puerta doble. */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UPointLightComponent> RoomLight;

	/** Luz del paso por dentro de la torre del homenaje. */
	UPROPERTY(VisibleAnywhere, Category = "Castle")
	TObjectPtr<UPointLightComponent> TunnelLight;

	/** Nombre del cartel de la puerta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Castle")
	FText GateName = NSLOCTEXT("Tortunabo", "CastleGateName", "TORTUNAVY");

	/** Mar de fuera y orilla que baja a él (el valle del lobby lo apaga con SetDrawSea). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Castle")
	bool bDrawSea = true;

private:
	/** Puerta 1 abierta (servidor: alguien se acerca por la plaza o quiere salir de la sala). */
	UPROPERTY(Replicated)
	bool bGateOpen = false;

	/** Huevos ocupados (bit por huevo). */
	UPROPERTY(Replicated)
	int32 EggMask = 0;

	/** Construye todas las mallas (editor y ejecución); no hace nada si ya están hechas y no se fuerza. */
	void BuildAll(bool bForce);
	void BuildCastle();
	void BuildGateAndEggs();

	/** Esconde las piezas de la maqueta que el castillo sustituye y apaga la zona de listos vieja (cada máquina). */
	void HideMaquette();

	/** Servidor: quién está en qué huevo o en la sala (listos), cómo se empezará y si la puerta 1 tiene que abrirse. */
	void ServerUpdate(float DeltaSeconds);

	float GateOpenness = 0.f;
	float EggClose[NumEggs] = {};
	float Clock = 0.f;
	float ServerTimer = 0.f;
	float GateHoldTimer = 0.f;
	bool bGateBlocking = true;
	bool bBuilt = false;
	/** Si la última construcción pintó el mar (la propiedad puede cambiar sin reconstruir, p. ej. desde Python). */
	bool bSeaBuilt = true;

	/** Servidor: el último estado de listo enviado por jugador. */
	TMap<TWeakObjectPtr<APlayerController>, bool> ReadySent;
};
