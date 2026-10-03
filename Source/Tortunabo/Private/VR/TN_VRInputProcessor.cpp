#include "TN_VRInputProcessor.h"
#include "VR/TN_VRMath.h"
#include "VR/TN_VRMode.h"
#include "VR/TN_VRRig.h"
#include "VR/TN_VRSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Widgets/SViewport.h"

namespace TNVRInputDetail
{
	/** Espera antes de repetir una dirección mantenida y cada cuánto se repite luego. */
	constexpr float FirstRepeat = 0.42f;
	constexpr float NextRepeat = 0.13f;

	bool IsTrigger(const FKey& Key)
	{
		return Key == FTNVRKeys::LeftTrigger || Key == FTNVRKeys::RightTrigger;
	}

	bool IsStickDirection(const FKey& Key)
	{
		return Key == FTNVRKeys::LeftStickUp || Key == FTNVRKeys::LeftStickDown || Key == FTNVRKeys::LeftStickLeft
			|| Key == FTNVRKeys::LeftStickRight || Key == FTNVRKeys::RightStickUp || Key == FTNVRKeys::RightStickDown
			|| Key == FTNVRKeys::RightStickLeft || Key == FTNVRKeys::RightStickRight;
	}

	/** ¿El ratón está sobre la imagen del juego? (En el editor, los clics en sus ventanas no son del puntero VR.) */
	bool IsOverGameViewport(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
	{
		const TSharedPtr<SViewport> GameViewport = SlateApp.GetGameViewport();
		return GameViewport.IsValid() && GameViewport->GetTickSpaceGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition());
	}
}

FTNVRInputProcessor::FTNVRInputProcessor(UTN_VRSubsystem* InOwner)
	: Owner(InOwner)
{
}

ATN_VRRig* FTNVRInputProcessor::GetRig() const
{
	const UTN_VRSubsystem* Subsystem = Owner.Get();
	return Subsystem ? Subsystem->GetActiveRig() : nullptr;
}

bool FTNVRInputProcessor::IsMenuUp() const
{
	const UTN_VRSubsystem* Subsystem = Owner.Get();
	return Subsystem && Subsystem->IsMenuMode();
}

void FTNVRInputProcessor::SendKey(FSlateApplication& SlateApp, const FKey& Key, bool bDown, bool bRepeat) const
{
	const FKeyEvent Event(Key, SlateApp.GetModifierKeys(), UserIndex, bRepeat, 0, 0);
	if (bDown)
	{
		SlateApp.ProcessKeyDownEvent(Event);
	}
	else
	{
		SlateApp.ProcessKeyUpEvent(Event);
	}
}

void FTNVRInputProcessor::TapKey(FSlateApplication& SlateApp, const FKey& Key) const
{
	SendKey(SlateApp, Key, true, false);
	SendKey(SlateApp, Key, false, false);
}

void FTNVRInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	using namespace TNVRInputDetail;
	if (!IsMenuUp() || !TNVR::IsHeadset())
	{
		StickDir[0] = StickDir[1] = 0;
		return;
	}
	// Las direcciones ya llegan como botones: con esas basta.
	if (FPlatformTime::Seconds() - LastDigitalStickTime < 1.0)
	{
		return;
	}
	for (int32 s = 0; s < 2; ++s)
	{
		const int32 Dir = TNVRMath::StickDirection(Sticks[s]);
		if (Dir != StickDir[s])
		{
			StickDir[s] = Dir;
			if (Dir != 0)
			{
				TapKey(SlateApp, TNVRMath::DirectionKey(Dir));
				StickRepeat[s] = FirstRepeat;
			}
			continue;
		}
		if (Dir == 0)
		{
			continue;
		}
		StickRepeat[s] -= DeltaTime;
		if (StickRepeat[s] <= 0.f)
		{
			TapKey(SlateApp, TNVRMath::DirectionKey(Dir));
			StickRepeat[s] = NextRepeat;
		}
	}
}

bool FTNVRInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	using namespace TNVRInputDetail;
	const FKey Key = InKeyEvent.GetKey();
	if (!FTNVRKeys::IsVRKey(Key) || !IsMenuUp())
	{
		return false;
	}
	UserIndex = InKeyEvent.GetUserIndex();
	if (IsTrigger(Key))
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (ATN_VRRig* Rig = GetRig()) { Rig->PointerPress(); }
		}
		return true;
	}
	if (IsStickDirection(Key))
	{
		LastDigitalStickTime = FPlatformTime::Seconds();
	}
	// Los agarres cambian de pestaña por su eje (HandleAnalogInputEvent) si llega: el botón no lo repite.
	if ((Key == FTNVRKeys::LeftGrip || Key == FTNVRKeys::RightGrip) && FPlatformTime::Seconds() - LastGripAxisTime < 1.0)
	{
		return true;
	}
	const FKey Mapped = TNVRMath::MenuKeyFor(Key);
	if (Mapped.IsValid())
	{
		Held.Add(Key, Mapped);
		SendKey(SlateApp, Mapped, true, InKeyEvent.IsRepeat());
	}
	// Con un menú delante, ningún botón VR llega al juego.
	return true;
}

bool FTNVRInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	using namespace TNVRInputDetail;
	const FKey Key = InKeyEvent.GetKey();
	if (!FTNVRKeys::IsVRKey(Key))
	{
		return false;
	}
	if (IsTrigger(Key))
	{
		if (ATN_VRRig* Rig = GetRig()) { Rig->PointerRelease(); }
	}
	if (const FKey* Sent = Held.Find(Key))
	{
		const FKey SentKey = *Sent;
		Held.Remove(Key);
		SendKey(SlateApp, SentKey, false, false);
	}
	// El «soltar» sigue hacia el juego: Enhanced Input no se queda con el botón apretado.
	return false;
}

bool FTNVRInputProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	const float Value = InAnalogInputEvent.GetAnalogValue();
	const bool bTriggerAxis = Key == FTNVRKeys::LeftTriggerAxis || Key == FTNVRKeys::RightTriggerAxis;
	const bool bGripAxis = Key == FTNVRKeys::LeftGripAxis || Key == FTNVRKeys::RightGripAxis;
	if (bTriggerAxis || bGripAxis)
	{
		const int32 Side = (Key == FTNVRKeys::RightTriggerAxis || Key == FTNVRKeys::RightGripAxis) ? 1 : 0;
		// Se lleva la cuenta de si está apretado también jugando, sin hacer nada: al abrir un menú con el gatillo o el agarre
		// ya apretados (el menú se abrió con ellos), no cuentan como un clic o un cambio de pestaña hasta soltarlos (por
		// debajo del 35 %) y volver a apretar.
		const int32 Edge = TNVRMath::AnalogButton(Value, bTriggerAxis ? bTriggerAxisHeld[Side] : bGripAxisHeld[Side]);
		if (!IsMenuUp() || !TNVR::IsHeadset())
		{
			// Jugando no se toca nada (el juego los lee por Enhanced Input y ATN_VRRig); solo se suelta la pestaña que quedó
			// pulsada en un menú que se cerró con el agarre apretado.
			if (bGripAxis && Edge < 0 && bGripKeySent[Side])
			{
				bGripKeySent[Side] = false;
				const FKey Mapped = TNVRMath::MenuKeyFor(Side == 1 ? FTNVRKeys::RightGrip : FTNVRKeys::LeftGrip);
				if (Mapped.IsValid()) { SendKey(SlateApp, Mapped, false, false); }
			}
			return false;
		}
		UserIndex = InAnalogInputEvent.GetUserIndex();
		if (bTriggerAxis)
		{
			// Gatillo = clic del láser (soltar sin haber pulsado en el menú no hace nada: PointerRelease lo mira).
			if (ATN_VRRig* Rig = GetRig())
			{
				if (Edge > 0) { Rig->PointerPress(); }
				else if (Edge < 0) { Rig->PointerRelease(); }
			}
		}
		else
		{
			// Agarre = pestaña anterior (izquierdo) o siguiente (derecho). El «soltar», solo si su pulsación fue en el menú.
			LastGripAxisTime = FPlatformTime::Seconds();
			const FKey Mapped = TNVRMath::MenuKeyFor(Side == 1 ? FTNVRKeys::RightGrip : FTNVRKeys::LeftGrip);
			if (Mapped.IsValid() && (Edge > 0 || (Edge < 0 && bGripKeySent[Side])))
			{
				bGripKeySent[Side] = Edge > 0;
				SendKey(SlateApp, Mapped, Edge > 0, false);
			}
		}
		// Con un menú delante, los gatillos y los agarres no llegan al juego.
		return true;
	}
	if (Key == FTNVRKeys::LeftStickX) { Sticks[0].X = Value; }
	else if (Key == FTNVRKeys::LeftStickY) { Sticks[0].Y = Value; }
	else if (Key == FTNVRKeys::RightStickX) { Sticks[1].X = Value; }
	else if (Key == FTNVRKeys::RightStickY) { Sticks[1].Y = Value; }
	// Los ejes nunca se comen: el juego tiene que ver volver el stick a cero.
	return false;
}

bool FTNVRInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!TNVR::IsSimulated() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsMenuUp()
		|| !TNVRInputDetail::IsOverGameViewport(SlateApp, MouseEvent))
	{
		return false;
	}
	ATN_VRRig* Rig = GetRig();
	if (!Rig)
	{
		return false;
	}
	Rig->PointerPress();
	bMousePointerDown = true;
	return true;
}

bool FTNVRInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bMousePointerDown || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return false;
	}
	bMousePointerDown = false;
	if (ATN_VRRig* Rig = GetRig()) { Rig->PointerRelease(); }
	return true;
}

bool FTNVRInputProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	// El segundo clic rápido llega como doble clic: para el puntero es otra pulsación.
	return HandleMouseButtonDownEvent(SlateApp, MouseEvent);
}

bool FTNVRInputProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent)
{
	if (!TNVR::IsSimulated() || !IsMenuUp() || !TNVRInputDetail::IsOverGameViewport(SlateApp, InWheelEvent))
	{
		return false;
	}
	ATN_VRRig* Rig = GetRig();
	if (!Rig)
	{
		return false;
	}
	Rig->PointerScroll(InWheelEvent.GetWheelDelta());
	return true;
}
