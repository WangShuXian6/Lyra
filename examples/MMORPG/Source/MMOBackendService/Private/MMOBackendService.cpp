#include "RequiredProgramMainCPPInclude.h"
#include "MMOContracts.h"
#include "MMOPersistence.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeCounter.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"

IMPLEMENT_APPLICATION(MMOBackendService, "MMOBackendService");
DEFINE_LOG_CATEGORY_STATIC(LogMMOBackend, Log, All);

namespace
{
struct FJob { MMO::FRequest Request; FHttpResultCallback Complete; };
struct FCompleted { MMO::FReply Reply; FHttpResultCallback Complete; };

// Exactly two PostgreSQL connections / password hashing workers and at most 32 outstanding requests.
// A callback travels through the completion queue; HTTPServer only sees it on its own ticker thread.
class FWorkQueue
{
    class FWorker final : public FRunnable
    {
    public:
        FWorkQueue& Owner;
        explicit FWorker(FWorkQueue& In):Owner(In) {}
        virtual uint32 Run() override
        {
            FMMOPersistence Database;
            while(!Owner.bStop.Load())
            {
                FJob Job; bool bFound=false;
                { FScopeLock Lock(&Owner.Mutex); if(Owner.Pending.Num()) { Job=MoveTemp(Owner.Pending[0]); Owner.Pending.RemoveAt(0); bFound=true; } }
                if(bFound)
                {
                    FCompleted Done{Database.Execute(Job.Request),MoveTemp(Job.Complete)};
                    Owner.Completed.Enqueue(MoveTemp(Done));
                }
                else FPlatformProcess::Sleep(0.005f);
            }
            return 0;
        }
    };
    FCriticalSection Mutex;
    TArray<FJob> Pending;
    TQueue<FCompleted,EQueueMode::Mpsc> Completed;
    FThreadSafeCounter Outstanding;
    TAtomic<bool> bStop{false};
    TArray<TUniquePtr<FWorker>> Workers;
    TArray<FRunnableThread*> Threads;
public:
    FWorkQueue()
    {
        for(int32 I=0;I<2;++I)
        {
            auto Worker=MakeUnique<FWorker>(*this);
            Threads.Add(FRunnableThread::Create(Worker.Get(),*FString::Printf(TEXT("MMODatabase%d"),I)));
            check(Threads.Last()); Workers.Add(MoveTemp(Worker));
        }
    }
    ~FWorkQueue()
    {
        bStop.Store(true); for(auto* Thread:Threads) { Thread->WaitForCompletion(); delete Thread; }
    }
    static void Send(const MMO::FReply& Reply,const FHttpResultCallback& Complete)
    {
        auto Response=FHttpServerResponse::Create(MMO::ToJson(Reply.Body),TEXT("application/json; charset=utf-8"));
        Response->Code=static_cast<EHttpServerResponseCodes>(Reply.Status);
        Response->Headers.Add(TEXT("Cache-Control"),{TEXT("no-store")});
        Complete(MoveTemp(Response));
    }
    bool Enqueue(MMO::FRequest Request,const FHttpResultCallback& Complete)
    {
        if(Outstanding.Increment()>32) { Outstanding.Decrement(); return false; }
        FScopeLock Lock(&Mutex); Pending.Add({MoveTemp(Request),Complete}); return true;
    }
    void Pump()
    {
        FCompleted Done;
        while(Completed.Dequeue(Done)) { Send(Done.Reply,Done.Complete); Outstanding.Decrement(); }
    }
    int32 Num() const { return Outstanding.GetValue(); }
};

FString Bearer(const FHttpServerRequest& Request)
{
    for(const auto& Pair:Request.Headers)
        if(Pair.Key.Equals(TEXT("Authorization"),ESearchCase::IgnoreCase) && Pair.Value.Num()==1 && Pair.Value[0].StartsWith(TEXT("Bearer ")))
            return Pair.Value[0].Mid(7);
    return {};
}
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
    FTaskTagScope Scope(ETaskTag::EGameThread);
    ON_SCOPE_EXIT
    {
        RequestEngineExit(TEXT("Backend stopped"));
        FEngineLoop::AppPreExit(); FModuleManager::Get().UnloadModulesAtShutdown(); FEngineLoop::AppExit();
    };
    if(int32 Result=GEngineLoop.PreInit(ArgC,ArgV)) return Result;
    if(!FMMOPersistence::InitializeCrypto()) return 2;
    if(FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_SERVER_SECRET")).Len()<32 || FPlatformMisc::GetEnvironmentVariable(TEXT("PGDATABASE")).IsEmpty())
    {
        UE_LOG(LogMMOBackend,Error,TEXT("Set PGDATABASE, PGUSER, database authentication, and MMO_SERVER_SECRET (at least 32 characters).")); return 2;
    }
    int32 Port=8088; FParse::Value(FCommandLine::Get(),TEXT("Port="),Port);
    if(Port<1024 || Port>65535) return 2;
    FString ShutdownFile; FParse::Value(FCommandLine::Get(),TEXT("ShutdownFile="),ShutdownFile);
    // HTTPServer is an internal listener. A production HTTPS reverse proxy is the public endpoint.
    GConfig->SetString(TEXT("HTTPServer.Listeners"),TEXT("DefaultBindAddress"),TEXT("127.0.0.1"),GEngineIni);
    FHttpServerModule& HTTP=FHttpServerModule::Get();
    auto Router=HTTP.GetHttpRouter(Port,true); if(!Router.IsValid()) return 3;
    FWorkQueue Work; bool bAccepting=true;
    auto Bind=[&](const TCHAR* Path,EHttpServerRequestVerbs Verbs)
    {
        auto Handle=Router->BindRoute(FHttpPath(Path),Verbs,FHttpRequestHandler::CreateLambda(
            [&,Route=FString(Path)](const FHttpServerRequest& In,const FHttpResultCallback& Complete)
            {
                auto Reject=[&](int32 Status,const TCHAR* Code,const TCHAR* Message) { FWorkQueue::Send(MMO::FReply::Error(Status,Code,Message),Complete); return true; };
                if(!bAccepting) return Reject(503,TEXT("shutting_down"),TEXT("The service is draining requests."));
                if(In.Body.Num()>MMO::MaxBodyBytes) return Reject(413,TEXT("body_too_large"),TEXT("Body is limited to 64 KiB."));
                MMO::FRequest Request; Request.Route=Route; Request.Method=In.Verb==EHttpServerRequestVerbs::VERB_GET?TEXT("GET"):TEXT("POST"); Request.Bearer=Bearer(In);
                if(In.Body.Num())
                {
                    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(In.Body.GetData()),In.Body.Num());
                    Request.Body=MMO::ParseJson(FString(Converted.Length(),Converted.Get()));
                    if(!Request.Body.IsValid()) return Reject(400,TEXT("invalid_json"),TEXT("A JSON object is required."));
                }
                if(!Work.Enqueue(MoveTemp(Request),Complete)) return Reject(503,TEXT("busy"),TEXT("The bounded worker queue is full; retry later."));
                return true;
            }));
        check(Handle.IsValid());
    };
    Bind(TEXT("/health"),EHttpServerRequestVerbs::VERB_GET);
    Bind(MMO::Characters,EHttpServerRequestVerbs::VERB_GET|EHttpServerRequestVerbs::VERB_POST);
    for(const TCHAR* Route:{MMO::Register,MMO::Login,MMO::Logout,MMO::JoinTicket,MMO::ConsumeTicket,MMO::Load,MMO::Save,MMO::Heartbeat,MMO::Release})
        Bind(Route,EHttpServerRequestVerbs::VERB_POST);
    HTTP.StartAllListeners();
    UE_LOG(LogMMOBackend,Display,TEXT("MMO backend listening on 127.0.0.1:%d; 2 workers, 32 request capacity."),Port);
    double Previous=FPlatformTime::Seconds();
    while(!IsEngineExitRequested())
    {
        double Now=FPlatformTime::Seconds(); FTSTicker::GetCoreTicker().Tick(static_cast<float>(Now-Previous)); Previous=Now;
        Work.Pump();
        if(!ShutdownFile.IsEmpty() && IFileManager::Get().FileExists(*ShutdownFile)) break;
        FPlatformProcess::Sleep(0.005f);
    }
    bAccepting=false;
    // Drain accepted transactions before stopping the listeners. SQL/connection timeouts bound each job.
    while(Work.Num()>0)
    {
        Work.Pump(); FTSTicker::GetCoreTicker().Tick(0.01f); FPlatformProcess::Sleep(0.01f);
    }
    const double FlushUntil=FPlatformTime::Seconds()+0.2;
    while(FPlatformTime::Seconds()<FlushUntil) { FTSTicker::GetCoreTicker().Tick(0.01f); FPlatformProcess::Sleep(0.01f); }
    HTTP.StopAllListeners();
    UE_LOG(LogMMOBackend,Display,TEXT("Accepted operations drained; backend shutdown complete."));
    return 0;
}
