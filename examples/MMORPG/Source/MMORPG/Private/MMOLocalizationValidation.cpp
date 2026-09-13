#include "MMOGameplay.h"
#include "MMOUI.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetInternationalizationLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Scalability.h"

void AMMOPlayerController::RecordCultureState(const FString& Stage)
{
#if !UE_BUILD_SHIPPING
    FString SavedCulture; GConfig->GetString(TEXT("Internationalization"), TEXT("Culture"), SavedCulture, GGameUserSettingsIni);
    const FString Culture = UKismetInternationalizationLibrary::GetCurrentCulture();
    FString ExpectedCulture; FParse::Value(FCommandLine::Get(), TEXT("MMOExpectedCulture="), ExpectedCulture);
    auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
    auto ActiveWidget = [Root](const TCHAR* Tag) -> UCommonActivatableWidget*
    { auto* Stack = Root ? Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(FName(Tag))) : nullptr; return Stack ? Stack->GetActiveWidget() : nullptr; };
    auto ReadText = [](UUserWidget* Widget, const TCHAR* Name) -> FString
    { auto* Text = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(FName(Name))) : nullptr; return Text ? Text->GetText().ToString() : FString(); };
    auto* HUD = Cast<UMMOHUDPanel>(ActiveWidget(TEXT("UI.Layer.Game")));
    auto* Menu = ActiveWidget(TEXT("UI.Layer.Menu"));
    const bool bBinding = HUD && HUD->ValidateManaBinding();
    auto Report = MakeShared<FJsonObject>(); Report->SetStringField(TEXT("stage"), Stage);
    Report->SetStringField(TEXT("culture"), Culture); Report->SetStringField(TEXT("savedCulture"), SavedCulture);
    Report->SetStringField(TEXT("settingsIni"), GGameUserSettingsIni); Report->SetBoolField(TEXT("isEditor"), GIsEditor);
    Report->SetBoolField(TEXT("hasHUD"), HUD != nullptr); Report->SetBoolField(TEXT("umgMVVMBinding"), bBinding);
    Report->SetStringField(TEXT("menuTitle"), ReadText(Menu, TEXT("Title"))); Report->SetStringField(TEXT("statusText"), ReadText(HUD, TEXT("Values")));
    Report->SetStringField(TEXT("targetText"), ReadText(HUD, TEXT("TargetText"))); Report->SetStringField(TEXT("inputHint"), ReadText(HUD, TEXT("InputHintText")));
    Report->SetBoolField(TEXT("persisted"), !GIsEditor && Culture == SavedCulture);
    const auto Quality = Scalability::GetQualityLevels(); auto Groups = MakeShared<FJsonObject>();
    Groups->SetNumberField(TEXT("ViewDistance"), Quality.ViewDistanceQuality); Groups->SetNumberField(TEXT("AntiAliasing"), Quality.AntiAliasingQuality);
    Groups->SetNumberField(TEXT("Shadow"), Quality.ShadowQuality); Groups->SetNumberField(TEXT("GlobalIllumination"), Quality.GlobalIlluminationQuality);
    Groups->SetNumberField(TEXT("Reflection"), Quality.ReflectionQuality); Groups->SetNumberField(TEXT("PostProcess"), Quality.PostProcessQuality);
    Groups->SetNumberField(TEXT("Texture"), Quality.TextureQuality); Groups->SetNumberField(TEXT("Effects"), Quality.EffectsQuality);
    Groups->SetNumberField(TEXT("Foliage"), Quality.FoliageQuality); Groups->SetNumberField(TEXT("Shading"), Quality.ShadingQuality);
    Report->SetObjectField(TEXT("qualityGroups"), Groups);
    if (!ExpectedCulture.IsEmpty()) Report->SetBoolField(TEXT("matchesExpectedCulture"), Culture == ExpectedCulture);
    FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    UE_LOG(LogTemp, Display, TEXT("MMO_CULTURE_RESULT %s"), *Text);
#endif
}
