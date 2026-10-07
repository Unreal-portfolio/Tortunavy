#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/TN_TctItemRules.h"
#include "Game/TN_TctRules.h"
#include "TN_TctItemComponent.generated.h"

class ACharacter;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UStaticMeshComponent;

/** El veneno del agua de una tortuga (#831) tal como se replica: ver FTNTctPoison. Las horas son del servidor. */
USTRUCT()
struct FTNTctPoisonNet
{
	GENERATED_BODY()

	UPROPERTY()
	float Level0 = 0.f;

	UPROPERTY()
	float T0 = 0.f;

	UPROPERTY()
	float Rate = 0.f;
};

/** Horas del servidor en que acaba cada efecto de objeto (ETNTctFx; 0 = sin él), replicadas en una sola notificación (#830). */
USTRUCT()
struct FTNTctFxState
{
	GENERATED_BODY()

	UPROPERTY()
	float SpringEnd = 0.f;

	UPROPERTY()
	float FinsEnd = 0.f;

	UPROPERTY()
	float BubbleEnd = 0.f;

	UPROPERTY()
	float SpikesEnd = 0.f;

	UPROPERTY()
	float GlideEnd = 0.f;

	UPROPERTY()
	float NetEnd = 0.f;

	UPROPERTY()
	float ScorchEnd = 0.f;
};

/**
 * Lo que los objetos de Todos contra Todos (#651) dejan en una tortuga y lo que se ve de sus disparos. No viene en la tortuga:
 * el servidor lo añade en ejecución la primera vez que hace falta y se replica solo (componente dinámico replicado, como
 * UTN_RaceItemComponent), así que los demás modos no lo llevan.
 *
 *  - Lastre del ancla (GrantHeavy): lenta y casi sin salto durante unos segundos. La hora de fin (del servidor) va replicada y
 *    cada máquina pone el tope de velocidad y de salto en su UTN_StaminaComponent, como el resto de límites: el dueño, el
 *    servidor y los demás mueven igual a la tortuga.
 *  - Disparos (MulticastShot): la estela de la pistola de noqueo, el cable del garfio y el abanico del trabuco, en todas las
 *    máquinas con pantalla (cosmético).
 *  - Resbalón del charco de alga (SetSlipping, #714): local, en cada máquina que mueve a la tortuga; lo ponen y lo quitan los
 *    charcos (ATN_TctAlgaPuddle) con su nombre. Con alguno puesto, el agarre del movimiento es TNTctItemRules::SlipperyGrip
 *    y el salto se queda corto; sin ninguno, vuelve el de antes.
 *  - Flotador (#777): el servidor lo da (ServerGrantFloat) y decide con él las caídas al agua (ServerResolveFall, la regla
 *    TNTctRules::ResolveFall). bHasFloat y la hora de fin de la flotación van replicados: cada máquina enseña el flotador
 *    (en el caparazón o, flotando, a la cintura) y, mientras flota, quita la gravedad y frena a la tortuga en su
 *    UTN_StaminaComponent, como el lastre. Al acabar, el GameMode la lanza al punto seco más cercano (ServerTakeRescue).
 *  - Efectos de los objetos nuevos (#830, ETNTctFx): botas de muelle, aletas, burbuja, púas, paraguas, red y quemadura del cohete.
 *    Cada uno es una hora de fin replicada (FTNTctFxState) y un límite de la tortuga mientras dura (TNTctItemRules::FxLimits,
 *    puesto en su UTN_StaminaComponent con un nombre propio, como el lastre): el dueño, el servidor y los demás la mueven igual.
 *    Las púas (servidor) empujan a quien se acerca y la burbuja (TNRaceItems::IsInvulnerable) la libra de empujones y derribos.
 *  - Veneno del agua (#831): el agua de TcT no mata al tocarla; intoxica mientras se está dentro y se recupera fuera. El
 *    servidor lo decide cada 0,1 s (ServerTickWater) y replica solo los cambios de ritmo (la recta de FTNTctPoison): cada
 *    máquina calcula el nivel de ahora (GetPoison) para el HUD. Al llegar a 1, queda eliminada. En la máquina de quien la lleva,
 *    mientras está dentro del agua venenosa, la vista se tiñe de verde (#920): un postproceso local, suave y con fundido de
 *    entrada y de salida (TNTctRules::PoisonVisionStep y PoisonVisionWeight).
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_TctItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_TctItemComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** El componente de la tortuga, si ya lo tiene (en cualquier máquina). */
	static UTN_TctItemComponent* FindOn(const AActor* Turtle);

	/** Servidor: el de la tortuga, creándolo y registrándolo (replicado) si aún no lo tiene. */
	static UTN_TctItemComponent* FindOrAddOn(ACharacter* Turtle);

	/** Servidor: lastre durante Seconds (alarga hasta el mayor de los dos finales). */
	void GrantHeavy(float Seconds);

	/** Servidor: quita el lastre ya (nueva ronda). */
	void ClearEffects();

	/** Lastrada ahora (cualquier máquina). */
	bool IsHeavy() const;

	/** Esta máquina: el charco Source empieza (bSlipping) o deja de hacer resbalar a la tortuga. */
	void SetSlipping(FName Source, bool bSlipping);

	/** Resbalando ahora en esta máquina (algún charco la tiene dentro). */
	bool IsSlipping() const { return SlipSources.Num() > 0; }

	/** Servidor: le da el flotador (en el caparazón, sin ocupar la mano). false si ya lleva uno. */
	bool ServerGrantFloat();

	/** Lleva el flotador sin estrenar (cualquier máquina). */
	bool HasFloat() const { return bHasFloat; }

	/** Flotando ahora tras salvarse del agua (cualquier máquina). */
	bool IsFloating() const;

	/**
	 * Servidor: la regla del flotador ante una caída de Cause (TNTctRules::ResolveFall). true si queda eliminada; si el
	 * flotador la salva, se gasta y empieza a flotar.
	 */
	bool ServerResolveFall(ETNTctFall Cause);

	/** Servidor: true una sola vez, cuando acaba de flotar y toca lanzarla al punto seco. */
	bool ServerTakeRescue();

	/**
	 * Servidor, cada 0,1 s (#831): la tortuga está (bInWater) o no con los pies en el agua. Sube o baja el veneno; el flotador
	 * sin estrenar salta al llegar a TNTctPoisonDefaults::FloatTrigger y, mientras flota, a salvo. true si el veneno ha
	 * llegado al máximo: queda eliminada.
	 */
	bool ServerTickWater(bool bInWater);

	/** Nivel de veneno de ahora (0-1), en cualquier máquina. */
	float GetPoison() const;

	/** Servidor: el efecto Fx durante Seconds (alarga hasta el mayor de los dos finales). */
	void GrantFx(ETNTctFx Fx, float Seconds);

	/** El efecto Fx está puesto ahora (cualquier máquina). */
	bool IsFxActive(ETNTctFx Fx) const;

	/** Lo que se queda del veneno del agua con los efectos de ahora (aletas); 1 = todo. */
	float GetPoisonScale() const;

	/** Estela de un disparo de Kind (ETNTctItem) de From a To, en todas las máquinas con pantalla. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(uint8 Kind, FVector_NetQuantize From, FVector_NetQuantize To);

private:
	/** Hora del servidor en que se acaba el lastre (0 = sin lastre). */
	UPROPERTY(ReplicatedUsing = OnRep_HeavyEnd)
	float HeavyEnd = 0.f;

	UFUNCTION()
	void OnRep_HeavyEnd();

	/** Pone o quita los topes del lastre según HeavyEnd y la hora del servidor. */
	void ApplyHeavy();

	/** Una estela que se desvanece. */
	struct FTrail
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		float Age = 0.f;
		float Life = 0.25f;
		float Width = 1.f;
	};
	TArray<FTrail> Trails;

	void AddTrail(const FVector& From, const FVector& To, const FLinearColor& Color, float Width, float Life);
	void TickTrails(float DeltaTime);
	void RefreshTick();

	double Now() const;

	bool bHeavyApplied = false;

	/** Charcos que tienen dentro a la tortuga en esta máquina. */
	TSet<FName> SlipSources;
	/** El agarre de antes del primer charco (se devuelve al salir del último). */
	float BaseGroundFriction = 0.f;
	float BaseBrakingDeceleration = 0.f;
	float BaseMaxAcceleration = 0.f;

	/** Pone o quita el agarre del charco y el salto corto en el movimiento de la tortuga. */
	void ApplySlip(bool bSlip);

	/** Lleva el flotador sin estrenar. */
	UPROPERTY(ReplicatedUsing = OnRep_Float)
	bool bHasFloat = false;

	/** Hora del servidor en que acaba de flotar (0 = no flota). */
	UPROPERTY(ReplicatedUsing = OnRep_Float)
	float FloatEnd = 0.f;

	UFUNCTION()
	void OnRep_Float();

	/** Pone o quita la flotación (sin gravedad y frenada) según FloatEnd y la hora del servidor. */
	void ApplyFloat();
	/** Enseña el flotador en el caparazón, a la cintura o nada. */
	void RefreshFloatLook();

	/** El flotador que pinta esta máquina. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FloatLook;

	/** Los efectos de los objetos nuevos: sus horas de fin. */
	UPROPERTY(ReplicatedUsing = OnRep_Fx)
	FTNTctFxState FxState;

	UFUNCTION()
	void OnRep_Fx();

	/** Pone o quita los límites de cada efecto según su hora de fin y la del servidor, y enseña la burbuja y las púas. */
	void ApplyFx();
	float& FxEnd(FTNTctFxState& State, ETNTctFx Fx) const;
	float FxEndOf(ETNTctFx Fx) const;
	/** Servidor: las púas empujan a quien se acerca. */
	void ServerSpikes();
	void RefreshFxLook();

	bool bFxApplied[static_cast<int32>(ETNTctFx::Count)] = {};
	float SpikesClock = 0.f;
	/** Servidor: hasta cuándo (hora del servidor) no vuelven a empujar a cada tortuga las púas. */
	TMap<TWeakObjectPtr<AActor>, double> SpikesHitUntil;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BubbleLook;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SpikesLook;

	/** El veneno del agua: la recta replicada (la hora de T0 es la del servidor). */
	UPROPERTY(ReplicatedUsing = OnRep_Poison)
	FTNTctPoisonNet PoisonNet;

	UFUNCTION()
	void OnRep_Poison();

	/** La vista de quien la lleva se tiñe de verde mientras está dentro del agua venenosa: el postproceso local y su fundido (0-1). */
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> PoisonLook;
	float PoisonFade = 0.f;

	/** Esta máquina es la de quien lleva la tortuga y tiene pantalla. */
	bool IsLocalView() const;
	/** Hay tinte puesto o por poner (dentro del agua o con el fundido de salida sin acabar). */
	bool NeedsPoisonLook() const;
	void TickPoisonLook(float DeltaTime);

	/** Servidor: la regla del flotador. */
	FTNTctFloatState FloatRule;
	bool bRescuePending = false;
	bool bFloatApplied = false;
};
