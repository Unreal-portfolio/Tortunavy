#include "Player/TN_ShellComponent.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_InventoryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellDecisions.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TN_TurtleFoleyComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachStun.h"

namespace TNShellComponentDetail
{
	bool IsCarried(const ATortugaCharacter* Turtle)
	{
		const UTN_CarryComponent* Carry = Turtle ? Turtle->GetCarryComponent() : nullptr;
		return Carry && Carry->IsBeingCarried();
	}
}

UTN_ShellComponent::UTN_ShellComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTN_ShellComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Sin condición: el resto de máquinas necesita el estado para el visual, y un
	// jugador que entra a mitad de partida lo recibe en el bunch inicial.
	DOREPLIFETIME(UTN_ShellComponent, bIsInShell);
	DOREPLIFETIME(UTN_ShellComponent, Body);
}

void UTN_ShellComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// La tortuga se va (jugador que sale, viaje de mapa): su caja no se queda suelta.
	if (ATN_ShellBody* OldBody = Body)
	{
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			OldBody->MarkReleased();
			OldBody->Destroy();
		}
	}
	Body = nullptr;
	LocalBody = nullptr;
	Super::EndPlay(EndPlayReason);
}

ATortugaCharacter* UTN_ShellComponent::GetTurtleOwner() const
{
	return Cast<ATortugaCharacter>(GetOwner());
}

void UTN_ShellComponent::RequestToggleShell()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		ServerToggleShell();
		return;
	}

	ServerToggleShell_Implementation();
}

void UTN_ShellComponent::ServerToggleShell_Implementation()
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (bIsInShell)
	{
		if (bExitLocked || !TNShellLogic::CanExitShell(Now - ShellEnteredServerTime, MinTimeInShellSeconds))
		{
			return;
		}

		SetShellState(false);
		return;
	}

	const UCharacterMovementComponent* Movement = Turtle->GetCharacterMovement();
	const UTN_InventoryComponent* Inventory = Turtle->GetInventoryComponent();
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();

	TNShellLogic::FShellEnterContext Context;
	Context.bIsSwimming = Movement && Movement->IsSwimming();
	Context.bIsDead = Turtle->IsDead();
	Context.bIsKnockedDown = Turtle->IsKnockedDown();
	Context.bIsDiving = Turtle->IsDiving();
	Context.bHasEquippedItem = Inventory && Inventory->HasEquippedItem();
	Context.bIsCarrying = Carry && (Carry->IsCarrying() || Carry->IsBeingCarried());

	if (!TNShellLogic::CanEnterShell(Context))
	{
		// Para el registro de las pruebas: la tecla no hace nada y no se ve por qué (lo más fácil de no ver, el objeto en la mano).
		UE_LOG(LogTortunabo, Log, TEXT("[Caparazón] %s no se mete en el caparazón:%s%s%s%s%s%s."), *GetNameSafe(Turtle),
			Context.bHasEquippedItem ? TEXT(" lleva un objeto en la mano") : TEXT(""), Context.bIsCarrying ? TEXT(" llevando o llevada") : TEXT(""),
			Context.bIsSwimming ? TEXT(" nadando") : TEXT(""),
			Context.bIsDiving ? TEXT(" en pleno panzazo") : TEXT(""), Context.bIsKnockedDown ? TEXT(" derribada") : TEXT(""),
			Context.bIsDead ? TEXT(" muerta") : TEXT(""));
		return;
	}
	// Colgando del pico de una gaviota o en la boca de un lagarto: se escurre. Quien la sujeta la suelta antes de que nazca
	// la bola (la gaviota, aturdida en bola como al acabar el vuelo, y entonces ya va metida en el caparazón): nunca hay una
	// bola que un enemigo sigue colocando en su pico o en su boca (TN_BeachStun.h, «quién mueve a la tortuga»).
	if (TNBeach::SlipFromHolder(Turtle) && bIsInShell)
	{
		return;
	}

	ShellEnteredServerTime = Now;
	SetShellState(true);
}

void UTN_ShellComponent::ForceExitShell()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	bExitLocked = false;
	if (!bIsInShell)
	{
		return;
	}

	// Sin comprobar permanencia mínima: esto lo llaman muerte y derribo, donde
	// dejar el caparazón puesto significaría dejar también el speed cap pegado.
	SetShellState(false);
}

void UTN_ShellComponent::ForceEnterShell(bool bPhysics, bool bExitOnRest)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bIsInShell)
	{
		return;
	}
	if (GetWorld())
	{
		ShellEnteredServerTime = GetWorld()->GetTimeSeconds();
	}
	SetShellState(true, bPhysics, bExitOnRest);
}

void UTN_ShellComponent::SetShellState(bool bInShell, bool bPhysics, bool bExitOnRest)
{
	if (bIsInShell == bInShell)
	{
		return;
	}

	bIsInShell = bInShell;

	// En un listen server OnRep no dispara en la máquina dueña de la variable,
	// así que aplicamos aquí y OnRep se encarga del resto de máquinas.
	ApplyShellState(bInShell);

	// Física propia (solo servidor; la caja y Body se replican solos).
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (bInShell)
	{
		if (bPhysics && Turtle && !TNShellComponentDetail::IsCarried(Turtle))
		{
			StartBody(Turtle->GetVelocity(), false, bExitOnRest);
		}
	}
	else
	{
		StopBody();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Caparazón con física propia
// ─────────────────────────────────────────────────────────────────────────────

void UTN_ShellComponent::StartBody(const FVector& Velocity, bool bLaunched, bool bExitOnRest)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	UWorld* World = GetWorld();
	if (!Turtle || !World || !Turtle->HasAuthority() || !bIsInShell || TNShellComponentDetail::IsCarried(Turtle))
	{
		return;
	}
	// Una caja cada vez.
	if (Body)
	{
		StopBody();
	}

	const FVector Location = Turtle->GetActorLocation();
	const bool bFast = Velocity.SizeSquared2D() > FMath::Square(150.0);
	const float Yaw = (bLaunched && bFast) ? static_cast<float>(Velocity.Rotation().Yaw) : static_cast<float>(Turtle->GetActorRotation().Yaw);

	// A mano: la caja nace de pie en el tronco (su +X, la cabeza, hacia arriba) y se vuelca hacia delante sobre la
	// tripa. Lanzada o soltada: nace tumbada donde está el actor.
	const FRotator Rotation = bLaunched ? FRotator(0.f, Yaw, 0.f) : FRotator(90.f, Yaw, 0.f);
	// Nunca metida en algo (decorado, una muralla, la arena): saldría empujada y podría cruzar la malla fina del terreno.
	const FVector Center = FindFreeBodySpot(bLaunched ? Location : Location - FVector(0.0, 0.0, 2.5), Rotation);

	FActorSpawnParameters Params;
	Params.Owner = Turtle;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_ShellBody* NewBody = World->SpawnActor<ATN_ShellBody>(ATN_ShellBody::StaticClass(), FTransform(Rotation, Center), Params);
	if (!NewBody || !NewBody->GetBox())
	{
		return;
	}
	NewBody->InitBody(Turtle, bExitOnRest);

	// Antes del primer paso de física: la cápsula deja de chocar para que la caja no nazca empujada por ella.
	Body = NewBody;
	AdoptBody(NewBody);

	UBoxComponent* BoxComp = NewBody->GetBox();
	const FVector Right = FRotator(0.f, Yaw, 0.f).RotateVector(FVector::RightVector);
	BoxComp->SetPhysicsLinearVelocity(Velocity);
	if (!bLaunched)
	{
		BoxComp->SetPhysicsAngularVelocityInRadians(Right * 3.0);
	}
	else if (bFast)
	{
		BoxComp->SetPhysicsAngularVelocityInRadians(Right * 7.0);
	}
	Turtle->ForceNetUpdate();
}

FVector UTN_ShellComponent::FindFreeBodySpot(const FVector& Center, const FRotator& Rotation) const
{
	const ATortugaCharacter* Turtle = GetTurtleOwner();
	const UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return Center;
	}
	// Lo que para a una caja de física (el terreno, el decorado, las murallas, otras tortugas), sin la propia tortuga (su
	// cápsula deja de chocar en cuanto se engancha a la caja) ni su caja de antes. La caja, un pelo más pequeña: tocar el
	// suelo no es estar metida en él.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TNShellFreeSpot), false, Turtle);
	if (Body)
	{
		Query.AddIgnoredActor(Body.Get());
	}
	const FQuat Rot = Rotation.Quaternion();
	const FCollisionShape Shape = FCollisionShape::MakeBox(ATN_ShellBody::BoxHalfExtent() - FVector(3.0));
	// Libre: nada que la pare solapándola.
	auto IsFree = [World, &Rot, &Shape, &Query](const FVector& At)
	{
		return !World->OverlapBlockingTestByChannel(At, Rot, ECC_PhysicsBody, Shape, Query);
	};
	// Con la cápsula de pie libre, la caja (de pie o tumbada) cabe dentro de ella: esto solo busca cuando a la tortuga la han
	// dejado metida en algo sin barrer (un enemigo que la arrastra en el pico o en la boca por el decorado, un teletransporte).
	if (IsFree(Center))
	{
		return Center;
	}
	const FVector Base = Center;
	// Hacia arriba (lo normal: la han dejado hundida en la arena o en algo bajo) y alrededor, cada vez más lejos y
	// más alto.
	static const float Ups[] = { 25.f, 50.f, 90.f, 140.f, 200.f };
	for (const float Up : Ups)
	{
		const FVector Try = Base + FVector(0.0, 0.0, Up);
		if (IsFree(Try))
		{
			return Try;
		}
	}
	static const float Rings[] = { 60.f, 120.f, 200.f };
	for (const float Ring : Rings)
	{
		for (int32 k = 0; k < 8; ++k)
		{
			const double Angle = UE_DOUBLE_TWO_PI * k / 8.0;
			const FVector Side(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, 0.0);
			for (const float Up : { 0.f, 50.f, 120.f })
			{
				const FVector Try = Base + Side + FVector(0.0, 0.0, Up);
				if (IsFree(Try))
				{
					return Try;
				}
			}
		}
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Caparazón] %s: sin sitio libre para la bola en (%.0f, %.0f, %.0f)."), *GetNameSafe(Turtle), Center.X, Center.Y, Center.Z);
	// Al menos encima de la arena, si se ha podido subir.
	return Base;
}

void UTN_ShellComponent::StopBody()
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle || !Turtle->HasAuthority())
	{
		return;
	}
	ATN_ShellBody* OldBody = Body;
	if (!OldBody && !bBodyLocalApplied)
	{
		return;
	}

	if (!TNShellComponentDetail::IsCarried(Turtle))
	{
		if (OldBody && OldBody->GetBox())
		{
			const UBoxComponent* BoxComp = OldBody->GetBox();
			PlaceStandingFromBox(BoxComp->GetComponentTransform(), ATN_ShellBody::IsInWater(*BoxComp), OldBody);
		}
		else if (bHasLastBox)
		{
			PlaceStandingFromBox(LastBoxTransform, false, nullptr);
		}
	}

	Body = nullptr;
	LocalBody = nullptr;
	ApplyBodyLocalState(false);
	if (OldBody)
	{
		OldBody->MarkReleased();
		OldBody->Destroy();
	}
	Turtle->ForceNetUpdate();
}

void UTN_ShellComponent::OnRep_Body()
{
	if (Body)
	{
		AdoptBody(Body);
	}
	else
	{
		ReleaseLocalBody();
	}
}

void UTN_ShellComponent::AdoptBody(ATN_ShellBody* InBody)
{
	if (!InBody || InBody->GetTurtle() != GetTurtleOwner())
	{
		return;
	}
	// En los clientes solo la caja que el servidor dice que es la actual (o la que llega antes que la referencia).
	if (GetOwner() && !GetOwner()->HasAuthority() && Body && Body != InBody)
	{
		return;
	}
	if (TNShellComponentDetail::IsCarried(GetTurtleOwner()))
	{
		return;
	}
	LocalBody = InBody;
	bHasLastBox = false;
	ApplyBodyLocalState(true);
}

void UTN_ShellComponent::DropLocalBody()
{
	if (!bBodyLocalApplied)
	{
		return;
	}
	LocalBody = nullptr;
	ApplyBodyLocalState(false);
}

void UTN_ShellComponent::ReleaseLocalBody()
{
	if (!bBodyLocalApplied)
	{
		LocalBody = nullptr;
		return;
	}
	ATN_ShellBody* OldBody = LocalBody.Get();
	if (!TNShellComponentDetail::IsCarried(GetTurtleOwner()))
	{
		if (OldBody && OldBody->GetBox())
		{
			const UBoxComponent* BoxComp = OldBody->GetBox();
			PlaceStandingFromBox(BoxComp->GetComponentTransform(), ATN_ShellBody::IsInWater(*BoxComp), OldBody);
		}
		else if (bHasLastBox)
		{
			PlaceStandingFromBox(LastBoxTransform, false, nullptr);
		}
	}
	LocalBody = nullptr;
	ApplyBodyLocalState(false);
}

void UTN_ShellComponent::FollowBody(ATN_ShellBody* InBody)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle || !InBody || !bBodyLocalApplied || LocalBody.Get() != InBody || !InBody->GetBox())
	{
		return;
	}
	// Si ya la han cogido (el enganche al portador llega antes que la caja se destruya), no se toca.
	if (TNShellComponentDetail::IsCarried(Turtle))
	{
		return;
	}
	EnforceBodyLocalState();
	LastBoxTransform = InBody->GetBox()->GetComponentTransform();
	bHasLastBox = true;
	Turtle->PlaceOnShellBody(LastBoxTransform);
}

void UTN_ShellComponent::NotifyBodyAtRest()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// ForceExitShell desbloquea la salida y, vía SetShellState, pone a la tortuga de pie donde quedó la caja.
	ForceExitShell();
}

void UTN_ShellComponent::NotifyBodyInWater()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	ForceExitShell();
}

void UTN_ShellComponent::NotifyBodyEnded(ATN_ShellBody* InBody)
{
	if (!InBody)
	{
		return;
	}
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (Body != InBody)
		{
			return;
		}
		Body = nullptr;
		ReleaseLocalBody();
		ForceExitShell();
		return;
	}
	// Cliente: la caja enganchada se ha ido antes que la réplica de Body.
	if (LocalBody.Get() == InBody)
	{
		ReleaseLocalBody();
	}
}

void UTN_ShellComponent::PlaceStandingFromBox(const FTransform& BoxWorld, bool bInWater, const AActor* IgnoreActor)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return;
	}
	const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	const double HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 70.0;
	const FVector Center = BoxWorld.GetLocation();

	// Mira hacia donde apunta la cabeza (+X de la caja); si la caja está de pie, hacia donde mira la tripa (-Z).
	FVector Heading = BoxWorld.GetUnitAxis(EAxis::X);
	Heading.Z = 0.0;
	if (Heading.SizeSquared() < 0.04)
	{
		Heading = -BoxWorld.GetUnitAxis(EAxis::Z);
		Heading.Z = 0.0;
	}
	const float Yaw = Heading.IsNearlyZero() ? static_cast<float>(Turtle->GetActorRotation().Yaw) : static_cast<float>(Heading.Rotation().Yaw);

	// De pie sobre el suelo que hay debajo del caparazón; en el agua, en su centro (el movimiento pasa a nadar).
	FVector Stand = bInWater ? Center : Center + FVector(0.0, 0.0, HalfHeight - ATN_ShellBody::BoxHalfExtent().Z);
	if (!bInWater)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TNShellStandUp), false, Turtle);
		if (IgnoreActor)
		{
			Query.AddIgnoredActor(IgnoreActor);
		}
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Center + FVector(0.0, 0.0, 40.0), Center - FVector(0.0, 0.0, 140.0), ECC_WorldStatic, Query))
		{
			Stand = Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 2.0);
		}
		// Sin suelo justo debajo: la caja puede haber quedado hundida en la malla fina del terreno (la traza empezaba ya por
		// debajo de la superficie). Se busca el suelo desde algo más arriba (2,5 m: lo que se hunde, no un puente por encima);
		// de pie encima, nunca debajo del mapa.
		else if (World->LineTraceSingleByChannel(Hit, Center + FVector(0.0, 0.0, 250.0), Center - FVector(0.0, 0.0, 140.0), ECC_WorldStatic, Query)
			&& Hit.ImpactNormal.Z > 0.5)
		{
			Stand = Hit.ImpactPoint + FVector(0.0, 0.0, HalfHeight + 2.0);
		}
	}
	Turtle->SetActorLocationAndRotation(Stand, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
}

void UTN_ShellComponent::ApplyBodyLocalState(bool bOn)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle || bBodyLocalApplied == bOn)
	{
		return;
	}
	bBodyLocalApplied = bOn;
	UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();

	if (bOn)
	{
		if (Move)
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
			Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
			Move->SetComponentTickEnabled(false);
		}
		if (Capsule)
		{
			// La cápsula ya no choca con nada (el cuerpo es la caja), pero sigue solapando: zonas, agua y disparadores
			// la siguen viendo.
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionResponseToAllChannels(ECR_Overlap);
			Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		}
		// La pose (encogerse en el caparazón) se anima cada fotograma también en el anfitrión con la de un cliente: ya no
		// llegan sus movimientos, que eran los que la animaban allí (#246).
		ApplyMeshPoseTicking(true);
		// Cada máquina sigue localmente a la caja, que ya se replica sola.
		if (Turtle->HasAuthority())
		{
			Turtle->SetReplicateMovement(false);
		}
		return;
	}

	// Al soltar, lo de serie de la clase de la tortuga, no una copia de lo que hubiera al engancharse: si otro sistema lo
	// tenía cambiado en ese momento (el ragdoll del derribo deja la cápsula sin colisión y su vuelta puede llegar a esta
	// máquina después que la bola; un enemigo o el panzazo apagan el suavizado), se devolvía eso, y la tortuga salía de la
	// bola sin chocar con el suelo o a saltitos. Así da igual cuántas veces entre y salga, y de dónde venga.
	const ACharacter* Defaults = Turtle->GetClass()->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* DefaultCapsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
	const UCharacterMovementComponent* DefaultMove = Defaults ? Defaults->GetCharacterMovement() : nullptr;
	USkeletalMeshComponent* SkelMesh = Turtle->GetMesh();
	const bool bRagdoll = SkelMesh && SkelMesh->IsSimulatingPhysics();
	// Vuelven los movimientos del cliente: en el servidor, otra vez animan ellos su pose.
	ApplyMeshPoseTicking(false);
	// La malla vuelve a chocar como toca (EnforceBodyLocalState le quitó la física mientras la movía la caja): la del
	// ragdoll (perfil Ragdoll) si el derribo sigue en esta máquina; si no, la de serie de la clase.
	if (SkelMesh)
	{
		const USkeletalMeshComponent* DefaultMesh = Defaults ? Defaults->GetMesh() : nullptr;
		const ECollisionEnabled::Type WantedMeshCollision = bRagdoll
			? ECollisionEnabled::QueryAndPhysics
			: (DefaultMesh ? DefaultMesh->GetCollisionEnabled() : SkelMesh->GetCollisionEnabled());
		if (SkelMesh->GetCollisionEnabled() != WantedMeshCollision)
		{
			SkelMesh->SetCollisionEnabled(WantedMeshCollision);
		}
	}
	if (Capsule)
	{
		if (DefaultCapsule && DefaultCapsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Capsule->SetCollisionObjectType(DefaultCapsule->GetCollisionObjectType());
			Capsule->SetCollisionResponseToChannels(DefaultCapsule->GetCollisionResponseToChannels());
			Capsule->SetCollisionEnabled(DefaultCapsule->GetCollisionEnabled());
		}
		else
		{
			// Sin la de la clase (no debería pasar): la de un personaje, sin tapar la cámara (como la pone la tortuga).
			Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
			Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		// El ragdoll de esta máquina sigue simulando (su derribo aún no ha acabado aquí): sin colisión hasta que se levante,
		// como la deja el derribo (ATortugaCharacter::ApplyKnockdownVisual se la devuelve al levantarse).
		if (bRagdoll)
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	if (bRagdoll)
	{
		// La malla, el movimiento y su réplica son del ragdoll hasta que se levante: los devuelve el derribo.
		if (Move)
		{
			Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		}
		return;
	}
	Turtle->ResetMeshTransform();
	if (Move)
	{
		Move->NetworkSmoothingMode = DefaultMove ? DefaultMove->NetworkSmoothingMode : ENetworkSmoothingMode::Exponential;
		Move->SetComponentTickEnabled(true);
		if (!TNShellComponentDetail::IsCarried(Turtle))
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	if (Turtle->HasAuthority())
	{
		Turtle->SetReplicateMovement(true);
	}
}

void UTN_ShellComponent::EnforceBodyLocalState()
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle || !bBodyLocalApplied)
	{
		return;
	}
	// Mientras la mueve su caja, nada más la mueve: si otro sistema le ha devuelto el movimiento o la colisión (la vuelta de
	// un derribo o una corrección de red que llegan a esta máquina con la bola ya puesta, una sujeción que se suelta...), la
	// cápsula andaría o caería por su cuenta y chocaría con su propia caja. Se vuelve a dejar como la deja la bola.
	UCharacterMovementComponent* Move = Turtle->GetCharacterMovement();
	if (Move && (Move->IsComponentTickEnabled() || Move->MovementMode != MOVE_None))
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
		Move->SetComponentTickEnabled(false);
		Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	}
	UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
	if (Capsule && Capsule->GetCollisionEnabled() != ECollisionEnabled::QueryOnly)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionResponseToAllChannels(ECR_Overlap);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	// Tampoco la malla: con el ragdoll de un derribo simulando (un golpe que la derriba ya en la bola, o una bola que nace
	// antes de que acabe el derribo en esta máquina), sus cuerpos tienen el perfil Ragdoll, que choca con la caja
	// (PhysicsActor). La tortuga se coloca dentro de la caja en cada fotograma (PlaceOnShellBody) y la física los separaba
	// en cada paso: la bola giraba como un torbellino alrededor de un punto de fuera de la concha (#25). Sin física, el
	// ragdoll sigue a la caja y ApplyBodyLocalState(false) le devuelve su colisión al soltarla.
	USkeletalMeshComponent* SkelMesh = Turtle->GetMesh();
	if (SkelMesh && CollisionEnabledHasPhysics(SkelMesh->GetCollisionEnabled()))
	{
		SkelMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	// Ni la pose congelada: si el servidor la vuelve a poseer con la bola puesta, el motor la deja otra vez animándose solo
	// con los movimientos del cliente, que no llegan.
	if (SkelMesh && SkelMesh->bOnlyAllowAutonomousTickPose)
	{
		ApplyMeshPoseTicking(true);
	}
}

void UTN_ShellComponent::ApplyMeshPoseTicking(bool bDrivenByBody)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	USkeletalMeshComponent* SkelMesh = Turtle ? Turtle->GetMesh() : nullptr;
	if (!SkelMesh)
	{
		return;
	}
	// Lo que decide el motor al poseerla (ACharacter::PossessedBy), sin mirar la réplica de movimiento: aquí aún está
	// apagada por la propia bola.
	const bool bServerOfRemotePlayer = Turtle->HasAuthority() && Turtle->GetRemoteRole() == ROLE_AutonomousProxy
		&& Turtle->GetNetConnection() != nullptr;
	SkelMesh->bOnlyAllowAutonomousTickPose = TNShellLogic::OnlyTickPoseFromClientMoves(bServerOfRemotePlayer, bDrivenByBody);
}

void UTN_ShellComponent::OnRep_IsInShell()
{
	ApplyShellState(bIsInShell);
}

void UTN_ShellComponent::ApplyShellState(bool bInShell)
{
	ATortugaCharacter* Turtle = GetTurtleOwner();
	if (!Turtle)
	{
		return;
	}

	if (UTN_StaminaComponent* Stamina = Turtle->GetStaminaComponent())
	{
		if (bInShell)
		{
			// El freno entra por el speed cap del componente de stamina, único punto
			// del proyecto que escribe MaxWalkSpeed. Cap 0 = el personaje no se desplaza.
			Stamina->SetSprintRequested(false);
			Stamina->SetSpeedCap(TNMovementLimits::ShellSource(), 0.f);
		}
		else
		{
			Stamina->ClearSpeedCap(TNMovementLimits::ShellSource());
		}
	}

	// El personaje se encarga de lo suyo (cancelar emote, visual del caparazón):
	// esta función corre en todas las máquinas, así que el reparto es el mismo.
	Turtle->OnShellStateChanged(bInShell);

	// Sonido local en cada máquina — no multicast: ApplyShellState ya se ejecuta
	// en todas, y un multicast encima duplicaría el disparo. Sintetizado por defecto;
	// los assets, de respaldo (bSynthShellSounds a false o sin sintetizador, como en
	// un servidor dedicado).
	UTN_TurtleFoleyComponent* Foley = bSynthShellSounds ? UTN_TurtleFoleyComponent::FindOrAddTo(Turtle) : nullptr;
	if (Foley)
	{
		Foley->PlayShell(bInShell);
	}
	else if (USoundBase* Sound = bInShell ? EnterShellSound : ExitShellSound)
	{
		UGameplayStatics::SpawnSoundAtLocation(Turtle, Sound, Turtle->GetActorLocation());
	}
}
