#include "MMOZoneComponent.h"
#include "MMOGameplay.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Modules/ModuleManager.h"
#include "GameFeatureData.h"
#include "GameFeatureAction_AddComponents.h"
#include "ModularGameState.h"
#include "UObject/ConstructorHelpers.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, MMOCore)
UMMOZoneComponent::UMMOZoneComponent()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    ZoneMesh = Cube.Object;
}
void UMMOContentLibrary::ConfigureFeatureData(UGameFeatureData* Data)
{
#if WITH_EDITOR
    if (!Data) return;
    auto* Action = NewObject<UGameFeatureAction_AddComponents>(Data, TEXT("AddTrainingZone"));
    FGameFeatureComponentEntry Entry;
    Entry.ActorClass = AModularGameStateBase::StaticClass(); Entry.ComponentClass = UMMOZoneComponent::StaticClass();
    Entry.bClientComponent = true; Entry.bServerComponent = true; Action->ComponentList.Add(Entry);
    Data->GetMutableActionsInEditor().Reset(); Data->GetMutableActionsInEditor().Add(Action); Data->MarkPackageDirty();
#endif
}
void UMMOZoneComponent::BeginPlay()
{
    Super::BeginPlay(); UWorld* World = GetWorld();
    UStaticMesh* Cube = ZoneMesh;
    auto Box = [this, World, Cube](const TCHAR* StableName, FVector Position, FVector Scale)
    {
        FActorSpawnParameters Parameters;
        Parameters.Name = FName(StableName);
        Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
        Parameters.bDeferConstruction = true;
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Position, FRotator::ZeroRotator, Parameters);
        if (!ensureMsgf(Actor && Cube, TEXT("MMOCore requires one uniquely named geometry actor and its cooked Cube mesh."))) return;
        // Explicit names are identical in every world's deterministic zone. Dynamic spawn suffixes differ in cooked clients.
        Actor->SetNetAddressable(); // Native contract: set before FinishSpawning.
        Actor->SetMobility(EComponentMobility::Movable);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->FinishSpawning(FTransform(FRotator::ZeroRotator, Position, Scale));
        Actor->SetMobility(EComponentMobility::Static);
        Spawned.Add(Actor);
        UE_LOG(LogTemp, Display, TEXT("MMO_ZONE_GEOMETRY name=%s stable=%d mobility=Static"), *Actor->GetName(), Actor->IsNameStableForNetworking());
    };
    // Fixed geometry is built on all peers; stable paths resolve any CharacterMovement base references.
    Box(TEXT("MMOZone_Floor"), FVector(0,0,-50), FVector(40,40,1));
    Box(TEXT("MMOZone_WallEast"), FVector(2000,0,100), FVector(1,40,3)); Box(TEXT("MMOZone_WallWest"), FVector(-2000,0,100), FVector(1,40,3));
    Box(TEXT("MMOZone_WallNorth"), FVector(0,2000,100), FVector(40,1,3)); Box(TEXT("MMOZone_WallSouth"), FVector(0,-2000,100), FVector(40,1,3));
    Box(TEXT("MMOZone_Obstacle"), FVector(750,600,100), FVector(2,7,3));
    if (GetOwner()->HasAuthority())
        for (int32 I=0; I<3; ++I) Spawned.Add(World->SpawnActor<AMMOTarget>(FVector(650, -400 + I*350, 90), FRotator::ZeroRotator));
    if (World->GetNetMode() != NM_DedicatedServer)
    {
        auto* Atmosphere = World->SpawnActor<ASkyAtmosphere>(); Spawned.Add(Atmosphere);
        auto* Sun = World->SpawnActor<ADirectionalLight>(FVector(0,0,800), FRotator(-45,-30,0)); Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sun->GetLightComponent()->SetIntensity(8); Spawned.Add(Sun);
        auto* Sky = World->SpawnActor<ASkyLight>(); Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sky->GetLightComponent()->SetIntensity(1.2f); Spawned.Add(Sky);
    }
    UE_LOG(LogTemp, Display, TEXT("MMOCore: zone added to %s"), *GetNameSafe(GetOwner()));
}
void UMMOZoneComponent::EndPlay(EEndPlayReason::Type Reason)
{
    for (const auto& Actor : Spawned) if (IsValid(Actor)) Actor->Destroy(); Spawned.Empty(); Super::EndPlay(Reason);
}

