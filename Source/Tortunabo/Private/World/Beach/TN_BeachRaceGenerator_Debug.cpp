// ─────────────────────────────────────────────────────────────────────────────
// Consola de pruebas del reparto de la playa del modo carrera (ronda 3; Docs/Comandos_Prueba.md):
//   TN.Beach.Go <salida|fortaleza|trinchera|poza|cresta|sprint|acantilado|meta|metros> [jugador]
//       lleva a una tortuga allí: la tuya o, con [jugador], la de ese índice. Fortaleza, trinchera, poza y cresta: la
//       siguiente por delante de la tortuga (si no hay, la primera). Un número: esos metros desde la línea de salida (0-800).
//   TN.Race.Difficulty [Easy|Normal|Hard]
//       dificultad del reparto desde la próxima ronda (sin argumento, dice la actual). TN.Beach.Reroll la aplica ya.
//   TN.Beach.Reroll [semilla]
//       rehace la ronda con la dificultad actual (semilla al azar si no se da).
// Van al mundo con autoridad del mismo proceso: en el anfitrión o desde la ventana de un cliente del PIE (el servidor
// mueve su tortuga y también se mueve en su ventana). A un cliente remoto de verdad no le llegan: escríbelo en el
// anfitrión con el índice del jugador. La ronda se monta por partes: lo que la necesita espera a que esté lista.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachLayout.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/TortugaCharacter.h"
#include "Player/TN_ShellComponent.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"

namespace TNBeachGoConsole
{
	/** Centímetros por metro: las posiciones van en cm y los mensajes en m. */
	constexpr double CmPerMeter = 100.0;

	/** El mundo con autoridad del mismo proceso: el propio si no es un cliente; en el PIE, el del servidor del mismo mapa. */
	UWorld* AuthorityWorldOf(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		if (GEngine)
		{
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
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] Los comandos del reparto van en la ventana del anfitrión (o en la de un cliente del PIE)."));
		return nullptr;
	}

	/** El generador de la playa del mundo con autoridad (avisa si no hay). */
	ATN_BeachRaceGenerator* FindGenerator(UWorld* World, const TCHAR* Command)
	{
		ATN_BeachRaceGenerator* Gen = World ? ATN_BeachRaceGenerator::Find(World) : nullptr;
		if (!Gen)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] %s: no hay playa de carrera en este mapa (LVL_BeachRace)."), Command);
		}
		return Gen;
	}

	const TCHAR* DifficultyName(ETNProcDifficulty Value)
	{
		switch (Value)
		{
			case ETNProcDifficulty::Easy: return TEXT("fácil");
			case ETNProcDifficulty::Hard: return TEXT("difícil");
			default: return TEXT("normal");
		}
	}

	bool ParseDifficulty(const FString& Arg, ETNProcDifficulty& Out)
	{
		const FString A = Arg.ToLower();
		if (A == TEXT("easy") || A == TEXT("facil") || A == TEXT("fácil") || A == TEXT("0")) { Out = ETNProcDifficulty::Easy; return true; }
		if (A == TEXT("normal") || A == TEXT("1")) { Out = ETNProcDifficulty::Normal; return true; }
		if (A == TEXT("hard") || A == TEXT("dificil") || A == TEXT("difícil") || A == TEXT("2")) { Out = ETNProcDifficulty::Hard; return true; }
		return false;
	}

	/** Corre Action cuando la ronda del generador está montada entera (ya, si lo está). */
	void WhenRoundReady(ATN_BeachRaceGenerator& Gen, TFunction<void(ATN_BeachRaceGenerator&)> Action)
	{
		if (Gen.IsRoundReady())
		{
			Action(Gen);
			return;
		}
		TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
		*Handle = Gen.OnRoundLayoutReady.AddLambda([Handle, Action](ATN_BeachRaceGenerator* Ready)
		{
			if (!Ready)
			{
				return;
			}
			Ready->OnRoundLayoutReady.Remove(*Handle);
			Action(*Ready);
		});
	}

	// ── TN.Beach.Go ──────────────────────────────────────────────────────────

	enum class EGoTarget : uint8
	{
		Start,
		Fortress,
		Trench,
		Pool,
		Ridge,
		Sprint,
		Cliff,
		Finish,
		Meters
	};

	bool ParseTarget(const FString& Arg, EGoTarget& Out, double& OutMeters)
	{
		FString A = Arg.ToLower();
		A.RemoveFromEnd(TEXT("m"));
		if (A.IsNumeric())
		{
			Out = EGoTarget::Meters;
			OutMeters = FCString::Atod(*A);
			return true;
		}
		A = Arg.ToLower();
		if (A == TEXT("salida") || A == TEXT("start")) { Out = EGoTarget::Start; return true; }
		if (A == TEXT("fortaleza") || A == TEXT("fortress")) { Out = EGoTarget::Fortress; return true; }
		if (A == TEXT("trinchera") || A == TEXT("trench")) { Out = EGoTarget::Trench; return true; }
		if (A == TEXT("poza") || A == TEXT("pool")) { Out = EGoTarget::Pool; return true; }
		if (A == TEXT("cresta") || A == TEXT("ridge")) { Out = EGoTarget::Ridge; return true; }
		if (A == TEXT("sprint")) { Out = EGoTarget::Sprint; return true; }
		if (A == TEXT("acantilado") || A == TEXT("cliff")) { Out = EGoTarget::Cliff; return true; }
		if (A == TEXT("meta") || A == TEXT("finish")) { Out = EGoTarget::Finish; return true; }
		return false;
	}

	/** Índice (en el GameState del mundo con autoridad) del jugador de Pawn, o INDEX_NONE. */
	int32 PlayerIndexOf(const UWorld& World, const APawn* Pawn)
	{
		const AGameStateBase* GS = World.GetGameState();
		if (!GS || !Pawn)
		{
			return INDEX_NONE;
		}
		for (int32 i = 0; i < GS->PlayerArray.Num(); ++i)
		{
			if (GS->PlayerArray[i] && GS->PlayerArray[i]->GetPawn() == Pawn)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * La tortuga a mover en el mundo con autoridad: la del jugador PlayerIndex o, si es INDEX_NONE, la del jugador de la
	 * ventana donde se escribe (en un cliente del PIE, su copia en el servidor, por su PlayerId). OutLocal: la copia de la
	 * ventana del cliente (null si se escribe en el anfitrión o se da el índice de otro jugador).
	 */
	APawn* ResolvePawn(UWorld& InWorld, UWorld& AuthWorld, int32 PlayerIndex, APawn*& OutLocal)
	{
		OutLocal = nullptr;
		const AGameStateBase* GS = AuthWorld.GetGameState();
		if (PlayerIndex != INDEX_NONE)
		{
			if (!GS || !GS->PlayerArray.IsValidIndex(PlayerIndex) || !GS->PlayerArray[PlayerIndex])
			{
				return nullptr;
			}
			return GS->PlayerArray[PlayerIndex]->GetPawn();
		}
		const APlayerController* LocalPC = InWorld.GetFirstPlayerController();
		APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
		if (&InWorld == &AuthWorld)
		{
			return LocalPawn;
		}
		const APlayerState* LocalPS = LocalPC ? LocalPC->PlayerState.Get() : nullptr;
		if (!GS || !LocalPS)
		{
			return nullptr;
		}
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS && PS->GetPlayerId() == LocalPS->GetPlayerId())
			{
				OutLocal = LocalPawn;
				return PS->GetPawn();
			}
		}
		return nullptr;
	}

	/**
	 * Un sitio libre cerca de Want (espacio local): sin el núcleo de ningún elemento de la ronda a menos de 1,5 m ni agua de
	 * poza, buscando en espiral hasta 45 m. Si no hay, Want.
	 */
	FVector2D FreeSpotNear(const TNBeachLayout::FRoundLayout& Layout, const FVector2D& Want)
	{
		for (int32 Ring = 0; Ring <= 15; ++Ring)
		{
			const int32 Steps = Ring == 0 ? 1 : 12;
			for (int32 k = 0; k < Steps; ++k)
			{
				const double Ang = TNProcMap::TwoPi * (k + 0.5 * (Ring % 2)) / Steps;
				const FVector2D P = Want + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * (300.0 * Ring);
				if (TNBeachLayout::PoolAt(P, 1.05) != INDEX_NONE)
				{
					continue;
				}
				bool bFree = true;
				for (const TNBeachLayout::FItem& Item : Layout.Items)
				{
					if (Item.bOverlay)
					{
						continue;
					}
					double T = 0.0;
					if (TNProcMap::DistPointSegment(P, Item.EndA(), Item.EndB(), T) < Item.Core + 150.0)
					{
						bFree = false;
						break;
					}
				}
				if (bFree)
				{
					return P;
				}
			}
		}
		return Want;
	}

	/** Lo siguiente por delante de FromX (o, si no hay nada, lo primero) de una lista ordenable por su X. */
	template <typename TItem, typename FGetX>
	const TItem* NextAhead(const TArray<const TItem*>& Options, double FromX, FGetX&& GetX)
	{
		const TItem* Best = nullptr;
		const TItem* First = nullptr;
		for (const TItem* Option : Options)
		{
			const double X = GetX(*Option);
			if (!First || X < GetX(*First)) { First = Option; }
			if (X > FromX + 1000.0 && (!Best || X < GetX(*Best))) { Best = Option; }
		}
		return Best ? Best : First;
	}

	/**
	 * A dónde va la tortuga (transformada del mundo, ya sobre el suelo) para Target, con la tortuga ahora en FromLocal
	 * (espacio local del generador). False si ese sitio no existe en esta ronda (sin fortalezas, por ejemplo).
	 */
	bool ResolveTarget(const ATN_BeachRaceGenerator& Gen, EGoTarget Target, double Meters, const FVector& FromLocal, int32 PlayerIndex, FTransform& OutWorld, FString& OutWhat)
	{
		const int32 Slot = FMath::Max(0, PlayerIndex);
		if (Target == EGoTarget::Start)
		{
			OutWorld = Gen.GetStartTransform(Slot);
			OutWhat = FString::Printf(TEXT("la salida (huevo %d)"), Slot);
			return true;
		}
		if (Target == EGoTarget::Sprint)
		{
			OutWorld = Gen.GetSprintStartTransform(Slot);
			OutWhat = FString::Printf(TEXT("la línea del sprint final (sitio %d)"), Slot);
			return true;
		}
		const TNBeachLayout::FRoundLayout& Layout = Gen.GetRoundLayout();
		const double FromY = FMath::Clamp(FromLocal.Y, -TNBeachLayout::HalfWidth + 1500.0, TNBeachLayout::HalfWidth - 1500.0);
		FVector2D Want(FromLocal.X, FromY);
		double Yaw = 0.0;
		switch (Target)
		{
			case EGoTarget::Fortress:
			{
				TArray<const TNBeachLayout::FItem*> Forts;
				for (const TNBeachLayout::FItem& Item : Layout.Items)
				{
					if (Item.Element == ETNBeachElement::FortressMedium || Item.Element == ETNBeachElement::FortressLarge || Item.Element == ETNBeachElement::FortressColossal)
					{
						Forts.Add(&Item);
					}
				}
				const TNBeachLayout::FItem* Fort = NextAhead(Forts, FromLocal.X, [](const TNBeachLayout::FItem& Item) { return Item.Pos.X; });
				if (!Fort)
				{
					return false;
				}
				// Delante de su puerta de la salida (-X local), mirándola.
				Want = Fort->Pos - Fort->Axis() * (Fort->Radius + 1500.0);
				Yaw = Fort->Yaw;
				OutWhat = FString::Printf(TEXT("la fortaleza %s a %.0f m"), *UEnum::GetValueAsString(Fort->Element), Fort->Pos.X / CmPerMeter);
				break;
			}
			case EGoTarget::Trench:
			{
				TArray<const TNBeachLayout::FTrench*> Lines;
				for (const TNBeachLayout::FTrench& Trench : TNBeachLayout::Trenches()) { Lines.Add(&Trench); }
				const TNBeachLayout::FTrench* Line = NextAhead(Lines, FromLocal.X, [](const TNBeachLayout::FTrench& Trench) { return Trench.Min.X; });
				if (!Line)
				{
					return false;
				}
				Want = FVector2D(Line->Min.X - 1200.0, FMath::Clamp(FromY, Line->Min.Y + 500.0, Line->Max.Y - 500.0));
				OutWhat = FString::Printf(TEXT("la trinchera de los %.0f m"), Line->Min.X / CmPerMeter);
				break;
			}
			case EGoTarget::Pool:
			{
				TArray<const TNBeachLayout::FPool*> Pools;
				for (const TNBeachLayout::FPool& Pool : TNBeachLayout::Pools()) { Pools.Add(&Pool); }
				const TNBeachLayout::FPool* Pool = NextAhead(Pools, FromLocal.X, [](const TNBeachLayout::FPool& P) { return P.Center.X; });
				if (!Pool)
				{
					return false;
				}
				Want = Pool->Center - FVector2D(Pool->OuterR() + 800.0, 0.0);
				OutWhat = FString::Printf(TEXT("la poza de los %.0f m"), Pool->Center.X / CmPerMeter);
				break;
			}
			case EGoTarget::Ridge:
			{
				TArray<const TNBeachLayout::FRidge*> Ridges;
				for (const TNBeachLayout::FRidge& Ridge : TNBeachLayout::Ridges())
				{
					if (Ridge.bLip) { Ridges.Add(&Ridge); }
				}
				const TNBeachLayout::FRidge* Ridge = NextAhead(Ridges, FromLocal.X, [](const TNBeachLayout::FRidge& R) { return R.Center.X; });
				if (!Ridge)
				{
					return false;
				}
				// Al pie de su cara empinada (la de la salida), mirando a la cornisa.
				const double Slip = TNBeachLayout::RidgeSlipWidth(TNBeachLayout::RidgeCrestHeight(*Ridge, 0.0));
				Want = TNBeachLayout::RidgeCrestPoint(*Ridge, 0.0) - Ridge->Windward * (Slip + 900.0);
				Yaw = FMath::RadiansToDegrees(FMath::Atan2(Ridge->Windward.Y, Ridge->Windward.X));
				OutWhat = FString::Printf(TEXT("la cresta con cornisa de los %.0f m"), Ridge->Center.X / CmPerMeter);
				break;
			}
			case EGoTarget::Cliff:
				Want = FVector2D(TNBeachLayout::EdgeX(FromY) - 2500.0, FromY);
				OutWhat = TEXT("el acantilado (25 m antes del filo)");
				break;
			case EGoTarget::Finish:
				Want = FVector2D(TNBeachLayout::EdgeX(FromY) - 350.0, FromY);
				OutWhat = TEXT("la meta (al borde del filo: un paso y al agua)");
				break;
			case EGoTarget::Meters:
			default:
			{
				const double X = FMath::Clamp(Meters * CmPerMeter, -500.0, TNBeachLayout::EdgeX(FromY) - 200.0);
				Want = FVector2D(X, FromY);
				OutWhat = FString::Printf(TEXT("los %.0f m del recorrido"), X / CmPerMeter);
				break;
			}
		}
		const FVector2D Spot = Target == EGoTarget::Finish ? Want : FreeSpotNear(Layout, Want);
		const FTransform& Xf = Gen.GetActorTransform();
		const FVector World2D = Xf.TransformPosition(FVector(Spot.X, Spot.Y, 0.0));
		const double Ground = Gen.GetGroundHeightAt(World2D);
		OutWorld = FTransform(FRotator(0.0, Gen.GetActorRotation().Yaw + Yaw, 0.0), FVector(World2D.X, World2D.Y, Ground + 150.0));
		return true;
	}

	/** Mueve la tortuga (en el servidor, también la saca de la bola o del caparazón) y le gira la cámara. */
	void TeleportTurtle(APawn& Pawn, const FTransform& Where, bool bAuthority)
	{
		if (bAuthority)
		{
			if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(&Pawn))
			{
				if (Turtle->IsKnockedDown()) { Turtle->RecoverFromKnockdown(); }
				if (UTN_ShellComponent* Shell = Turtle->GetShellComponent())
				{
					Shell->SetExitLocked(false);
					Shell->ForceExitShell();
				}
			}
		}
		UCharacterMovementComponent* Move = nullptr;
		if (ACharacter* Character = Cast<ACharacter>(&Pawn)) { Move = Character->GetCharacterMovement(); }
		if (Move) { Move->StopMovementImmediately(); }
		const FRotator Rotation = Where.Rotator();
		Pawn.SetActorLocationAndRotation(Where.GetLocation(), Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		if (Move) { Move->SetMovementMode(MOVE_Falling); }
		if (APlayerController* PC = Cast<APlayerController>(Pawn.GetController()))
		{
			PC->SetControlRotation(Rotation);
			if (bAuthority && !PC->IsLocalController()) { PC->ClientSetRotation(Rotation, true); }
		}
	}

	void RunGo(const TArray<FString>& Args, UWorld* InWorld)
	{
		EGoTarget Target = EGoTarget::Start;
		double Meters = 0.0;
		if (Args.Num() < 1 || !ParseTarget(Args[0], Target, Meters))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Uso: TN.Beach.Go <salida|fortaleza|trinchera|poza|cresta|sprint|acantilado|meta|metros> [jugador]. Metros: desde la línea de salida, de 0 a 800 (p. ej. TN.Beach.Go 400, la mitad del recorrido)."));
			return;
		}
		UWorld* AuthWorld = AuthorityWorldOf(InWorld);
		ATN_BeachRaceGenerator* Gen = FindGenerator(AuthWorld, TEXT("TN.Beach.Go"));
		if (!Gen || !InWorld)
		{
			return;
		}
		const int32 PlayerArg = Args.Num() > 1 && Args[1].IsNumeric() ? FCString::Atoi(*Args[1]) : INDEX_NONE;
		TWeakObjectPtr<UWorld> WeakIn(InWorld);
		TWeakObjectPtr<UWorld> WeakAuth(AuthWorld);
		WhenRoundReady(*Gen, [WeakIn, WeakAuth, PlayerArg, Target, Meters](ATN_BeachRaceGenerator& ReadyGen)
		{
			UWorld* In = WeakIn.Get();
			UWorld* Auth = WeakAuth.Get();
			if (!In || !Auth)
			{
				return;
			}
			APawn* LocalCopy = nullptr;
			APawn* Pawn = ResolvePawn(*In, *Auth, PlayerArg, LocalCopy);
			if (!Pawn)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Go: no encuentro la tortuga (¿jugador %d?)."), PlayerArg);
				return;
			}
			const int32 Slot = PlayerArg != INDEX_NONE ? PlayerArg : PlayerIndexOf(*Auth, Pawn);
			const FVector FromLocal = ReadyGen.GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
			FTransform Where;
			FString What;
			if (!ResolveTarget(ReadyGen, Target, Meters, FromLocal, Slot, Where, What))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Go: en esta ronda no hay ese sitio."));
				return;
			}
			TeleportTurtle(*Pawn, Where, true);
			if (LocalCopy) { TeleportTurtle(*LocalCopy, Where, false); }
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Go: %s va a %s (%.0f m, %.0f m a lo ancho)."), *Pawn->GetName(), *What,
				ReadyGen.GetActorTransform().InverseTransformPosition(Where.GetLocation()).X / CmPerMeter,
				ReadyGen.GetActorTransform().InverseTransformPosition(Where.GetLocation()).Y / CmPerMeter);
		});
	}

	// ── TN.Race.Difficulty y TN.Beach.Reroll ─────────────────────────────────

	void RunDifficulty(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = AuthorityWorldOf(InWorld);
		ATN_BeachRaceGenerator* Gen = FindGenerator(AuthWorld, TEXT("TN.Race.Difficulty"));
		if (!Gen)
		{
			return;
		}
		UMP_GameInstance* GI = Cast<UMP_GameInstance>(AuthWorld->GetGameInstance());
		if (Args.Num() == 0)
		{
			const TNBeachLayout::FDifficultyProfile P = TNBeachLayout::DifficultyProfileOf(Gen->Difficulty);
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Dificultad: ronda actual %s; próximas rondas %s (ayudas x%.1f, trampas x%.1f, enemigos x%.1f). Uso: TN.Race.Difficulty <Easy|Normal|Hard>."),
				DifficultyName(Gen->GetRoundDifficulty()), DifficultyName(Gen->Difficulty), P.Aids, P.Traps, P.Enemies);
			return;
		}
		ETNProcDifficulty Wanted = ETNProcDifficulty::Normal;
		if (!ParseDifficulty(Args[0], Wanted))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Uso: TN.Race.Difficulty <Easy|Normal|Hard> (o fácil, normal, difícil)."));
			return;
		}
		// En la GameInstance también: el GameMode de la carrera la vuelve a poner antes de cada ronda con la del general.
		if (GI) { GI->SelectedProcDifficulty = Wanted; }
		Gen->Difficulty = Wanted;
		const TNBeachLayout::FDifficultyProfile P = TNBeachLayout::DifficultyProfileOf(Wanted);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Race.Difficulty: %s desde la próxima ronda (ayudas x%.1f, trampas x%.1f, enemigos x%.1f). TN.Beach.Reroll la aplica ya."),
			DifficultyName(Wanted), P.Aids, P.Traps, P.Enemies);
	}

	void RunReroll(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = AuthorityWorldOf(InWorld);
		ATN_BeachRaceGenerator* Gen = FindGenerator(AuthWorld, TEXT("TN.Beach.Reroll"));
		if (!Gen)
		{
			return;
		}
		const int32 Seed = Args.Num() > 0 && Args[0].IsNumeric() ? FCString::Atoi(*Args[0]) : (FMath::Rand() & 0x7FFFFFFF);
		Gen->GenerateRound(Seed);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Reroll: ronda %d con la semilla %d y dificultad %s; se monta por partes (los huevos vuelven a cerrarse)."),
			Gen->GetRoundNumber(), Seed, DifficultyName(Gen->Difficulty));
		const int32 Round = Gen->GetRoundNumber();
		WhenRoundReady(*Gen, [Round](ATN_BeachRaceGenerator& ReadyGen)
		{
			const TNBeachLayout::FRoundLayout& Layout = ReadyGen.GetRoundLayout();
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Reroll: ronda %d lista: %d elementos (%d decorado, %d trampas, %d enemigos), %d fortalezas, %d cofres."),
				Round, Layout.Items.Num(), Layout.NumDecor, Layout.NumTraps, Layout.NumEnemies, Layout.NumFortresses, Layout.NumChests);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs CmdBeachGo(TEXT("TN.Beach.Go"),
		TEXT("Playa del modo carrera: lleva tu tortuga (o la del jugador N) a un sitio: TN.Beach.Go <salida|fortaleza|trinchera|poza|cresta|sprint|acantilado|meta|metros> [jugador]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunGo));

	FAutoConsoleCommandWithWorldAndArgs CmdRaceDifficulty(TEXT("TN.Race.Difficulty"),
		TEXT("Playa del modo carrera: dificultad del reparto desde la próxima ronda (Easy, Normal o Hard; sin argumento, la actual). TN.Beach.Reroll la aplica ya."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDifficulty));

	FAutoConsoleCommandWithWorldAndArgs CmdBeachReroll(TEXT("TN.Beach.Reroll"),
		TEXT("Playa del modo carrera: rehace la ronda con la dificultad actual. TN.Beach.Reroll [semilla] (al azar si no se da)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunReroll));
}
