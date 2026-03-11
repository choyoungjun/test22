// AUTO-GENERATED FILE. DO NOT EDIT.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SheetGenerated/GS_Enums.h"
#include "GS_MonstersRow.generated.h"

USTRUCT(BlueprintType)
struct FMonstersRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 value;

};
