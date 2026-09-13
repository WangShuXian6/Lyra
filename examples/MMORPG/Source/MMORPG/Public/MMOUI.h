#pragma once
#include "CoreMinimal.h"
#include "CommonInputBaseTypes.h"
#include "MVVMViewModelBase.h"
#include "GameUIManagerSubsystem.h"
#include "GameUIPolicy.h"
#include "PrimaryGameLayout.h"
#include "CommonActivatableWidget.h"
#include "UIExtensionSystem.h"
#include "MMOUI.generated.h"

class AMMOPlayerState;
class AMMOCharacter;
class AMMOTarget;
class UCommonLocalPlayer;
class UTextBlock;
class UVerticalBox;
class UEditableTextBox;
class UComboBoxString;
class UMMOLoginPanel;
class UMMOHUDPanel;
class UInputMappingContext;
class UButton;
class UProgressBar;
class UMMOInventoryPanel;
class UMMOSettingsPanel;
class UMMOConfirmPanel;

/** Native equivalent of a CommonUI InputData Blueprint; reusable across LocalPlayers. */
UCLASS()
class MMORPG_API UMMOInputData : public UCommonUIInputData
{
    GENERATED_BODY()
public:
    UMMOInputData();
};

/** Event-driven projection; UI never changes the authoritative attribute or inventory. */
UCLASS(BlueprintType)
class MMORPG_API UMMOPlayerViewModel : public UMVVMViewModelBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText Status;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText InventoryText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float Health = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float Mana = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float ManaFraction = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText ManaLabel;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText StatusText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText InventoryLabel;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText TargetLabel;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText CooldownLabel;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float TargetFraction = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") FText InputHint;
    void SetInputMethod(ECommonInputType Type);
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float Cooldown = 0;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Player") float TargetHealth = 0;
    UFUNCTION(BlueprintCallable) void BindPlayer(AMMOPlayerState* State, AMMOCharacter* Pawn);
    UFUNCTION(BlueprintCallable) void Unbind();
    void Refresh();
    void BindTarget();
    virtual void BeginDestroy() override;
private:
    TWeakObjectPtr<AMMOPlayerState> Player;
    TWeakObjectPtr<AMMOCharacter> Character;
    TWeakObjectPtr<AMMOTarget> Target;
    FDelegateHandle HealthHandle, ManaHandle, ProgressHandle, TargetHandle, TargetHealthHandle;
    FDelegateHandle EffectAddedHandle, EffectRemovedHandle;
    void RefreshStatus();
    void RefreshMana();
    void RefreshInventory();
    void RefreshTarget();
    void RefreshProgress();
    void RefreshCooldown();
    void RefreshInputHint();
    bool bUsingGamepad = false;
    FTimerHandle CooldownTimer;
};

UCLASS()
class MMORPG_API UMMOUIManager : public UGameUIManagerSubsystem { GENERATED_BODY() };
UCLASS(Blueprintable)
class MMORPG_API UMMOUIPolicy : public UGameUIPolicy { GENERATED_BODY() };

UCLASS()
class MMORPG_API UMMORootLayout : public UPrimaryGameLayout
{
    GENERATED_BODY()
public:
    UMMORootLayout(const FObjectInitializer& Initializer);
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    void RefreshScreens();
    UPROPERTY(BlueprintReadOnly) TObjectPtr<UMMOPlayerViewModel> ViewModel;
    UFUNCTION(BlueprintCallable) UMMOInventoryPanel* ShowInventory();
    UFUNCTION(BlueprintCallable) UMMOSettingsPanel* ShowSettings();
    UFUNCTION(BlueprintCallable) UMMOConfirmPanel* ShowConfirm();
    UPROPERTY(EditDefaultsOnly, Category="UI|Screens") TSoftClassPtr<UMMOLoginPanel> LoginClass;
    UPROPERTY(EditDefaultsOnly, Category="UI|Screens") TSoftClassPtr<UMMOHUDPanel> HUDClass;
    UPROPERTY(EditDefaultsOnly, Category="UI|Screens") TSoftClassPtr<UMMOInventoryPanel> InventoryClass;
    UPROPERTY(EditDefaultsOnly, Category="UI|Screens") TSoftClassPtr<UMMOSettingsPanel> SettingsClass;
    UPROPERTY(EditDefaultsOnly, Category="UI|Screens") TSoftClassPtr<UMMOConfirmPanel> ConfirmClass;
private:
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UCommonActivatableWidgetStack> GameLayer;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UCommonActivatableWidgetStack> MenuLayer;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UCommonActivatableWidgetStack> ModalLayer;
    UPROPERTY() TObjectPtr<UMMOLoginPanel> Login;
    UPROPERTY() TObjectPtr<UMMOHUDPanel> HUD;
    TWeakObjectPtr<AMMOPlayerState> ObservedState;
    TWeakObjectPtr<ULocalPlayer> InputPlayer;
    UPROPERTY() TObjectPtr<UInputMappingContext> UIInputContext;
    FDelegateHandle StateHandle, PawnHandle, ProgressHandle;
    FDelegateHandle InputMethodHandle;
    FUIExtensionHandle StatusExtension;
};

/** Designer owns layout and styling. Native parents own business callbacks and lifecycle. */
UCLASS()
class MMORPG_API UMMOPanel : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    UMMOPanel(const FObjectInitializer& Initializer);
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
protected:
    UPROPERTY() TObjectPtr<UWidget> FirstFocus;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidgetOptional, AllowPrivateAccess="true")) TObjectPtr<UTextBlock> Message;
};

UCLASS()
class MMORPG_API UMMOLoginPanel : public UMMOPanel
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void LoginClicked();
    UFUNCTION() void RegisterClicked();
    UFUNCTION() void RefreshClicked();
    UFUNCTION() void CreateClicked();
    UFUNCTION() void JoinClicked();
    void Authenticate(bool bRegister);
    void SetBusy(bool bBusy, const FText& Status);
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UEditableTextBox> Username;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UEditableTextBox> Password;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UEditableTextBox> CharacterName;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UComboBoxString> CharacterList;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UVerticalBox> Form;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> LoginButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> RegisterButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> RefreshButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> CreateButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> JoinButton;
    TArray<FString> CharacterIDs;
    bool bRequestPending = false;
    uint32 RequestGeneration = 0;
};

UCLASS()
class MMORPG_API UMMOHUDPanel : public UMMOPanel
{
    GENERATED_BODY()
public:
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    void SetViewModel(UMMOPlayerViewModel* Model);
    UFUNCTION(BlueprintPure, Category="MMO|UI") bool ValidateManaBinding() const;
private:
    UFUNCTION() void TargetClicked();
    UFUNCTION() void AttackClicked();
    UFUNCTION() void SpellClicked();
    UFUNCTION() void InventoryClicked();
    UFUNCTION() void SettingsClicked();
    UPROPERTY() TObjectPtr<UMMOPlayerViewModel> VM;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UTextBlock> Values;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UUserWidget> ManaStatus;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UTextBlock> TargetText;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UProgressBar> TargetHealthBar;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UTextBlock> CooldownText;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UTextBlock> InputHintText;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> TargetButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> AttackButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> SpellButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> InventoryButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> SettingsButton;
    FUIExtensionPointHandle ExtensionPoint;
};

UCLASS()
class MMORPG_API UMMOInventoryPanel : public UMMOPanel
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void PotionClicked();
    UFUNCTION() void CloseClicked();
    UPROPERTY() TObjectPtr<UMMOPlayerViewModel> VM;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> PotionButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> CloseButton;
};

UCLASS()
class MMORPG_API UMMOSettingsPanel : public UMMOPanel
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void LowQualityClicked();
    UFUNCTION() void MediumQualityClicked();
    UFUNCTION() void HighQualityClicked();
    void ChangeQuality(int32 Level);
    UFUNCTION() void ChineseClicked();
    UFUNCTION() void EnglishClicked();
    void ChangeCulture(const FString& Culture);
    UFUNCTION() void LogoutClicked();
    UFUNCTION() void CloseClicked();
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> LowQualityButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> MediumQualityButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> HighQualityButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> LogoutButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> CloseButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> ChineseButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> EnglishButton;
};

UCLASS()
class MMORPG_API UMMOConfirmPanel : public UMMOPanel
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void ConfirmClicked();
    UFUNCTION() void CancelClicked();
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(BlueprintReadOnly, Category="UI", meta=(BindWidget, AllowPrivateAccess="true")) TObjectPtr<UButton> CancelButton;
};
