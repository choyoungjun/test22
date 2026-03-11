#include "SGoogleSheetImporterPanel.h"

#include "GoogleSheetImporterService.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

void SGoogleSheetImporterPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(4.0f)
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Validate")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnValidateClicked)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Generate Code Only")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnGenerateCodeOnlyClicked)
			]
			+ SUniformGridPanel::Slot(2, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Create/Update Assets")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnCreateAssetsClicked)
			]
			+ SUniformGridPanel::Slot(3, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Import")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnImportClicked)
			]
			+ SUniformGridPanel::Slot(0, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Regenerate Project Files")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnRegenerateProjectFilesClicked)
			]
			+ SUniformGridPanel::Slot(1, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Build Editor")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnBuildEditorClicked)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SAssignNew(LogTextBox, SMultiLineEditableTextBox)
			.IsReadOnly(true)
			.AlwaysShowScrollbars(true)
		]
	];

	AppendLogLine(TEXT("Google Sheet Importer ready."));
}

FReply SGoogleSheetImporterPanel::OnValidateClicked()
{
	AppendLogLine(TEXT("== Validate =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::ValidateOnly));
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnGenerateCodeOnlyClicked()
{
	AppendLogLine(TEXT("== Generate Code Only =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::GenerateCodeOnly));
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnCreateAssetsClicked()
{
	AppendLogLine(TEXT("== Create/Update Assets =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::CreateOrUpdateAssets));
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnImportClicked()
{
	AppendLogLine(TEXT("== Full Import =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::FullImport));
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnRegenerateProjectFilesClicked()
{
	FString Message;
	FGoogleSheetImporterService::RegenerateProjectFiles(Message);
	AppendLogLine(Message);
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnBuildEditorClicked()
{
	FString Message;
	FGoogleSheetImporterService::BuildEditorTarget(Message);
	AppendLogLine(Message);
	return FReply::Handled();
}

void SGoogleSheetImporterPanel::AppendLogLine(const FString& Message)
{
	LogBuffer += Message + LINE_TERMINATOR;
	if (LogTextBox.IsValid())
	{
		LogTextBox->SetText(FText::FromString(LogBuffer));
	}
}

void SGoogleSheetImporterPanel::AppendImportResult(const FGoogleSheetImportResult& Result)
{
	for (const FString& Message : Result.Messages)
	{
		AppendLogLine(Message);
	}

	AppendLogLine(FString::Printf(
		TEXT("Result: %s | Success=%d Error=%d"),
		Result.bSuccess ? TEXT("OK") : TEXT("FAILED"),
		Result.SuccessCount,
		Result.ErrorCount));
}
