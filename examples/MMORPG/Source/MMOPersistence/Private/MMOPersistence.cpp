#include "MMOPersistence.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Guid.h"
#include <libpq-fe.h>
#include <sodium.h>
IMPLEMENT_MODULE(FDefaultModuleImpl, MMOPersistence)

namespace
{
using namespace MMO;
struct FRows
{
    PGresult* Result = nullptr;
    explicit FRows(PGresult* In) : Result(In) {}
    ~FRows() { if (Result) PQclear(Result); }
    FRows(const FRows&) = delete;
    bool Good() const { return Result && (PQresultStatus(Result)==PGRES_TUPLES_OK || PQresultStatus(Result)==PGRES_COMMAND_OK); }
    int32 Num() const { return Good() ? PQntuples(Result) : 0; }
    FString At(int32 Row, int32 Col) const { return UTF8_TO_TCHAR(PQgetvalue(Result,Row,Col)); }
    bool UniqueViolation() const { const char* Code = Result ? PQresultErrorField(Result,PG_DIAG_SQLSTATE):nullptr; return Code && FCStringAnsi::Strcmp(Code,"23505")==0; }
};
FRows Query(PGconn* DB, const ANSICHAR* SQL, const TArray<FString>& Args = {})
{
    TArray<TArray<ANSICHAR>> Bytes; TArray<const ANSICHAR*> Values;
    for (const FString& Arg : Args)
    {
        FTCHARToUTF8 Utf8(*Arg); TArray<ANSICHAR>& B=Bytes.AddDefaulted_GetRef();
        B.Append(Utf8.Get(),Utf8.Length()+1);
    }
    for (const auto& B : Bytes) Values.Add(B.GetData());
    return FRows(PQexecParams(DB,SQL,Args.Num(),nullptr,Values.GetData(),nullptr,nullptr,0));
}
struct FTransaction
{
    PGconn* DB; bool Active;
    explicit FTransaction(PGconn* In):DB(In),Active(Query(DB,"BEGIN").Good()) {}
    ~FTransaction() { if (Active) Query(DB,"ROLLBACK"); }
    bool Commit() { if (!Active) return false; const bool Ok=Query(DB,"COMMIT").Good(); Active=false; return Ok; }
};
FString Hash(const FString& Text)
{
    FTCHARToUTF8 Utf8(*Text); unsigned char Digest[32]; char Hex[65];
    crypto_generichash(Digest,sizeof(Digest),reinterpret_cast<const unsigned char*>(Utf8.Get()),Utf8.Length(),nullptr,0);
    sodium_bin2hex(Hex,sizeof(Hex),Digest,sizeof(Digest)); return UTF8_TO_TCHAR(Hex);
}
FString Token()
{
    unsigned char Bytes[32]; char Hex[65]; randombytes_buf(Bytes,sizeof(Bytes));
    sodium_bin2hex(Hex,sizeof(Hex),Bytes,sizeof(Bytes)); return UTF8_TO_TCHAR(Hex);
}
bool ConstantEqual(const FString& A,const FString& B)
{
    FTCHARToUTF8 UA(*A), UB(*B); return UA.Length()==UB.Length() && sodium_memcmp(UA.Get(),UB.Get(),UA.Length())==0;
}
FString Field(const FRequest& R, const TCHAR* Name) { FString Value; R.Body->TryGetStringField(Name,Value); return Value; }
bool IsUUID(const FString& Value) { FGuid GUID; return FGuid::ParseExact(Value,EGuidFormats::DigitsWithHyphens,GUID); }
bool IsToken(const FString& Value) { if (Value.Len()!=64) return false; for(TCHAR C:Value) if (!FChar::IsHexDigit(C)) return false; return true; }
FReply Bad() { return FReply::Error(400,TEXT("invalid_request"),TEXT("Check the documented request fields.")); }
FReply Unauth() { return FReply::Error(401,TEXT("unauthorized"),TEXT("Authentication is required or expired.")); }
FReply Unavailable() { return FReply::Error(503,TEXT("database_unavailable"),TEXT("Persistence is unavailable; retry with the same save requestId.")); }
FReply Conflict(const TCHAR* Code) { return FReply::Error(409,Code,TEXT("The operation conflicts with current state.")); }
FReply Ok() { FReply R; R.Body->SetBoolField(TEXT("ok"),true); return R; }
TSharedPtr<FJsonObject> Character(const FRows& Rows,int32 Index)
{
    auto C=MakeShared<FJsonObject>(); C->SetStringField(TEXT("id"),Rows.At(Index,0)); C->SetStringField(TEXT("name"),Rows.At(Index,1));
    C->SetNumberField(TEXT("version"),FCString::Atoi64(*Rows.At(Index,2))); C->SetObjectField(TEXT("state"),ParseJson(Rows.At(Index,3))); return C;
}
}

FMMOPersistence::FMMOPersistence()
{
    ServerSecret=FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_SERVER_SECRET"));
    ServerID=FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_SERVER_ID")); if (ServerID.IsEmpty()) ServerID=TEXT("local-1");
    ServerAddress=FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_GAME_ADDRESS")); if (ServerAddress.IsEmpty()) ServerAddress=TEXT("127.0.0.1:7777");
}
FMMOPersistence::~FMMOPersistence() { if(Connection) PQfinish(Connection); }
bool FMMOPersistence::InitializeCrypto() { return sodium_init()>=0; }
bool FMMOPersistence::Open()
{
    if(Connection) { PQfinish(Connection); Connection=nullptr; }
    // libpq reads PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD/PGSSLMODE from the process environment.
    Connection=PQconnectdb("connect_timeout=3 application_name=lyra_mmo_backend options='-c statement_timeout=5000 -c lock_timeout=3000'");
    return PQstatus(Connection)==CONNECTION_OK && Query(Connection,"SELECT 1 FROM schema_migrations WHERE version=2").Num()==1;
}

MMO::FReply FMMOPersistence::Execute(const MMO::FRequest& R)
{
    using namespace MMO;
    if(!Connection || PQstatus(Connection)!=CONNECTION_OK) if(!Open()) return Unavailable();
    if(R.Route==TEXT("/health")) return Query(Connection,"SELECT 1").Good()?Ok():Unavailable();
    if(R.Route==Register || R.Route==Login)
    {
        FString User=Field(R,TEXT("username")).ToLower(), Password=Field(R,TEXT("password"));
        if(User.Len()<3 || User.Len()>32 || Password.Len()<12 || Password.Len()>128) return Bad();
        for(TCHAR C:User) if(!((C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='_')) return Bad();
        FTCHARToUTF8 Pw(*Password);
        if(R.Route==Register)
        {
            char Encoded[crypto_pwhash_STRBYTES];
            if(crypto_pwhash_str(Encoded,Pw.Get(),Pw.Length(),crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE)!=0) return Unavailable();
            auto Rows=Query(Connection,"INSERT INTO accounts(username,password_hash) VALUES($1,$2) RETURNING id",{User,UTF8_TO_TCHAR(Encoded)});
            if(!Rows.Good()) return Rows.UniqueViolation()?Conflict(TEXT("username_taken")):Unavailable();
            FReply Reply; Reply.Status=201; Reply.Body->SetStringField(TEXT("accountId"),Rows.At(0,0)); return Reply;
        }
        auto Rows=Query(Connection,"SELECT id,password_hash FROM accounts WHERE username=$1",{User}); if(!Rows.Good()) return Unavailable();
        // Always run a password KDF, including unknown accounts, so the missing-user path has similar cost.
        if(Rows.Num()==0) { char Dummy[crypto_pwhash_STRBYTES]; const int Result=crypto_pwhash_str(Dummy,Pw.Get(),Pw.Length(),crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE); (void)Result; return Unauth(); }
        FTCHARToUTF8 Encoded(*Rows.At(0,1)); if(crypto_pwhash_str_verify(Encoded.Get(),Pw.Get(),Pw.Length())!=0) return Unauth();
        FString Session=Token(); FTransaction Tx(Connection); if(!Tx.Active) return Unavailable();
        if(!Query(Connection,"SELECT id FROM accounts WHERE id=$1::uuid FOR UPDATE",{Rows.At(0,0)}).Good()) return Unavailable();
        if(!Query(Connection,"DELETE FROM sessions WHERE account_id=$1::uuid",{Rows.At(0,0)}).Good()) return Unavailable();
        // Replacing a login also revokes admission credentials issued by its previous session.
        if(!Query(Connection,"DELETE FROM join_tickets WHERE account_id=$1::uuid AND consumed_at IS NULL",{Rows.At(0,0)}).Good()) return Unavailable();
        if(!Query(Connection,"INSERT INTO sessions(token_hash,account_id,expires_at) VALUES($1,$2::uuid,now()+interval '24 hours')",{Hash(Session),Rows.At(0,0)}).Good() || !Tx.Commit()) return Unavailable();
        FReply Reply; Reply.Body->SetStringField(TEXT("token"),Session); Reply.Body->SetNumberField(TEXT("expiresIn"),SessionSeconds); return Reply;
    }
    if(R.Route.StartsWith(TEXT("/v1/server/")))
    {
        if(ServerSecret.Len()<32 || !ConstantEqual(R.Bearer,ServerSecret)) return Unauth();
        if(Field(R,TEXT("serverId"))!=ServerID) return FReply::Error(403,TEXT("wrong_server"),TEXT("Unknown serverId."));
        FTransaction Tx(Connection); if(!Tx.Active) return Unavailable();
        if(R.Route==ConsumeTicket)
        {
            FString Ticket=Field(R,TEXT("ticket")); if(!IsToken(Ticket)) return Bad();
            // Discover the account without locking the ticket. All admission paths take locks
            // in account -> ticket -> lease order, including login/logout ticket revocation.
            auto AccountRow=Query(Connection,"SELECT account_id FROM join_tickets WHERE token_hash=$1 AND server_id=$2 AND expires_at>now() AND consumed_at IS NULL",{Hash(Ticket),ServerID});
            if(!AccountRow.Good()) return Unavailable(); if(AccountRow.Num()!=1) return FReply::Error(401,TEXT("invalid_ticket"),TEXT("Ticket expired, consumed, or bound to another server."));
            const FString Account=AccountRow.At(0,0);
            auto AccountLock=Query(Connection,"SELECT id FROM accounts WHERE id=$1::uuid FOR UPDATE",{Account});
            if(!AccountLock.Good()) return Unavailable(); if(AccountLock.Num()!=1) return Unauth();
            // The ticket may have been revoked or consumed while the account lock was pending.
            auto TicketRow=Query(Connection,"SELECT character_id,account_id FROM join_tickets WHERE token_hash=$1 AND server_id=$2 AND account_id=$3::uuid AND expires_at>clock_timestamp() AND consumed_at IS NULL FOR UPDATE",{Hash(Ticket),ServerID,Account});
            if(!TicketRow.Good()) return Unavailable(); if(TicketRow.Num()!=1) return FReply::Error(401,TEXT("invalid_ticket"),TEXT("Ticket expired, consumed, or bound to another server."));
            FString ID=TicketRow.At(0,0), Lease=Token();
            if(!Query(Connection,"DELETE FROM game_leases WHERE account_id=$1::uuid AND expires_at<=now()",{Account}).Good()) return Unavailable();
            auto Existing=Query(Connection,"SELECT character_id FROM game_leases WHERE account_id=$1::uuid",{Account}); if(!Existing.Good()) return Unavailable(); if(Existing.Num()) return Conflict(TEXT("already_online"));
            if(!Query(Connection,"INSERT INTO game_leases(character_id,account_id,token_hash,server_id,expires_at) VALUES($1::uuid,$2::uuid,$3,$4,now()+interval '120 seconds')",{ID,Account,Hash(Lease),ServerID}).Good()) return Unavailable();
            if(!Query(Connection,"UPDATE join_tickets SET consumed_at=now() WHERE token_hash=$1",{Hash(Ticket)}).Good()) return Unavailable();
            auto C=Query(Connection,"SELECT id,name,version,state FROM characters WHERE id=$1::uuid",{ID}); if(!C.Good() || C.Num()!=1 || !Tx.Commit()) return Unavailable();
            FReply Reply; Reply.Body->SetStringField(TEXT("leaseToken"),Lease); Reply.Body->SetObjectField(TEXT("character"),Character(C,0)); return Reply;
        }
        FString ID=Field(R,TEXT("characterId")), Lease=Field(R,TEXT("leaseToken"));
        if(!IsUUID(ID) || !IsToken(Lease)) return Bad();
        auto LeaseRows=Query(Connection,"SELECT character_id FROM game_leases WHERE character_id=$1::uuid AND token_hash=$2 AND server_id=$3 AND expires_at>now() FOR UPDATE",{ID,Hash(Lease),ServerID});
        if(!LeaseRows.Good()) return Unavailable(); if(LeaseRows.Num()!=1) return FReply::Error(403,TEXT("invalid_lease"),TEXT("The active game lease is missing or expired."));
        if(R.Route==Release)
        {
            if(!Query(Connection,"DELETE FROM game_leases WHERE character_id=$1::uuid",{ID}).Good() || !Tx.Commit()) return Unavailable(); return Ok();
        }
        if(!Query(Connection,"UPDATE game_leases SET expires_at=now()+interval '120 seconds' WHERE character_id=$1::uuid",{ID}).Good()) return Unavailable();
        if(R.Route==Heartbeat) { if(!Tx.Commit()) return Unavailable(); return Ok(); }
        if(R.Route==Load)
        {
            auto C=Query(Connection,"SELECT id,name,version,state FROM characters WHERE id=$1::uuid",{ID}); if(!C.Good()||C.Num()!=1||!Tx.Commit()) return Unavailable();
            FReply Reply; Reply.Body->SetObjectField(TEXT("character"),Character(C,0)); return Reply;
        }
        if(R.Route==Save)
        {
            FString RequestID=Field(R,TEXT("requestId")); double Expected; const TSharedPtr<FJsonObject>* State;
            if(!IsUUID(RequestID)||!R.Body->TryGetNumberField(TEXT("expectedVersion"),Expected)||Expected<0||Expected>9e15||Expected!=FMath::FloorToDouble(Expected)||!R.Body->TryGetObjectField(TEXT("state"),State)||!ValidateState(*State)) return Bad();
            const FString StateJson=ToJson(*State), Version=FString::Printf(TEXT("%.0f"),Expected);
            // PostgreSQL canonicalizes JSONB key order before hashing, so retry key order is irrelevant.
            auto Canon=Query(Connection,"SELECT $1::jsonb::text",{StateJson}); if(!Canon.Good()) return Unavailable();
            FString PayloadHash=Hash(Version+TEXT(":")+Canon.At(0,0));
            auto Prior=Query(Connection,"SELECT result_version,payload_hash,lease_hash FROM save_requests WHERE character_id=$1::uuid AND request_id=$2::uuid",{ID,RequestID}); if(!Prior.Good()) return Unavailable();
            if(Prior.Num())
            {
                if(Prior.At(0,1)!=PayloadHash || Prior.At(0,2)!=Hash(Lease)) return Conflict(TEXT("idempotency_conflict"));
                if(!Tx.Commit()) return Unavailable(); FReply Reply; Reply.Body->SetNumberField(TEXT("version"),FCString::Atoi64(*Prior.At(0,0))); return Reply;
            }
            auto Saved=Query(Connection,"UPDATE characters SET state=$3::jsonb,version=version+1,updated_at=now() WHERE id=$1::uuid AND version=$2::bigint RETURNING version",{ID,Version,StateJson});
            if(!Saved.Good()) return Unavailable(); if(Saved.Num()!=1) return Conflict(TEXT("version_conflict"));
            if(!Query(Connection,"INSERT INTO save_requests(character_id,request_id,lease_hash,payload_hash,result_version) VALUES($1::uuid,$2::uuid,$3,$4,$5::bigint)",{ID,RequestID,Hash(Lease),PayloadHash,Saved.At(0,0)}).Good()||!Tx.Commit()) return Unavailable();
            FReply Reply; Reply.Body->SetNumberField(TEXT("version"),FCString::Atoi64(*Saved.At(0,0))); return Reply;
        }
        return FReply::Error(404,TEXT("not_found"),TEXT("Unknown route."));
    }
    if(!IsToken(R.Bearer)) return Unauth();
    auto Session=Query(Connection,"SELECT account_id FROM sessions WHERE token_hash=$1 AND expires_at>now()",{Hash(R.Bearer)});
    if(!Session.Good()) return Unavailable(); if(Session.Num()!=1) return Unauth(); FString Account=Session.At(0,0);
    // The first lookup only locates the lock. Authorize again after acquiring it: otherwise a
    // request waiting here could create a new ticket after another request completed logout.
    FTransaction Tx(Connection); if(!Tx.Active) return Unavailable();
    auto AccountLock=Query(Connection,"SELECT id FROM accounts WHERE id=$1::uuid FOR UPDATE",{Account});
    if(!AccountLock.Good()) return Unavailable(); if(AccountLock.Num()!=1) return Unauth();
    auto CurrentSession=Query(Connection,"SELECT account_id FROM sessions WHERE token_hash=$1 AND account_id=$2::uuid AND expires_at>clock_timestamp()",{Hash(R.Bearer),Account});
    if(!CurrentSession.Good()) return Unavailable(); if(CurrentSession.Num()!=1) return Unauth();
    if(R.Route==Logout)
    {
        if(!Query(Connection,"DELETE FROM sessions WHERE token_hash=$1",{Hash(R.Bearer)}).Good()||!Query(Connection,"DELETE FROM join_tickets WHERE account_id=$1::uuid AND consumed_at IS NULL",{Account}).Good()||!Tx.Commit()) return Unavailable(); return Ok();
    }
    if(R.Route==Characters)
    {
        if(R.Method==TEXT("GET"))
        {
            auto Rows=Query(Connection,"SELECT id,name,version,state FROM characters WHERE account_id=$1::uuid ORDER BY name",{Account}); if(!Rows.Good()) return Unavailable();
            TArray<TSharedPtr<FJsonValue>> Array; for(int32 I=0;I<Rows.Num();++I) Array.Add(MakeShared<FJsonValueObject>(Character(Rows,I)));
            if(!Tx.Commit()) return Unavailable();
            FReply Reply; Reply.Body->SetArrayField(TEXT("characters"),Array); return Reply;
        }
        FString Name=Field(R,TEXT("name")).TrimStartAndEnd(); if(Name.Len()<2||Name.Len()>24) return Bad();
        for(TCHAR C:Name) if(FChar::IsControl(C)) return Bad();
        auto Count=Query(Connection,"SELECT count(*) FROM characters WHERE account_id=$1::uuid",{Account}); if(!Count.Good()) return Unavailable(); if(FCString::Atoi(*Count.At(0,0))>=8) return Conflict(TEXT("character_limit"));
        auto Rows=Query(Connection,"INSERT INTO characters(account_id,name) VALUES($1::uuid,$2) RETURNING id,name,version,state",{Account,Name}); if(!Rows.Good()) return Rows.UniqueViolation()?Conflict(TEXT("name_taken")):Unavailable();
        if(!Tx.Commit()) return Unavailable(); FReply Reply; Reply.Status=201; Reply.Body=Character(Rows,0); return Reply;
    }
    if(R.Route==JoinTicket)
    {
        FString ID=Field(R,TEXT("characterId")); if(!IsUUID(ID)||Field(R,TEXT("serverId"))!=ServerID) return Bad();
        auto C=Query(Connection,"SELECT id FROM characters WHERE id=$1::uuid AND account_id=$2::uuid",{ID,Account}); if(!C.Good()) return Unavailable(); if(C.Num()!=1) return FReply::Error(404,TEXT("character_not_found"),TEXT("Character is unavailable."));
        auto Active=Query(Connection,"SELECT character_id FROM game_leases WHERE account_id=$1::uuid AND expires_at>now()",{Account}); if(!Active.Good()) return Unavailable(); if(Active.Num()) return Conflict(TEXT("already_online"));
        if(!Query(Connection,"DELETE FROM join_tickets WHERE account_id=$1::uuid AND consumed_at IS NULL",{Account}).Good()) return Unavailable();
        FString Ticket=Token(); if(!Query(Connection,"INSERT INTO join_tickets(token_hash,account_id,character_id,server_id,expires_at) VALUES($1,$2::uuid,$3::uuid,$4,now()+interval '60 seconds')",{Hash(Ticket),Account,ID,ServerID}).Good()||!Tx.Commit()) return Unavailable();
        FReply Reply; Reply.Body->SetStringField(TEXT("ticket"),Ticket); Reply.Body->SetNumberField(TEXT("expiresIn"),TicketSeconds); Reply.Body->SetStringField(TEXT("address"),ServerAddress); return Reply;
    }
    return FReply::Error(404,TEXT("not_found"),TEXT("Unknown route."));
}
