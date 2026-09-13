#include "MMOBackendClient.h"
#include "MMOContracts.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, MMOBackendClient)

void UMMOBackendClient::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FParse::Value(FCommandLine::Get(), TEXT("MMOBackend="), BaseURL);
    BaseURL.RemoveFromEnd(TEXT("/"));
}
void UMMOBackendClient::Deinitialize()
{
    for (auto& Request : Pending) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); }
    Pending.Empty(); SessionToken.Empty(); Super::Deinitialize();
}
void UMMOBackendClient::Request(const FString& Verb, const FString& Route, TSharedPtr<FJsonObject> Body, FMMOResponse Callback)
{
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseURL + Route); Request->SetVerb(Verb);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!SessionToken.IsEmpty()) Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + SessionToken);
    if (Body && !Verb.Equals(TEXT("GET"), ESearchCase::IgnoreCase) && !Verb.Equals(TEXT("HEAD"), ESearchCase::IgnoreCase)) Request->SetContentAsString(MMO::ToJson(Body));
    Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Callback = MoveTemp(Callback)](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bTransport)
    {
        // Some platform implementations dispatch failure synchronously from ProcessRequest.
        // The explicit fallback below and the platform callback must complete only once.
        if (Pending.Remove(Req) == 0) return;
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        const int32 Code = Response ? Response->GetResponseCode() : 0;
        const bool bJson = Response && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
        if (!bJson) { Json = MakeShared<FJsonObject>(); Json->SetStringField(TEXT("message"), TEXT("Backend unavailable or invalid JSON response")); }
        Callback(bTransport && bJson && Code >= 200 && Code < 300, Code, Json);
    });
    Pending.Add(Request);
    if (!Request->ProcessRequest()) Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
}
UMMORequestAction* UMMORequestAction::RequestMMO(UObject* WorldContextObject, FString Verb, FString Route, FString JsonBody)
{
    auto* Action = NewObject<UMMORequestAction>(); Action->Context = WorldContextObject;
    Action->Method = MoveTemp(Verb); Action->Path = MoveTemp(Route); Action->BodyText = MoveTemp(JsonBody);
    Action->RegisterWithGameInstance(WorldContextObject); return Action;
}
void UMMORequestAction::Activate()
{
    auto* GI = UGameplayStatics::GetGameInstance(Context);
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    if (!GI || (!BodyText.IsEmpty() && !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BodyText), Body)))
    { Completed.Broadcast(false, 0, TEXT("{\"error\":\"invalid_context_or_json\"}")); SetReadyToDestroy(); return; }
    GI->GetSubsystem<UMMOBackendClient>()->Request(Method, Path, Body, [WeakThis = TWeakObjectPtr<UMMORequestAction>(this)](bool bOK, int32 Code, TSharedPtr<FJsonObject> Json)
    {
        if (auto* Self = WeakThis.Get()) { FString Text; FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); Self->Completed.Broadcast(bOK, Code, Text); Self->SetReadyToDestroy(); }
    });
}
