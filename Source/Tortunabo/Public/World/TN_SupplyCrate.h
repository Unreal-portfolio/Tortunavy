#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "World/TN_LootTable.h"
#include "TN_SupplyCrate.generated.h"

class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;

/**
 * Caja de suministros del mapa (#861; plan maestro §1 decisión 8: sustituye a los cofres). Se coloca a mano en el nivel y
 * se abre manteniendo la interacción, como un rebuscable (ATN_ProcSearchSpot): el servidor cuenta el tiempo, vigila que la
 * tortuga siga cerca y en condiciones, y el HUD enseña el aro de progreso.
 *
 *  - Se abre UNA vez por partida para todo el grupo. El estado es el replicado del rebuscable (FTNSearchSpotState), así
 *    que quien entra tarde la ve abierta (tapa levantada, sin aviso) y no puede volver a abrirla.
 *  - Al abrirse reparte su tabla de botín (FTNLootTableDef: la del asset CrateLoot o, sin él, DefaultLoot): objetos de la
 *    tabla del coop (TNCoopItems::RollLoot: filas de DT_Items y objetos del Excel definidos en código) y chapas con las
 *    probabilidades de la hoja Economía. Todo cae al suelo junto a la caja para quien lo coja.
 *  - Sin clase de chapa (la crea #858) el reparto de chapas solo se registra (LogTNLoot).
 *
 * Malla: CajaMadera_V3 y su tapa TapaCajaMadera_V3 (Content/Meshses/Assets), sin material propio todavía: se tiñen de
 * madera con el material básico del motor hasta que arte les ponga el suyo. Sonido y efecto de abrir, opcionales
 * (OpenSound, OpenFX); sin ellos suenan el «¡puf!» y las chispas del rebuscable.
 *
 * Pruebas: Tortunabo.Supply.* y TN.Supply.Crate [jugador] (una caja 3 m delante de esa tortuga).
 */
UCLASS()
class TORTUNABO_API ATN_SupplyCrate : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_SupplyCrate();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor: crea una caja en Location (en el suelo) mirando a YawDeg. Null si no se ha podido. */
	static ATN_SupplyCrate* ServerSpawn(UWorld* World, const FVector& Location, float YawDeg);

	/**
	 * Servidor: abre la caja ya, sin que nadie mantenga la tecla (consola, pruebas). false si no hay autoridad, ya estaba
	 * abierta o aún no se puede abrir (el airdrop en el aire): entonces no reparte nada.
	 */
	UFUNCTION(BlueprintCallable, Category = "Supply")
	bool ServerOpen();

	/** Ya abierta (en todas las máquinas, también para quien entra tarde). */
	UFUNCTION(BlueprintPure, Category = "Supply")
	bool IsOpened() const { return IsSearched(); }

	/** La tabla que usa: la del asset CrateLoot o, sin él, DefaultLoot. */
	const FTNLootTableDef& GetLootDef() const;

	/** Servidor, antes de abrirla: cambia la tabla (consola y pruebas); quita el asset. */
	void OverrideLoot(const FTNLootTableDef& InLoot);

	/** Servidor, antes de abrirla: semilla fija del reparto (pruebas); 0 = al azar. */
	void SetRollSeed(int32 InSeed) { RollSeed = InSeed; }

	/** Lo que dio la última apertura en el servidor y cuántas chapas se soltaron de verdad (0 sin clase de chapa). */
	const FTNLootRoll& GetLastRoll() const { return LastRoll; }
	int32 GetLastSpawnedChapas() const { return LastSpawnedChapas; }

protected:
	// ── ATN_ProcSearchSpot ──
	virtual float GetLuck() const override { return 1.f; }
	virtual float GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const override;
	virtual AActor* SpawnSearchReward(APawn* Pawn, const FVector& From, FVector& OutLanding) override;
	virtual void OnSearchStateChanged(const FTNSearchSpotState& OldState) override;
	virtual bool WantsFrameTick() const override { return bLidAnimating; }

	/** Se puede abrir ya (el airdrop, solo en el suelo). */
	virtual bool IsReadyToOpen() const { return true; }

	/** Cuerpo de la caja (bloquea a las tortugas). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Supply")
	TObjectPtr<UStaticMeshComponent> CrateMesh;

	/** Tapa: se levanta al abrirse (gira sobre su bisagra, el borde -X de la caja). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Supply")
	TObjectPtr<UStaticMeshComponent> LidMesh;

	/** Tabla de botín como asset (opcional): manda sobre DefaultLoot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply|Loot")
	TObjectPtr<UTN_LootTable> CrateLoot;

	/** Tabla de botín de esta clase cuando no hay asset (la caja del mapa: hoja Economía, «Caja de suministros»). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply|Loot")
	FTNLootTableDef DefaultLoot;

	/** Sonido al abrirse (opcional; se suma al «¡puf!» sintetizado). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply|FX")
	TObjectPtr<USoundBase> OpenSound;

	/** Efecto Niagara al abrirse (opcional; se suma a las chispas del rebuscable). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply|FX")
	TObjectPtr<UNiagaraSystem> OpenFX;

	/** Grados que gira la tapa al abrirse. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float LidOpenDegrees = 110.f;

	/** Color de madera provisional (mientras la malla no tenga material). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Supply")
	FLinearColor PlaceholderWood = FLinearColor(0.42f, 0.25f, 0.12f);

	/** Pone la tapa abierta o cerrada según el estado (sin animar). */
	void SnapLid();

private:
	/** Un objeto de la tabla del coop con la tirada de Stream. */
	bool PickItem(FRandomStream& Stream, FTN_InventoryItem& OutItem) const;

	/** Suelta Count chapas junto a la caja (o solo lo registra sin clase de chapa). Devuelve la primera soltada. */
	AActor* SpawnChapas(const APawn* Pawn, const FVector& From, int32 Count);

	/** Tiñe Target de madera si su malla no trae material. */
	void ApplyPlaceholderMaterial(UStaticMeshComponent* Target) const;

	void TickLid(float DeltaSeconds);

	/** Clase de la chapa ya cargada (servidor, en BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UClass> LoadedChapaClass;

	FTNLootRoll LastRoll;
	int32 LastSpawnedChapas = 0;
	int32 RollSeed = 0;

	/** Apertura de la tapa (0 cerrada, 1 abierta) y si se está animando. */
	float LidOpen = 0.f;
	bool bLidAnimating = false;
};
