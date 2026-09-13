#pragma once
#include "MMOContracts.h"
struct pg_conn;
class MMOPERSISTENCE_API FMMOPersistence
{
public:
    FMMOPersistence();
    ~FMMOPersistence();
    bool Open();
    MMO::FReply Execute(const MMO::FRequest& Request);
    static bool InitializeCrypto();
private:
    pg_conn* Connection = nullptr;
    FString ServerSecret;
    FString ServerID;
    FString ServerAddress;
};
