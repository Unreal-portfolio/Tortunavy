// Objeto que lleva un kart (#304): lo da una caja (ATN_KartItemBox) según el puesto, con la ruleta del HUD, y lo usa la
// artillera o, si va sola, la conductora (ATN_KartBuggy y ATN_KartGunnerPawn mandan la petición por RPC validada). Todo lo
// decide el servidor; el objeto, sus cargas y las horas de fin de la ruleta, el acelerón y la estrella se replican. El
// acelerón lo empujan el servidor y la conductora local (que simulan el chasis), como el turbo del buggy. Los bots usan el
// suyo solos (TNKart::ShouldBotUseItem).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kart/TN_KartItems.h"
#include "TN_KartItemComponent.generated.h"

class ATN_Buggy;

UCLASS(ClassGroup = (Karts), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_KartItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_KartItemComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ── Cualquier máquina ─────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "Karts|Objetos")
	ETNKartItem GetItem() const { return Item; }

	UFUNCTION(BlueprintPure, Category = "Karts|Objetos")
	int32 GetCharges() const { return Charges; }

	/** Segundos de ruleta que quedan (0 = ya se ve y se puede usar). */
	UFUNCTION(BlueprintPure, Category = "Karts|Objetos")
	float GetRouletteSecondsLeft() const;

	UFUNCTION(BlueprintPure, Category = "Karts|Objetos")
	bool IsBoosting() const;

	UFUNCTION(BlueprintPure, Category = "Karts|Objetos")
	float GetStarSecondsLeft() const;

	/** Lleva un objeto que ya se puede usar. */
	bool CanUseItem() const { return Item != ETNKartItem::None && Charges > 0 && GetRouletteSecondsLeft() <= 0.f; }

	// ── Servidor ──────────────────────────────────────────────────────────────

	/** Caja: un objeto según el puesto (TNKart::ItemWeightsForPlace) si no lleva ninguno. False si ya lleva uno. */
	bool TryGiveFromBox(int32 Place, int32 NumKarts);

	/** Pone Item con sus cargas (pruebas: TN.Kart.GiveItem); sin ruleta si bSkipRoulette. */
	void GiveItem(ETNKartItem NewItem, bool bSkipRoulette);

	/** Usa el objeto (quien lo pide ya está validado). bBackward: la concha sale hacia atrás. False si no se ha usado. */
	bool UseItem(bool bBackward);

	/** Lo que hace trompear una concha o el empujón de una estrella a un kart (respeta su escudo y el fantasma). */
	static bool SpinOut(ATN_Buggy& Victim, const FVector& HitDir);

private:
	ATN_Buggy* GetKart() const;
	double GetServerNow() const;
	/** Servidor y conductora local: empuje del acelerón y de la estrella hasta su velocidad tope. */
	void ApplyPush();
	/** Servidor: escudo continuo de la estrella y empujones a los karts que toca. */
	void TickStar(float DeltaTime);
	/** Servidor: un bot al volante usa el objeto cuando le conviene. */
	void TickBot(float DeltaTime);
	void FireShell(bool bHoming, bool bBackward);
	void DropAlga();
	void SpillInk();
	/** Puesto de este kart y número de karts en carrera (1/1 si no hay carrera). */
	void GetPlace(int32& OutPlace, int32& OutKarts) const;

	UPROPERTY(Replicated)
	ETNKartItem Item = ETNKartItem::None;

	UPROPERTY(Replicated)
	int32 Charges = 0;

	/** Horas del servidor del fin de la ruleta, del acelerón y de la estrella (0 = nada). */
	UPROPERTY(Replicated)
	float RouletteEndServerTime = 0.f;

	UPROPERTY(Replicated)
	float BoostEndServerTime = 0.f;

	UPROPERTY(Replicated)
	float StarEndServerTime = 0.f;

	double LastUseTime = -1000.0;
	bool bStarWasActive = false;
	float BotHeldSeconds = 0.f;
	/** Karts que ya ha apartado la estrella (y cuándo), para no empujarlos en cada fotograma. */
	TMap<TWeakObjectPtr<ATN_Buggy>, double> StarBumped;
};
