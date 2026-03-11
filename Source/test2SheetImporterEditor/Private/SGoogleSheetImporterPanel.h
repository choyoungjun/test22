#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableTextBox;
class SVerticalBox;

class SGoogleSheetImporterPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGoogleSheetImporterPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnSyncSheetsClicked();
	FReply OnValidateClicked();
	FReply OnGenerateCodeOnlyClicked();
	FReply OnCreateAssetsClicked();
	FReply OnImportClicked();
	FReply OnRegenerateProjectFilesClicked();
	FReply OnBuildEditorClicked();

	void RefreshSheetList();
	void AppendLogLine(const FString& Message);
	void AppendImportResult(const struct FGoogleSheetImportResult& Result);

private:
	TSharedPtr<SVerticalBox> SheetListBox;
	TSharedPtr<SMultiLineEditableTextBox> LogTextBox;
	FString LogBuffer;
};
