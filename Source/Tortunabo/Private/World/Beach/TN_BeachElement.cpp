#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTickWakeSubsystem.h"
#include "Core/TN_Log.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

ATN_BeachElement::ATN_BeachElement()
{
	bReplicates = true;
	SetReplicateMovement(false);
	// Lo de serie para lo que no pase por SpawnElement; los de la ronda cambian a ApplyRoundNetProfile (relevancia por
	// distancia y dormancy).
	SetNetCullDistanceSquared(FMath::Square(MinNetRelevance));
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATN_BeachElement::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachElement, Spec);
}

ATN_BeachElement* ATN_BeachElement::SpawnElement(UWorld* World, const FTransform& Transform, const FTNBeachElementSpec& InSpec)
{
	if (!World) { return nullptr; }
	const FString Path = FString::Printf(TEXT("/Script/Tortunabo.%s"), TNBeach::ClassNameOf(InSpec.Element));
	UClass* Class = FindObject<UClass>(nullptr, *Path);
	if (!Class || !Class->IsChildOf(ATN_BeachElement::StaticClass()))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] Sin clase %s para %s: no se crea."), *Path,
			*UEnum::GetValueAsString(InSpec.Element));
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	ATN_BeachElement* Element = World->SpawnActor<ATN_BeachElement>(Class, Transform, Params);
	if (!Element) { return nullptr; }
	Element->Spec = InSpec;
	// Después del constructor de la subclase y antes de la primera réplica: manda sobre lo que ponga cada clase.
	Element->ApplyRoundNetProfile();
	Element->FinishSpawning(Transform);
	return Element;
}

float ATN_BeachElement::GetFootprintRadius() const
{
	return static_cast<float>(TNBeach::FootprintRadius(Spec.Element)) * FMath::Max(0.1f, Spec.SizeScale);
}

bool ATN_BeachElement::WantsNetDormancy() const
{
	return TNBeach::CategoryOf(Spec.Element) != ETNBeachCategory::Enemy;
}

bool ATN_BeachElement::WantsAlwaysRelevant() const
{
	return TNBeach::CategoryOf(Spec.Element) != ETNBeachCategory::Enemy && GetFootprintRadius() >= LandmarkFootprint;
}

float ATN_BeachElement::GetNetRelevanceDistance() const
{
	// Lo que ocupa, con el largo de los alargados (alambre, pasos de quads): lo grande se ve desde más lejos.
	const float Reach = GetFootprintRadius() + 0.5f * FMath::Max(0.f, Spec.Extent);
	if (TNBeach::CategoryOf(Spec.Element) == ETNBeachCategory::Enemy)
	{
		// Los enemigos no duermen (se mueven y mandan su estado a 10 Hz cerca): cada cliente que los tiene cuesta. Con ocho
		// jugadores, la zona de gaviotas (350 m antes) y los pasos de quads (450 m) iban a casi todos a la vez. Hasta 300 m,
		// donde empieza la niebla.
		return FMath::Clamp(EnemyMinNetRelevance + 2.f * Reach, EnemyMinNetRelevance, EnemyMaxNetRelevance);
	}
	return FMath::Clamp(MinNetRelevance + 3.f * Reach, MinNetRelevance, MaxNetRelevance);
}

void ATN_BeachElement::ApplyRoundNetProfile()
{
	// Lo quieto (trampas, estructuras: bloquean) lo tiene cada cliente desde que aparece (#828): por distancia, un elemento
	// le llegaba al acercarse y, mientras, el servidor ya chocaba con él (muro invisible). Duerme nada más llegar
	// (WantsNetDormancy), así que no cuesta red ni tiempo del servidor. Los enemigos se mueven y no duermen: esos, por
	// distancia (200-300 m, más que lo que se recorre mientras llegan).
	bAlwaysRelevant = WantsAlwaysRelevant() || WantsNetDormancy();
	const float Relevance = GetNetRelevanceDistance();
	SetNetCullDistanceSquared(Relevance * Relevance);
	if (!WantsNetDormancy()) { return; }
	// Quietos: mandan su estado al aparecer (Spec y lo suyo) y se duermen; cada cambio de estado los despierta con
	// ForceNetUpdate (que vacía la dormancy) y los multicast abren canal solos. Mientras duermen no cuestan nada.
	NetDormancy = DORM_Initial;
	bRoundNetDormancy = true;
	SetNetUpdateFrequency(FMath::Min(GetNetUpdateFrequency(), DormantNetFrequency));
	SetMinNetUpdateFrequency(FMath::Min(GetMinNetUpdateFrequency(), FMath::Min(1.f, GetNetUpdateFrequency())));
}

void ATN_BeachElement::ForceNetUpdate()
{
	UWorld* World = GetWorld();
	if (!bRoundNetDormancy || !HasAuthority() || !World || IsActorBeingDestroyed())
	{
		Super::ForceNetUpdate();
		return;
	}
	// Despierto de verdad (con DORM_DormantAll, vaciar la dormancy sola no siempre abre el canal) y enviado ya; unos
	// segundos después del último cambio vuelve a dormir.
	if (NetDormancy != DORM_Awake)
	{
		SetNetDormancy(DORM_Awake);
	}
	Super::ForceNetUpdate();
	World->GetTimerManager().SetTimer(NetSleepTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (NetDormancy == DORM_Awake)
		{
			SetNetDormancy(DORM_DormantAll);
		}
	}), NetWakeSeconds, false);
}

void ATN_BeachElement::BeginPlay()
{
	Super::BeginPlay();
	if (!bSpecApplied)
	{
		bSpecApplied = true;
		ApplySpec();
	}
	UWorld* World = GetWorld();
	if (PrimaryActorTick.bCanEverTick && GetTickWakeDistance() > 0.f && World && World->IsGameWorld())
	{
		if (UTN_BeachTickWakeSubsystem* Wake = World->GetSubsystem<UTN_BeachTickWakeSubsystem>())
		{
			bTickWakeRegistered = true;
			Wake->Register(this);
		}
	}
}

void ATN_BeachElement::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bTickWakeRegistered)
	{
		bTickWakeRegistered = false;
		if (UTN_BeachTickWakeSubsystem* Wake = GetWorld() ? GetWorld()->GetSubsystem<UTN_BeachTickWakeSubsystem>() : nullptr)
		{
			Wake->Unregister(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachElement::OnRep_Spec()
{
	bSpecApplied = true;
	ApplySpec();
}
