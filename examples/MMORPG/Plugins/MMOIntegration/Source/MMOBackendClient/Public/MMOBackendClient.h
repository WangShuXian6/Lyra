#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "MMOBackendClient.generated.h"

using FMMOResponse = TFunction<void(bool, int32, TSharedPtr<FJsonObject>)>;

/** HTTP credentials live in the game instance, never in replicated PlayerState. */
UCLASS()
class MMOBACKENDCLIENT_API UMMOBackendClient : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Request(const FString& Verb, const FString& Route, TSharedPtr<FJsonObject> Body, FMMOResponse Callback);
    UFUNCTION(BlueprintCallable, Category="MMO|Backend") void SetSessionToken(const FString& Token) { SessionToken = Token; }
    UFUNCTION(BlueprintPure, Category="MMO|Backend") bool IsLoggedIn() const { return !SessionToken.IsEmpty(); }
    FString BaseURL = TEXT("http://127.0.0.1:8088");
private:
    FString SessionToken;
    TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> Pending;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMMOJsonReply, bool, bSuccess, int32, StatusCode, FString, Json);

/** Native Blueprint node: JSON is intentionally visible for HTTP protocol lessons. */
UCLASS()
class MMOBACKENDCLIENT_API UMMORequestAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable) FMMOJsonReply Completed;
    UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true",WorldContext="WorldContextObject"), Category="MMO|Backend")
    static UMMORequestAction* RequestMMO(UObject* WorldContextObject, FString Verb, FString Route, FString JsonBody);
    virtual void Activate() override;
private:
    UPROPERTY() TObjectPtr<UObject> Context;
    FString Method, Path, BodyText;
};
