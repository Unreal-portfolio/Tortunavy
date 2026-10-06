#pragma once

#include "CoreMinimal.h"
#include "Settings/TN_SettingsSaveGame.h"

/**
 * Reglas puras del modo local (#311): hasta cuatro jugadores en el mismo PC a pantalla partida, sin Steam ni sesión. Sin
 * mundo ni controladores: las usan UTN_LocalPlaySubsystem, UMP_GameInstance y UTN_GameSettingsSubsystem, y las prueban
 * los tests de Tortunabo.LocalPlay. Docs/Modo_Local.md.
 */
namespace TNLocalPlay
{
	/** Jugadores locales como mucho (el motor no dibuja más de cuatro vistas: UGameViewportClient::MaxSplitscreenPlayers). */
	inline constexpr int32 MaxPlayers = 4;

	/** Segundos que un invitado mantiene B en el lobby para dejar de jugar (un toque de B es meterse en el caparazón). */
	inline constexpr float LeaveHoldSeconds = 1.5f;

	/** Un trozo de la pantalla en fracciones del viewport (0..1): esquina de arriba a la izquierda y tamaño. */
	struct FViewRect
	{
		float X = 0.f;
		float Y = 0.f;
		float W = 1.f;
		float H = 1.f;

		bool operator==(const FViewRect& Other) const
		{
			return FMath::IsNearlyEqual(X, Other.X) && FMath::IsNearlyEqual(Y, Other.Y) && FMath::IsNearlyEqual(W, Other.W)
				&& FMath::IsNearlyEqual(H, Other.H);
		}
	};

	/**
	 * Reparto de la pantalla para NumPlayers (se recorta a 1..4), uno por jugador en su orden: 1, la pantalla entera; 2, en
	 * horizontal (el 1 arriba y el 2 abajo); 3 y 4, en cuadrantes (1 arriba a la izquierda, 2 arriba a la derecha, 3 abajo a
	 * la izquierda y 4 abajo a la derecha). Con 3 el cuadrante de abajo a la derecha se queda libre (EmptyQuadrant).
	 */
	TORTUNABO_API TArray<FViewRect> SplitLayout(int32 NumPlayers);

	/** El cuadrante que queda sin jugador (solo con 3: abajo a la derecha). false si no sobra ninguno. */
	TORTUNABO_API bool EmptyQuadrant(int32 NumPlayers, FViewRect& OutRect);

	/**
	 * Escala de más de la interfaz con NumViews vistas (multiplica la del jugador 1): 1 con una, 0,75 con dos (vistas a lo
	 * ancho y de media altura) y 0,6 con tres o cuatro (cuadrantes). Así el HUD de cada jugador cabe en su trozo.
	 */
	TORTUNABO_API float UIScaleForViews(int32 NumViews);

	/** Con tres o cuatro vistas se baja un poco la calidad (distancia de dibujo y sombras) mientras dure la partida local. */
	TORTUNABO_API bool ShouldReduceQuality(int32 NumViews);

	/** Un nivel de calidad de escalabilidad un punto más bajo (0 es el mínimo; -1, «personalizado», se queda igual). */
	TORTUNABO_API int32 ReducedQualityLevel(int32 Level);

	/** Respuesta a un mando que pulsa Start para unirse. */
	enum class EJoin : uint8
	{
		/** Entra: se crea su jugador local y su tortuga aparece en el lobby. */
		Accept,
		/** No es una partida local (en red se entra por la sala). */
		NotLocal,
		/** Solo se entra en el lobby (en una partida empezada, no). */
		NotLobby,
		/** Ya hay cuatro. */
		Full,
		/** El teclado (y el ratón) son siempre del jugador 1. */
		Keyboard,
		/** Ese mando ya tiene jugador (también el del jugador 1). */
		AlreadyPlaying,
	};

	struct FJoinQuery
	{
		bool bLocalMode = false;
		bool bInLobby = false;
		int32 Players = 0;
		bool bGamepad = false;
		bool bDeviceHasPlayer = false;
	};

	TORTUNABO_API EJoin DecideJoin(const FJoinQuery& Query);

	/** Número del siguiente invitado (2, 3 o 4): el más bajo libre. INDEX_NONE si están los tres. */
	TORTUNABO_API int32 NextGuestNumber(const TArray<int32>& TakenNumbers);

	/** ¿Puede dejar de jugar este jugador (mantener B o «Dejar de jugar»)? Los invitados, en el lobby; el jugador 1, nunca. */
	TORTUNABO_API bool CanLeave(bool bLocalMode, bool bInLobby, bool bPrimary);

	/**
	 * ¿Se guarda lo que cambia este jugador (ajustes, controles, cosméticos, puntos de la tienda, tutorial)? En red, siempre;
	 * en local, solo lo del jugador 1: lo de los invitados dura la partida.
	 */
	TORTUNABO_API bool ShouldSave(bool bLocalMode, bool bPrimary);

	/** ¿Abre el micrófono la tortuga? Solo la de este PC y nunca en la partida local, que no tiene chat de voz (#650). */
	TORTUNABO_API bool ShouldOpenVoiceCapture(bool bLocalMode, bool bLocallyOwned);

	/**
	 * ¿Puede el controlador crear su HUD? Solo uno local y con su jugador ya asignado: el de un invitado que entra en una
	 * partida local hace BeginPlay dentro del Login, antes de SetPlayer, y CreateWidget falla (#650).
	 */
	TORTUNABO_API bool CanCreatePlayerWidgets(bool bLocalController, bool bHasLocalPlayer);

	/**
	 * Mandos que el jugador 1 deja libres al empezar la partida local: todos los que el sistema le había dado menos el que usó
	 * para elegir «Local» (ChosenPad; INDEX_NONE si fue con el teclado o el ratón). Así cualquier otro mando puede unirse con
	 * Start y un mando nunca mueve a dos tortugas.
	 */
	TORTUNABO_API TArray<int32> PadsToRelease(const TArray<int32>& PrimaryUserPads, int32 ChosenPad);

	/**
	 * Copia los ajustes de cada jugador (sensibilidad e inversión de la cámara, teclas y botones, tecla del menú, temblor de
	 * cámara y campo de visión) de From a To. El resto (sonido, gráficos, idioma, accesibilidad...) es del PC entero y lo
	 * decide el jugador 1.
	 */
	TORTUNABO_API void CopyPerPlayerSettings(const FTNGameSettings& From, FTNGameSettings& To);

	/** Los ajustes con los que juega un jugador: los del PC (Shared, del jugador 1) con los suyos (Own) encima. */
	TORTUNABO_API FTNGameSettings EffectiveSettings(const FTNGameSettings& Shared, const FTNGameSettings& Own);
}
