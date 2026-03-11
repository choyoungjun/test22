#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

struct FToolMenuSection;

class Ftest2SheetImporterEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnImporterTab(const class FSpawnTabArgs& SpawnTabArgs);
	void OpenImporterTab();

private:
	static const FName ImporterTabName;
};
