// Comandos de depuración de los vitales (#855): vida, veneno e hidratación. Docs/Comandos_Prueba.md, «Vitales».
// Se ejecutan en el mundo con autoridad del mismo proceso (en PIE, también desde la ventana de un cliente) sobre la tortuga
// del jugador N (0 = anfitrión) o sobre todas con «todas». TN.Vitals.Dump escribe los valores de cada mundo (servidor y
// clientes del PIE) para comparar lo que ve cada uno.

#include "Player/TN_VitalsComponent.h"

#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Player/TortugaCharacter.h"

#if !UE_BUILD_SHIPPING

namespace TNVitalsCommands
{
	UWorld* FindVitalsAuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld || InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		const FString MapName = UWorld::RemovePIEPrefix(InWorld->GetMapName());
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate != InWorld && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client
				&& UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	/** Las tortugas a las que va el comando: la del jugador Index (por orden de PlayerId) o todas con «todas». */
	TArray<UTN_VitalsComponent*> PickVitals(UWorld* World, const FString& Who)
	{
		TArray<ATortugaCharacter*> Turtles;
		for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
		{
			Turtles.Add(*It);
		}
		Turtles.Sort([](const ATortugaCharacter& A, const ATortugaCharacter& B)
		{
			const int32 IdA = A.GetPlayerState() ? A.GetPlayerState()->GetPlayerId() : MAX_int32;
			const int32 IdB = B.GetPlayerState() ? B.GetPlayerState()->GetPlayerId() : MAX_int32;
			return IdA < IdB;
		});
		TArray<UTN_VitalsComponent*> Result;
		const bool bAll = Who.Equals(TEXT("todas"), ESearchCase::IgnoreCase);
		const int32 Index = Who.IsNumeric() ? FCString::Atoi(*Who) : 0;
		for (int32 i = 0; i < Turtles.Num(); ++i)
		{
			UTN_VitalsComponent* Vitals = Turtles[i]->GetVitalsComponent();
			if (Vitals && (bAll || i == Index))
			{
				Result.Add(Vitals);
			}
		}
		return Result;
	}

	float ArgOr(const TArray<FString>& Args, int32 Index, float Default)
	{
		return Args.IsValidIndex(Index) && Args[Index].IsNumeric() ? FCString::Atof(*Args[Index]) : Default;
	}

	FString WhoArg(const TArray<FString>& Args, int32 Index)
	{
		return Args.IsValidIndex(Index) ? Args[Index] : FString(TEXT("0"));
	}

	template <typename TAction>
	void RunOnTargets(UWorld* InWorld, const FString& Who, TAction Action)
	{
		UWorld* AuthWorld = FindVitalsAuthorityWorld(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Vitals] Sin mundo con autoridad en este proceso: lánzalo en el anfitrión."));
			return;
		}
		for (UTN_VitalsComponent* Vitals : PickVitals(AuthWorld, Who))
		{
			Action(*Vitals);
		}
	}

	void RunDamage(const TArray<FString>& Args, UWorld* World)
	{
		const float Amount = ArgOr(Args, 0, 20.f);
		RunOnTargets(World, WhoArg(Args, 1), [Amount](UTN_VitalsComponent& Vitals) { Vitals.ApplyDamage(Amount); });
	}

	void RunPoison(const TArray<FString>& Args, UWorld* World)
	{
		const float Dps = ArgOr(Args, 0, 5.f);
		const float Seconds = ArgOr(Args, 1, 4.f);
		RunOnTargets(World, WhoArg(Args, 2), [Dps, Seconds](UTN_VitalsComponent& Vitals) { Vitals.ApplyPoison(Dps, Seconds); });
	}

	void RunHeal(const TArray<FString>& Args, UWorld* World)
	{
		const float Amount = ArgOr(Args, 0, 25.f);
		RunOnTargets(World, WhoArg(Args, 1), [Amount](UTN_VitalsComponent& Vitals) { Vitals.Heal(Amount); });
	}

	void RunHydrate(const TArray<FString>& Args, UWorld* World)
	{
		const float Amount = ArgOr(Args, 0, 100.f);
		RunOnTargets(World, WhoArg(Args, 1), [Amount](UTN_VitalsComponent& Vitals) { Vitals.Hydrate(Amount); });
	}

	void RunDump(const TArray<FString>& Args, UWorld* World)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (!Candidate || !Candidate->IsGameWorld())
			{
				continue;
			}
			const TCHAR* Role = Candidate->GetNetMode() == NM_Client ? TEXT("cliente") : TEXT("servidor");
			for (TActorIterator<ATortugaCharacter> It(Candidate); It; ++It)
			{
				const UTN_VitalsComponent* Vitals = It->GetVitalsComponent();
				if (!Vitals)
				{
					continue;
				}
				const FString Line = FString::Printf(TEXT("[Vitals] %s %s: vida %.1f/%.0f, veneno %.1f/s (%.1f s), hidratación %.1f/%.0f"),
					Role, *GetNameSafe(It->GetPlayerState()), Vitals->GetHealth(), Vitals->GetMaxHealth(),
					Vitals->GetPoisonDamagePerSecond(), Vitals->GetPoisonSecondsLeft(), Vitals->GetHydration(), Vitals->GetMaxHydration());
				UE_LOG(LogTortunabo, Display, TEXT("%s"), *Line);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.f, FColor::Cyan, Line);
				}
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DamageCommand(
		TEXT("TN.Vitals.Damage"),
		TEXT("Quita vida: TN.Vitals.Damage [cantidad=20] [jugador=0|todas]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDamage), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs PoisonCommand(
		TEXT("TN.Vitals.Poison"),
		TEXT("Envenena: TN.Vitals.Poison [daño por segundo=5] [segundos=4] [jugador=0|todas]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPoison), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs HealCommand(
		TEXT("TN.Vitals.Heal"),
		TEXT("Cura vida: TN.Vitals.Heal [cantidad=25] [jugador=0|todas]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHeal), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs HydrateCommand(
		TEXT("TN.Vitals.Hydrate"),
		TEXT("Hidrata: TN.Vitals.Hydrate [cantidad=100] [jugador=0|todas]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHydrate), ECVF_Cheat);

	FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("TN.Vitals.Dump"),
		TEXT("Escribe en el registro y en pantalla los vitales de cada tortuga en cada mundo (servidor y clientes del PIE)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDump), ECVF_Cheat);
}

#endif
