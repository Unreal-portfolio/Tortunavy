#include "World/TN_AirdropPoint.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"

ATN_AirdropPoint::ATN_AirdropPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
	Icon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Icon"));
	if (Icon)
	{
		Icon->SetupAttachment(SceneRoot);
		Icon->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
		Icon->bIsScreenSizeScaled = true;
	}
#endif
}
