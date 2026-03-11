#include "test2SheetImporterEditorModule.h"

#include "LevelEditor.h"
#include "SGoogleSheetImporterPanel.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

const FName Ftest2SheetImporterEditorModule::ImporterTabName(TEXT("GoogleSheetImporter"));

void Ftest2SheetImporterEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ImporterTabName,
		FOnSpawnTab::CreateRaw(this, &Ftest2SheetImporterEditorModule::SpawnImporterTab))
		.SetDisplayName(FText::FromString(TEXT("Google Sheet Importer")))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &Ftest2SheetImporterEditorModule::RegisterMenus));
}

void Ftest2SheetImporterEditorModule::ShutdownModule()
{
	if (UToolMenus::TryGet())
	{
		UToolMenus::UnRegisterStartupCallback(this);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImporterTabName);
}

void Ftest2SheetImporterEditorModule::RegisterMenus()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GoogleSheetImporter"));
	Section.AddMenuEntry(
		TEXT("OpenGoogleSheetImporter"),
		FText::FromString(TEXT("Google Sheet Importer")),
		FText::FromString(TEXT("Open Google Sheets to DataTable/Enum importer.")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &Ftest2SheetImporterEditorModule::OpenImporterTab)));
}

TSharedRef<SDockTab> Ftest2SheetImporterEditorModule::SpawnImporterTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SGoogleSheetImporterPanel)
		];
}

void Ftest2SheetImporterEditorModule::OpenImporterTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ImporterTabName);
}

IMPLEMENT_MODULE(Ftest2SheetImporterEditorModule, test2SheetImporterEditor)
