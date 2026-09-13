#include "MMOFrameworkSubsystem.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MMOFramework)
namespace MMOInitTags
{
    UE_DEFINE_GAMEPLAY_TAG(Spawned, "InitState.Spawned");
    UE_DEFINE_GAMEPLAY_TAG(DataAvailable, "InitState.DataAvailable");
    UE_DEFINE_GAMEPLAY_TAG(DataInitialized, "InitState.DataInitialized");
    UE_DEFINE_GAMEPLAY_TAG(GameplayReady, "InitState.GameplayReady");
}
void UMMOFrameworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UGameFrameworkComponentManager* Manager = Collection.InitializeDependency<UGameFrameworkComponentManager>();
    Manager->RegisterInitState(MMOInitTags::Spawned, false, FGameplayTag());
    Manager->RegisterInitState(MMOInitTags::DataAvailable, false, MMOInitTags::Spawned);
    Manager->RegisterInitState(MMOInitTags::DataInitialized, false, MMOInitTags::DataAvailable);
    Manager->RegisterInitState(MMOInitTags::GameplayReady, false, MMOInitTags::DataInitialized);
}
