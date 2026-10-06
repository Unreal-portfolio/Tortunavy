// La tortuga de Arte (#581, Docs/Arte_Assets.md, «La tortuga»): una sola fuente para la malla (la del personaje), piezas
// estáticas pegadas a los huesos desde el catálogo y cosméticos que no rompen con una malla sin las ranuras de la de demo.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Art.Turtle; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtCatalog.h"
#include "Art/TN_ArtSettings.h"
#include "Art/TN_TurtleArt.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Lobby/TN_CosmeticPreview.h"
#include "Lobby/TN_ShopKeeper.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurtleArtTestDetail
{
	UStaticMesh* EngineMesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	/** Malla esquelética del motor que no es una tortuga (un hueso, una ranura de material). */
	USkeletalMesh* OtherSkeletalMesh()
	{
		return LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	}

	/** Mundo de juego con BeginPlay ya hecho: los actores que se crean después lo reciben al aparecer. */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		explicit FPlayWorld(bool bBeginPlay)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNTurtleArtTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			if (bBeginPlay)
			{
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FPlayWorld()
		{
			if (World->HasBegunPlay()) { World->EndPlay(EEndPlayReason::Quit); }
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** Actor con una malla de tortuga registrada (en la postura de referencia: no se anima) y el componente del casco. */
	struct FTurtle
	{
		AActor* Actor = nullptr;
		USkeletalMeshComponent* Body = nullptr;
		UStaticMeshComponent* Helmet = nullptr;
		TArray<TObjectPtr<UMaterialInterface>> Defaults;

		FTurtle(UWorld* World, USkeletalMesh* Mesh)
		{
			Actor = World->SpawnActor<AActor>();
			Body = NewObject<USkeletalMeshComponent>(Actor, TEXT("Body"));
			Actor->SetRootComponent(Body);
			Body->SetSkeletalMeshAsset(Mesh);
			Body->RegisterComponent();
			Helmet = NewObject<UStaticMeshComponent>(Actor, TEXT("Helmet"));
			Helmet->SetupAttachment(Body);
			Helmet->RegisterComponent();
		}
	};

	/** Valor de una propiedad float (protegida) del actor: la escala del tendero y del general. */
	float FloatProperty(const UObject* Object, const TCHAR* Name)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(Object->GetClass(), Name);
		return Property ? Property->GetPropertyValue_InContainer(Object) : 0.f;
	}

	UTN_TurtlePieceComponent* PieceOf(const USkeletalMeshComponent* Body, FName Slot)
	{
		TArray<UPrimitiveComponent*> Pieces;
		TNTurtleArt::GetPieceComponents(Body, Pieces);
		for (UPrimitiveComponent* Prim : Pieces)
		{
			UTN_TurtlePieceComponent* Piece = Cast<UTN_TurtlePieceComponent>(Prim);
			if (Piece && Piece->Slot == Slot) { return Piece; }
		}
		return nullptr;
	}

	/** Pone los nombres de las ranuras de los cosméticos y los devuelve como estaban al salir. */
	struct FSlotNames
	{
		UTN_ArtSettings* Settings = GetMutableDefault<UTN_ArtSettings>();
		FName Helmet = Settings->HelmetMaterialSlot;
		FName Body = Settings->BodyMaterialSlot;

		void Set(FName InHelmet, FName InBody) const
		{
			Settings->HelmetMaterialSlot = InHelmet;
			Settings->BodyMaterialSlot = InBody;
		}

		~FSlotNames() { Set(Helmet, Body); }
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Una sola fuente
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleArtSourceTest,
	"Tortunabo.Art.Turtle.CharacterIsTheSource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleArtSourceTest::RunTest(const FString& Parameters)
{
	// Lo que dicen los ajustes del proyecto: el personaje de la tortuga carga, tiene malla y sus animaciones son de su esqueleto.
	UClass* Class = TNTurtleArt::GetCharacterClass();
	if (!TestNotNull(TEXT("El personaje de la tortuga de los ajustes carga (UTN_ArtSettings::TurtleCharacter)"), Class)) { return false; }
	const ACharacter* Defaults = Cast<ACharacter>(Class->GetDefaultObject());
	USkeletalMesh* Mesh = TNTurtleArt::GetMesh();
	if (!TestNotNull(TEXT("El personaje tiene malla"), Mesh)) { return false; }
	TestTrue(TEXT("La malla de la tortuga es la del Mesh del personaje"), Defaults && Defaults->GetMesh()->GetSkeletalMeshAsset() == Mesh);
	TestTrue(TEXT("Con la malla de demo en el Blueprint, las copias no se corrigen"),
		TNTurtleArt::GetCopyCorrection().Equals(FTransform::Identity, 1.e-3));
	const TPair<ETNTurtleClip, const TCHAR*> Clips[] = {
		{ ETNTurtleClip::Idle, TEXT("Idle") }, { ETNTurtleClip::Walk, TEXT("Walk") },
		{ ETNTurtleClip::Cheer, TEXT("Cheer") }, { ETNTurtleClip::Salute, TEXT("Salute") } };
	for (const TPair<ETNTurtleClip, const TCHAR*>& Clip : Clips)
	{
		const UAnimSequence* Anim = TNTurtleArt::GetClip(Clip.Key);
		if (TestNotNull(FString::Printf(TEXT("Animación %s de los ajustes"), Clip.Value), Anim))
		{
			TestTrue(FString::Printf(TEXT("La animación %s es del esqueleto de la malla"), Clip.Value), Anim->GetSkeleton() == Mesh->GetSkeleton());
		}
	}

	// Corrección de las copias (pura): la de referencia no cambia nada; otra escala, otro pivote u otro giro en el Blueprint
	// se aplican igual a la copia, respecto a su malla.
	const FTransform& Reference = TNTurtleArt::GetReferenceMeshTransform();
	TestTrue(TEXT("Referencia: identidad"), TNTurtleArt::ComputeCopyCorrection(Reference).Equals(FTransform::Identity));
	const FTransform KeeperDemo(FRotator(0.f, -90.f, 0.f), FVector::ZeroVector, FVector(3.4));
	{
		FTransform Bigger = Reference;
		Bigger.SetScale3D(FVector(3.0));
		const FTransform Keeper = TNTurtleArt::ComputeCopyCorrection(Bigger) * KeeperDemo;
		TestTrue(TEXT("Escala 3 en el personaje (x1,2): el tendero x1,2"), Keeper.GetScale3D().Equals(FVector(3.4 * 1.2), 1.e-3));
		TestTrue(TEXT("y en su sitio"), Keeper.GetLocation().Equals(FVector::ZeroVector, 1.e-3));
	}
	{
		FTransform Lower = Reference;
		Lower.SetLocation(FVector(0.0, 0.0, -80.0));
		const FTransform Keeper = TNTurtleArt::ComputeCopyCorrection(Lower) * KeeperDemo;
		TestTrue(TEXT("10 cm más abajo en el personaje (escala 2,5): el tendero (3,4) 13,6 cm más abajo"),
			Keeper.GetLocation().Equals(FVector(0.0, 0.0, -10.0 * 3.4 / 2.5), 1.e-3));
	}
	{
		FTransform FacingX = Reference;
		FacingX.SetRotation(FQuat::Identity);
		const FTransform Keeper = TNTurtleArt::ComputeCopyCorrection(FacingX) * KeeperDemo;
		TestTrue(TEXT("Malla que mira a +X (sin giro en el personaje): el tendero tampoco gira"),
			Keeper.GetRotation().Equals(FQuat::Identity, 1.e-4));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleArtCopiesTest,
	"Tortunabo.Art.Turtle.CopiesUseCharacterMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleArtCopiesTest::RunTest(const FString& Parameters)
{
	// #581: se cambiaba la malla de BP_TortugaCharacter y el tendero y el escaparate seguían con TotugaDemo_Rig (cada uno
	// la cargaba por su ruta). Con otra malla (y otra escala) en el personaje, todas la siguen.
	using namespace TNTurtleArtTestDetail;
	USkeletalMesh* Other = OtherSkeletalMesh();
	if (!TestNotNull(TEXT("Malla esquelética del motor SkeletalCube"), Other)) { return false; }
	TStrongObjectPtr<USkeletalMeshComponent> Template(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
	Template->SetSkeletalMeshAsset(Other);
	FTransform CharacterMesh = TNTurtleArt::GetReferenceMeshTransform();
	CharacterMesh.SetScale3D(FVector(3.0));
	Template->SetRelativeTransform(CharacterMesh);
	TNTurtleArt::SetTemplateMeshForTest(Template.Get());
	ON_SCOPE_EXIT { TNTurtleArt::SetTemplateMeshForTest(nullptr); };

	FPlayWorld Play(true);
	UWorld* World = Play.World;
	ATN_ShopKeeper* Keeper = World->SpawnActor<ATN_ShopKeeper>();
	ATN_CosmeticPreview* Preview = ATN_CosmeticPreview::Get(World);
	const AActor* Copies[] = { Keeper, Preview };
	const TCHAR* Names[] = { TEXT("Tendero"), TEXT("Escaparate") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Copies); ++i)
	{
		if (!TestNotNull(FString::Printf(TEXT("%s creado"), Names[i]), Copies[i])) { continue; }
		TInlineComponentArray<USkeletalMeshComponent*> Bodies(Copies[i]);
		TestTrue(FString::Printf(TEXT("%s: tiene tortugas"), Names[i]), Bodies.Num() > 0);
		for (const USkeletalMeshComponent* Body : Bodies)
		{
			TestTrue(FString::Printf(TEXT("%s (%s): la malla del personaje"), Names[i], *Body->GetName()), Body->GetSkeletalMeshAsset() == Other);
		}
	}
	if (Keeper)
	{
		const USkeletalMeshComponent* Body = Keeper->FindComponentByClass<USkeletalMeshComponent>();
		TestTrue(TEXT("Tendero: la escala del personaje (x1,2) sobre la suya (3,4)"),
			Body && Body->GetRelativeScale3D().Equals(FVector(FloatProperty(Keeper, TEXT("KeeperScale")) * 1.2f), 1.e-3));
		// La maqueta del tendero (una tortuga suelta con la malla del personaje) se reconoce como tortuga.
		TestTrue(TEXT("La malla del personaje es una tortuga de maqueta"), TNTurtleArt::IsTurtleMesh(Other));
	}
	// Las maquetas que aún llevan la de demo siguen siendo tortugas; una malla cualquiera, no.
	TNTurtleArt::SetTemplateMeshForTest(nullptr);
	TestTrue(TEXT("La de demo es una tortuga de maqueta"), TNTurtleArt::IsTurtleMesh(TNTurtleArt::GetMesh()));
	TestFalse(TEXT("SkeletalCube no es una tortuga de maqueta"), TNTurtleArt::IsTurtleMesh(Other));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Piezas pegadas a los huesos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleArtPiecesTest,
	"Tortunabo.Art.Turtle.PiecesFollowBones",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleArtPiecesTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleArtTestDetail;
	USkeletalMesh* Mesh = TNTurtleArt::GetMesh();
	UStaticMesh* Cube = EngineMesh(TEXT("Cube"));
	UStaticMesh* Sphere = EngineMesh(TEXT("Sphere"));
	if (!TestNotNull(TEXT("Malla de la tortuga"), Mesh) || !TestNotNull(TEXT("Mallas del motor"), Cube && Sphere ? Cube : nullptr)) { return false; }
	const FName Helmet(TEXT("Turtle.Helmet"));
	const FName Shell(TEXT("Turtle.Shell"));
	const FName Eyes(TEXT("Turtle.Eyes"));
	TestEqual(TEXT("Turtle.Helmet es el casco de serie"), TNTurtleArt::HelmetPiece(), Helmet);
	for (const TNTurtleArt::FPieceInfo& Info : TNTurtleArt::GetPieces())
	{
		TestTrue(FString::Printf(TEXT("%s: su hueso por defecto existe en la malla de la tortuga"), *Info.Slot.ToString()),
			Mesh->GetRefSkeleton().FindBoneIndex(Info.DefaultBone) != INDEX_NONE);
	}

	// Catálogo: el casco en la coronilla (exportado con el cuerpo, movido con Adjust), el caparazón en otro hueso y los ojos
	// en uno que no existe.
	UTN_ArtCatalog* Catalog = NewObject<UTN_ArtCatalog>(GetTransientPackage());
	const FTransform HelmetAdjust(FRotator(0.f, 30.f, 0.f), FVector(0.0, 5.5, 51.0), FVector(0.1));
	FTNArtOverride& HelmetEntry = Catalog->Pieces.Add(Helmet);
	HelmetEntry.Mesh = Cube;
	HelmetEntry.Adjust = HelmetAdjust;
	FTNArtOverride& ShellEntry = Catalog->Pieces.Add(Shell);
	ShellEntry.Mesh = Sphere;
	ShellEntry.Bone = TEXT("Spine2");
	FTNArtOverride& EyesEntry = Catalog->Pieces.Add(Eyes);
	EyesEntry.Mesh = Cube;
	EyesEntry.Bone = TEXT("NoEsUnHueso");
	TNArt::SetCatalogsForTest({ Catalog });
	ON_SCOPE_EXIT { TNArt::SetCatalogsForTest({}); };

	FPlayWorld Play(false);
	FTurtle Turtle(Play.World, Mesh);
	FLightingChannels Channels;
	Channels.bChannel0 = false;
	Channels.bChannel2 = true;
	Turtle.Body->LightingChannels = Channels;
	UTN_CosmeticLook::ApplyLook(Turtle.Actor, Turtle.Body, Turtle.Helmet, FTN_TurtleLook(), Turtle.Defaults);

	UTN_TurtlePieceComponent* HelmetPiece = PieceOf(Turtle.Body, Helmet);
	if (TestNotNull(TEXT("El casco del catálogo va pegado a la tortuga"), HelmetPiece))
	{
		TestEqual(TEXT("Al hueso Head (el de la tabla)"), HelmetPiece->GetAttachSocketName(), FName(TEXT("Head")));
		TestTrue(TEXT("Con su malla"), HelmetPiece->GetStaticMesh() == Cube);
		// En la postura de referencia (la malla no se anima aquí) queda donde se exportó, movida por Adjust.
		const FTransform InMesh = HelmetPiece->GetComponentTransform().GetRelativeTransform(Turtle.Body->GetComponentTransform());
		TestTrue(TEXT("En la postura de referencia, en su sitio del espacio de la malla"), InMesh.Equals(HelmetAdjust, 1.e-3));
		TestTrue(TEXT("Se ve"), HelmetPiece->IsVisible());
		TestTrue(TEXT("No choca"), HelmetPiece->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Con los canales de luz de la tortuga"), HelmetPiece->LightingChannels.bChannel2 && !HelmetPiece->LightingChannels.bChannel0);
		if (UMaterialInstanceDynamic* Slot = Cast<UMaterialInstanceDynamic>(Turtle.Body->GetMaterial(Turtle.Body->GetMaterialIndex(
			GetDefault<UTN_ArtSettings>()->HelmetMaterialSlot))))
		{
			float Hide = 0.f;
			Slot->GetScalarParameterValue(TEXT("HideHelmet"), Hide);
			TestEqual(TEXT("El casco de serie de Arte quita el pintado de la malla de demo"), Hide, 1.f);
		}
	}
	UTN_TurtlePieceComponent* ShellPiece = PieceOf(Turtle.Body, Shell);
	TestTrue(TEXT("El caparazón va al hueso del catálogo (Spine2)"), ShellPiece && ShellPiece->GetAttachSocketName() == FName(TEXT("Spine2")));
	UTN_TurtlePieceComponent* EyesPiece = PieceOf(Turtle.Body, Eyes);
	if (TestNotNull(TEXT("Hueso que no existe: la pieza se pone igual"), EyesPiece))
	{
		TestEqual(TEXT("pegada a la malla, sin hueso"), EyesPiece->GetAttachSocketName(), FName(NAME_None));
		TestTrue(TEXT("con Adjust respecto a la malla"), EyesPiece->GetRelativeTransform().Equals(FTransform::Identity, 1.e-3));
	}
	TestNull(TEXT("Sin malla en el catálogo, sin pieza (lengua)"), PieceOf(Turtle.Body, TEXT("Turtle.Tongue")));

	// Sigue al hueso: si la cabeza se mueve, el casco va con ella.
	if (HelmetPiece)
	{
		const FTransform Before = HelmetPiece->GetComponentTransform();
		const FTransform HeadNow = Turtle.Body->GetSocketTransform(TEXT("Head"));
		TestTrue(TEXT("El casco está donde dice la cabeza"),
			Before.Equals(HelmetPiece->GetRelativeTransform() * HeadNow, 1.e-2));
	}

	// Casco de la tienda puesto: el de serie se esconde; el resto sigue.
	TNTurtleArt::ApplyPieces(Turtle.Body, true);
	TestTrue(TEXT("Con un casco de la tienda, el de serie no se ve"), HelmetPiece && !HelmetPiece->IsVisible());
	TestTrue(TEXT("y el caparazón sí"), ShellPiece && ShellPiece->IsVisible());
	TNTurtleArt::ApplyPieces(Turtle.Body, false);
	TestTrue(TEXT("Sin él, vuelve"), HelmetPiece && HelmetPiece->IsVisible());
	TestTrue(TEXT("Volver a vestirla no duplica las piezas"), PieceOf(Turtle.Body, Helmet) == HelmetPiece);

	// Se esconde con la malla y su dueño la ve como la malla.
	Turtle.Body->SetVisibility(false);
	if (ShellPiece) { ShellPiece->SyncWithBody(); }
	TestTrue(TEXT("Malla escondida: pieza escondida"), ShellPiece && !ShellPiece->IsVisible());
	Turtle.Body->SetVisibility(true);
	Turtle.Body->SetOwnerNoSee(true);
	if (ShellPiece) { ShellPiece->SyncWithBody(); }
	TestTrue(TEXT("Malla a la vista: pieza a la vista"), ShellPiece && ShellPiece->IsVisible());
	TestTrue(TEXT("Su dueño no ve la pieza si no ve la malla"), ShellPiece && ShellPiece->bOwnerNoSee);

	// Se quita del catálogo: se va.
	TNArt::SetCatalogsForTest({ NewObject<UTN_ArtCatalog>(GetTransientPackage()) });
	TNTurtleArt::ApplyPieces(Turtle.Body, false);
	TArray<UPrimitiveComponent*> Left;
	TNTurtleArt::GetPieceComponents(Turtle.Body, Left);
	TestEqual(TEXT("Sin piezas en el catálogo, sin piezas en la tortuga"), Left.Num(), 0);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cosméticos con otra malla
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleArtLookSlotsTest,
	"Tortunabo.Art.Turtle.LookWithOtherSlots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleArtLookSlotsTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleArtTestDetail;
	USkeletalMesh* Mesh = TNTurtleArt::GetMesh();
	USkeletalMesh* Other = OtherSkeletalMesh();
	if (!TestNotNull(TEXT("Malla de la tortuga"), Mesh) || !TestNotNull(TEXT("SkeletalCube"), Other)) { return false; }
	FSlotNames Slots;
	TNArt::SetCatalogsForTest({ NewObject<UTN_ArtCatalog>(GetTransientPackage()) });
	ON_SCOPE_EXIT { TNArt::SetCatalogsForTest({}); };
	FPlayWorld Play(false);
	auto IsTurtleBody = [](const UMaterialInterface* Mat)
	{
		const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
		return MID && MID->Parent && MID->Parent->GetName() == TEXT("M_TurtleBody");
	};

	// La de demo con sus ranuras de siempre: cuerpo pintado en «lambert4».
	{
		FTurtle Turtle(Play.World, Mesh);
		UTN_CosmeticLook::ApplyLook(Turtle.Actor, Turtle.Body, Turtle.Helmet, FTN_TurtleLook(), Turtle.Defaults);
		const int32 BodySlot = Turtle.Body->GetMaterialIndex(Slots.Body);
		TestTrue(TEXT("Demo: M_TurtleBody en la ranura del cuerpo"), BodySlot != INDEX_NONE && IsTurtleBody(Turtle.Body->GetMaterial(BodySlot)));
		TestTrue(TEXT("Demo: es la tortuga de la cara"), UTN_CosmeticLook::IsDemoTurtle(Turtle.Body));
	}

	// Ranuras que la malla no tiene (otra malla con sus nombres): se queda con sus materiales, sin índices -1 ni fallos.
	Slots.Set(TEXT("Casco_NoExiste"), TEXT("Cuerpo_NoExiste"));
	{
		FTurtle Turtle(Play.World, Mesh);
		TArray<UMaterialInterface*> Own;
		for (int32 i = 0; i < Turtle.Body->GetNumMaterials(); ++i) { Own.Add(Turtle.Body->GetMaterial(i)); }
		UTN_CosmeticLook::ApplyLook(Turtle.Actor, Turtle.Body, Turtle.Helmet, FTN_TurtleLook(), Turtle.Defaults);
		bool bSame = true;
		for (int32 i = 0; i < Own.Num(); ++i) { bSame &= Turtle.Body->GetMaterial(i) == Own[i]; }
		TestTrue(TEXT("Sin sus ranuras: los materiales de la malla, sin pintar por número"), bSame);
		TestNull(TEXT("Sin ranura del cuerpo: sin material de la cara"), UTN_CosmeticLook::GetBodyMaterial(Turtle.Body));
		TestFalse(TEXT("Sin sus ranuras: no es la tortuga de la cara"), UTN_CosmeticLook::IsDemoTurtle(Turtle.Body));
		UTN_CosmeticLook::SetEyeState(Turtle.Body, 1.f, 1.f);
	}

	// Una malla que no es la tortuga (una ranura, un hueso): nada se rompe.
	Slots.Set(Slots.Helmet, Slots.Body);
	{
		FTurtle Turtle(Play.World, Other);
		UMaterialInterface* Own = Turtle.Body->GetMaterial(0);
		UTN_CosmeticLook::ApplyLook(Turtle.Actor, Turtle.Body, Turtle.Helmet, FTN_TurtleLook(), Turtle.Defaults);
		TestTrue(TEXT("Otra malla: su material"), Turtle.Body->GetMaterial(0) == Own);
		UTN_CosmeticLook::SetEyeState(Turtle.Body, 0.5f, 0.f);
	}

	// Las ranuras se configuran: con los nombres cambiados, el cuerpo va en la otra.
	Slots.Set(TEXT("lambert4"), TEXT("lambert2"));
	{
		FTurtle Turtle(Play.World, Mesh);
		UTN_CosmeticLook::ApplyLook(Turtle.Actor, Turtle.Body, Turtle.Helmet, FTN_TurtleLook(), Turtle.Defaults);
		const int32 Configured = Turtle.Body->GetMaterialIndex(TEXT("lambert2"));
		TestTrue(TEXT("Ranuras configuradas: M_TurtleBody en la que dicen los ajustes"),
			Configured != INDEX_NONE && IsTurtleBody(Turtle.Body->GetMaterial(Configured)));
		TestTrue(TEXT("y la cara la encuentra ahí"), UTN_CosmeticLook::GetBodyMaterial(Turtle.Body) == Turtle.Body->GetMaterial(Configured));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
