#pragma once

#include "CoreMinimal.h"

/**
 * Emotes ocultos (#839): los dos emotes del código que no están en la rueda (el 4, «Aplaudir», y el 7, «Señalar») solo salen
 * al escribir «tortunabo» con el teclado durante la partida. Cuentas puras, sin mundo ni entrada: las usan
 * AMP_GamePlayerController (que lee las teclas) y ATortugaCharacter (que los reproduce, los pide al servidor y los acepta) y
 * las prueba Tortunabo.SecretEmote.
 *
 * No salen en la rueda (el catálogo DA_EmoteWheelCatalog no los lleva), ni en las teclas directas (IsValidWheelEmoteId los
 * rechaza), ni en menús o textos: no tienen nombre ni icono.
 */
namespace TNSecretEmote
{
	/** Lo que hay que teclear, en minúsculas. */
	inline constexpr TCHAR Code[] = TEXT("tortunabo");

	/** Los emotes ocultos, en el orden en que salen al repetir el código. */
	inline constexpr int32 HiddenEmotes[] = { 4, 7 };

	/** Enfriamiento de un emote oculto en el servidor (el de casi toda la rueda), ya que no tienen entrada en el catálogo. */
	inline constexpr float CooldownSeconds = 0.5f;

	inline bool IsHiddenEmote(int32 EmoteID)
	{
		for (const int32 Hidden : HiddenEmotes)
		{
			if (Hidden == EmoteID) { return true; }
		}
		return false;
	}

	/** El emote oculto de la vez Turn de escribir el código (0 la primera): salen por turnos, uno y otro. */
	inline int32 HiddenEmoteForTurn(int32 Turn)
	{
		return HiddenEmotes[FMath::Abs(Turn) % UE_ARRAY_COUNT(HiddenEmotes)];
	}

	/** La letra (en minúscula) de una tecla del teclado a partir de su nombre («T», «O»...). false si no es una letra («Space», «Zero»). */
	inline bool LetterFromKeyName(const FString& KeyName, TCHAR& OutLetter)
	{
		if (KeyName.Len() != 1 || !FChar::IsAlpha(KeyName[0])) { return false; }
		OutLetter = FChar::ToLower(KeyName[0]);
		return true;
	}

	/**
	 * Cuenta cuántas letras del código lleva escritas seguidas. Quien lee el teclado le pasa cada letra con Press (true al
	 * completar el código, y vuelve a empezar) y llama a Reset con cualquier otra tecla.
	 *
	 * Al equivocarse se reinicia, pero la letra que falla puede ser el principio de otro intento, o seguir lo que ya valía:
	 * se queda con el trozo más largo del principio del código en que acaba lo escrito (en «ttortunabo» el segundo «t» ya
	 * es el primero del código; en «tortortunabo» sigue «tor»).
	 */
	struct FCodeMatcher
	{
		explicit FCodeMatcher(const TCHAR* InCode = Code)
			: Target(InCode)
		{
			Target.ToLowerInline();
		}

		bool Press(TCHAR Letter)
		{
			if (Target.IsEmpty()) { return false; }
			Letter = FChar::ToLower(Letter);
			const FString Typed = Target.Left(Matched) + FString(1, &Letter);
			int32 Keep = FMath::Min(Typed.Len(), Target.Len());
			while (Keep > 0 && Typed.Right(Keep) != Target.Left(Keep))
			{
				--Keep;
			}
			Matched = Keep;
			if (Matched == Target.Len())
			{
				Matched = 0;
				return true;
			}
			return false;
		}

		void Reset() { Matched = 0; }

		/** Letras del código que lleva escritas ahora. */
		int32 GetMatched() const { return Matched; }

	private:
		FString Target;
		int32 Matched = 0;
	};
}
