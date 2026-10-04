// Franja de abajo del menú de salas (#245): el aviso (el de versión distinta ocupa dos líneas) va encima de la ayuda de la
// opción enfocada y esta encima de los atajos. Midan lo que midan, ninguno pisa al otro ni sube hasta la tarjeta de «Unirse».
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.RoomMenu; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "UI/Menu/TN_RoomMenuWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Layout/Geometry.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRoomMenuLayoutTestDetail
{
	/**
	 * Mundo de juego vacío con un jugador local suelto: UUserWidget::Initialize solo monta el menú (NativeOnInitialized) con
	 * un jugador. Sin GameInstance, el menú no busca salas.
	 */
	struct FTestWorld
	{
		UWorld* World = nullptr;
		APlayerController* PC = nullptr;
		ULocalPlayer* Player = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRoomMenuTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			Player = NewObject<ULocalPlayer>(GEngine);
			PC = World->SpawnActor<APlayerController>();
			if (PC)
			{
				// Sin InitializeActorsForPlay, el mundo no apunta solo el controlador (lo busca ULocalPlayer::GetPlayerController).
				World->AddController(PC);
				PC->Player = Player;
				Player->PlayerController = PC;
			}
		}

		~FTestWorld()
		{
			if (PC)
			{
				PC->Player = nullptr;
				World->RemoveController(PC);
			}
			if (Player) { Player->PlayerController = nullptr; }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** Una pieza privada del menú (UPROPERTY) por su nombre. */
	template <typename T>
	T* Part(UObject* Object, FName Name)
	{
		const FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name);
		return Prop ? Cast<T>(Prop->GetObjectPropertyValue_InContainer(Object)) : nullptr;
	}

	/** Coloca los hijos de Parent, uno por uno hacia abajo, hasta dar con Wanted (solo lo que se ve). */
	bool FindGeometry(const FArrangedWidget& Parent, const SWidget* Wanted, FGeometry& OutGeometry)
	{
		FArrangedChildren Children(EVisibility::Visible);
		Parent.Widget->ArrangeChildren(Parent.Geometry, Children);
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			const FArrangedWidget& Child = Children[i];
			if (&Child.Widget.Get() == Wanted)
			{
				OutGeometry = Child.Geometry;
				return true;
			}
			if (FindGeometry(Child, Wanted, OutGeometry))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Dónde queda Widget en una pantalla de 1920 × 1080 (el lienzo de diseño del menú, sin encoger). Root es el árbol de Slate
	 * del menú: quien llama lo guarda mientras mide (UMG solo guarda referencias débiles; sin nadie que lo sujete, se borra).
	 */
	TOptional<FSlateRect> Box(const TSharedRef<SWidget>& Root, UWidget* Widget)
	{
		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		if (!Slate.IsValid())
		{
			return {};
		}
		Root->SlatePrepass(1.f);
		const FArrangedWidget RootArranged(Root, FGeometry::MakeRoot(FVector2D(1920.f, 1080.f), FSlateLayoutTransform()));
		FGeometry Found;
		if (!FindGeometry(RootArranged, Slate.Get(), Found))
		{
			return {};
		}
		return Found.GetLayoutBoundingRect();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRoomMenuFooterTest,
	"Tortunabo.UI.RoomMenu.NoticeAboveHelp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRoomMenuFooterTest::RunTest(const FString& Parameters)
{
	using namespace TNRoomMenuLayoutTestDetail;

	// Medir textos necesita Slate con su renderizador (las fuentes); sin él (un commandlet) no hay nada que medir.
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		AddInfo(TEXT("Sin Slate: no se mide la franja de abajo."));
		return true;
	}

	FTestWorld TestWorld;
	if (!TestNotNull(TEXT("Jugador local de prueba"), TestWorld.PC))
	{
		return false;
	}
	UTN_RoomMenuWidget* Menu = CreateWidget<UTN_RoomMenuWidget>(TestWorld.PC, UTN_RoomMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("Se crea el menú de salas"), Menu))
	{
		return false;
	}
	const TSharedRef<SWidget> Root = Menu->TakeWidget();
	Menu->Open(ETNRoomMenuPage::Join);

	UTextBlock* Notice = Part<UTextBlock>(Menu, TEXT("NoticeText"));
	UTextBlock* Help = Part<UTextBlock>(Menu, TEXT("HelpText"));
	UTextBlock* Hint = Part<UTextBlock>(Menu, TEXT("HintText"));
	UWidget* Pages = Part<UWidget>(Menu, TEXT("Pages"));
	if (!TestNotNull(TEXT("Aviso"), Notice) || !TestNotNull(TEXT("Ayuda"), Help) || !TestNotNull(TEXT("Atajos"), Hint)
		|| !TestNotNull(TEXT("Pantallas"), Pages))
	{
		return false;
	}

	// Una línea de cada para tener la medida de una línea.
	Help->SetText(FText::AsCultureInvariant(TEXT("Vuelve al menú principal.")));
	Menu->ShowNotice(FText::AsCultureInvariant(TEXT("Faltan letras.")), true, 9.f);
	const TOptional<FSlateRect> NoticeOne = Box(Root, Notice);
	const TOptional<FSlateRect> HelpOne = Box(Root, Help);
	if (!TestTrue(TEXT("El aviso y la ayuda están en pantalla"), NoticeOne.IsSet() && HelpOne.IsSet()))
	{
		return false;
	}
	const double NoticeLine = NoticeOne->GetSize().Y;
	const double HelpLine = HelpOne->GetSize().Y;
	TestTrue(TEXT("Una línea de aviso mide algo (hay fuentes)"), NoticeLine > 10.0);

	// Lo que vio la QA: el aviso de versión distinta (dos líneas) con la ayuda del campo del código (otras dos).
	Help->SetText(FText::AsCultureInvariant(TEXT(
		"Escribe o pega (Ctrl+V, clic derecho) el código de 5 letras y números. Con mando: A para escribirlo. Intro: entrar.")));
	Menu->ShowNotice(FText::AsCultureInvariant(TEXT(
		"Tu versión del juego no es la misma que la del anfitrión. Poneos los dos en la misma versión y volved a intentarlo.")),
		true, 9.f);
	const TOptional<FSlateRect> NoticeBox = Box(Root, Notice);
	const TOptional<FSlateRect> HelpBox = Box(Root, Help);
	const TOptional<FSlateRect> HintBox = Box(Root, Hint);
	const TOptional<FSlateRect> PagesBox = Box(Root, Pages);
	if (!TestTrue(TEXT("Todo en pantalla"), NoticeBox.IsSet() && HelpBox.IsSet() && HintBox.IsSet() && PagesBox.IsSet()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Unirse: tarjeta hasta %.0f; aviso %.0f-%.0f; ayuda %.0f-%.0f; atajos %.0f-%.0f (de 1080)."),
		PagesBox->Bottom, NoticeBox->Top, NoticeBox->Bottom, HelpBox->Top, HelpBox->Bottom, HintBox->Top, HintBox->Bottom));

	TestTrue(TEXT("El aviso largo ocupa al menos dos líneas"), NoticeBox->GetSize().Y > 1.5 * NoticeLine);
	TestTrue(TEXT("La ayuda larga ocupa al menos dos líneas"), HelpBox->GetSize().Y > 1.5 * HelpLine);
	TestTrue(TEXT("El aviso acaba antes de que empiece la ayuda"), NoticeBox->Bottom <= HelpBox->Top + 0.5);
	TestTrue(TEXT("La ayuda acaba antes de que empiecen los atajos"), HelpBox->Bottom <= HintBox->Top + 0.5);
	TestTrue(TEXT("Los atajos caben en la pantalla"), HintBox->Bottom <= 1080.0 + 0.5);
	TestTrue(TEXT("El aviso no sube hasta la tarjeta de «Unirse»"), PagesBox->Bottom <= NoticeBox->Top + 0.5);

	// La ayuda no se mueve cuando sale o se va el aviso.
	Menu->Close();
	Menu->Open(ETNRoomMenuPage::Join);
	Help->SetText(FText::AsCultureInvariant(TEXT("Vuelve al menú principal.")));
	const TOptional<FSlateRect> HelpAlone = Box(Root, Help);
	TestTrue(TEXT("Sin aviso, la ayuda sigue en su sitio"), HelpAlone.IsSet() && FMath::IsNearlyEqual(HelpAlone->Bottom, HelpOne->Bottom, 0.5f));

	Menu->Close();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
