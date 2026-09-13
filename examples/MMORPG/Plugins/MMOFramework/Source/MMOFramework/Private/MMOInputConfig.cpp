#include "MMOInputConfig.h"
#include "InputAction.h"

UInputAction* UMMOInputConfig::FindInputActionForTag(FGameplayTag InputTag) const
{
    if (!InputTag.IsValid()) return nullptr;
    for (const FMMOTaggedInputAction& Entry : NativeInputActions)
    {
        if (Entry.InputTag == InputTag && !Entry.InputAction.IsNull()) return Entry.InputAction.Get();
    }
    return nullptr;
}

void UMMOInputConfig::CollectAssetPaths(TArray<FSoftObjectPath>& OutPaths) const
{
    for (const FMMOTaggedInputAction& Entry : NativeInputActions)
    {
        if (!Entry.InputAction.IsNull()) OutPaths.AddUnique(Entry.InputAction.ToSoftObjectPath());
    }
}
