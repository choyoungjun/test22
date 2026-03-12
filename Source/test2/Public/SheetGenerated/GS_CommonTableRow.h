// AUTO-GENERATED FILE. DO NOT EDIT.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SheetGenerated/GS_Enums.h"
#include "GS_CommonTableRow.generated.h"

USTRUCT(BlueprintType)
struct FCommonTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText name;

};
