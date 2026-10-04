// Menú de pausa con mando y teclado (#311): la cruceta, el stick, las flechas, A y B recorren y usan sus filas, en la partida
// de siempre (el menú principal abre los mismos ajustes), en las pantallas de salas (las mismas filas) y en la pantalla
// partida, donde solo lo maneja el jugador que lo abrió. Las pulsaciones entran como las manda el motor (FSlateApplication,
// con el mando de un usuario de la plataforma), así que hace falta el juego con su ventana pintándose: en el editor estas
// pruebas no salen (son de contexto de cliente).
//   UnrealEditor-Win64-DebugGame.exe <uproject> -game -RenderOffScreen -NoSteam -ResX=1280 -ResY=720 -unattended -nosound
//       -ExecCmds="Automation RunTests Tortunabo.UI.PausePad; Quit"

#include "Misc/AutomationTest.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "UI/Loading/TN_LoadingScreenSubsystem.h"
#include "UI/Menu/TN_RoomMenuWidget.h"
#include "UI/Pause/TN_PauseMenuWidget.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "UObject/UObjectIterator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNPausePadTestDetail
{
	/** Segundos que puede tardar un paso (cargar el lobby de la partida local en DebugGame es lo más lento). */
	constexpr double StepTimeout = 240.0;

	UWorld* GameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	APlayerController* PlayerAt(UWorld* World, int32 Index)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance || !GameInstance->GetLocalPlayers().IsValidIndex(Index))
		{
			return nullptr;
		}
		const ULocalPlayer* Player = GameInstance->GetLocalPlayers()[Index];
		return Player ? Player->GetPlayerController(World) : nullptr;
	}

	/** Ni pantalla de carga ni viaje, y con jugador: el mundo ya se puede usar. */
	bool IsSettled(UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UTN_LoadingScreenSubsystem* Loading = GameInstance ? GameInstance->GetSubsystem<UTN_LoadingScreenSubsystem>() : nullptr;
		return GameInstance && !World->IsInSeamlessTravel() && !(Loading && Loading->IsShowing()) && PlayerAt(World, 0);
	}

	/** La fila del menú que tiene el foco de PC (null si ninguna). */
	UTN_PauseRow* FocusedRow(UWorld* World, APlayerController* PC)
	{
		for (TObjectIterator<UTN_PauseRow> It; It; ++It)
		{
			UTN_PauseRow* Row = *It;
			if (IsValid(Row) && Row->GetWorld() == World && Row->GetCachedWidget().IsValid() && Row->HasUserFocus(PC))
			{
				return Row;
			}
		}
		return nullptr;
	}

	/** ¿La fila está dentro de una lista (la de una pestaña de ajustes)? Las de la portada y las pestañas, no. */
	bool IsInList(const UTN_PauseRow* Row)
	{
		for (const UPanelWidget* Parent = Row ? Row->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			if (Parent->IsA<UScrollBox>())
			{
				return true;
			}
		}
		return false;
	}

	FVector2D RowAt(const UTN_PauseRow* Row)
	{
		return Row ? FVector2D(Row->GetCachedGeometry().GetAbsolutePosition()) : FVector2D::ZeroVector;
	}

	/** Para los mensajes: la fila, dónde está y en qué panel. */
	FString Describe(const UTN_PauseRow* Row)
	{
		if (!Row)
		{
			return TEXT("ninguna");
		}
		const FVector2D At = RowAt(Row);
		return FString::Printf(TEXT("%s en %s (%.0f, %.0f)"), *Row->GetName(), *GetNameSafe(Row->GetParent()), At.X, At.Y);
	}

	float RowValue(const UTN_PauseRow* Row)
	{
		return Row ? Row->GetSliderValue() : -1.f;
	}

	/** Un mando de mentira del usuario User, como lo daría el sistema al enchufarlo. */
	FInputDeviceId ConnectPad(FPlatformUserId User)
	{
		IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
		const FInputDeviceId Pad = Mapper.AllocateNewInputDeviceId();
		Mapper.Internal_MapInputDeviceToUser(Pad, User, EInputDeviceConnectionState::Connected);
		return Pad;
	}

	void DisconnectPad(FInputDeviceId Pad)
	{
		if (Pad.IsValid())
		{
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			Mapper.Internal_MapInputDeviceToUser(Pad, Mapper.GetUserForUnpairedInputDevices(), EInputDeviceConnectionState::Disconnected);
		}
	}

	/** Pulsa y suelta un botón del mando Pad (de su usuario). */
	void TapPad(FInputDeviceId Pad, const FName& Button)
	{
		const FPlatformUserId User = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Pad);
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.OnControllerButtonPressed(Button, User, Pad, false);
		Slate.OnControllerButtonReleased(Button, User, Pad, false);
	}

	/** El stick izquierdo hacia abajo y de vuelta al centro, como lo manda el motor: el eje y su botón de dirección. */
	void FlickStickDown(FInputDeviceId Pad)
	{
		const FPlatformUserId User = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(Pad);
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.OnControllerAnalog(FGamepadKeyNames::LeftAnalogY, User, Pad, -1.f);
		Slate.OnControllerButtonPressed(FGamepadKeyNames::LeftStickDown, User, Pad, false);
		Slate.OnControllerAnalog(FGamepadKeyNames::LeftAnalogY, User, Pad, 0.f);
		Slate.OnControllerButtonReleased(FGamepadKeyNames::LeftStickDown, User, Pad, false);
	}

	/** El ratón, a mitad de la fila (como si pasara por encima). */
	void HoverRow(const UTN_PauseRow* Row)
	{
		if (!Row)
		{
			return;
		}
		const FVector2D At = FVector2D(Row->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
		FSlateApplication& Slate = FSlateApplication::Get();
		const FPointerEvent Move(FSlateApplication::CursorPointerIndex, At, At - FVector2D(4.0, 0.0), TSet<FKey>(), EKeys::Invalid, 0.f,
			FModifierKeysState());
		Slate.ProcessMouseMoveEvent(Move);
	}

	/** Pulsa y suelta una tecla del teclado. */
	void TapKey(const FKey& Key)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const int32 User = Slate.GetUserIndexForKeyboard();
		Slate.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), User, false, 0, 0));
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), User, false, 0, 0));
	}

	/**
	 * Pasos de una prueba, uno detrás de otro y cada uno en su fotograma: un paso se repite hasta que devuelve true. Un paso
	 * que falla del todo pone bAbort y la prueba acaba ahí.
	 */
	struct FScript
	{
		TArray<TFunction<bool()>> Steps;
		bool bAbort = false;

		void Then(TFunction<bool()> Step) { Steps.Add(MoveTemp(Step)); }

		/** Deja pasar Frames fotogramas (la interfaz se pinta y el foco que se pidió llega). */
		void Wait(int32 Frames)
		{
			TSharedRef<int32> Left = MakeShared<int32>(Frames);
			Steps.Add([Left]() { return --(*Left) <= 0; });
		}
	};

	class FRunScript : public IAutomationLatentCommand
	{
	public:
		FRunScript(FAutomationTestBase* InTest, TSharedRef<FScript> InScript, TFunction<void()> InCleanup)
			: Test(InTest), Script(InScript), Cleanup(MoveTemp(InCleanup))
		{
		}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StepStart < 0.0) { StepStart = Now; }
			if (!Script->bAbort && Index < Script->Steps.Num())
			{
				if (Now - StepStart > StepTimeout)
				{
					Test->AddError(FString::Printf(TEXT("El paso %d no acaba en %.0f s."), Index, StepTimeout));
					Script->bAbort = true;
				}
				else
				{
					// Un paso por fotograma: la navegación y el foco que pide uno se ven en el siguiente.
					if (Script->Steps[Index]())
					{
						++Index;
						StepStart = Now;
					}
					return false;
				}
			}
			if (Cleanup) { Cleanup(); }
			return true;
		}

	private:
		FAutomationTestBase* Test;
		TSharedRef<FScript> Script;
		TFunction<void()> Cleanup;
		int32 Index = 0;
		double StepStart = -1.0;
	};

	/** Estado de una prueba: el mundo, quién maneja el menú, su mando y las filas vistas. */
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APlayerController> Owner;
		FInputDeviceId Pad = INPUTDEVICEID_NONE;
		TWeakObjectPtr<UTN_PauseRow> First;
		TWeakObjectPtr<UTN_PauseRow> Second;
		TWeakObjectPtr<UTN_PauseRow> Saved;
		float SavedValue = 0.f;
		int32 Tries = 0;
		double SettledSince = -1.0;

		UTN_PauseRow* Focused() const { return FocusedRow(World.Get(), Owner.Get()); }
	};

	/** Espera a que el mundo de juego lleve Seconds segundos seguidos quieto (y, si se pide, a que se cumpla Extra). */
	TFunction<bool()> WaitSettled(TSharedRef<FState> State, double Seconds, TFunction<bool(UWorld*)> Extra = nullptr)
	{
		return [State, Seconds, Extra]()
		{
			UWorld* World = GameWorld();
			if (!IsSettled(World) || (Extra && !Extra(World)))
			{
				State->SettledSince = -1.0;
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			if (State->SettledSince < 0.0) { State->SettledSince = Now; }
			if (Now - State->SettledSince < Seconds)
			{
				return false;
			}
			State->SettledSince = -1.0;
			State->World = World;
			return true;
		};
	}

	/** Comprueba bCond; si no se cumple, apunta el error y para la prueba. Devuelve true (el paso acaba igual). */
	bool Check(FAutomationTestBase* Test, FScript& Script, const TCHAR* What, bool bCond)
	{
		if (!Test->TestTrue(What, bCond))
		{
			Script.bAbort = true;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPausePadNavigationTest,
	"Tortunabo.UI.PausePad.Navigation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTNPausePadNavigationTest::RunTest(const FString& Parameters)
{
	using namespace TNPausePadTestDetail;
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Sin Slate: hace falta el juego con su ventana."));
		return true;
	}
	TSharedRef<FScript> Script = MakeShared<FScript>();
	TSharedRef<FState> State = MakeShared<FState>();
	FScript& S = Script.Get();
	FAutomationTestBase* Test = this;

	// El menú de pausa (en una partida) o los ajustes del menú principal (el mismo menú), con un mando del jugador 1.
	S.Then(WaitSettled(State, 2.0));
	S.Then([Test, Script, State]()
	{
		UWorld* World = State->World.Get();
		APlayerController* PC = PlayerAt(World, 0);
		UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(World);
		if (Settings && PC)
		{
			if (Settings->CanOpenPauseMenu(PC)) { Settings->OpenPauseMenu(PC); }
			else { Settings->OpenMainMenuSettings(PC); }
		}
		State->Owner = PC;
		State->Pad = ConnectPad(IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser());
		return Check(Test, *Script, TEXT("Se abre el menú de pausa"), Settings && Settings->IsPauseMenuOpen());
	});
	S.Wait(10);
	S.Then([Test, Script, State]()
	{
		State->First = State->Focused();
		return Check(Test, *Script, TEXT("Al abrirse, una fila tiene el foco del jugador"), State->First.IsValid());
	});

	// Cruceta abajo y arriba.
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadDown); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		State->Second = Now;
		return Check(Test, *Script, TEXT("Cruceta abajo: la fila de debajo"),
			Now && Now != State->First.Get() && RowAt(Now).Y > RowAt(State->First.Get()).Y);
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadUp); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Cruceta arriba: vuelve a la primera"), State->Focused() == State->First.Get());
	});

	// Stick izquierdo abajo; flechas del teclado arriba y abajo.
	S.Then([State]() { FlickStickDown(State->Pad); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Stick abajo: la fila de debajo"), State->Focused() == State->Second.Get());
	});
	S.Then([]() { TapKey(EKeys::Up); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Flecha arriba: vuelve a la primera"), State->Focused() == State->First.Get());
	});
	S.Then([]() { TapKey(EKeys::Down); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Flecha abajo: la de debajo («Ajustes»)"), State->Focused() == State->Second.Get());
	});

	// El ratón sigue enfocando al pasar por encima.
	S.Then([State]() { HoverRow(State->First.Get()); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Ratón encima de la primera: la enfoca"), State->Focused() == State->First.Get());
	});
	S.Then([State]() { HoverRow(State->Second.Get()); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Ratón encima de «Ajustes»: la enfoca"), State->Focused() == State->Second.Get());
	});

	// A: entra en «Ajustes»; RB: la pestaña de al lado. El foco, en su lista.
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonBottom); return true; });
	S.Wait(4);
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::RightShoulder); return true; });
	S.Wait(4);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		return Check(Test, *Script, TEXT("A en «Ajustes» y RB: el foco en la lista de la pestaña"), IsInList(Now));
	});
	// Abajo hasta un deslizador que se pueda cambiar (en «Sonido», el volumen general).
	S.Then([State]() { State->Tries = 0; return true; });
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		if (Now && Now->GetKind() == ETNPauseRowKind::Slider && Now->IsRowEnabled())
		{
			State->Saved = Now;
			State->SavedValue = RowValue(Now);
			return true;
		}
		if (++State->Tries > 12)
		{
			return Check(Test, *Script, TEXT("Abajo llega a un deslizador"), false);
		}
		TapPad(State->Pad, FGamepadKeyNames::DPadDown);
		return false;
	});
	// Derecha (o izquierda, si ya estaba al máximo) cambia el valor sin mover el foco; la contraria lo devuelve.
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Row = State->Saved.Get();
		TapPad(State->Pad, FGamepadKeyNames::DPadRight);
		FName Back = FGamepadKeyNames::DPadLeft;
		if (FMath::IsNearlyEqual(RowValue(Row), State->SavedValue))
		{
			TapPad(State->Pad, FGamepadKeyNames::DPadLeft);
			Back = FGamepadKeyNames::DPadRight;
		}
		Check(Test, *Script, TEXT("Cruceta derecha o izquierda: cambia el valor de la fila"), !FMath::IsNearlyEqual(RowValue(Row), State->SavedValue));
		Check(Test, *Script, TEXT("Cambiar el valor no mueve el foco"), State->Focused() == Row);
		TapPad(State->Pad, Back);
		return Check(Test, *Script, TEXT("Y la contraria: el valor de antes"), FMath::IsNearlyEqual(RowValue(Row), State->SavedValue));
	});

	// Arriba hasta la barra de pestañas; allí, derecha cambia de pestaña y el foco se queda en la barra.
	S.Then([State]() { State->Tries = 0; return true; });
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		if (Now && !IsInList(Now))
		{
			State->First = Now;
			return Check(Test, *Script, TEXT("Arriba desde la lista: la pestaña abierta"), Now->IsActive());
		}
		if (++State->Tries > 16)
		{
			return Check(Test, *Script, TEXT("Arriba llega a la barra de pestañas"), false);
		}
		TapPad(State->Pad, FGamepadKeyNames::DPadUp);
		return false;
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadRight); return true; });
	S.Wait(3);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		UTN_PauseRow* ListRow = State->Saved.Get();
		Test->AddInfo(FString::Printf(TEXT("Barra de pestañas: %s; con derecha, %s."), *Describe(State->First.Get()), *Describe(Now)));
		Check(Test, *Script, TEXT("Derecha en la barra: el foco pasa a otra pestaña de la barra"),
			Now && Now != State->First.Get() && !IsInList(Now) && FMath::IsNearlyEqual(RowAt(Now).Y, RowAt(State->First.Get()).Y, 2.0));
		Check(Test, *Script, TEXT("Derecha en la barra: se abre esa pestaña (la lista se rehace)"),
			Now && Now->IsActive() && (!ListRow || !ListRow->GetParent()));
		State->Second = Now;
		return true;
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadLeft); return true; });
	S.Wait(3);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		return Check(Test, *Script, TEXT("Izquierda en la barra: vuelve a la pestaña de antes, abierta"), Now && Now == State->First.Get() && Now->IsActive());
	});

	// B: atrás (a la portada) y, otra vez, se cierra.
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonRight); return true; });
	S.Wait(3);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		return Check(Test, *Script, TEXT("B: vuelve a la portada, con el foco en una de sus filas"),
			Now && Now->GetKind() == ETNPauseRowKind::Button && !IsInList(Now));
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonRight); return true; });
	S.Wait(3);
	S.Then([Test, Script, State]()
	{
		const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(State->World.Get());
		return Check(Test, *Script, TEXT("B en la portada: se cierra"), Settings && !Settings->IsPauseMenuOpen());
	});

	ADD_LATENT_AUTOMATION_COMMAND(FRunScript(this, Script, [State]()
	{
		if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(State->World.Get())) { Settings->ClosePauseMenu(); }
		DisconnectPad(State->Pad);
	}));
	return true;
}

// Las pantallas de salas del menú principal («Crear partida» y «Unirse») usan las mismas filas: también con el mando.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPausePadRoomsTest,
	"Tortunabo.UI.PausePad.RoomsMenu",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTNPausePadRoomsTest::RunTest(const FString& Parameters)
{
	using namespace TNPausePadTestDetail;
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Sin Slate: hace falta el juego con su ventana."));
		return true;
	}
	TSharedRef<FScript> Script = MakeShared<FScript>();
	TSharedRef<FState> State = MakeShared<FState>();
	TSharedRef<TWeakObjectPtr<UTN_RoomMenuWidget>> Rooms = MakeShared<TWeakObjectPtr<UTN_RoomMenuWidget>>();
	FScript& S = Script.Get();
	FAutomationTestBase* Test = this;

	auto FindIn = [](UWorld* World) -> UTN_RoomMenuWidget*
	{
		for (TObjectIterator<UTN_RoomMenuWidget> It; It; ++It)
		{
			if (IsValid(*It) && It->GetWorld() == World && It->GetCachedWidget().IsValid()) { return *It; }
		}
		return nullptr;
	};
	auto CodeFieldFocused = [](UWorld* World, APlayerController* PC)
	{
		for (TObjectIterator<UTN_RoomCodeField> It; It; ++It)
		{
			if (IsValid(*It) && It->GetWorld() == World && It->HasUserFocus(PC)) { return true; }
		}
		return false;
	};

	// En el menú principal.
	S.Then(WaitSettled(State, 1.0, [FindIn](UWorld* World) { return FindIn(World) != nullptr; }));
	S.Then([Test, Script, State, Rooms, FindIn]()
	{
		UWorld* World = State->World.Get();
		*Rooms = FindIn(World);
		State->Owner = PlayerAt(World, 0);
		State->Pad = ConnectPad(IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser());
		if (UTN_RoomMenuWidget* Menu = Rooms->Get()) { Menu->Open(ETNRoomMenuPage::Create); }
		return Check(Test, *Script, TEXT("Se abre «Crear partida»"), Rooms->IsValid() && (*Rooms)->IsOpen());
	});
	S.Wait(6);
	S.Then([Test, Script, State]()
	{
		State->First = State->Focused();
		return Check(Test, *Script, TEXT("«Crear partida»: una fila con el foco"), State->First.IsValid());
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadUp); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		return Check(Test, *Script, TEXT("Cruceta arriba: la fila de encima"), Now && Now != State->First.Get() && RowAt(Now).Y < RowAt(State->First.Get()).Y);
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadDown); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Cruceta abajo: vuelve a la de antes"), State->Focused() == State->First.Get());
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonRight); return true; });
	S.Wait(2);
	S.Then([Test, Script, Rooms]()
	{
		return Check(Test, *Script, TEXT("B: se cierra"), Rooms->IsValid() && !(*Rooms)->IsOpen());
	});

	// «Unirse»: del campo del código, derecha a «Entrar» e izquierda de vuelta al campo.
	S.Then([Test, Script, Rooms]()
	{
		if (UTN_RoomMenuWidget* Menu = Rooms->Get()) { Menu->Open(ETNRoomMenuPage::Join); }
		return Check(Test, *Script, TEXT("Se abre «Unirse»"), Rooms->IsValid() && (*Rooms)->IsOpen());
	});
	S.Wait(6);
	S.Then([Test, Script, State, CodeFieldFocused]()
	{
		return Check(Test, *Script, TEXT("«Unirse»: el foco en el campo del código"), CodeFieldFocused(State->World.Get(), State->Owner.Get()));
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadRight); return true; });
	S.Wait(2);
	S.Then([Test, Script, State, CodeFieldFocused]()
	{
		return Check(Test, *Script, TEXT("Cruceta derecha: del campo a «Entrar»"),
			State->Focused() != nullptr && !CodeFieldFocused(State->World.Get(), State->Owner.Get()));
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadLeft); return true; });
	S.Wait(2);
	S.Then([Test, Script, State, CodeFieldFocused]()
	{
		return Check(Test, *Script, TEXT("Cruceta izquierda: de vuelta al campo"), CodeFieldFocused(State->World.Get(), State->Owner.Get()));
	});
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonRight); return true; });
	S.Wait(2);
	S.Then([Test, Script, Rooms]()
	{
		return Check(Test, *Script, TEXT("B: se cierra"), Rooms->IsValid() && !(*Rooms)->IsOpen());
	});

	ADD_LATENT_AUTOMATION_COMMAND(FRunScript(this, Script, [State, Rooms]()
	{
		if (UTN_RoomMenuWidget* Menu = Rooms->Get()) { Menu->Close(); }
		DisconnectPad(State->Pad);
	}));
	return true;
}

// Después de Navigation (van por orden alfabético): esta deja la partida local en marcha, y en ella un mando nuevo del jugador
// 1 queda libre para otro jugador (UTN_LocalPlaySubsystem::ProcessConnection).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNPausePadGuestTest,
	"Tortunabo.UI.PausePad.SplitScreenGuest",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTNPausePadGuestTest::RunTest(const FString& Parameters)
{
	using namespace TNPausePadTestDetail;
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Sin Slate: hace falta el juego con su ventana."));
		return true;
	}
	// Al entrar un invitado en pleno lobby, el BeginPlay de BP_GamePlayerController crea widgets antes de que el motor le dé
	// su jugador (UWorld::SpawnPlayActor llama a SetPlayer después del Login) y el motor lo apunta como error. No es de esta
	// prueba: se ignora (en inglés o en español, según el idioma del motor).
	AddExpectedError(TEXT("(CreateWidget cannot be used|Crear widget no puede usarse)"), EAutomationExpectedErrorFlags::Contains, -1);

	TSharedRef<FScript> Script = MakeShared<FScript>();
	TSharedRef<FState> State = MakeShared<FState>();
	FScript& S = Script.Get();
	FAutomationTestBase* Test = this;

	// Partida local en el lobby, con un invitado que tiene su mando.
	S.Then(WaitSettled(State, 1.0));
	S.Then([State]()
	{
		UWorld* World = State->World.Get();
		const UTN_LocalPlaySubsystem* LocalPlay = UTN_LocalPlaySubsystem::Get(World);
		if (!(LocalPlay && LocalPlay->IsLocalMode() && UTN_LocalPlaySubsystem::IsLobbyWorld(World)))
		{
			if (UMP_GameInstance* GameInstance = World ? Cast<UMP_GameInstance>(World->GetGameInstance()) : nullptr) { GameInstance->StartLocalGame(); }
		}
		return true;
	});
	S.Then(WaitSettled(State, 3.0, [](UWorld* World)
	{
		const UTN_LocalPlaySubsystem* LocalPlay = UTN_LocalPlaySubsystem::Get(World);
		const APlayerController* PC = PlayerAt(World, 0);
		return LocalPlay && LocalPlay->IsLocalMode() && UTN_LocalPlaySubsystem::IsLobbyWorld(World) && PC && PC->GetPawn();
	}));
	S.Then([Test, Script, State]()
	{
		UWorld* World = State->World.Get();
		UTN_LocalPlaySubsystem* LocalPlay = UTN_LocalPlaySubsystem::Get(World);
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (GameInstance && GameInstance->GetNumLocalPlayers() < 2 && LocalPlay) { LocalPlay->AddTestGuest(); }
		return Check(Test, *Script, TEXT("Entra un invitado"), GameInstance && GameInstance->GetNumLocalPlayers() >= 2);
	});
	S.Then(WaitSettled(State, 2.0, [](UWorld* World)
	{
		const APlayerController* Guest = PlayerAt(World, 1);
		return Guest && Guest->GetPawn();
	}));
	S.Then([Test, Script, State]()
	{
		UWorld* World = State->World.Get();
		APlayerController* Guest = PlayerAt(World, 1);
		UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(World);
		State->Owner = Guest;
		if (Guest && Guest->GetLocalPlayer()) { State->Pad = ConnectPad(Guest->GetLocalPlayer()->GetPlatformUserId()); }
		if (Settings && Guest) { Settings->OpenPauseMenu(Guest); }
		return Check(Test, *Script, TEXT("El invitado abre el menú de pausa"), Settings && Guest && Settings->GetPauseMenuOwner() == Guest);
	});
	S.Wait(10);
	S.Then([Test, Script, State]()
	{
		State->First = State->Focused();
		return Check(Test, *Script, TEXT("Al abrirse, una fila tiene el foco del invitado"), State->First.IsValid());
	});

	// Su cruceta lo recorre.
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadDown); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		UTN_PauseRow* Now = State->Focused();
		State->Second = Now;
		return Check(Test, *Script, TEXT("Cruceta abajo del invitado: la fila de debajo"),
			Now && Now != State->First.Get() && RowAt(Now).Y > RowAt(State->First.Get()).Y);
	});

	// El teclado y un mando del jugador 1 no lo tocan.
	S.Then([State]()
	{
		TapKey(EKeys::Up);
		const FInputDeviceId Primary = ConnectPad(IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser());
		TapPad(Primary, FGamepadKeyNames::DPadUp);
		DisconnectPad(Primary);
		return true;
	});
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("El teclado y el mando del jugador 1 no mueven el menú del invitado"), State->Focused() == State->Second.Get());
	});

	// Su cruceta y su stick, otra vez.
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::DPadUp); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Cruceta arriba del invitado: vuelve a la primera"), State->Focused() == State->First.Get());
	});
	S.Then([State]() { FlickStickDown(State->Pad); return true; });
	S.Wait(2);
	S.Then([Test, Script, State]()
	{
		return Check(Test, *Script, TEXT("Stick abajo del invitado: la fila de debajo"), State->Focused() == State->Second.Get());
	});

	// Su B lo cierra (en la portada).
	S.Then([State]() { TapPad(State->Pad, FGamepadKeyNames::FaceButtonRight); return true; });
	S.Wait(3);
	S.Then([Test, Script, State]()
	{
		const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(State->World.Get());
		return Check(Test, *Script, TEXT("B del invitado: se cierra"), Settings && !Settings->IsPauseMenuOpen());
	});

	ADD_LATENT_AUTOMATION_COMMAND(FRunScript(this, Script, [State]()
	{
		if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(State->World.Get())) { Settings->ClosePauseMenu(); }
		DisconnectPad(State->Pad);
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
