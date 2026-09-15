#include "MMODocProjectGuard.h"
#include "MMODocExperienceTools.h"
#include "MMOAbilitySet.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace
{
TSharedRef<FJsonObject> TaggedInputSnapshot(const UMMOAbilitySet* Abilities)
{
    auto Snapshot = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Grants, Effects, Attributes;
    for (const FMMOAbilityGrant& Grant : Abilities->GrantedAbilities)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("ability"), Grant.Ability.ToSoftObjectPath().ToString());
        Row->SetNumberField(TEXT("level"), Grant.Level);
        Row->SetNumberField(TEXT("inputID"), Grant.InputID);
        Row->SetStringField(TEXT("inputTag"), Grant.InputTag.ToString());
        Grants.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const FMMOEffectGrant& Grant : Abilities->GrantedEffects)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("effect"), Grant.Effect.ToSoftObjectPath().ToString());
        Row->SetNumberField(TEXT("level"), Grant.Level);
        Effects.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const auto& Attribute : Abilities->GrantedAttributes)
        Attributes.Add(MakeShared<FJsonValueString>(Attribute.ToSoftObjectPath().ToString()));
    Snapshot->SetArrayField(TEXT("grantedAbilities"), Grants);
    Snapshot->SetArrayField(TEXT("grantedEffects"), Effects);
    Snapshot->SetArrayField(TEXT("grantedAttributes"), Attributes);
    return Snapshot;
}
}

FString UMMODocExperienceTools::ConfigureTaggedAbilityInputs(UObject* AbilityAsset)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetBoolField(TEXT("saved"), false);
    Report->SetBoolField(TEXT("assetsModified"), false);
    Report->SetBoolField(TEXT("graphsReconstructed"), false);
    Report->SetBoolField(TEXT("gameplayExecuted"), false);
    Report->SetStringField(TEXT("date"), FDateTime::UtcNow().ToIso8601());
    auto Encode = [&]() { FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text)); return Text; };
    auto Fail = [&](const TCHAR* Message) { Report->SetStringField(TEXT("error"), Message); return Encode(); };

    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FPaths::NormalizeFilename(Project);
    Report->SetStringField(TEXT("project"), Project);
    if (!IsInGameThread() || !IsMMODocProject())
        return Fail(TEXT("Requires the game thread of the exact isolated MMORPG lab project."));
    auto* Abilities = Cast<UMMOAbilitySet>(AbilityAsset);
    if (!IsValid(Abilities) || Abilities->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
        || Abilities->GetPathName() != TEXT("/Game/MMO/Abilities/DA_MMOAbilities.DA_MMOAbilities"))
        return Fail(TEXT("Requires the existing DA_MMOAbilities asset at its exact tutorial path."));
    Report->SetStringField(TEXT("asset"), Abilities->GetPathName());
    Report->SetObjectField(TEXT("before"), TaggedInputSnapshot(Abilities));
    if (Abilities->GrantedAbilities.Num() != 2)
        return Fail(TEXT("Expected exactly two existing grants; no array will be recreated."));

    const TCHAR* ExpectedAbilities[] = {
        TEXT("/Script/MMORPG.MMOAttackAbility"),
        TEXT("/Game/Tutorial/GA_ArcaneBolt.GA_ArcaneBolt_C")
    };
    const FGameplayTag ExpectedTags[] = {
        FGameplayTag::RequestGameplayTag(FName(TEXT("InputTag.MMO.Attack")), false),
        FGameplayTag::RequestGameplayTag(FName(TEXT("InputTag.MMO.Spell")), false)
    };
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (Abilities->GrantedAbilities[Index].Ability.ToSoftObjectPath().ToString() != ExpectedAbilities[Index])
            return Fail(TEXT("Unexpected existing grant class/order; inspect the asset instead of replacing it."));
        if (!ExpectedTags[Index].IsValid())
            return Fail(TEXT("Register InputTag.MMO.Attack and InputTag.MMO.Spell before changing the asset."));
    }
    if (ExpectedTags[0] == ExpectedTags[1]) return Fail(TEXT("Attack and spell require distinct input tags."));

    // Keep the original arrays and all class/level/effect/attribute fields intact.
    const TArray<FMMOAbilityGrant> BeforeGrants = Abilities->GrantedAbilities;
    const TArray<FMMOEffectGrant> BeforeEffects = Abilities->GrantedEffects;
    const auto BeforeAttributes = Abilities->GrantedAttributes;
    bool bChanged = false;
    for (int32 Index = 0; Index < 2; ++Index)
        bChanged |= BeforeGrants[Index].InputTag != ExpectedTags[Index] || BeforeGrants[Index].InputID != INDEX_NONE;
    if (bChanged)
    {
        Abilities->Modify();
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Abilities->GrantedAbilities[Index].InputTag = ExpectedTags[Index];
            Abilities->GrantedAbilities[Index].InputID = INDEX_NONE;
        }
        Abilities->MarkPackageDirty();
    }

    bool bPreserved = Abilities->GrantedAbilities.Num() == BeforeGrants.Num()
        && Abilities->GrantedEffects.Num() == BeforeEffects.Num()
        && Abilities->GrantedAttributes == BeforeAttributes;
    bool bExpectedInputs = Abilities->GrantedAbilities.Num() == 2;
    TSet<FGameplayTag> UniqueTags;
    for (int32 Index = 0; Index < Abilities->GrantedAbilities.Num(); ++Index)
    {
        const FMMOAbilityGrant& Grant = Abilities->GrantedAbilities[Index];
        bPreserved &= BeforeGrants.IsValidIndex(Index)
            && Grant.Ability == BeforeGrants[Index].Ability && Grant.Level == BeforeGrants[Index].Level;
        bExpectedInputs &= Index < 2 && Grant.InputTag == ExpectedTags[Index] && Grant.InputID == INDEX_NONE;
        UniqueTags.Add(Grant.InputTag);
    }
    for (int32 Index = 0; Index < Abilities->GrantedEffects.Num(); ++Index)
        bPreserved &= BeforeEffects.IsValidIndex(Index)
            && Abilities->GrantedEffects[Index].Effect == BeforeEffects[Index].Effect
            && Abilities->GrantedEffects[Index].Level == BeforeEffects[Index].Level;
    Report->SetObjectField(TEXT("after"), TaggedInputSnapshot(Abilities));
    Report->SetBoolField(TEXT("assetsModified"), bChanged);
    Report->SetBoolField(TEXT("onlyInputFieldsChanged"), bPreserved);
    Report->SetBoolField(TEXT("uniqueInputTags"), UniqueTags.Num() == 2);
    Report->SetBoolField(TEXT("inputIDsAreNone"), bExpectedInputs);
    Report->SetNumberField(TEXT("modifiedAssetCount"), bChanged ? 1 : 0);
    Report->SetBoolField(TEXT("passed"), bPreserved && bExpectedInputs && UniqueTags.Num() == 2);
    if (!Report->GetBoolField(TEXT("passed"))) Report->SetStringField(TEXT("error"), TEXT("Post-change preservation or input-tag validation failed; do not save."));
    return Encode();
}
