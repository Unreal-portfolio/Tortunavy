#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_ShopWidgets.generated.h"

class AMP_GamePlayerController;
class ATN_ChangingBooth;
class ATN_CosmeticPreview;
class ATN_ShopKeeper;
class UBorder;
class UImage;
class UMaterialInstanceDynamic;
class UMP_GameInstance;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UTextureRenderTarget2D;
class UWrapBox;

/** Una carta del catálogo: su categoría (la pestaña del buggy mezcla modelos y pinturas) y su Id (NAME_None = de serie). */
struct FTNShopItem
{
	ETNCosmeticCategory Category = ETNCosmeticCategory::Helmet;
	FName Id = NAME_None;
};

/** Botón de la tienda hecho en código: cartel con texto que crece un poco al pasar el ratón. */
UCLASS()
class TORTUNABO_API UTN_ShopButton : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(const FText& Label, UTexture2D* Art, const FLinearColor& TextColor, int32 FontSize, const FVector2D& MinSize, TFunction<void()> InOnClick);
	void SetLabel(const FText& Label);
	void SetArt(UTexture2D* Art);
	void SetDisabled(bool bInDisabled);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Frame;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Sizer;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText;

	TFunction<void()> OnClick;
	bool bPressed = false;
	bool bDisabled = false;
	void ApplyScale(float Scale);
};

/** Carta del catálogo: miniatura (captura del escaparate), nombre y etiqueta (precio, tuyo, puesto). */
UCLASS()
class TORTUNABO_API UTN_ShopCard : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(UTextureRenderTarget2D* Thumb, const FText& Name, TFunction<void()> InOnClick);
	void SetState(bool bInSelected, const FText& Tag, const FLinearColor& TagColor);

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Frame;

	UPROPERTY(Transient)
	TObjectPtr<UImage> ThumbImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThumbMID;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TagText;

	TFunction<void()> OnClick;
	bool bSelected = false;
	bool bHover = false;
	void RefreshLook();
};

/**
 * Base de la tienda y del probador: la vista previa grande (ATN_CosmeticPreview, se gira arrastrando), el foco del
 * teclado y el cierre (Escape). Al cerrarse, el PlayerController devuelve el control al juego (CloseShopUI).
 */
UCLASS(Abstract)
class TORTUNABO_API UTN_CosmeticMenuBase : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Teclas propias de cada menú; true si la usa. */
	virtual bool HandleKey(const FKey& Key) { return false; }

	/** Cartel con la captura del escaparate (se gira arrastrando) y la pista de debajo. */
	UWidget* MakePreviewPanel(float Size);

	ATN_CosmeticPreview* GetPreview() const { return Preview.Get(); }
	AMP_GamePlayerController* GetTNPC() const;
	UMP_GameInstance* GetTNGI() const;

	/** Lo que lleva puesto el jugador (PlayerState; si aún no hay, lo guardado en la GameInstance). */
	FTN_TurtleLook GetWornLook() const;

	/** El buggy del Rally que tiene puesto (igual: PlayerState o GameInstance). */
	FTN_BuggyLook GetWornBuggyLook() const;

	/** Miniatura de un cosmético (del escaparate). */
	UTextureRenderTarget2D* Thumbnail(ETNCosmeticCategory Category, FName Id) const;

	/** Material de UI que pinta una captura del escaparate. */
	UMaterialInstanceDynamic* MakeCaptureMID(UTextureRenderTarget2D* RT);

	void CloseMenu();

	UPROPERTY(Transient)
	TObjectPtr<UImage> PreviewImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMID;

	TWeakObjectPtr<ATN_CosmeticPreview> Preview;

private:
	bool bDragging = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
};

/**
 * La tienda de Don Tortugo, estilo Mario Kart: a la izquierda tu tortuga posando y girando con lo que miras puesto; a
 * la derecha el tendero hablando en su bocadillo, las pestañas (cascos, caparazones, colores, ojos y buggy) y el catálogo
 * con miniaturas y precio. En la pestaña del buggy, el escaparate enseña el buggy del Rally con la tortuga al volante y
 * el catálogo mezcla modelos y pinturas. Comprar desbloquea (lo de la tortuga hoy cuesta 0 conchas; el buggy, conchas
 * de verdad); para ponérselo, al probador.
 */
UCLASS()
class TORTUNABO_API UTN_ShopWidget : public UTN_CosmeticMenuBase
{
	GENERATED_BODY()

public:
	void SetShop(ATN_ShopKeeper* InShop);

#if !UE_BUILD_SHIPPING
	/** Pruebas (TN.Shop.UIShots): abre la pestaña Index y elige la carta Item. */
	void DebugShowTab(int32 Index, int32 Item);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool HandleKey(const FKey& Key) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KeeperNameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DialogText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WalletText;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> GridScroll;

	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> Grid;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> Tabs;

	UPROPERTY(Transient)
	TObjectPtr<UTN_ShopButton> BuyButton;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopCard>> Cards;

	TWeakObjectPtr<ATN_ShopKeeper> Shop;
	ETNCosmeticCategory Tab = ETNCosmeticCategory::Helmet;
	TArray<FTNShopItem> Items;
	int32 Selected = 0;
	FString FullLine;
	float Reveal = 0.f;

	void BuildTree();
	void ShowTab(ETNCosmeticCategory Category);
	void Select(int32 Index, bool bSpeak);
	void Buy();
	void Say(const FText& Line);
	void RefreshCards();
	void RefreshBuyButton();
	void RefreshWallet();
	FText TagFor(const FTNShopItem& Item, FLinearColor& OutColor) const;
};

/**
 * El probador, estilo Mario Kart: la tortuga girando a la izquierda y, a la derecha, dos páginas que se cambian con Q/E
 * o con sus botones: la tortuga (casco, caparazón, color y ojos) y el buggy del Rally (modelo y pintura, con la tortuga
 * al volante en el escaparate). Cada fila se cambia con las flechas y solo salen los cosméticos desbloqueados. ¡Listo!
 * se lo pone todo (se guarda y se replica) y la tortuga sale de la botella; Cancelar sale sin cambios.
 */
UCLASS()
class TORTUNABO_API UTN_BoothWidget : public UTN_CosmeticMenuBase
{
	GENERATED_BODY()

public:
	void SetBooth(ATN_ChangingBooth* InBooth);

#if !UE_BUILD_SHIPPING
	/** Pruebas (TN.Shop.UIShots): pasa a la página (0 tortuga, 1 buggy) y cambia la fila Row Steps veces. */
	void DebugShowPage(int32 InPage, int32 Row, int32 Steps);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual bool HandleKey(const FKey& Key) override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> RowFrames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> RowThumbs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RowThumbMIDs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> RowNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> RowCounts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> RowCaptions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTN_ShopButton>> PageButtons;

	TWeakObjectPtr<ATN_ChangingBooth> Booth;
	/** Opciones y elección por categoría (índice = ETNCosmeticCategory). */
	TArray<TArray<FName>> Options;
	TArray<int32> Choice;
	int32 FocusedRow = 0;
	/** 0 = tortuga, 1 = buggy. */
	int32 Page = 0;
	FTN_TurtleLook Initial;
	FTN_BuggyLook InitialBuggy;

	void BuildTree();
	void LoadOptions();
	void Cycle(int32 Row, int32 Dir);
	void FocusRow(int32 Row);
	void ShowPage(int32 NewPage);
	void RefreshRows();
	/** Categorías de las filas de la página que se ve. */
	const TArray<ETNCosmeticCategory>& PageRows() const;
	FName ChosenId(ETNCosmeticCategory Category) const;
	FTN_TurtleLook ChosenLook() const;
	FTN_BuggyLook ChosenBuggyLook() const;
	void Accept();
};
