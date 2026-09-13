// Original LyraDoc teaching code. No official Lyra source is modified.
#include "Modules/ModuleManager.h"

#if UE_BUILD_DEVELOPMENT && !UE_SERVER
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "CommonActivatableWidget.h"
#include "Character/LyraHeroComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LoadingScreenManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/LyraPlayerState.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingVerification, Log, All);

namespace TrainingVerification
{
constexpr TCHAR Map[] = TEXT("/TrainingRange/Maps/L_TrainingRange");
constexpr TCHAR Experience[] = TEXT("/TrainingRange/Experiences/B_TrainingRange.B_TrainingRange_C");
constexpr TCHAR Dash[] = TEXT("/TrainingRange/Game/GA_TrainingDash.GA_TrainingDash_C");
constexpr TCHAR HUD[] = TEXT("/ShooterCore/UserInterface/W_ShooterHUDLayout.W_ShooterHUDLayout_C");
constexpr double TimeoutSeconds = 90.0;

// These two Lyra classes expose BlueprintCallable functions but do not export their
// C++ class from LyraGame. Invoke that public reflected API, never protected fields.
UObject* ActiveItem(UObject* QuickBar)
{
    UFunction* Function = QuickBar ? QuickBar->FindFunction(TEXT("GetActiveSlotItem")) : nullptr;
    FObjectPropertyBase* Result = Function ? FindFProperty<FObjectPropertyBase>(Function, TEXT("ReturnValue")) : nullptr;
    if (!Result || !Result->HasAnyPropertyFlags(CPF_ReturnParm)) { return nullptr; }
    FStructOnScope Parameters(Function);
    QuickBar->ProcessEvent(Function, Parameters.GetStructMemory());
    return Result->GetObjectPropertyValue_InContainer(Parameters.GetStructMemory());
}

bool ReadAmmo(UObject* Item, const FGameplayTag& Tag, int32& Value)
{
    UFunction* Function = Item ? Item->FindFunction(TEXT("GetStatTagStackCount")) : nullptr;
    FStructProperty* Argument = Function ? FindFProperty<FStructProperty>(Function, TEXT("Tag")) : nullptr;
    FIntProperty* Result = Function ? FindFProperty<FIntProperty>(Function, TEXT("ReturnValue")) : nullptr;
    if (!Argument || Argument->Struct != FGameplayTag::StaticStruct() || !Result || !Tag.IsValid()) { return false; }
    FStructOnScope Parameters(Function);
    Argument->CopyCompleteValue(Argument->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), &Tag);
    Item->ProcessEvent(Function, Parameters.GetStructMemory());
    Value = Result->GetPropertyValue_InContainer(Parameters.GetStructMemory());
    return true;
}

TSharedRef<FJsonObject> Detail(const TCHAR* Key, const FString& Value)
{
    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(Key, Value);
    return Object;
}
}

class FTrainingRangeVerificationModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Compile this body in Development Editor too, so its short modular build
        // validates the native API and exports before the much longer Game build.
        // Editor/PIE never register a ticker or any gameplay/capture delegates.
        if (GIsEditor || !FPlatformProperties::RequiresCookedData() ||
            !FParse::Param(FCommandLine::Get(), TEXT("LyraTrainingVerify"))) { return; }
        if (!FParse::Value(FCommandLine::Get(), TEXT("LyraTrainingVerifyOutput="), OutputDirectory) || OutputDirectory.IsEmpty())
        {
            UE_LOG(LogTrainingVerification, Error, TEXT("-LyraTrainingVerifyOutput=<unique directory> is required"));
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("Training verification missing output"));
            return;
        }
        OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
        ReportPath = FPaths::Combine(OutputDirectory, TEXT("runtime.json"));
        ScreenshotPath = FPaths::Combine(OutputDirectory, TEXT("training-package.png"));
        if (IFileManager::Get().FileExists(*ReportPath) || IFileManager::Get().FileExists(*ScreenshotPath) ||
            !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
        {
            UE_LOG(LogTrainingVerification, Error, TEXT("Output must be writable and must not contain old runtime artifacts: %s"), *OutputDirectory);
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("Training verification output rejected"));
            return;
        }
        Started = FPlatformTime::Seconds();
        Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("startedAt"), FDateTime::UtcNow().ToIso8601());
        Report->SetStringField(TEXT("scope"), TEXT("packaged-single-player"));
        Report->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
        Report->SetStringField(TEXT("configuration"), TEXT("Development"));
        Report->SetStringField(TEXT("executable"), FPlatformProcess::ExecutablePath());
        Report->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
        Report->SetNumberField(TEXT("timeoutSeconds"), TrainingVerification::TimeoutSeconds);
        Report->SetNumberField(TEXT("expectedCheckCount"), 11);
        Report->SetStringField(TEXT("notCovered"), TEXT("Packaged networking, pickup, respawn, packaging provenance, four-direction Dash, damage values, bot decisions and UI navigation are not asserted by this run."));
        WorldCleanupDelegate = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FTrainingRangeVerificationModule::WorldCleanup);
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTrainingRangeVerificationModule::Tick));
        UE_LOG(LogTrainingVerification, Display, TEXT("Explicit packaged verification started: %s"), *OutputDirectory);
    }

    virtual void ShutdownModule() override
    {
        if (Ticker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Ticker); }
        ReleaseInputs();
        RemoveGameplayDelegates();
        if (ScreenshotDelegate.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegate); }
    }

private:
    enum class EStage { Waiting, DisplaySettle, Move, Dash, Recover, Fire, FireRecovery, Reload, CaptureSettle, Screenshot, Finished };
    EStage Stage = EStage::Waiting;
    FTSTicker::FDelegateHandle Ticker;
    FDelegateHandle ScreenshotDelegate;
    FDelegateHandle AbilityActivatedDelegate, WorldCleanupDelegate;
    TSharedPtr<FJsonObject> Report;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<APawn> Pawn;
    TWeakObjectPtr<UAbilitySystemComponent> ASC;
    TWeakObjectPtr<UObject> QuickBar;
    TWeakObjectPtr<UObject> Item;
    FGameplayTag MagazineTag, SpareTag;
    FString OutputDirectory, ReportPath, ScreenshotPath, LastWorld, WaitingFor;
    double Started = 0, StageStarted = 0;
    uint64 StageStartedFrame = 0;
    FVector MovementStart = FVector::ZeroVector, MovementForward = FVector::ForwardVector, DashStart = FVector::ZeroVector;
    int32 MagazineBefore = 0, SpareBefore = 0;
    bool bPassed = true, bDashActive = false, bDashActivationEvent = false, bScreenshotProcessed = false;
    TSet<FString> ActiveAbilitiesDuringFire;
    TSet<FString> HeldKeys;

    void Check(const TCHAR* Name, bool bSuccess, const TSharedRef<FJsonObject>& Evidence)
    {
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("id"), Name);
        Entry->SetBoolField(TEXT("passed"), bSuccess);
        Entry->SetObjectField(TEXT("evidence"), Evidence);
        Checks.Add(MakeShared<FJsonValueObject>(Entry));
        bPassed &= bSuccess;
        UE_LOG(LogTrainingVerification, Display, TEXT("%s: %s"), Name, bSuccess ? TEXT("PASS") : TEXT("FAIL"));
    }

    void Advance(EStage Next)
    { Stage = Next; StageStarted = FPlatformTime::Seconds(); StageStartedFrame = GFrameCounter; }

    void Key(const TCHAR* Name, bool bDown)
    {
        if (!bDown && !HeldKeys.Contains(Name)) { return; }
        if (Controller.IsValid())
        {
            Controller->ConsoleCommand(FString::Printf(TEXT("Input.%skey %s%s"), bDown ? TEXT("+") : TEXT("-"), Name, bDown ? TEXT(" 1") : TEXT("")), true);
            if (bDown) { HeldKeys.Add(Name); } else { HeldKeys.Remove(Name); }
        }
    }

    void ReleaseInputs()
    {
        const TArray<FString> Keys = HeldKeys.Array();
        for (const FString& Name : Keys) { Key(*Name, false); }
        HeldKeys.Empty();
    }

    void RemoveGameplayDelegates()
    {
        if (AbilityActivatedDelegate.IsValid() && ASC.IsValid())
        { ASC->AbilityActivatedCallbacks.Remove(AbilityActivatedDelegate); }
        AbilityActivatedDelegate.Reset();
        if (WorldCleanupDelegate.IsValid())
        { FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupDelegate); WorldCleanupDelegate.Reset(); }
    }

    void AbilityActivated(UGameplayAbility* Ability)
    {
        if (Stage == EStage::Dash && HeldKeys.Contains(TEXT("LeftShift")) && Ability &&
            Ability->GetClass()->GetPathName() == TrainingVerification::Dash &&
            Ability->GetAbilitySystemComponentFromActorInfo() == ASC.Get() &&
            Ability->GetAvatarActorFromActorInfo() == Pawn.Get())
        {
            // Native PreActivate notification occurs after CanActivate has succeeded
            // and actor info is installed. It also observes an ability that starts
            // and ends between two ticker samples during a slow rendered frame.
            bDashActivationEvent = true;
        }
    }

    void WorldCleanup(UWorld* CleaningWorld, bool bSessionEnded, bool bCleanupResources)
    {
        if (CleaningWorld == World.Get() && Stage != EStage::Finished)
        {
            Check(TEXT("runtime-error"), false, TrainingVerification::Detail(TEXT("error"), TEXT("The sampled world was cleaned up before verification completed.")));
            Check(TEXT("native-screenshot"), false, TrainingVerification::Detail(TEXT("error"), TEXT("No completed screenshot can establish the destroyed test world.")));
            Finish();
        }
    }

    void Fail(const FString& Error)
    {
        Check(TEXT("runtime-error"), false, TrainingVerification::Detail(TEXT("error"), Error));
        BeginScreenshot();
    }

    int32 CountHUD() const
    {
        int32 Count = 0;
        for (TObjectIterator<UCommonActivatableWidget> It; It; ++It)
        {
            if (It->GetWorld() == World.Get() && It->GetOwningPlayer() == Controller.Get() &&
                It->GetClass()->GetPathName() == TrainingVerification::HUD && It->IsVisible() && It->IsActivated()) { ++Count; }
        }
        return Count;
    }

    int32 CountDash(bool& bActive) const
    {
        int32 Count = 0;
        bActive = false;
        if (ASC.IsValid())
        {
            for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
            {
                if (Spec.Ability && Spec.Ability->GetClass()->GetPathName() == TrainingVerification::Dash)
                { ++Count; bActive |= Spec.IsActive(); }
            }
        }
        return Count;
    }

    bool SampleAmmo(int32& Magazine, int32& Spare) const
    {
        return Item.IsValid() && TrainingVerification::ActiveItem(QuickBar.Get()) == Item.Get() &&
            TrainingVerification::ReadAmmo(Item.Get(), MagazineTag, Magazine) &&
            TrainingVerification::ReadAmmo(Item.Get(), SpareTag, Spare);
    }

    bool TryInitialize()
    {
        WaitingFor = TEXT("A rendered cooked Game/Standalone world on the exact training map");
        if (!GEngine || !FPlatformProperties::RequiresCookedData()) { return false; }
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* Candidate = Context.World();
            if (!Candidate || Candidate->WorldType != EWorldType::Game) { continue; }
            LastWorld = Candidate->GetOutermost()->GetName();
            if (LastWorld == TrainingVerification::Map && Candidate->GetNetMode() == NM_Standalone) { World = Candidate; break; }
        }
        if (!World.IsValid()) { return false; }
        AGameStateBase* GameState = World->GetGameState();
        ULyraExperienceManagerComponent* Manager = GameState ? GameState->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
        WaitingFor = TEXT("ExperienceManager Loaded");
        if (!Manager || !Manager->IsExperienceLoaded()) { return false; }
        Controller = World->GetFirstPlayerController();
        WaitingFor = TEXT("Local PlayerController");
        if (!Controller.IsValid() || !Controller->IsLocalController()) { return false; }
        Pawn = Controller->GetPawn();
        ALyraPlayerState* PS = Controller->GetPlayerState<ALyraPlayerState>();
        ASC = PS ? PS->GetAbilitySystemComponent() : nullptr;
        WaitingFor = TEXT("Possessed Pawn and PlayerState-owned ASC with current Avatar");
        if (!Pawn.IsValid() || !ASC.IsValid() || ASC->GetAvatarActor() != Pawn.Get() || ASC->GetOwnerActor() != PS) { return false; }
        ULyraHeroComponent* Hero = Pawn->FindComponentByClass<ULyraHeroComponent>();
        WaitingFor = TEXT("Hero input bindings ready and Enhanced Input console commands registered");
        if (!Hero || !Hero->IsReadyToBindInputs() || !Controller->PlayerInput ||
            !IConsoleManager::Get().FindConsoleObject(TEXT("Input.+key")) ||
            !IConsoleManager::Get().FindConsoleObject(TEXT("Input.-key"))) { return false; }
        for (UActorComponent* Component : Controller->GetComponents())
        {
            for (UClass* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
            {
                if (Class->GetPathName() == TEXT("/Script/LyraGame.LyraQuickBarComponent")) { QuickBar = Component; break; }
            }
        }
        Item = TrainingVerification::ActiveItem(QuickBar.Get());
        MagazineTag = FGameplayTag::RequestGameplayTag(TEXT("Lyra.ShooterGame.Weapon.MagazineAmmo"), false);
        SpareTag = FGameplayTag::RequestGameplayTag(TEXT("Lyra.ShooterGame.Weapon.SpareAmmo"), false);
        bool bActive = false;
        WaitingFor = TEXT("Active weapon with magazine/spare ammo, exactly one active HUD and one inactive TrainingDash grant");
        if (!SampleAmmo(MagazineBefore, SpareBefore) || MagazineBefore <= 0 || SpareBefore <= 0 || CountHUD() != 1 || CountDash(bActive) != 1 || bActive) { return false; }
        WaitingFor = TEXT("Game viewport and GameUserSettings");
        if (!GEngine->GameViewport || !GEngine->GameViewport->Viewport || !GEngine->GetGameUserSettings()) { return false; }
        ULoadingScreenManager* LoadingScreen = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<ULoadingScreenManager>() : nullptr;
        if (LoadingScreen && LoadingScreen->GetLoadingScreenDisplayStatus())
        {
            WaitingFor = FString::Printf(TEXT("Loading screen dismissal: %s"), *LoadingScreen->GetDebugReasonForShowingOrHidingLoadingScreen());
            return false;
        }

        Check(TEXT("packaged-world"), true, TrainingVerification::Detail(TEXT("map"), LastWorld));
        const FString ExperienceClass = Manager->GetCurrentExperienceChecked()->GetClass()->GetPathName();
        Check(TEXT("experience"), ExperienceClass == TrainingVerification::Experience, TrainingVerification::Detail(TEXT("class"), ExperienceClass));
        TSharedRef<FJsonObject> Identity = TrainingVerification::Detail(TEXT("pawn"), Pawn->GetPathName());
        Identity->SetStringField(TEXT("playerState"), PS->GetPathName());
        Identity->SetStringField(TEXT("asc"), ASC->GetPathName());
        Identity->SetStringField(TEXT("owner"), ASC->GetOwnerActor()->GetPathName());
        Identity->SetStringField(TEXT("avatar"), ASC->GetAvatarActor()->GetPathName());
        Identity->SetBoolField(TEXT("heroReadyToBindInputs"), Hero->IsReadyToBindInputs());
        Identity->SetBoolField(TEXT("loadingScreenManagerPresent"), LoadingScreen != nullptr);
        Identity->SetBoolField(TEXT("loadingScreenVisible"), LoadingScreen && LoadingScreen->GetLoadingScreenDisplayStatus());
        Check(TEXT("pawn-asc"), true, Identity);
        TSharedRef<FJsonObject> Grant = TrainingVerification::Detail(TEXT("abilityClass"), TrainingVerification::Dash);
        Grant->SetNumberField(TEXT("count"), CountDash(bActive));
        Check(TEXT("training-dash-granted"), true, Grant);
        TSharedRef<FJsonObject> HUD = TrainingVerification::Detail(TEXT("class"), TrainingVerification::HUD);
        HUD->SetNumberField(TEXT("visibleActiveCount"), CountHUD());
        Check(TEXT("hud"), true, HUD);
        Report->SetStringField(TEXT("item"), Item->GetPathName());
        AbilityActivatedDelegate = ASC->AbilityActivatedCallbacks.AddRaw(this, &FTrainingRangeVerificationModule::AbilityActivated);

        UGameUserSettings* Settings = GEngine->GetGameUserSettings();
        Settings->SetOverallScalabilityLevel(1);
        Settings->SetScreenResolution(FIntPoint(1920, 1080));
        Settings->SetFullscreenMode(EWindowMode::Windowed);
        Settings->ApplyResolutionSettings(true);
        Settings->ApplyNonResolutionSettings();
        Advance(EStage::DisplaySettle);
        return true;
    }

    void CheckDisplay()
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetNumberField(TEXT("width"), Size.X);
        Data->SetNumberField(TEXT("height"), Size.Y);
        Data->SetBoolField(TEXT("viewportHasFocus"), GEngine->GameViewport->Viewport->HasFocus());
        bool bValid = Size == FIntPoint(1920, 1080);
        for (const TCHAR* Group : { TEXT("ViewDistance"), TEXT("AntiAliasing"), TEXT("Shadow"), TEXT("GlobalIllumination"), TEXT("Reflection"), TEXT("PostProcess"), TEXT("Texture"), TEXT("Effects"), TEXT("Foliage"), TEXT("Shading") })
        {
            const FString Name = FString::Printf(TEXT("sg.%sQuality"), Group);
            IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name);
            const int32 Value = Variable ? Variable->GetInt() : -1;
            Data->SetNumberField(Name, Value);
            bValid &= Value == 1;
        }
        Check(TEXT("display-medium-1080"), bValid, Data);
    }

    bool Tick(float DeltaSeconds)
    {
        const double Now = FPlatformTime::Seconds();
        const double Elapsed = Now - StageStarted;
        // EnhancedInput's global FTickableGameObject injects keys after the world
        // tick; the PlayerController consumes them on a later world tick. Wall
        // time alone could release R before it is consumed during a slow frame.
        const uint64 Frames = GFrameCounter - StageStartedFrame;
        const bool bInputHadFramesToRun = Frames >= 3;
        if (Stage == EStage::Finished) { return false; }
        if (Stage == EStage::Screenshot)
        {
            if (bScreenshotProcessed && IFileManager::Get().FileSize(*ScreenshotPath) > 24)
            {
                TArray<uint8> Bytes;
                const bool bRead = FFileHelper::LoadFileToArray(Bytes, *ScreenshotPath);
                const uint8 Signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
                const uint8 EndChunk[] = { 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130 };
                const bool bPNG = bRead && Bytes.Num() >= 45 && FMemory::Memcmp(Bytes.GetData(), Signature, 8) == 0 &&
                    FMemory::Memcmp(Bytes.GetData() + 12, "IHDR", 4) == 0 && FMemory::Memcmp(Bytes.GetData() + Bytes.Num() - 12, EndChunk, 12) == 0;
                const auto ReadBE = [&Bytes](int32 Offset) -> uint32 { return (uint32(Bytes[Offset]) << 24) | (uint32(Bytes[Offset + 1]) << 16) | (uint32(Bytes[Offset + 2]) << 8) | uint32(Bytes[Offset + 3]); };
                TSharedRef<FJsonObject> Image = TrainingVerification::Detail(TEXT("path"), ScreenshotPath);
                const uint32 Width = bPNG ? ReadBE(16) : 0, Height = bPNG ? ReadBE(20) : 0;
                Image->SetNumberField(TEXT("width"), Width); Image->SetNumberField(TEXT("height"), Height);
                Image->SetBoolField(TEXT("processedCallback"), bScreenshotProcessed);
                Image->SetBoolField(TEXT("showUI"), true); Image->SetBoolField(TEXT("restrictToGameViewport"), true);
                Check(TEXT("native-screenshot"), bPNG && Width == 1920 && Height == 1080, Image);
                Finish();
                return false;
            }
            if (Elapsed > 10.0)
            {
                Check(TEXT("native-screenshot"), false, TrainingVerification::Detail(TEXT("error"), TEXT("Screenshot was not processed and saved as PNG within 10 seconds.")));
                Finish(); return false;
            }
            return true;
        }
        if (Now - Started > TrainingVerification::TimeoutSeconds)
        { Fail(FString::Printf(TEXT("Bounded runtime timeout. Last observed world: %s; stage: %d; waiting for: %s"), *LastWorld, int32(Stage), *WaitingFor)); return Stage != EStage::Finished; }
        if (Stage == EStage::Waiting) { TryInitialize(); return true; }
        if (!World.IsValid() || !Controller.IsValid() || !Pawn.IsValid() || !ASC.IsValid() || Controller->GetPawn() != Pawn.Get() || ASC->GetAvatarActor() != Pawn.Get())
        { Fail(TEXT("The sampled pawn, world or ASC changed during this single-life test.")); return Stage != EStage::Finished; }

        switch (Stage)
        {
        case EStage::DisplaySettle:
            if (Elapsed > 2.0 && bInputHadFramesToRun)
            {
                CheckDisplay(); MovementStart = Pawn->GetActorLocation();
                MovementForward = FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f).RotateVector(FVector::ForwardVector);
                Key(TEXT("W"), true); Advance(EStage::Move);
            }
            break;
        case EStage::Move:
            if (Elapsed > 0.6 && bInputHadFramesToRun)
            {
                const FVector Displacement = Pawn->GetActorLocation() - MovementStart;
                const double Distance = Displacement.Size2D();
                const double ForwardDistance = FVector::DotProduct(Displacement, MovementForward);
                TSharedRef<FJsonObject> Data = TrainingVerification::Detail(TEXT("input"), TEXT("Input.+key W 1"));
                Data->SetNumberField(TEXT("distanceCm"), Distance);
                Data->SetNumberField(TEXT("forwardDistanceCm"), ForwardDistance);
                Data->SetNumberField(TEXT("verticalDisplacementCm"), Displacement.Z);
                Data->SetNumberField(TEXT("sampledFrames"), double(Frames)); Data->SetNumberField(TEXT("elapsedSeconds"), Elapsed);
                Check(TEXT("movement"), ForwardDistance > 20.0, Data);
                bool bAlreadyActive = false; CountDash(bAlreadyActive);
                if (bAlreadyActive) { Fail(TEXT("Dash was already active before the test's Shift input; input causality cannot be established.")); break; }
                DashStart = Pawn->GetActorLocation(); Advance(EStage::Dash); Key(TEXT("LeftShift"), true);
            }
            break;
        case EStage::Dash:
        {
            bool bActive = false; CountDash(bActive); bDashActive |= bActive;
            if (Elapsed > 0.8 && bInputHadFramesToRun)
            {
                ReleaseInputs();
                TSharedRef<FJsonObject> Data = TrainingVerification::Detail(TEXT("input"), TEXT("Input.+key LeftShift 1 (while W is held)"));
                Data->SetBoolField(TEXT("abilitySpecActiveObserved"), bDashActive);
                Data->SetBoolField(TEXT("nativeActivationCallbackObserved"), bDashActivationEvent);
                Data->SetNumberField(TEXT("distanceCm"), FVector::Distance(DashStart, Pawn->GetActorLocation()));
                Data->SetNumberField(TEXT("sampledFrames"), double(Frames)); Data->SetNumberField(TEXT("elapsedSeconds"), Elapsed);
                // Hitting a wall can stop movement; the exact Dash spec must actually activate.
                Check(TEXT("dash-input-activation"), bDashActivationEvent || bDashActive, Data); Advance(EStage::Recover);
            }
            break;
        }
        case EStage::Recover:
            if (Elapsed > 1.0 && bInputHadFramesToRun)
            {
                if (!SampleAmmo(MagazineBefore, SpareBefore)) { Fail(TEXT("Weapon changed before firing.")); break; }
                Key(TEXT("LeftMouseButton"), true); Advance(EStage::Fire);
            }
            break;
        case EStage::Fire:
        {
            for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
            { if (Spec.Ability && Spec.IsActive()) { ActiveAbilitiesDuringFire.Add(Spec.Ability->GetClass()->GetPathName()); } }
            int32 Magazine = 0, Spare = 0;
            if (!SampleAmmo(Magazine, Spare)) { Fail(TEXT("Active weapon changed while firing.")); break; }
            if (Magazine < MagazineBefore || (Elapsed > 2.0 && bInputHadFramesToRun))
            {
                ReleaseInputs();
                TSharedRef<FJsonObject> Data = TrainingVerification::Detail(TEXT("input"), TEXT("Input.+key LeftMouseButton 1"));
                Data->SetNumberField(TEXT("magazineBefore"), MagazineBefore); Data->SetNumberField(TEXT("magazineAfter"), Magazine);
                Data->SetBoolField(TEXT("viewportHasFocus"), GEngine->GameViewport && GEngine->GameViewport->Viewport && GEngine->GameViewport->Viewport->HasFocus());
                Data->SetNumberField(TEXT("sampledFrames"), double(Frames)); Data->SetNumberField(TEXT("elapsedSeconds"), Elapsed);
                TArray<TSharedPtr<FJsonValue>> Active;
                for (const FString& Name : ActiveAbilitiesDuringFire) { Active.Add(MakeShared<FJsonValueString>(Name)); }
                Data->SetArrayField(TEXT("observedActiveAbilityClasses"), Active);
                Check(TEXT("fire-ammo"), Magazine < MagazineBefore, Data);
                Advance(EStage::FireRecovery);
            }
            break;
        }
        case EStage::FireRecovery:
            // Let the fire input-release and weapon ability finish before pressing R.
            if (Elapsed > 0.5 && bInputHadFramesToRun)
            {
                if (!SampleAmmo(MagazineBefore, SpareBefore)) { Fail(TEXT("Weapon changed before reloading.")); break; }
                Key(TEXT("R"), true); Advance(EStage::Reload);
            }
            break;
        case EStage::Reload:
        {
            if (Elapsed > 0.25 && bInputHadFramesToRun) { Key(TEXT("R"), false); }
            int32 Magazine = 0, Spare = 0;
            if (!SampleAmmo(Magazine, Spare)) { Fail(TEXT("Active weapon changed while reloading.")); break; }
            if ((Magazine > MagazineBefore && Spare < SpareBefore) || (Elapsed > 5.0 && bInputHadFramesToRun))
            {
                ReleaseInputs();
                TSharedRef<FJsonObject> Data = TrainingVerification::Detail(TEXT("input"), TEXT("Input.+key R 1"));
                Data->SetNumberField(TEXT("magazineBefore"), MagazineBefore); Data->SetNumberField(TEXT("magazineAfter"), Magazine);
                Data->SetNumberField(TEXT("spareBefore"), SpareBefore); Data->SetNumberField(TEXT("spareAfter"), Spare);
                Data->SetNumberField(TEXT("sampledFrames"), double(Frames)); Data->SetNumberField(TEXT("elapsedSeconds"), Elapsed);
                Check(TEXT("reload-ammo"), Magazine > MagazineBefore && Spare < SpareBefore && Magazine - MagazineBefore == SpareBefore - Spare, Data);
                Advance(EStage::CaptureSettle);
            }
            break;
        }
        case EStage::CaptureSettle:
            if (Elapsed > 0.5) { BeginScreenshot(); }
            break;
        default: break;
        }
        return Stage != EStage::Finished;
    }

    void BeginScreenshot()
    {
        ReleaseInputs();
        if (!World.IsValid() || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
        {
            Check(TEXT("native-screenshot"), false, TrainingVerification::Detail(TEXT("error"), TEXT("The game viewport was unavailable before screenshot request.")));
            Finish(); return;
        }
        Advance(EStage::Screenshot);
        ScreenshotDelegate = FScreenshotRequest::OnScreenshotRequestProcessed().AddRaw(this, &FTrainingRangeVerificationModule::ScreenshotProcessed);
        // UE 5.8: true showUI + true restrictToGameViewport preserves the native UI
        // inside the game viewport without OS borders; no image resize or crop follows.
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false, false, FIntRect(), true);
    }

    void ScreenshotProcessed() { bScreenshotProcessed = true; }

    void Finish()
    {
        ReleaseInputs();
        RemoveGameplayDelegates();
        if (ScreenshotDelegate.IsValid())
        { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegate); ScreenshotDelegate.Reset(); }
        Report->SetArrayField(TEXT("checks"), Checks);
        Report->SetBoolField(TEXT("passed"), bPassed);
        Report->SetStringField(TEXT("status"), bPassed ? TEXT("passed") : TEXT("failed"));
        Report->SetStringField(TEXT("finishedAt"), FDateTime::UtcNow().ToIso8601());
        Report->SetNumberField(TEXT("elapsedSeconds"), FPlatformTime::Seconds() - Started);
        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        const bool bSaved = FJsonSerializer::Serialize(Report.ToSharedRef(), Writer) &&
            FFileHelper::SaveStringToFile(Text, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTrainingVerification, Display, TEXT("Finished %s; JSON saved=%d: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), bSaved, *ReportPath);
        Stage = EStage::Finished;
        FPlatformMisc::RequestExitWithStatus(false, bPassed && bSaved ? 0 : 1, TEXT("Training packaged verification finished"));
    }
};
#else
// No implementation or verification dependencies in DebugGame, Test, Shipping or
// dedicated-server runtime. Development Editor compiles the body but cannot run it.
class FTrainingRangeVerificationModule final : public IModuleInterface {};
#endif

IMPLEMENT_MODULE(FTrainingRangeVerificationModule, TrainingRangeVerification)
