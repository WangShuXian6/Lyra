#include "MMOContracts.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
IMPLEMENT_MODULE(FDefaultModuleImpl, MMOContracts)

namespace MMO
{
FReply FReply::Error(int32 InStatus, const FString& Code, const FString& Message)
{
    FReply R; R.Status = InStatus;
    auto Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("code"), Code); Error->SetStringField(TEXT("message"), Message);
    R.Body->SetObjectField(TEXT("error"), Error); return R;
}
FString ToJson(const TSharedPtr<FJsonObject>& Object)
{
    FString Text; auto Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer); return Text;
}
TSharedPtr<FJsonObject> ParseJson(const FString& Text)
{
    TSharedPtr<FJsonObject> Object;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object); return Object;
}
TSharedPtr<FJsonObject> InitialState()
{
    return ParseJson(TEXT(R"({"level":1,"xp":0,"health":100,"mana":100,"inventory":[],"position":{"x":0,"y":0,"z":150}})"));
}
bool ValidateState(const TSharedPtr<FJsonObject>& State)
{
    if (!State.IsValid()) return false;
    double Level, XP, Health, Mana;
    if (!State->TryGetNumberField(TEXT("level"),Level) || Level < 1 || Level > 1000 || Level != FMath::FloorToDouble(Level)) return false;
    if (!State->TryGetNumberField(TEXT("xp"),XP) || XP < 0 || XP > 1e12 || XP != FMath::FloorToDouble(XP)) return false;
    if (!State->TryGetNumberField(TEXT("health"),Health) || Health < 0 || Health > 1e6) return false;
    if (!State->TryGetNumberField(TEXT("mana"),Mana) || Mana < 0 || Mana > 1e6) return false;
    const TArray<TSharedPtr<FJsonValue>>* Items;
    if (!State->TryGetArrayField(TEXT("inventory"),Items) || Items->Num() > 128) return false;
    TSet<FString> Seen;
    for (const auto& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* Row; FString ID; double Qty;
        if (!Item->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("itemId"),ID) || ID.IsEmpty() || ID.Len()>64 || Seen.Contains(ID)) return false;
        if (!(*Row)->TryGetNumberField(TEXT("quantity"),Qty) || Qty < 1 || Qty > 1000000 || Qty != FMath::FloorToDouble(Qty)) return false;
        Seen.Add(ID);
    }
    const TSharedPtr<FJsonObject>* Position;
    if (!State->TryGetObjectField(TEXT("position"), Position)) return false;
    for (const TCHAR* Axis : {TEXT("x"),TEXT("y"),TEXT("z")})
    {
        double Value; if (!(*Position)->TryGetNumberField(Axis,Value) || !FMath::IsFinite(Value) || FMath::Abs(Value) > 1e8) return false;
    }
    return FMath::IsFinite(Health) && FMath::IsFinite(Mana);
}
}
