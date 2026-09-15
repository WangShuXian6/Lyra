#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// Accept the original lab or a new project explicitly enrolled by Advance-Lesson.ps1.
// Asset-specific type/path checks remain at every operation's call site.
inline bool IsMMODocProject()
{
    if (!FPaths::GetCleanFilename(FPaths::GetProjectFilePath()).Equals(TEXT("MMORPG.uproject"), ESearchCase::IgnoreCase)) return false;
    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectDir);
    if (ProjectDir.Equals(TEXT("F:/UE/LyraDocLabs/MMORPG"), ESearchCase::IgnoreCase)) return true;
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(ProjectDir, TEXT(".lesson-state.json")))) return false;
    TSharedPtr<FJsonObject> State;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), State) || !State) return false;
    FString Destination; double Stage = -1;
    if (!State->TryGetStringField(TEXT("destination"), Destination) || !State->TryGetNumberField(TEXT("stage"), Stage) || Stage < 3) return false;
    Destination = FPaths::ConvertRelativePathToFull(Destination);
    FPaths::NormalizeDirectoryName(Destination);
    return Destination.Equals(ProjectDir, ESearchCase::IgnoreCase);
}
