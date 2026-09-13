#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MMO
{
    inline constexpr int32 SessionSeconds = 86400;
    inline constexpr int32 TicketSeconds = 60;
    inline constexpr int32 LeaseSeconds = 120;
    inline constexpr int32 MaxBodyBytes = 65536;
    inline constexpr const TCHAR* Register = TEXT("/v1/auth/register");
    inline constexpr const TCHAR* Login = TEXT("/v1/auth/login");
    inline constexpr const TCHAR* Logout = TEXT("/v1/auth/logout");
    inline constexpr const TCHAR* Characters = TEXT("/v1/characters");
    inline constexpr const TCHAR* JoinTicket = TEXT("/v1/join-ticket");
    inline constexpr const TCHAR* ConsumeTicket = TEXT("/v1/server/consume-ticket");
    inline constexpr const TCHAR* Load = TEXT("/v1/server/load");
    inline constexpr const TCHAR* Save = TEXT("/v1/server/save");
    inline constexpr const TCHAR* Heartbeat = TEXT("/v1/server/heartbeat");
    inline constexpr const TCHAR* Release = TEXT("/v1/server/release");

    struct MMOCONTRACTS_API FReply
    {
        int32 Status = 200;
        TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
        static FReply Error(int32 Status, const FString& Code, const FString& Message);
    };
    struct MMOCONTRACTS_API FRequest
    {
        FString Route;
        FString Method;
        FString Bearer;
        TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    };
    MMOCONTRACTS_API FString ToJson(const TSharedPtr<FJsonObject>& Object);
    MMOCONTRACTS_API TSharedPtr<FJsonObject> ParseJson(const FString& Text);
    MMOCONTRACTS_API TSharedPtr<FJsonObject> InitialState();
    MMOCONTRACTS_API bool ValidateState(const TSharedPtr<FJsonObject>& State);
}
