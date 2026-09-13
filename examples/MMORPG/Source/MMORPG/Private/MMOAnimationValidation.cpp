#include "MMOGameplay.h"

#if !UE_BUILD_SHIPPING
#include "Animation/AnimInstance.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#endif

void AMMOPlayerController::ShowcaseMoveTick()
{
#if !UE_BUILD_SHIPPING
    if (GetWorld()->GetTimeSeconds() - SmokeStart >= 26) return;
    if (auto* TestCharacter = Cast<AMMOCharacter>(GetPawn()))
        TestCharacter->AddMovementInput(FVector::ForwardVector, .25f);
    // Movement input is consumed each frame; a 10 Hz showcase timer would create isolated impulses.
    GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this] { ShowcaseMoveTick(); }));
#endif
}

void AMMOPlayerController::RecordLocomotionState(const FString& Stage)
{
#if !UE_BUILD_SHIPPING
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("stage"), Stage);
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetStringField(TEXT("method"), TEXT("Read the evaluated Linked Anim Layer BlendSpace player after joining its parallel evaluation task."));
    auto* TestCharacter = Cast<AMMOCharacter>(GetPawn());
    auto* Mesh = TestCharacter ? TestCharacter->GetMesh() : nullptr;
    if (Mesh)
    {
        // Development evidence only, twice per showcase. Normal animation keeps its parallel path.
        Mesh->HandleExistingParallelEvaluationTask(true, true);
        Report->SetNumberField(TEXT("pawnSpeed"), TestCharacter->GetVelocity().Size2D());
        bool bFound = false;
        const USkeletalMeshComponent* ReadOnlyMesh = Mesh;
        for (UAnimInstance* LayerInstance : ReadOnlyMesh->GetLinkedAnimInstances())
        {
            if (!LayerInstance) continue;
            for (TFieldIterator<FStructProperty> It(LayerInstance->GetClass()); It; ++It)
            {
                if (!It->Struct->IsChildOf(FAnimNode_BlendSpacePlayer::StaticStruct())) continue;
                const auto* Node = It->ContainerPtrToValuePtr<FAnimNode_BlendSpacePlayer>(LayerInstance);
                UBlendSpace* BlendSpace = Node->GetBlendSpace();
                if (!BlendSpace || BlendSpace->GetName() != TEXT("BS_Idle_Walk_Run")) continue;
                const FVector Position = Node->GetPosition();
                const FVector Filtered = Node->GetFilteredPosition();
                Report->SetStringField(TEXT("layerClass"), LayerInstance->GetClass()->GetPathName());
                Report->SetStringField(TEXT("nodeProperty"), It->GetName());
                Report->SetStringField(TEXT("blendSpace"), BlendSpace->GetPathName());
                Report->SetStringField(TEXT("axisX"), BlendSpace->GetBlendParameter(0).DisplayName);
                Report->SetStringField(TEXT("axisY"), BlendSpace->GetBlendParameter(1).DisplayName);
                Report->SetNumberField(TEXT("inputX"), Position.X);
                Report->SetNumberField(TEXT("inputY"), Position.Y);
                Report->SetNumberField(TEXT("filteredY"), Filtered.Y);
                Report->SetNumberField(TEXT("blendWeight"), Node->GetCachedBlendWeight());
                Report->SetNumberField(TEXT("assetTime"), Node->GetCurrentAssetTime());
                Report->SetNumberField(TEXT("assetLength"), Node->GetCurrentAssetLength());
                Report->SetBoolField(TEXT("passed"), Position.Y > 1.f && Filtered.Y > 1.f && Node->GetCachedBlendWeight() > .1f
                    && BlendSpace->GetBlendParameter(1).DisplayName.Equals(TEXT("Speed"), ESearchCase::IgnoreCase));
                bFound = true;
                break;
            }
            if (bFound) break;
        }
    }
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    UE_LOG(LogTemp, Display, TEXT("MMO_LOCOMOTION_RESULT %s"), *Text);
#endif
}
