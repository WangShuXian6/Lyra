#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MMOBackendServer.generated.h"
using FMMOServerReply = TFunction<void(bool, int32, TSharedPtr<FJsonObject>)>;

UCLASS()
class MMOBACKENDSERVER_API UMMOBackendServer : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    void Request(const FString& Route, TSharedPtr<FJsonObject> Body, FMMOServerReply Callback);
    FString ServerID = TEXT("local-1");
    bool IsConfigured() const { return !Secret.IsEmpty(); }
private:
    FString BaseURL = TEXT("http://127.0.0.1:8088");
    FString Secret;
};
