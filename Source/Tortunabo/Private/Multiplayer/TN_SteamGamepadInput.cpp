#include "Multiplayer/TN_SteamGamepadInput.h"
#include "Core/TN_Log.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"

#if TN_WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END

// Las reglas de TNInputGlyphs llevan estos valores sin la cabecera de Steam: si un SDK nuevo los cambia, no compila.
static_assert(k_ESteamInputType_PS4Controller == 5 && k_ESteamInputType_PS3Controller == 12
	&& k_ESteamInputType_PS5Controller == 13 && k_ESteamInputType_SteamDeckController == 14,
	"ESteamInputType ha cambiado: revisa TNInputGlyphs::FamilyFromSteamInputType");
#endif

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del bloque.
namespace TNSteamGamepadInputDetail
{
	/** Quien espera el texto del teclado de pantalla completa (solo se toca en el hilo del juego). */
	TFunction<void(bool, const FString&)> PendingText;

	/** Llega el texto en el hilo del juego: se entrega una vez y se olvida. */
	void DeliverText(bool bSubmitted, const FString& Text)
	{
		check(IsInGameThread());
		TFunction<void(bool, const FString&)> Callback = MoveTemp(PendingText);
		PendingText = nullptr;
		if (Callback)
		{
			Callback(bSubmitted, Text);
		}
	}

#if TN_WITH_STEAMWORKS
	/**
	 * Escucha el cierre del teclado de pantalla completa. Steam llama desde donde el subsistema en línea ejecuta sus callbacks
	 * (puede ser su hilo propio): el texto se copia allí y se entrega en el hilo del juego.
	 */
	class FKeyboardListener
	{
	public:
		STEAM_CALLBACK(FKeyboardListener, OnDismissed, GamepadTextInputDismissed_t);
	};

	void FKeyboardListener::OnDismissed(GamepadTextInputDismissed_t* Param)
	{
		ISteamUtils* Utils = SteamUtils();
		const bool bSubmitted = Param && Param->m_bSubmitted && Utils;
		FString Text;
		if (bSubmitted)
		{
			const uint32 Length = Utils->GetEnteredGamepadTextLength();
			TArray<char> Buffer;
			Buffer.SetNumZeroed(static_cast<int32>(Length) + 1);
			if (Utils->GetEnteredGamepadTextInput(Buffer.GetData(), Length + 1))
			{
				Text = UTF8_TO_TCHAR(Buffer.GetData());
			}
		}
		AsyncTask(ENamedThreads::GameThread, [bSubmitted, Text]() { DeliverText(bSubmitted, Text); });
	}

	/** Se crea la primera vez que hace falta (con Steam ya iniciado) y se suelta antes de que se cierre Steam. */
	FKeyboardListener* Listener = nullptr;

	void EnsureListener()
	{
		if (Listener)
		{
			return;
		}
		Listener = new FKeyboardListener();
		FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			delete Listener;
			Listener = nullptr;
			PendingText = nullptr;
		});
	}

	bool TryOpen(ETNSteamKeyboard Kind, const FTNSteamKeyboardRequest& Request)
	{
		ISteamUtils* Utils = SteamUtils();
		if (!Utils)
		{
			return false;
		}
		if (Kind == ETNSteamKeyboard::Floating)
		{
			return Utils->ShowFloatingGamepadTextInput(k_EFloatingGamepadTextInputModeModeSingleLine, Request.FieldRect.Min.X,
				Request.FieldRect.Min.Y, Request.FieldRect.Width(), Request.FieldRect.Height());
		}
		const FTCHARToUTF8 Description(*Request.Description.ToString());
		const FTCHARToUTF8 Existing(*Request.ExistingText);
		return Utils->ShowGamepadTextInput(k_EGamepadTextInputModeNormal, k_EGamepadTextInputLineModeSingleLine, Description.Get(),
			static_cast<uint32>(FMath::Max(1, Request.MaxChars)), Existing.Get());
	}
#endif

#if !UE_BUILD_SHIPPING
	/**
	 * Teclado de Steam simulado, para probar el código de sala sin Steam Deck ni Big Picture: TN.Steam.FakeKeyboard 1 (el de
	 * pantalla completa) o 2 (el flotante) hace que abrirlo funcione, y TN.Steam.KeyboardText <texto|cancelar> lo cierra
	 * como si se hubiera escrito ese texto (o cancelado).
	 */
	TAutoConsoleVariable<int32> CVarFakeKeyboard(TEXT("TN.Steam.FakeKeyboard"), 0,
		TEXT("Teclado de Steam simulado: 0 el de verdad, 1 el de pantalla completa, 2 el flotante."), ECVF_Cheat);

	TOptional<ETNSteamKeyboard> OpenFakeKeyboard(bool bUsingGamepad, TFunction<void(bool, const FString&)>& OnText)
	{
		const int32 Fake = CVarFakeKeyboard.GetValueOnGameThread();
		if (Fake <= 0 || !bUsingGamepad)
		{
			return {};
		}
		const ETNSteamKeyboard Kind = Fake == 2 ? ETNSteamKeyboard::Floating : ETNSteamKeyboard::Overlay;
		PendingText = Kind == ETNSteamKeyboard::Overlay ? MoveTemp(OnText) : nullptr;
		UE_LOG(LogTortunabo, Log, TEXT("[Steam] Teclado en pantalla simulado abierto (%s)."),
			Kind == ETNSteamKeyboard::Floating ? TEXT("flotante") : TEXT("pantalla completa"));
		return Kind;
	}

	void FakeKeyboardText(const TArray<FString>& Args)
	{
		const FString Text = FString::Join(Args, TEXT(" "));
		const bool bCancelled = Text.IsEmpty() || Text == TEXT("cancelar");
		UE_LOG(LogTortunabo, Log, TEXT("[Steam] Teclado simulado cerrado: %s."), bCancelled ? TEXT("cancelado") : *Text);
		DeliverText(!bCancelled, bCancelled ? FString() : Text);
	}

	FAutoConsoleCommand CmdFakeKeyboardText(TEXT("TN.Steam.KeyboardText"),
		TEXT("TN.Steam.KeyboardText <texto|cancelar>: cierra el teclado de Steam simulado con ese texto (o cancelado)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FakeKeyboardText));
#endif
}

bool TNSteamGamepadInput::IsSteamActive()
{
#if TN_WITH_STEAMWORKS
	// Solo con el subsistema de Steam en marcha (su API iniciada): sin él no se llama a nada de steam_api (ni se carga).
	if (!IOnlineSubsystem::IsLoaded(STEAM_SUBSYSTEM))
	{
		return false;
	}
	const IOnlineSubsystem* Online = IOnlineSubsystem::Get();
	return Online && Online->GetSubsystemName() == STEAM_SUBSYSTEM && SteamUtils() != nullptr;
#else
	return false;
#endif
}

bool TNSteamGamepadInput::IsOnSteamDeck()
{
#if TN_WITH_STEAMWORKS
	return IsSteamActive() && SteamUtils()->IsSteamRunningOnSteamDeck();
#else
	return false;
#endif
}

int32 TNSteamGamepadInput::GetPadInputType()
{
#if TN_WITH_STEAMWORKS
	ISteamInput* Input = IsSteamActive() ? SteamInput() : nullptr;
	if (!Input)
	{
		return 0;
	}
	// Init sin RunFrame explícito: lo hace SteamAPI_RunCallbacks, que ya llama el subsistema de Steam. No cambia cómo llega
	// el mando al juego (la emulación de mando sigue igual): solo deja preguntar qué mando hay detrás.
	static const bool bInputReady = Input->Init(false);
	if (!bInputReady)
	{
		return 0;
	}
	InputHandle_t Handle = Input->GetControllerForGamepadIndex(0);
	if (Handle == 0)
	{
		InputHandle_t Connected[STEAM_INPUT_MAX_COUNT] = {};
		Handle = Input->GetConnectedControllers(Connected) > 0 ? Connected[0] : 0;
	}
	return Handle != 0 ? static_cast<int32>(Input->GetInputTypeForHandle(Handle)) : 0;
#else
	return 0;
#endif
}

TArray<ETNSteamKeyboard> TNSteamGamepadInput::KeyboardOrder(bool bSteamActive, bool bOnDeck, bool bUsingGamepad)
{
	if (!bSteamActive || !bUsingGamepad)
	{
		return {};
	}
	return bOnDeck ? TArray<ETNSteamKeyboard>{ ETNSteamKeyboard::Floating, ETNSteamKeyboard::Overlay }
		: TArray<ETNSteamKeyboard>{ ETNSteamKeyboard::Overlay, ETNSteamKeyboard::Floating };
}

TOptional<ETNSteamKeyboard> TNSteamGamepadInput::OpenKeyboard(const FTNSteamKeyboardRequest& Request, bool bUsingGamepad,
	TFunction<void(bool, const FString&)> OnText)
{
	using namespace TNSteamGamepadInputDetail;
#if !UE_BUILD_SHIPPING
	if (const TOptional<ETNSteamKeyboard> Fake = OpenFakeKeyboard(bUsingGamepad, OnText))
	{
		return Fake;
	}
#endif
#if TN_WITH_STEAMWORKS
	const bool bSteam = IsSteamActive();
	for (const ETNSteamKeyboard Kind : KeyboardOrder(bSteam, bSteam && IsOnSteamDeck(), bUsingGamepad))
	{
		if (Kind == ETNSteamKeyboard::Overlay)
		{
			EnsureListener();
		}
		if (TryOpen(Kind, Request))
		{
			PendingText = Kind == ETNSteamKeyboard::Overlay ? MoveTemp(OnText) : nullptr;
			UE_LOG(LogTortunabo, Log, TEXT("[Steam] Teclado en pantalla abierto (%s)."),
				Kind == ETNSteamKeyboard::Floating ? TEXT("flotante") : TEXT("pantalla completa"));
			return Kind;
		}
	}
	if (bSteam && bUsingGamepad)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Steam] Sin teclado en pantalla (fuera de Big Picture y de la Steam Deck): se escribe con el mando en las casillas."));
	}
#endif
	return {};
}

void TNSteamGamepadInput::CancelPendingKeyboard()
{
	TNSteamGamepadInputDetail::PendingText = nullptr;
}
