#include "MMOBackendServer.h"
#include "MMOContracts.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, MMOBackendServer)
bool UMMOBackendServer::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && CastChecked<UGameInstance>(Outer)->IsDedicatedServerInstance();
}
void UMMOBackendServer::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Secret = FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_SERVER_SECRET"));
    FParse::Value(FCommandLine::Get(), TEXT("MMOBackend="), BaseURL);
    FParse::Value(FCommandLine::Get(), TEXT("MMOServerID="), ServerID);
    BaseURL.RemoveFromEnd(TEXT("/"));
}
void UMMOBackendServer::Request(const FString& Route, TSharedPtr<FJsonObject> Body, FMMOServerReply Callback)
{
    if (!IsConfigured()) { Callback(false, 0, MakeShared<FJsonObject>()); return; }
    Body->SetStringField(TEXT("serverId"), ServerID);
    auto Request = FHttpModule::Get().CreateRequest(); Request->SetURL(BaseURL + Route); Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Secret); Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(MMO::ToJson(Body));
    Request->OnProcessRequestComplete().BindLambda([Callback = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bTransport)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>(); int32 Code = Response ? Response->GetResponseCode() : 0;
        const bool bJson = Response && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
        Callback(bTransport && bJson && Code >= 200 && Code < 300, Code, Json ? Json : MakeShared<FJsonObject>());
    });
    if (!Request->ProcessRequest()) Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
}
