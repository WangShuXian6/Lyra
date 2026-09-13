#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "ModularPlayerState.h"
#include "ModularCharacter.h"
#include "CommonPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "CommonGameInstance.h"
#include "Dom/JsonObject.h"
#include "InputActionValue.h"
#include "ModularGameState.h"
#include "MMOAbilitySet.h"
#include "MMOGameplay.generated.h"

class UInputMappingContext;
class UInputAction;
class UMMORootLayout;
class UMMOBackendServer;
class UStaticMeshComponent;
class UMMOPawnData;
class UMMOPawnExtensionComponent;
class UMMOExperienceManagerComponent;
class UMMOExperienceDefinition;
class UMMOHealthComponent;

#define MMO_ATTRIBUTE_ACCESSORS(Name) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UMMOAttributes, Name) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Name) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Name) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Name)

UCLASS()
class MMORPG_API UMMOAttributes : public UAttributeSet
{
    GENERATED_BODY()
public:
    UMMOAttributes();
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Attributes") FGameplayAttributeData Health;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Mana, Category="Attributes") FGameplayAttributeData Mana;
    MMO_ATTRIBUTE_ACCESSORS(Health)
    MMO_ATTRIBUTE_ACCESSORS(Mana)
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& Old);
};

USTRUCT(BlueprintType)
struct FMMOItem
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString ItemId;
    UPROPERTY(BlueprintReadOnly) int32 Quantity = 0;
};

/** Owner survives Pawn replacement; avatar is initialized by PossessedBy/OnRep_PlayerState. */
UCLASS()
class MMORPG_API AMMOPlayerState : public AModularPlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    AMMOPlayerState(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void LoadSnapshot(const TSharedPtr<FJsonObject>& Character);
    TSharedPtr<FJsonObject> MakeSnapshot() const;
    void Reward();
    bool SetPawnData(UMMOPawnData* Data);
    UMMOPawnData* GetPawnData() const { return PawnDefinition; }
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    bool bCharacterLoaded = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAbilitySystemComponent> ASC;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UMMOAttributes> Attributes;
    UPROPERTY(ReplicatedUsing=OnRep_Progress, BlueprintReadOnly) TArray<FMMOItem> Inventory;
    UPROPERTY(ReplicatedUsing=OnRep_Progress, BlueprintReadOnly) int32 XP = 0;
    UPROPERTY(ReplicatedUsing=OnRep_Progress, BlueprintReadOnly) int32 Level = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Progress, BlueprintReadOnly) bool bSessionReady = false;
    FVector SavedPosition = FVector(0, 0, 120);
    FString CharacterID;
    FSimpleMulticastDelegate OnProgressChanged;
    UFUNCTION() void OnRep_Progress();
private:
    UPROPERTY() TObjectPtr<UMMOPawnData> PawnDefinition;
    UPROPERTY() TArray<FMMOGrantedAbilityHandles> GrantedAbilitySets;
};

UCLASS()
class MMORPG_API AMMOGameState : public AModularGameStateBase
{
    GENERATED_BODY()
public:
    AMMOGameState(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UMMOExperienceManagerComponent> ExperienceManager;
};

UCLASS()
class MMORPG_API AMMOTarget : public AActor
{
    GENERATED_BODY()
public:
    AMMOTarget();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void Hit(float Damage, AMMOPlayerState* Attacker);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly) float Health = 100;
    FSimpleMulticastDelegate OnHealthChanged;
    UFUNCTION() void OnRep_Health();
private:
    FTimerHandle RespawnTimer;
};

UCLASS()
class MMORPG_API UMMOSpellCost : public UGameplayEffect { GENERATED_BODY() public: UMMOSpellCost(); };
UCLASS()
class MMORPG_API UMMOSpellCooldown : public UGameplayEffect { GENERATED_BODY() public: UMMOSpellCooldown(); };
UCLASS()
class MMORPG_API UMMOAttackCooldown : public UGameplayEffect { GENERATED_BODY() public: UMMOAttackCooldown(); };

UCLASS(Blueprintable)
class MMORPG_API UMMOAttackAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UMMOAttackAbility();
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
protected:
    float Damage = 15;
    float Range = 500;
};
UCLASS(Blueprintable)
class MMORPG_API UMMOSpellAbility : public UMMOAttackAbility { GENERATED_BODY() public: UMMOSpellAbility(); };

UCLASS()
class MMORPG_API AMMOCharacter : public AModularCharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    AMMOCharacter(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void UnPossessed() override;
    virtual void OnRep_PlayerState() override;
    virtual void OnRep_Controller() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void InitializeAbilitySystem();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UMMOPawnExtensionComponent> PawnExtension;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UMMOHealthComponent> HealthComponent;
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void SelectTarget();
    void Attack();
    void Spell();
    void OpenInventory();
    void OpenSettings();
    UFUNCTION(Server, Reliable) void ServerSelectTarget(AMMOTarget* Target);
    UFUNCTION(Server, Reliable) void ServerActivate(FGameplayTag InputTag);
    UFUNCTION(Server, Reliable) void ServerUsePotion();
    UFUNCTION(NetMulticast, Unreliable) void MulticastAttackCue(FVector End, bool bSpell);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(ReplicatedUsing=OnRep_Target, BlueprintReadOnly) TObjectPtr<AMMOTarget> SelectedTarget;
    FSimpleMulticastDelegate OnTargetChanged;
    UFUNCTION() void OnRep_Target();
private:
    UPROPERTY() TObjectPtr<UInputMappingContext> InputContext;
    UPROPERTY() TArray<TObjectPtr<UInputAction>> Actions;
    TWeakObjectPtr<ULocalPlayer> InputOwner;
    void UninitializeAbilitySystem();
    UFUNCTION() void TryInitializeInput();
    bool CanProcessGameplayInput() const;
};

UCLASS()
class MMORPG_API AMMOPlayerController : public ACommonPlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void OnPossess(APawn* Pawn) override;
    virtual void OnUnPossess() override;
    virtual void OnRep_PlayerState() override;
    UFUNCTION(Client, Reliable) void ClientBackendError(const FString& Message);
    FString PendingTicket;
private:
    void SmokeTick();
    void SmokeLogin(const FString& Username);
    void ShowcaseTick();
    void CaptureShowcase(const FString& Stage);
    void DeathSmokeTick(AMMOPlayerState* State, AMMOCharacter* Character, double Elapsed);
    bool RecordAbilityInputState(const FString& Stage);
    void RecordCultureState(const FString& Stage);
    void RecordLocomotionState(const FString& Stage);
    void ShowcaseMoveTick();
    int32 LocomotionSamples = 0;
    bool bShowcaseMovementStarted = false;
    FTimerHandle SmokeTimer;
    double SmokeStart = -1;
    int32 SmokeStep = 0;
    bool bSmokeSawDamage = false, bSmokeCooldownPassed = false, bSmokeSawOtherMovement = false;
    TWeakObjectPtr<AMMOCharacter> SmokeOther;
    FVector SmokeOtherStart = FVector::ZeroVector;
    bool bSmokeSawPlayerDeath = false;
    double SmokeLastAttack = -10;
    FString SmokeOriginalPawn;
    TWeakObjectPtr<UAbilitySystemComponent> SmokeOriginalASC;
};

struct FMMOSaveRecord;
UCLASS()
class MMORPG_API AMMOGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMMOGameMode();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* Controller) override;
    virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* Controller, const FTransform& Transform) override;
    UPROPERTY(EditDefaultsOnly, Category="Experience") FPrimaryAssetId DefaultExperience = FPrimaryAssetId(TEXT("MMOExperience"), TEXT("DA_MMOExperience"));
    UPROPERTY(EditDefaultsOnly, Category="Respawn", meta=(ClampMin="0.1")) float RespawnDelay = 3.f;
    void HandlePlayerDeath(AMMOCharacter* Character);
private:
    void ExperienceReady(const UMMOExperienceDefinition* Experience);
    UFUNCTION() void ExperienceFailed(const FString& Reason);
    void TryStartPlayer(APlayerController* Controller);
    void PeriodicSave();
    void Save(const TSharedPtr<FMMOSaveRecord>& Record, bool bFinal);
    TMap<TWeakObjectPtr<AMMOPlayerState>, TSharedPtr<FMMOSaveRecord>> Records;
    TMap<TWeakObjectPtr<APlayerController>, FTimerHandle> RespawnTimers;
    FTimerHandle SaveTimer;
};

UCLASS()
class MMORPG_API UMMOGameInstance : public UCommonGameInstance
{
    GENERATED_BODY()
public:
    UMMOGameInstance(const FObjectInitializer& Initializer);
    virtual void Init() override;
    bool bSmokeLoginStarted = false;
    bool bSmokeReturnExpected = false;
    bool bSmokeInputTagsVerifiedBeforeReturn = false;
    UPROPERTY(Transient) FText PendingLoginMessage;
};

