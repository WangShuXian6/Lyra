#include "MMOExperienceManagerComponent.h"
#include "MMOExperienceDefinition.h"
#include "MMOPawnData.h"
#include "MMOAbilitySet.h"
#include "MMOInputConfig.h"
#include "MMOPawnExtensionComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/StreamableManager.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFeaturesSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogMMOExperience, Log, All);

namespace
{
    // GameFeatures is process-scoped; one PIE client must not deactivate another world's feature.
    TMap<FString, int32> PluginUsers;
    void ReleasePlugin(const FString& URL)
    {
        check(IsInGameThread());
        int32* Count = PluginUsers.Find(URL);
        if (Count && --*Count == 0) { PluginUsers.Remove(URL); UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(URL); }
    }
    struct FActionTeardown
    {
        TArray<TStrongObjectPtr<UGameFeatureAction>> Actions;
        TArray<TSharedPtr<FStreamableHandle>> Handles;
        TArray<FString> URLs;
        int32 Expected = INDEX_NONE;
        int32 Completed = 0;
        bool bFinished = false;
        void TryFinish()
        {
            if (bFinished || Expected == INDEX_NONE || Completed != Expected) return;
            bFinished = true;
            for (const auto& Action : Actions) Action->OnGameFeatureUnregistering();
            for (const FString& URL : URLs) ReleasePlugin(URL);
            Actions.Reset(); Handles.Reset(); URLs.Reset();
        }
    };
}

UMMOExperienceManagerComponent::UMMOExperienceManagerComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{ SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = false; }
UMMOExperienceManagerComponent* UMMOExperienceManagerComponent::FindExperienceManager(const UWorld* World)
{ return World && World->GetGameState() ? World->GetGameState()->FindComponentByClass<UMMOExperienceManagerComponent>() : nullptr; }
void UMMOExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMMOExperienceManagerComponent, SelectedExperienceId); }
void UMMOExperienceManagerComponent::BeginPlay()
{ Super::BeginPlay(); if (SelectedExperienceId.IsValid() && LoadState == EMMOExperienceLoadState::Unloaded) StartExperienceLoad(); }
void UMMOExperienceManagerComponent::EndPlay(EEndPlayReason::Type Reason)
{ ReleaseExperience(); ReadyCallbacks.Clear(); Super::EndPlay(Reason); }
bool UMMOExperienceManagerComponent::SetCurrentExperience(FPrimaryAssetId ExperienceId)
{
    if (!GetOwner()->HasAuthority() || !ExperienceId.IsValid() || ExperienceId.PrimaryAssetType != FPrimaryAssetType(TEXT("MMOExperience"))) return false;
    if (SelectedExperienceId.IsValid()) return SelectedExperienceId == ExperienceId;
    SelectedExperienceId = ExperienceId; GetOwner()->ForceNetUpdate();
    if (HasBegunPlay()) StartExperienceLoad();
    return true;
}
void UMMOExperienceManagerComponent::OnRep_SelectedExperienceId()
{ if (HasBegunPlay() && LoadState == EMMOExperienceLoadState::Unloaded) StartExperienceLoad(); }
UMMOPawnData* UMMOExperienceManagerComponent::GetPawnData() const
{ return IsExperienceReady() && Experience ? Experience->DefaultPawnData.Get() : nullptr; }
void UMMOExperienceManagerComponent::CallOrRegister_OnReady(FOnMMOExperienceReady::FDelegate Delegate)
{ if (IsExperienceReady()) Delegate.ExecuteIfBound(Experience); else ReadyCallbacks.Add(Delegate); }
void UMMOExperienceManagerComponent::StartExperienceLoad()
{
    if (LoadState != EMMOExperienceLoadState::Unloaded || !SelectedExperienceId.IsValid()) return;
    LoadState = EMMOExperienceLoadState::LoadingAssets;
    GetWorld()->GetTimerManager().SetTimer(LoadTimeout, FTimerDelegate::CreateWeakLambda(this, [this]() { Fail(TEXT("Experience load exceeded 60 seconds")); }), 60.f, false);
    const FSoftObjectPath Path = UAssetManager::Get().GetPrimaryAssetPath(SelectedExperienceId);
    if (!Path.IsValid()) { Fail(TEXT("Asset Manager has no entry for ") + SelectedExperienceId.ToString()); return; }
    auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path, FStreamableDelegate::CreateWeakLambda(this, [this, Path]()
    {
        if (LoadState != EMMOExperienceLoadState::LoadingAssets) return;
        Experience = Cast<UMMOExperienceDefinition>(Path.ResolveObject());
        if (!Experience) { Fail(TEXT("Experience is not an MMOExperienceDefinition data asset: ") + Path.ToString()); return; }
        LoadDependencyClosure();
    }));
    if (Handle) LoadHandles.Add(Handle); else Fail(TEXT("Could not request Experience asset"));
}
void UMMOExperienceManagerComponent::LoadDependencyClosure()
{
    if (LoadState != EMMOExperienceLoadState::LoadingAssets || !Experience) return;
    TArray<FSoftObjectPath> Paths;
    if (Experience->DefaultPawnData.IsNull()) { Fail(TEXT("Experience.DefaultPawnData is required")); return; }
    Paths.Add(Experience->DefaultPawnData.ToSoftObjectPath());
    TArray<UPrimaryDataAsset*> DataAssets { Experience };
    for (const auto& Set : Experience->ActionSets)
    {
        if (Set.IsNull()) { Fail(TEXT("Experience contains an empty ActionSet")); return; }
        Paths.AddUnique(Set.ToSoftObjectPath()); if (Set.Get()) DataAssets.Add(Set.Get());
    }
    if (UMMOPawnData* Data = Experience->DefaultPawnData.Get())
    {
        DataAssets.Add(Data);
        if (Data->PawnClass.IsNull()) { Fail(TEXT("PawnData.PawnClass is required")); return; }
        Paths.AddUnique(Data->PawnClass.ToSoftObjectPath());
        for (const auto& Set : Data->AbilitySets)
        {
            if (Set.IsNull()) { Fail(TEXT("PawnData contains an empty AbilitySet")); return; }
            Paths.AddUnique(Set.ToSoftObjectPath());
            if (Set.Get()) { DataAssets.Add(Set.Get()); Set->CollectAssetPaths(Paths); }
        }
        if (GetNetMode() != NM_DedicatedServer)
        {
            if (!Data->InputMappingContext.IsNull()) Paths.AddUnique(Data->InputMappingContext.ToSoftObjectPath());
            if (!Data->InputConfig.IsNull())
            {
                Paths.AddUnique(Data->InputConfig.ToSoftObjectPath());
                if (UMMOInputConfig* InputConfig = Data->InputConfig.Get())
                {
                    DataAssets.Add(InputConfig);
                    InputConfig->CollectAssetPaths(Paths);
                }
            }
            if (!Data->AnimInstanceClass.IsNull()) Paths.AddUnique(Data->AnimInstanceClass.ToSoftObjectPath());
        }
    }
    // Include metadata-derived soft component/widget references used by the world's Actions.
    for (UPrimaryDataAsset* Data : DataAssets)
    {
        TArray<FAssetBundleEntry> Bundles;
        UAssetManager::Get().GetAssetBundleEntries(Data->GetPrimaryAssetId(), Bundles);
        for (const auto& Bundle : Bundles)
        {
            if ((Bundle.BundleName == TEXT("Client") && GetNetMode() == NM_DedicatedServer) ||
                (Bundle.BundleName == TEXT("Server") && GetNetMode() == NM_Client)) continue;
            for (const auto& Path : Bundle.AssetPaths) Paths.AddUnique(FSoftObjectPath(Path));
        }
    }
    TArray<FSoftObjectPath> Missing;
    for (const FSoftObjectPath& Path : Paths)
    {
        if (Path.ResolveObject()) continue;
        if (RequestedPaths.Contains(Path)) { Fail(TEXT("Dependency failed to load: ") + Path.ToString()); return; }
        Missing.AddUnique(Path);
    }
    if (Missing.IsEmpty()) { ActivateFeatures(); return; }
    for (const auto& Path : Missing) RequestedPaths.Add(Path);
    auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Missing, FStreamableDelegate::CreateUObject(this, &ThisClass::LoadDependencyClosure));
    if (Handle) LoadHandles.Add(Handle); else Fail(TEXT("Could not request Experience dependencies"));
}
void UMMOExperienceManagerComponent::ActivateFeatures()
{
    LoadState = EMMOExperienceLoadState::LoadingGameFeatures;
    TArray<FString> Names = Experience->GameFeaturesToEnable;
    for (const auto& Set : Experience->ActionSets) for (const FString& Name : Set->GameFeaturesToEnable) Names.AddUnique(Name);
    TArray<FString> ResolvedURLs;
    for (const FString& Name : Names)
    {
        FString URL;
        if (Name.IsEmpty() || !UGameFeaturesSubsystem::Get().GetPluginURLByName(Name, URL)) { Fail(TEXT("Unknown Game Feature: ") + Name); return; }
        ResolvedURLs.AddUnique(URL);
    }
    AcquiredPluginURLs = MoveTemp(ResolvedURLs);
    for (const FString& URL : AcquiredPluginURLs) ++PluginUsers.FindOrAdd(URL);
    PendingPlugins = AcquiredPluginURLs.Num();
    if (!PendingPlugins) { FinishActivation(); return; }
    for (const FString& URL : AcquiredPluginURLs)
    {
        UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(URL, FGameFeaturePluginLoadComplete::CreateWeakLambda(this, [this, URL](const UE::GameFeatures::FResult& Result)
        {
            if (LoadState != EMMOExperienceLoadState::LoadingGameFeatures) return;
            if (Result.HasError()) { Fail(TEXT("Game Feature failed: ") + URL + TEXT(" / ") + Result.GetError()); return; }
            if (--PendingPlugins == 0) FinishActivation();
        }));
        if (LoadState != EMMOExperienceLoadState::LoadingGameFeatures) break;
    }
}
void UMMOExperienceManagerComponent::FinishActivation()
{
    LoadState = EMMOExperienceLoadState::ExecutingActions;
    FGameFeatureActivatingContext Context;
    if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(GetWorld())) Context.SetRequiredWorldContextHandle(WorldContext->ContextHandle);
    auto Activate = [this, &Context](const TArray<TObjectPtr<UGameFeatureAction>>& Actions)
    {
        for (UGameFeatureAction* Action : Actions) if (Action)
        {
            // Duplicate instanced actions per manager so different worlds never share mutable action state.
            UGameFeatureAction* Instance = DuplicateObject<UGameFeatureAction>(Action, this);
            ActivatedActions.Add(Instance);
            Instance->OnGameFeatureRegistering(); Instance->OnGameFeatureLoading(); Instance->OnGameFeatureActivating(Context);
        }
    };
    Activate(Experience->Actions);
    for (const auto& Set : Experience->ActionSets) Activate(Set->Actions);
    GetWorld()->GetTimerManager().ClearTimer(LoadTimeout);
    LoadState = EMMOExperienceLoadState::Ready;
    UE_LOG(LogMMOExperience, Display, TEXT("Experience Ready: %s (%s)"), *SelectedExperienceId.ToString(), *GetWorld()->GetName());
    ReadyCallbacks.Broadcast(Experience); ReadyCallbacks.Clear(); OnExperienceReady.Broadcast(Experience);
    // Resolve the replicated-GameState/Pawn arrival-order case without polling.
    for (TActorIterator<APawn> It(GetWorld()); It; ++It) if (auto* Extension = It->FindComponentByClass<UMMOPawnExtensionComponent>()) Extension->CheckDefaultInitialization();
}
void UMMOExperienceManagerComponent::Fail(const FString& Reason)
{
    if (LoadState == EMMOExperienceLoadState::Failed || LoadState == EMMOExperienceLoadState::Deactivating) return;
    FailureReason = Reason; ReleaseExperience(); LoadState = EMMOExperienceLoadState::Failed;
    ReadyCallbacks.Clear(); UE_LOG(LogMMOExperience, Error, TEXT("%s"), *Reason); OnExperienceFailed.Broadcast(Reason);
}
void UMMOExperienceManagerComponent::ReleaseExperience()
{
    LoadState = EMMOExperienceLoadState::Deactivating;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(LoadTimeout);
    for (const auto& Handle : LoadHandles) if (Handle && !Handle->HasLoadCompleted()) Handle->CancelHandle();
    auto Teardown = MakeShared<FActionTeardown>();
    Teardown->Handles = MoveTemp(LoadHandles); Teardown->URLs = MoveTemp(AcquiredPluginURLs);
    for (UGameFeatureAction* Action : ActivatedActions) Teardown->Actions.Emplace(Action);
    FGameFeatureDeactivatingContext Context(TEXT("MMOExperienceEnd"), [Teardown](FStringView) { ++Teardown->Completed; Teardown->TryFinish(); });
    if (GEngine && GetWorld()) if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(GetWorld())) Context.SetRequiredWorldContextHandle(WorldContext->ContextHandle);
    for (int32 Index = ActivatedActions.Num() - 1; Index >= 0; --Index) ActivatedActions[Index]->OnGameFeatureDeactivating(Context);
    ActivatedActions.Reset(); Teardown->Expected = Context.GetNumPausers(); Teardown->TryFinish();
}
