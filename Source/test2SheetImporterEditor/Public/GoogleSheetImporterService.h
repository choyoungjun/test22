#pragma once

#include "CoreMinimal.h"
#include "GoogleSheetImporterSettings.h"

enum class EGoogleSheetImportAction : uint8
{
	ValidateOnly,
	GenerateCodeOnly,
	CreateOrUpdateAssets,
	FullImport
};

struct FGoogleSheetImportResult
{
	bool bSuccess = true;
	int32 SuccessCount = 0;
	int32 ErrorCount = 0;
	TArray<FString> Messages;
};

class TEST2SHEETIMPORTEREDITOR_API FGoogleSheetImporterService
{
public:
	static FGoogleSheetImportResult Execute(EGoogleSheetImportAction Action);
	static bool SyncSheetsFromDocument(FGoogleSheetImportResult& OutResult);
	static TArray<FGoogleSheetDefinition> GetConfiguredSheets();
	static bool RegenerateProjectFiles(FString& OutMessage);
	static bool BuildEditorTarget(FString& OutMessage);
};
