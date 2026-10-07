#include "World/TN_HazardEffects.h"

#include "Core/TN_Log.h"
#include "Player/TN_VitalsComponent.h"
#include "Player/TortugaCharacter.h"

void TNHazard::Apply(const FTNHazardEffect& Effect, ACharacter* Turtle, AActor* Source)
{
	ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	if (!TurtleCharacter || !TurtleCharacter->HasAuthority() || TurtleCharacter->IsDead())
	{
		return;
	}
	const ETNDeathCause Cause = Effect.DeathCause != ETNDeathCause::Unknown
		? Effect.DeathCause
		: TNDeathCause::FromInstigator(Source, TurtleCharacter);
	if (Effect.bKills)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Peligro] %s mata a %s (%s)"), *GetNameSafe(Source), *GetNameSafe(TurtleCharacter),
			*UEnum::GetValueAsString(Cause));
		TurtleCharacter->RequestKillBy(Cause);
		return;
	}
	UTN_VitalsComponent* Vitals = TurtleCharacter->GetVitalsComponent();
	if (!Vitals)
	{
		return;
	}
	if (Effect.Damage > 0.f)
	{
		Vitals->ApplyDamage(Effect.Damage, Source, Cause);
	}
	if (Effect.PoisonDamagePerSecond > 0.f && Effect.PoisonSeconds > 0.f)
	{
		Vitals->ApplyPoison(Effect.PoisonDamagePerSecond, Effect.PoisonSeconds);
	}
}

void TNHazard::Heal(ACharacter* Turtle, float Amount)
{
	const ATortugaCharacter* TurtleCharacter = Cast<ATortugaCharacter>(Turtle);
	UTN_VitalsComponent* Vitals = TurtleCharacter ? TurtleCharacter->GetVitalsComponent() : nullptr;
	if (Vitals && TurtleCharacter->HasAuthority())
	{
		Vitals->Heal(Amount);
	}
}
