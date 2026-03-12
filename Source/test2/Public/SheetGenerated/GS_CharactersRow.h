// AUTO-GENERATED FILE. DO NOT EDIT.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SheetGenerated/GS_Enums.h"
#include "GS_CharactersRow.generated.h"

USTRUCT(BlueprintType)
struct FCharactersRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EEnumsCharacter type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Price;

};
