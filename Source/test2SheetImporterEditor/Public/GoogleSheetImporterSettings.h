#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GoogleSheetImporterSettings.generated.h"

UENUM()
enum class EGoogleSheetDefinitionType : uint8
{
	Enum,
	Table
};

USTRUCT()
struct FGoogleSheetDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Google Sheet")
	FString SheetName;

	UPROPERTY(EditAnywhere, Config, Category = "Google Sheet")
	FString Gid;

	UPROPERTY(EditAnywhere, Config, Category = "Google Sheet")
	EGoogleSheetDefinitionType DefinitionType = EGoogleSheetDefinitionType::Table;

	UPROPERTY(EditAnywhere, Config, Category = "Output")
	FString AssetOutputPath = TEXT("/Game/Data/Generated");
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Google Sheet Importer"))
class TEST2SHEETIMPORTEREDITOR_API UGoogleSheetImporterSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(EditAnywhere, Config, Category = "Google Sheet")
	FString DocumentId;

	UPROPERTY(EditAnywhere, Config, Category = "Google Sheet")
	TArray<FGoogleSheetDefinition> Sheets;

	UPROPERTY(EditAnywhere, Config, Category = "Output")
	FString CodeOutputDirectory = TEXT("Source/test2/Public/SheetGenerated");

	UPROPERTY(EditAnywhere, Config, Category = "Output")
	FString DefaultDataAssetOutputPath = TEXT("/Game/Data/Generated");
};
