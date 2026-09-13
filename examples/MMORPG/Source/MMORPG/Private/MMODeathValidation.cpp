#include "MMOGameplay.h"
#include "MMOHealthComponent.h"
#include "MMOPawnExtensionComponent.h"
#include "MMOUI.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Serialization/JsonSerializer.h"

void AMMOPlayerController::DeathSmokeTick(AMMOPlayerState* State, AMMOCharacter* TestCharacter, double Elapsed)
{
#if !UE_BUILD_SHIPPING
    if (SmokeOriginalPawn.IsEmpty()) { SmokeOriginalPawn = TestCharacter->GetName(); SmokeOriginalASC = State->ASC; }
    if (State->Attributes->GetHealth() <= 0 && TestCharacter->HealthComponent->IsDead()) bSmokeSawPlayerDeath = true;
    if (bSmokeSawPlayerDeath && FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeDeadLogout")))
    {
        auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
        auto* Settings = Root ? Root->ShowSettings() : nullptr;
        if (!Settings) return;
        FString Snapshot; FJsonSerializer::Serialize(State->MakeSnapshot().ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Snapshot));
        UE_LOG(LogTemp, Display, TEXT("MMO_DEAD_LOGOUT_SNAPSHOT %s"), *Snapshot);
        Settings->ProcessEvent(Settings->FindFunctionChecked(TEXT("LogoutClicked")), nullptr);
        auto* Modal = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal")));
        if (auto* Confirm = Modal ? Cast<UMMOConfirmPanel>(Modal->GetActiveWidget()) : nullptr)
        {
            GetGameInstance<UMMOGameInstance>()->bSmokeInputTagsVerifiedBeforeReturn = RecordAbilityInputState(TEXT("before-death-logout"));
            GetGameInstance<UMMOGameInstance>()->bSmokeReturnExpected = true;
            GetWorldTimerManager().ClearTimer(SmokeTimer);
            Confirm->ProcessEvent(Confirm->FindFunctionChecked(TEXT("ConfirmClicked")), nullptr);
        }
        return;
    }
    if (!bSmokeSawPlayerDeath)
    {
        TestCharacter->SelectTarget();
        if (auto* Target = TestCharacter->SelectedTarget.Get())
        {
            const FVector Direction = Target->GetActorLocation() - TestCharacter->GetActorLocation();
            if (Direction.Size2D() > 380) TestCharacter->AddMovementInput(Direction.GetSafeNormal2D(), 1.f);
            if (Direction.Size2D() <= 470 && Elapsed - SmokeLastAttack >= 1)
            { SmokeLastAttack = Elapsed; TestCharacter->Attack(); }
        }
    }
    const bool bNewPawn = TestCharacter->GetName() != SmokeOriginalPawn;
    if ((bSmokeSawPlayerDeath && bNewPawn && Elapsed >= 13) || Elapsed > 30)
    {
        auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
        auto* GameLayer = Root ? Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Game"))) : nullptr;
        auto* HUD = GameLayer ? Cast<UMMOHUDPanel>(GameLayer->GetActiveWidget()) : nullptr;
        const bool bBinding = HUD && HUD->ValidateManaBinding();
        const bool bSameASC = SmokeOriginalASC.Get() == State->ASC && State->ASC->GetAvatarActor() == TestCharacter;
        const int32 AbilityCount = State->ASC->GetActivatableAbilities().Num();
        const bool bPassed = bSmokeSawPlayerDeath && bNewPawn && bSameASC && AbilityCount == 2 && bBinding
            && TestCharacter->PawnExtension->IsPawnReady() && !TestCharacter->HealthComponent->IsDead() && FMath::IsNearlyEqual(State->Attributes->GetHealth(), 100.f);
        FString TestRole; FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeRole="), TestRole);
        auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("role"), TestRole); Result->SetBoolField(TEXT("deathRespawn"), true);
        Result->SetBoolField(TEXT("passed"), bPassed); Result->SetBoolField(TEXT("deathObserved"), bSmokeSawPlayerDeath);
        Result->SetBoolField(TEXT("pawnReplaced"), bNewPawn); Result->SetBoolField(TEXT("sameASC"), bSameASC);
        Result->SetNumberField(TEXT("abilityCount"), AbilityCount); Result->SetNumberField(TEXT("health"), State->Attributes->GetHealth());
        Result->SetNumberField(TEXT("mana"), State->Attributes->GetMana()); Result->SetBoolField(TEXT("umgMVVMBinding"), bBinding);
        const bool bTaggedInput = RecordAbilityInputState(TEXT("death-respawn"));
        Result->SetBoolField(TEXT("tagDrivenInput"), bTaggedInput);
        Result->SetBoolField(TEXT("passed"), bPassed && bTaggedInput);
        FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        UE_LOG(LogTemp, Display, TEXT("MMO_SMOKE_RESULT %s"), *Text);
        GetWorldTimerManager().ClearTimer(SmokeTimer); ConsoleCommand(TEXT("quit"));
    }
#endif
}
