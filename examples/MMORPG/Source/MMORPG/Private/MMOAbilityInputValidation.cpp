#include "MMOGameplay.h"

#if !UE_BUILD_SHIPPING
#include "MMOAbilitySystemComponent.h"
#include "Serialization/JsonSerializer.h"
#endif

bool AMMOPlayerController::RecordAbilityInputState(const FString& Stage)
{
#if !UE_BUILD_SHIPPING
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("stage"), Stage);
    auto* State = GetPlayerState<AMMOPlayerState>();
    auto* TaggedASC = State ? Cast<UMMOAbilitySystemComponent>(State->ASC.Get()) : nullptr;
    Report->SetStringField(TEXT("ascClass"), TaggedASC ? TaggedASC->GetClass()->GetPathName() : TEXT(""));
    Report->SetBoolField(TEXT("authoritative"), TaggedASC && TaggedASC->IsOwnerActorAuthoritative());
    Report->SetNumberField(TEXT("abilityCount"), TaggedASC ? TaggedASC->GetActivatableAbilities().Num() : 0);
    const bool bOwnerAndAvatar = TaggedASC && TaggedASC->GetOwnerActor() == State && GetPawn()
        && TaggedASC->GetAvatarActor() == GetPawn();
    Report->SetBoolField(TEXT("ownerAndAvatarMatch"), bOwnerAndAvatar);
    bool bPassed = bOwnerAndAvatar && TaggedASC->GetClass() == UMMOAbilitySystemComponent::StaticClass()
        && TaggedASC->GetActivatableAbilities().Num() == 2;
    TArray<TSharedPtr<FJsonValue>> Bindings;
    for (const TCHAR* TagName : {TEXT("InputTag.MMO.Attack"), TEXT("InputTag.MMO.Spell")})
    {
        auto Binding = MakeShared<FJsonObject>();
        Binding->SetStringField(TEXT("tag"), TagName);
        const auto Matches = TaggedASC ? TaggedASC->GetGrantedAbilitiesForInputTag(FGameplayTag::RequestGameplayTag(FName(TagName)))
            : TArray<FMMOGrantedAbilityInput>();
        Binding->SetNumberField(TEXT("matchingSpecCount"), Matches.Num());
        TArray<TSharedPtr<FJsonValue>> Specs;
        for (const auto& Match : Matches)
        {
            auto Spec = MakeShared<FJsonObject>();
            Spec->SetStringField(TEXT("abilityClass"), Match.AbilityClassPath);
            Spec->SetNumberField(TEXT("inputID"), Match.InputID);
            Spec->SetBoolField(TEXT("handleValid"), Match.Handle.IsValid());
            Specs.Add(MakeShared<FJsonValueObject>(Spec));
        }
        Binding->SetArrayField(TEXT("specs"), Specs);
        const bool bExpectedGrant = Matches.Num() == 1 && Matches[0].Handle.IsValid() && Matches[0].InputID == INDEX_NONE;
        Binding->SetBoolField(TEXT("passed"), bExpectedGrant);
        bPassed = bPassed && bExpectedGrant;
        Bindings.Add(MakeShared<FJsonValueObject>(Binding));
    }
    Report->SetArrayField(TEXT("bindings"), Bindings);
    Report->SetBoolField(TEXT("passed"), bPassed);
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    UE_LOG(LogTemp, Display, TEXT("MMO_INPUT_GRANTS_RESULT %s"), *Text);
    return bPassed;
#else
    return true;
#endif
}
