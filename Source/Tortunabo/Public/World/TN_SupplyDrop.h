#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "World/TN_SupplyCrate.h"
#include "TN_SupplyDrop.generated.h"

class UNiagaraSystem;
class USoundBase;
class UStaticMeshComponent;
class UWorld;

/** Fase de un airdrop: aviso (marca y haz en el suelo, sin caja), caída con paracaídas y en el suelo (se puede abrir). */
enum class ETNAirdropPhase : uint8
{
	Warning,
	Falling,
	Landed
};

/**
 * Vuelo de un airdrop, fijado por el servidor al crearlo y replicado una vez: cada máquina calcula con él y su reloj del
 * servidor dónde va la caja (sin replicar el movimiento), también quien entra tarde.
 */
USTRUCT()
struct FTNAirdropFlight
{
	GENERATED_BODY()

	/** Dónde aterriza (en el suelo). */
	UPROPERTY()
	FVector_NetQuantize10 Landing = FVector_NetQuantize10(FVector::ZeroVector);

	/** Altura (cm) sobre Landing desde la que empieza a caer. */
	UPROPERTY()
	float Height = 4000.f;

	/** Hora del servidor a la que empieza el aviso. */
	UPROPERTY()
	float StartTime = 0.f;

	/** Segundos de aviso antes de que aparezca la caja. */
	UPROPERTY()
	float WarnSeconds = 8.f;

	/** Segundos de caída (a velocidad constante: la del paracaídas). */
	UPROPERTY()
	float FallSeconds = 12.f;
};

/** Reglas puras del vuelo (sin mundo): las recorren las pruebas Tortunabo.Supply.Airdrop.*. */
namespace TNAirdropRules
{
	/** Fase del vuelo a la hora del servidor Now. */
	TORTUNABO_API ETNAirdropPhase PhaseAt(const FTNAirdropFlight& Flight, double Now);

	/** Altura (cm) de la caja sobre Landing a la hora Now: Height en el aviso, bajando en línea recta durante la caída, 0 en el suelo. */
	TORTUNABO_API float HeightAt(const FTNAirdropFlight& Flight, double Now);

	/** Segundos que faltan para aterrizar (0 si ya está en el suelo). */
	TORTUNABO_API float SecondsToLand(const FTNAirdropFlight& Flight, double Now);
}

/**
 * Caja de suministros que cae del cielo (airdrop, #860; plan maestro §1 decisión 8). La lanza el gestor del servidor
 * (UTN_AirdropSubsystem) sobre un punto colocado a mano (ATN_AirdropPoint) o la consola (TN.Airdrop.Force):
 *
 *  1. Aviso (WarnSeconds): anillo dorado y haz de luz vertical en el punto de aterrizaje, y un cartel en el HUD de cada
 *     jugador (UTN_RunHUDWidget) con la cuenta atrás.
 *  2. Caída (FallSeconds) a velocidad constante colgada de un paracaídas.
 *  3. En el suelo: el paracaídas desaparece y se abre como la caja del mapa (ATN_SupplyCrate: mantener la interacción,
 *     una vez por partida) con su propia tabla (hoja Economía, «Airdrop»: 2 objetos; chapas 0,7 / 0,3 / 0,15). El haz se
 *     queda hasta que alguien la abre.
 *
 * Red: siempre relevante (el aviso llega a todos, estén donde estén). El vuelo (FTNAirdropFlight) se replica una vez y cada
 * máquina coloca la caja con su reloj del servidor; la apertura es la de la caja.
 *
 * Arte pendiente: el paracaídas es una esfera aplastada del motor (sin malla propia todavía).
 */
UCLASS()
class TORTUNABO_API ATN_SupplyDrop : public ATN_SupplyCrate
{
	GENERATED_BODY()

public:
	ATN_SupplyDrop();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Servidor: lanza un airdrop de Class (nulo = esta clase) que aterriza en Landing tras WarnSeconds de aviso y
	 * FallSeconds de caída desde Height cm. Null si no se ha podido.
	 */
	static ATN_SupplyDrop* ServerLaunch(UWorld* World, TSubclassOf<ATN_SupplyDrop> Class, const FVector& Landing, float Height,
		float WarnSeconds, float FallSeconds);

	/** El airdrop de World que está por llegar (en aviso o cayendo) y que antes aterriza; null si no hay ninguno. */
	static const ATN_SupplyDrop* FindIncoming(const UWorld* World);

	ETNAirdropPhase GetPhase() const;
	float GetSecondsToLand() const;
	const FTNAirdropFlight& GetFlight() const { return Flight; }

protected:
	virtual bool IsReadyToOpen() const override { return GetPhase() == ETNAirdropPhase::Landed; }
	virtual bool IsSpentFor(const APawn* Interactor) const override { return !IsReadyToOpen() || Super::IsSpentFor(Interactor); }
	virtual bool IsSpentForLocalView() const override { return !IsReadyToOpen() || Super::IsSpentForLocalView(); }
	virtual bool WantsFrameTick() const override;

	/** Paracaídas (provisional: esfera aplastada del motor). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Airdrop")
	TObjectPtr<UStaticMeshComponent> ParachuteMesh;

	/** Color del paracaídas provisional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop")
	FLinearColor ParachuteColor = FLinearColor(0.9f, 0.32f, 0.12f);

	/** Radio (cm) del anillo del aviso en el suelo y alto (cm) del haz de luz. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop|Aviso", meta = (ClampMin = "50.0"))
	float WarningRingRadius = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop|Aviso", meta = (ClampMin = "500.0"))
	float BeamHeight = 6000.f;

	/** Sonido del aviso, en el punto de aterrizaje (opcional). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop|FX")
	TObjectPtr<USoundBase> IncomingSound;

	/** Sonido y efecto al tocar el suelo (opcionales). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop|FX")
	TObjectPtr<USoundBase> LandSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Airdrop|FX")
	TObjectPtr<UNiagaraSystem> LandFX;

private:
	UPROPERTY(Replicated)
	FTNAirdropFlight Flight;

	/** Coloca la caja según el vuelo y, al cambiar de fase, su colisión, el paracaídas y los avisos. */
	void UpdateFlight(float DeltaSeconds);
	void ApplyPhase(ETNAirdropPhase Phase, bool bFresh);
	void EnsureWarningMarkers();
	void TickWarningMarkers(float DeltaSeconds);

	/** Anillo y haz del aviso (solo en máquinas con pantalla; se crean al primer uso). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WarningRing;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WarningBeam;

	/** Fase aplicada (0xFF = ninguna todavía). */
	uint8 AppliedPhase = 0xFF;
	float WarningClock = 0.f;
};
