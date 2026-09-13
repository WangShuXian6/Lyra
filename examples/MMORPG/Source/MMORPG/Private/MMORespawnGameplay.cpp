#include "MMOGameplay.h"
#include "MMOHealthComponent.h"
#include "MMOPawnExtensionComponent.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"

void AMMOGameMode::HandlePlayerDeath(AMMOCharacter* Character)
{
    auto* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    auto* State = PC ? PC->GetPlayerState<AMMOPlayerState>() : nullptr;
    if (!HasAuthority() || !PC || !State || !State->bSessionReady || !Character->HealthComponent->IsDead() || RespawnTimers.Contains(PC)) return;
    FTimerHandle& Timer = RespawnTimers.Add(PC);
    GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this,
        [this, WeakPC = TWeakObjectPtr<APlayerController>(PC), WeakPawn = TWeakObjectPtr<AMMOCharacter>(Character)]
    {
        RespawnTimers.Remove(WeakPC);
        auto* Controller = WeakPC.Get(); auto* OldPawn = WeakPawn.Get();
        auto* Player = Controller ? Controller->GetPlayerState<AMMOPlayerState>() : nullptr;
        if (!Controller || !OldPawn || !Player || !Player->bSessionReady || Controller->GetPawn() != OldPawn || !OldPawn->HealthComponent->IsDead()) return;
        const int32 Before = Player->ASC->GetActivatableAbilities().Num();
        Controller->UnPossess(); // Old PawnExtension clears only its own avatar and observers.
        OldPawn->Destroy();
        Player->ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Status.MMO.Dead")));
        Player->ASC->SetNumericAttributeBase(UMMOAttributes::GetHealthAttribute(), 100.f);
        Player->SavedPosition = FVector(0, 0, 150); // Safe zone checkpoint; mana, inventory and progress persist.
        TryStartPlayer(Controller); // Same Experience, PawnData and PlayerState-owned grants.
        auto* NewPawn = Cast<AMMOCharacter>(Controller->GetPawn());
        UE_LOG(LogTemp, Display, TEXT("MMO_PLAYER_RESPAWNED old=%s new=%s ready=%d abilitiesBefore=%d abilitiesAfter=%d health=%.1f"),
            *GetNameSafe(OldPawn), *GetNameSafe(NewPawn), NewPawn && NewPawn->PawnExtension->IsPawnReady(), Before,
            Player->ASC->GetActivatableAbilities().Num(), Player->Attributes->GetHealth());
    }), RespawnDelay, false);
}
