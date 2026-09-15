#include "MMODocProjectGuard.h"
#include "MMODocExperienceTools.h"
#include "MMOExperienceDefinition.h"
#include "MMOPawnData.h"
#include "MMOAbilitySet.h"
#include "MMOInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "UObject/UnrealType.h"
#include "GameplayTagContainer.h"

namespace
{
template<typename T> T* InputAsset(const TCHAR* Name)
{
    const FString Package = FString(TEXT("/Game/MMO/Input/")) + Name;
    const FString Object = Package + TEXT(".") + Name;
    if (FPackageName::DoesPackageExist(Package)) return LoadObject<T>(nullptr, *Object);
    auto* Factory = NewObject<UDataAssetFactory>(); Factory->DataAssetClass = T::StaticClass();
    return Cast<T>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(Name, TEXT("/Game/MMO/Input"), T::StaticClass(), Factory));
}

bool SaveInput(UObject* Asset)
{
    Asset->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
}
bool ConfigureInputs(UMMOPawnData* Pawn, const TSharedRef<FJsonObject>& Report)
{
    UInputAction* Move = InputAsset<UInputAction>(TEXT("IA_Move")); UInputAction* Look = InputAsset<UInputAction>(TEXT("IA_Look"));
    UInputAction* Target = InputAsset<UInputAction>(TEXT("IA_Target")); UInputAction* Attack = InputAsset<UInputAction>(TEXT("IA_Attack"));
    UInputAction* Spell = InputAsset<UInputAction>(TEXT("IA_Spell"));
    UInputAction* Inventory = InputAsset<UInputAction>(TEXT("IA_Inventory")); UInputAction* Settings = InputAsset<UInputAction>(TEXT("IA_Settings"));
    auto* Mapping = InputAsset<UInputMappingContext>(TEXT("IMC_MMOPlayer")); auto* Input = InputAsset<UMMOInputConfig>(TEXT("DA_MMOInputConfig"));
    if (!Move || !Look || !Target || !Attack || !Spell || !Inventory || !Settings || !Mapping || !Input) return false;
    Move->Modify(); Look->Modify(); Target->Modify(); Attack->Modify(); Spell->Modify(); Mapping->Modify(); Input->Modify();
    Move->ValueType = EInputActionValueType::Axis2D; Look->ValueType = EInputActionValueType::Axis2D;
    Target->ValueType = EInputActionValueType::Boolean; Attack->ValueType = EInputActionValueType::Boolean; Spell->ValueType = EInputActionValueType::Boolean;
    Inventory->Modify(); Settings->Modify(); Inventory->ValueType = EInputActionValueType::Boolean; Settings->ValueType = EInputActionValueType::Boolean;
    Mapping->UnmapAll();
    Mapping->MapKey(Move, EKeys::D);
    Mapping->MapKey(Move, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
    Mapping->MapKey(Move, EKeys::W).Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Mapping));
    auto& Back = Mapping->MapKey(Move, EKeys::S); Back.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping)); Back.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Mapping));
    Mapping->MapKey(Move, EKeys::Gamepad_Left2D); Mapping->MapKey(Look, EKeys::Mouse2D); Mapping->MapKey(Look, EKeys::Gamepad_Right2D);
    Mapping->MapKey(Target, EKeys::Tab); Mapping->MapKey(Target, EKeys::Gamepad_FaceButton_Top);
    Mapping->MapKey(Attack, EKeys::One); Mapping->MapKey(Attack, EKeys::Gamepad_FaceButton_Bottom);
    Mapping->MapKey(Spell, EKeys::Two); Mapping->MapKey(Spell, EKeys::Gamepad_FaceButton_Left);
    Mapping->MapKey(Inventory, EKeys::I); Mapping->MapKey(Inventory, EKeys::Gamepad_Special_Left);
    Mapping->MapKey(Settings, EKeys::P); Mapping->MapKey(Settings, EKeys::Gamepad_Special_Right);
    Input->NativeInputActions.Reset();
    TArray<UInputAction*> Actions {Move, Look, Target, Attack, Spell, Inventory, Settings};
    const TCHAR* Tags[] {TEXT("InputTag.MMO.Move"), TEXT("InputTag.MMO.Look"), TEXT("InputTag.MMO.Target"), TEXT("InputTag.MMO.Attack"), TEXT("InputTag.MMO.Spell"), TEXT("InputTag.MMO.Inventory"), TEXT("InputTag.MMO.Settings")};
    for (int32 Index = 0; Index < Actions.Num(); ++Index)
    {
        auto& Entry = Input->NativeInputActions.AddDefaulted_GetRef(); Entry.InputAction = Actions[Index]; Entry.InputTag = FGameplayTag::RequestGameplayTag(FName(Tags[Index]));
        if (!SaveInput(Actions[Index])) return false;
    }
    Pawn->InputConfig = Input; Pawn->InputMappingContext = Mapping;
    Report->SetStringField(TEXT("inputConfig"), Input->GetPathName()); Report->SetStringField(TEXT("mappingContext"), Mapping->GetPathName());
    Report->SetNumberField(TEXT("inputActionCount"), Actions.Num()); Report->SetNumberField(TEXT("physicalMappingCount"), Mapping->GetMappings().Num());
    return SaveInput(Input) && SaveInput(Mapping);
}
}

FString UMMODocExperienceTools::ConfigureExperienceAssets(UDataAsset* ExperienceAsset, UDataAsset* PawnAsset, UDataAsset* AbilityAsset)
{
    auto Report = MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"), false);
    auto Encode = [&]() { FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text)); return Text; };
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()); FPaths::NormalizeFilename(Project);
    auto* Experience = Cast<UMMOExperienceDefinition>(ExperienceAsset); auto* Pawn = Cast<UMMOPawnData>(PawnAsset); auto* Abilities = Cast<UMMOAbilitySet>(AbilityAsset);
    if (!IsMMODocProject() || !Experience || !Pawn || !Abilities
        || Experience->GetPathName() != TEXT("/Game/MMO/Experiences/DA_MMOExperience.DA_MMOExperience")
        || Pawn->GetPathName() != TEXT("/Game/MMO/Pawns/DA_MMOPlayer.DA_MMOPlayer")
        || Abilities->GetPathName() != TEXT("/Game/MMO/Abilities/DA_MMOAbilities.DA_MMOAbilities"))
    { Report->SetStringField(TEXT("error"), TEXT("Unexpected project or tutorial asset paths")); return Encode(); }
    Experience->Modify(); Pawn->Modify(); Abilities->Modify();
    Abilities->GrantedAbilities.Reset(); Abilities->GrantedEffects.Reset(); Abilities->GrantedAttributes.Reset();
    auto& Attack = Abilities->GrantedAbilities.AddDefaulted_GetRef();
    Attack.Ability = FSoftObjectPath(TEXT("/Script/MMORPG.MMOAttackAbility")); Attack.Level = 1; Attack.InputID = INDEX_NONE; Attack.InputTag = FGameplayTag::RequestGameplayTag(FName(TEXT("InputTag.MMO.Attack")));
    auto& Spell = Abilities->GrantedAbilities.AddDefaulted_GetRef();
    Spell.Ability = FSoftObjectPath(TEXT("/Game/Tutorial/GA_ArcaneBolt.GA_ArcaneBolt_C")); Spell.Level = 1; Spell.InputID = INDEX_NONE; Spell.InputTag = FGameplayTag::RequestGameplayTag(FName(TEXT("InputTag.MMO.Spell")));
    Pawn->PawnClass = FSoftObjectPath(TEXT("/Game/Tutorial/BP_MMOCharacter.BP_MMOCharacter_C"));
    Pawn->AnimInstanceClass = FSoftObjectPath(TEXT("/Game/Characters/MMO/ABP_MMOCharacter.ABP_MMOCharacter_C"));
    Pawn->AbilitySets = {TSoftObjectPtr<UMMOAbilitySet>(Abilities)};
    Experience->DefaultPawnData = Pawn; Experience->GameFeaturesToEnable = {TEXT("MMOCore")}; Experience->ActionSets.Reset();
    if (!ConfigureInputs(Pawn, Report)) { Report->SetStringField(TEXT("error"), TEXT("Input assets failed creation or save")); return Encode(); }
    Experience->MarkPackageDirty(); Pawn->MarkPackageDirty(); Abilities->MarkPackageDirty();
    Report->SetStringField(TEXT("date"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("experience"), Experience->GetPathName());
    Report->SetStringField(TEXT("primaryAssetId"), Experience->GetPrimaryAssetId().ToString());
    Report->SetStringField(TEXT("pawnData"), Pawn->GetPathName()); Report->SetStringField(TEXT("pawnClass"), Pawn->PawnClass.ToString());
    Report->SetStringField(TEXT("abilitySet"), Abilities->GetPathName()); Report->SetStringField(TEXT("animationClass"), Pawn->AnimInstanceClass.ToString());
    Report->SetNumberField(TEXT("grantedAbilityCount"), Abilities->GrantedAbilities.Num());
    Report->SetBoolField(TEXT("passed"), Experience->GetPrimaryAssetId() == FPrimaryAssetId(TEXT("MMOExperience"), TEXT("DA_MMOExperience")));
    Report->SetBoolField(TEXT("saved"), false); Report->SetBoolField(TEXT("gameplayExecuted"), false);
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Evidence/mmorpg-experience-assets.json"));
    Report->SetStringField(TEXT("reportPath"), Path); IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(Encode(), *Path); return Encode();
}

FString UMMODocExperienceTools::RecompileGameplayBlueprints()
{
    auto Report = MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"), false);
    auto Encode = [&]() { FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text)); return Text; };
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()); FPaths::NormalizeFilename(Project);
    if (!IsMMODocProject()) return Encode();
    TArray<TSharedPtr<FJsonValue>> Assets;
    for (const TCHAR* Path : {TEXT("/Game/Tutorial/BP_MMOCharacter.BP_MMOCharacter"), TEXT("/Game/Tutorial/GA_ArcaneBolt.GA_ArcaneBolt"), TEXT("/Game/MMO/BP_MMOGameMode.BP_MMOGameMode")})
    {
        auto* Blueprint = LoadObject<UBlueprint>(nullptr, Path);
        if (!Blueprint) return Encode();
        FCompilerResultsLog Log; FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("asset"), Path);
        Row->SetNumberField(TEXT("errors"), Log.NumErrors); Row->SetNumberField(TEXT("warnings"), Log.NumWarnings);
        Assets.Add(MakeShared<FJsonValueObject>(Row)); Report->SetArrayField(TEXT("assets"), Assets);
        if (Log.NumErrors || Log.NumWarnings || Blueprint->Status != BS_UpToDate || !Blueprint->GeneratedClass) return Encode();
        UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
        if (Blueprint->GetName() == TEXT("BP_MMOCharacter"))
        {
            const auto* Property = FindFProperty<FObjectProperty>(Defaults->GetClass(), TEXT("HealthComponent"));
            UObject* Component = Property ? Property->GetObjectPropertyValue_InContainer(Defaults) : nullptr;
            const bool bHealth = Component && Component->GetClass()->GetPathName() == TEXT("/Script/MMORPG.MMOHealthComponent");
            Row->SetBoolField(TEXT("inheritedHealthComponent"), bHealth); if (!bHealth) return Encode();
        }
        if (Blueprint->GetName() == TEXT("GA_ArcaneBolt"))
        {
            const auto* Property = FindFProperty<FStructProperty>(Defaults->GetClass(), TEXT("ActivationBlockedTags"));
            const auto* Blocked = Property ? Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Defaults) : nullptr;
            const bool bBlocked = Blocked && Blocked->HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("Status.MMO.Dead")));
            Row->SetBoolField(TEXT("blockedWhileDead"), bBlocked); if (!bBlocked) return Encode();
        }
        if (Blueprint->GetName() == TEXT("BP_MMOGameMode"))
        {
            const auto* Property = FindFProperty<FFloatProperty>(Defaults->GetClass(), TEXT("RespawnDelay"));
            const float Delay = Property ? Property->GetPropertyValue_InContainer(Defaults) : 0.f;
            Row->SetNumberField(TEXT("respawnDelay"), Delay); if (!FMath::IsNearlyEqual(Delay, 3.f)) return Encode();
        }
        const bool bSaved = SaveInput(Blueprint); Row->SetBoolField(TEXT("saved"), bSaved); if (!bSaved) return Encode();
    }
    Report->SetArrayField(TEXT("assets"), Assets); Report->SetBoolField(TEXT("graphsReconstructed"), false);
    Report->SetBoolField(TEXT("passed"), true); Report->SetStringField(TEXT("date"), FDateTime::UtcNow().ToIso8601());
    return Encode();
}
