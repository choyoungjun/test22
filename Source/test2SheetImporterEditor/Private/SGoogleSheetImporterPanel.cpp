#include "SGoogleSheetImporterPanel.h"

#include "GoogleSheetImporterService.h"
#include "GoogleSheetImporterSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
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
				.Text(FText::FromString(TEXT("Sync Sheets From Document")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnSyncSheetsClicked)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Import All Tables")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnImportClicked)
			]
			+ SUniformGridPanel::Slot(0, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Validate")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnValidateClicked)
			]
			+ SUniformGridPanel::Slot(1, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Generate Code Only")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnGenerateCodeOnlyClicked)
			]
			+ SUniformGridPanel::Slot(2, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Create/Update Assets")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnCreateAssetsClicked)
			]
			+ SUniformGridPanel::Slot(3, 1)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Regenerate Project Files")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnRegenerateProjectFilesClicked)
			]
			+ SUniformGridPanel::Slot(0, 2)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Build Editor")))
				.OnClicked(this, &SGoogleSheetImporterPanel::OnBuildEditorClicked)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.FillHeight(0.65f)
		.Padding(8.0f)
		[
			SNew(SBorder)
			.Padding(4.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(SheetListBox, SVerticalBox)
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(0.35f)
		.Padding(8.0f)
		[
			SAssignNew(LogTextBox, SMultiLineEditableTextBox)
			.IsReadOnly(true)
			.AlwaysShowScrollbars(true)
		]
	];

	AppendLogLine(TEXT("Google Sheet Importer ready."));
	RefreshSheetList();
}

FReply SGoogleSheetImporterPanel::OnSyncSheetsClicked()
{
	AppendLogLine(TEXT("== Sync Sheets From Document =="));
	FGoogleSheetImportResult Result;
	FGoogleSheetImporterService::SyncSheetsFromDocument(Result);
	AppendImportResult(Result);
	RefreshSheetList();
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnValidateClicked()
{
	AppendLogLine(TEXT("== Validate =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::ValidateOnly));
	RefreshSheetList();
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnGenerateCodeOnlyClicked()
{
	AppendLogLine(TEXT("== Generate Code Only =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::GenerateCodeOnly));
	RefreshSheetList();
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnCreateAssetsClicked()
{
	AppendLogLine(TEXT("== Create/Update Assets =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::CreateOrUpdateAssets));
	RefreshSheetList();
	return FReply::Handled();
}

FReply SGoogleSheetImporterPanel::OnImportClicked()
{
	AppendLogLine(TEXT("== Import All Tables =="));
	AppendImportResult(FGoogleSheetImporterService::Execute(EGoogleSheetImportAction::FullImport));
	RefreshSheetList();
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

void SGoogleSheetImporterPanel::RefreshSheetList()
{
	if (!SheetListBox.IsValid())
	{
		return;
	}

	SheetListBox->ClearChildren();

	const UGoogleSheetImporterSettings* Settings = GetDefault<UGoogleSheetImporterSettings>();
	const TArray<FGoogleSheetDefinition> Sheets = FGoogleSheetImporterService::GetConfiguredSheets();

	if (Settings != nullptr)
	{
		int32 DocumentCount = 0;
		for (const FString& Item : Settings->DocumentIds)
		{
			if (!Item.TrimStartAndEnd().IsEmpty())
			{
				DocumentCount++;
			}
		}
		// if (DocumentCount == 0 && !Settings->DocumentId.TrimStartAndEnd().IsEmpty())
		// {
		// 	DocumentCount = 1;
		// }

		SheetListBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Documents: %d | Legacy DocumentId: %s | Sheets: %d"),
				DocumentCount,
				*Settings->DocumentIds[0],
				Sheets.Num())))
		];
	}

	if (Sheets.Num() == 0)
	{
		SheetListBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No sheets configured. Use 'Sync Sheets From Document'.")))
		];
		return;
	}

	for (const FGoogleSheetDefinition& Sheet : Sheets)
	{
		const bool bIsTable = (Sheet.DefinitionType == EGoogleSheetDefinitionType::Table);
		const FLinearColor RowColor = bIsTable
			? FLinearColor(0.05f, 0.30f, 0.05f, 0.65f)
			: FLinearColor(0.08f, 0.08f, 0.08f, 0.65f);
		const FString TypeLabel = bIsTable ? TEXT("Table") : TEXT("Enum");
		const FString SourceDocumentId = Sheet.SourceDocumentId.TrimStartAndEnd().IsEmpty() ? TEXT("(default)") : Sheet.SourceDocumentId;

		SheetListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(RowColor)
			.Padding(2.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(FText::FromString(Sheet.SheetName))
				.BodyContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(
						TEXT("Type: %s  |  DocumentId: %s  |  Gid: %s  |  Output: %s"),
						*TypeLabel,
						*SourceDocumentId,
						*Sheet.Gid,
						*Sheet.AssetOutputPath)))
				]
			]
		];
	}
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
