#pragma once

// C++ puro (sin tipos de Unreal), como el motor de música: así se compila y se prueba fuera del motor en el arnés.
// UTN_MatchMusicSubsystem (TN_MatchMusicSubsystem.cpp) le pasa las fotos del estado replicado y aplica sus peticiones.
#include "TN_MusicSynthDSP.h"
#include <cstdint>

/**
 * Director de la música de fin de partida: decide qué pista toca a partir de una foto del estado replicado (flujo de la
 * partida, jugador local y equipo) que le llega varias veces por segundo, y solo pide algo cuando cambia lo que debería
 * sonar (una petición repetida no reinicia nada).
 *
 * Solo actúa en un mundo donde ha visto la partida en marcha (InProgress): el lobby y los mapas de transición no suenan
 * aunque arrastren estados de la partida anterior.
 *  - Llegar a la meta: Victory (fanfarria y bucle festivo mientras miras a los demás).
 *  - Eliminado con la partida en marcha: Eliminated (jingle corto, sin bucle) tras un pequeño margen, para no pisar la
 *    derrota si la partida se acaba justo por esa muerte. Si te reviven, se apaga.
 *  - Cuenta atrás (Countdown): el equipo llegó = Victory; si no, Eliminated si no ha sonado ya.
 *  - Fin de partida (Results): victoria del equipo (alguien llegó a la meta) = Victory; si no, Defeat.
 *  - El resultado se decide tras un momento de asentamiento y se reevalúa un poco más por si la replicación trae los
 *    datos desordenados; después queda fijo.
 *  - El último segundo de la cuenta atrás funde a silencio (antes del viaje al lobby o de la ronda siguiente); preparar
 *    o empezar una ronda nueva también.
 */
namespace TNMatchMusic
{
	/** Fase del flujo de la partida (mismo orden que ETNMatchFlowState). */
	enum class EFlow : uint8_t { WaitingForPlayers, Countdown, Cinematic, InProgress, Results };

	/** Foto del estado que le interesa al director (la rellena el subsistema a partir de los estados replicados). */
	struct FSnapshot
	{
		double NowSeconds = 0.0;
		EFlow Flow = EFlow::WaitingForPlayers;
		/** Ya ha llegado el PlayerState del jugador local. */
		bool bHasLocalPlayer = false;
		/** El jugador local cruzó la meta (bHasFinishedRun sin bIsEliminated). */
		bool bLocalFinished = false;
		/** El jugador local está eliminado o muerto (bIsEliminated o !bIsAlive; el DBNO todavía no cuenta). */
		bool bLocalOut = false;
		/** Alguien del equipo llegó a la meta (jugadores o tabla de resultados). */
		bool bTeamReachedGoal = false;
		/** Segundos que muestra la cuenta atrás de Countdown/Results. */
		int32_t CountdownValue = 0;
	};

	/** Petición al componente de música: pista (TNMusic::ETrack) y fundido. bValid = false si no hay que tocar nada. */
	struct FRequest
	{
		bool bValid = false;
		uint8_t Track = TNMusic::ETrack::None;
		float FadeSeconds = 0.8f;
	};

	class FDirector
	{
	public:
		/** Margen antes de decidir el resultado al entrar en Countdown/Results (la replicación puede llegar desordenada). */
		static constexpr double OutcomeSettleSeconds = 0.35;
		/** Hasta aquí se reevalúa el resultado; luego queda fijo. */
		static constexpr double OutcomeLockSeconds = 1.6;
		/** Margen antes del jingle de eliminado (si la partida acaba por esa muerte, suena la derrota y no el jingle). */
		static constexpr double EliminatedSettleSeconds = 0.45;
		/** Tiempo mínimo de resultado antes del fundido final de la cuenta atrás. */
		static constexpr double TailFadeMinSeconds = 2.0;

		void Reset() { *this = FDirector(); }

		bool IsArmed() const { return bArmed; }
		uint8_t GetCurrentTrack() const { return CurrentTrack; }

		/** Alguien ha puesto otra pista a mano (TN.Music.Play): el director la da por sonando. */
		void NotifyExternalTrack(uint8_t InTrack) { CurrentTrack = InTrack; }

		FRequest Update(const FSnapshot& In)
		{
			FRequest Out;
			if (!bHasFlow || In.Flow != LastFlow)
			{
				bHasFlow = true;
				LastFlow = In.Flow;
				PhaseStart = In.NowSeconds;
				EnterFlow(In, Out);
			}
			if (!bArmed) { return Out; }

			switch (In.Flow)
			{
			case EFlow::InProgress:
				UpdateRacing(In, Out);
				break;
			case EFlow::Countdown:
			case EFlow::Results:
				UpdateOutcome(In, Out);
				break;
			default:
				break;
			}
			return Out;
		}

	private:
		enum class EOutcome : uint8_t { Win, RoundLoss, MatchLoss };

		void Ask(FRequest& Out, uint8_t InTrack, float InFadeSeconds)
		{
			Out.bValid = true;
			Out.Track = InTrack;
			Out.FadeSeconds = InFadeSeconds;
			CurrentTrack = InTrack;
		}

		void EnterFlow(const FSnapshot& In, FRequest& Out)
		{
			bEliminatedPending = false;
			bOutcomeLocked = false;
			bTailFaded = false;
			switch (In.Flow)
			{
			case EFlow::InProgress:
				// Partida en marcha: el director se arma y parte de la situación actual como base, para
				// no confundir estados viejos con una llegada o una eliminación.
				bArmed = true;
				bRoundLossSignaled = false;
				TakeBaseline(In);
				if (CurrentTrack != TNMusic::ETrack::None) { Ask(Out, TNMusic::ETrack::None, 1.5f); }
				break;
			case EFlow::WaitingForPlayers:
			case EFlow::Cinematic:
				// Se prepara otra ronda: fuera la música de la anterior.
				if (bArmed && CurrentTrack != TNMusic::ETrack::None) { Ask(Out, TNMusic::ETrack::None, 1.5f); }
				break;
			default:
				// Countdown y Results: el resultado se decide en UpdateOutcome, pasado el margen de asentamiento.
				break;
			}
		}

		void TakeBaseline(const FSnapshot& In)
		{
			bBaseline = In.bHasLocalPlayer;
			if (!bBaseline) { return; }
			bWasFinished = In.bLocalFinished;
			bWasOut = In.bLocalOut;
		}

		void UpdateRacing(const FSnapshot& In, FRequest& Out)
		{
			if (!In.bHasLocalPlayer) { return; }
			if (!bBaseline)
			{
				TakeBaseline(In);
				return;
			}

			if (In.bLocalFinished && !bWasFinished)
			{
				bEliminatedPending = false;
				// Llegar ya asegura la victoria del equipo.
				if (CurrentTrack != TNMusic::ETrack::Victory)
				{
					Ask(Out, TNMusic::ETrack::Victory, 0.5f);
				}
			}
			if (In.bLocalOut && !bWasOut)
			{
				bEliminatedPending = true;
				EliminatedAt = In.NowSeconds;
			}
			if (!In.bLocalOut && bWasOut)
			{
				// Rescatado: de vuelta a la partida sin música.
				bEliminatedPending = false;
				if (CurrentTrack == TNMusic::ETrack::Eliminated) { Ask(Out, TNMusic::ETrack::None, 0.6f); }
			}
			if (bEliminatedPending && In.bLocalOut && In.NowSeconds - EliminatedAt >= EliminatedSettleSeconds)
			{
				bEliminatedPending = false;
				bRoundLossSignaled = true;
				Ask(Out, TNMusic::ETrack::Eliminated, 0.3f);
			}
			bWasFinished = In.bLocalFinished;
			bWasOut = In.bLocalOut;
		}

		EOutcome ChooseOutcome(const FSnapshot& In) const
		{
			const bool bRoundEnd = In.Flow == EFlow::Countdown;
			if (In.bTeamReachedGoal || In.bLocalFinished) { return EOutcome::Win; }
			return bRoundEnd ? EOutcome::RoundLoss : EOutcome::MatchLoss;
		}

		void UpdateOutcome(const FSnapshot& In, FRequest& Out)
		{
			const double InPhase = In.NowSeconds - PhaseStart;
			// Último segundo de la cuenta atrás: fundido a silencio para llegar callados al viaje o a la ronda nueva.
			if (!bTailFaded && InPhase >= TailFadeMinSeconds && In.CountdownValue <= 1)
			{
				bTailFaded = true;
				bOutcomeLocked = true;
				if (CurrentTrack != TNMusic::ETrack::None) { Ask(Out, TNMusic::ETrack::None, 1.1f); }
				return;
			}
			if (bOutcomeLocked || InPhase < OutcomeSettleSeconds) { return; }

			const float Fade = CurrentTrack == TNMusic::ETrack::None ? 0.3f : 0.6f;
			switch (ChooseOutcome(In))
			{
			case EOutcome::Win:
				if (CurrentTrack != TNMusic::ETrack::Victory) { Ask(Out, TNMusic::ETrack::Victory, Fade); }
				break;
			case EOutcome::MatchLoss:
				if (CurrentTrack != TNMusic::ETrack::Defeat) { Ask(Out, TNMusic::ETrack::Defeat, Fade); }
				break;
			case EOutcome::RoundLoss:
				// Ronda perdida con la partida en marcha: el jingle corto una vez por ronda (si ya sonó al eliminarte, no
				// se repite); una pista larga que no toca se apaga.
				if (!bRoundLossSignaled)
				{
					bRoundLossSignaled = true;
					Ask(Out, TNMusic::ETrack::Eliminated, Fade);
				}
				else if (CurrentTrack == TNMusic::ETrack::Victory || CurrentTrack == TNMusic::ETrack::Defeat)
				{
					Ask(Out, TNMusic::ETrack::None, 0.6f);
				}
				break;
			}
			if (InPhase >= OutcomeLockSeconds) { bOutcomeLocked = true; }
		}

		bool bHasFlow = false;
		EFlow LastFlow = EFlow::WaitingForPlayers;
		double PhaseStart = 0.0;
		bool bArmed = false;
		bool bBaseline = false;
		bool bWasFinished = false;
		bool bWasOut = false;
		bool bEliminatedPending = false;
		double EliminatedAt = 0.0;
		bool bRoundLossSignaled = false;
		bool bOutcomeLocked = false;
		bool bTailFaded = false;
		uint8_t CurrentTrack = TNMusic::ETrack::None;
	};
}
