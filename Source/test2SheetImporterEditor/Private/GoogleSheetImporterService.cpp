#include "GoogleSheetImporterService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "GoogleSheetImporterSettings.h"
#include "HAL/PlatformFileManager.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Misc/ScopedSlowTask.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace GoogleSheetImporter
{
	struct FParsedEnumEntry
	{
		FString Name;
		bool bHasValue = false;
		int32 Value = 0;
		FString DisplayName;
		FString Comment;
	};

	struct FParsedEnumSheet
	{
		FString SheetName;
		FString EnumName;
		FString AssetName;
		FString AssetOutputPath;
		bool bCreateAsUserDefinedAsset = false;
		TArray<FParsedEnumEntry> Entries;
	};

	struct FParsedTableColumn
	{
		FString Name;
		FString Type;
		FString SourceHeader;
		bool bIsNestedField = false;
		FString NestedMemberName;
		FString NestedFieldName;
		FString NestedStructType;
		FString NestedStructSheetName;
	};

	struct FParsedTableSheet
	{
		FString SheetName;
		FString StructName;
		TArray<FParsedTableColumn> Columns;
		TArray<TArray<FString>> Rows;
		int32 RowNameColumnIndex = INDEX_NONE;
		bool bHasExplicitRowNameColumn = false;
		FString AssetOutputPath;
	};

	struct FParsedWorkbook
	{
		TArray<FParsedEnumSheet> NativeEnums;
		TArray<FParsedEnumSheet> UserDefinedEnums;
		TArray<FParsedTableSheet> Tables;
	};

	struct FNestedColumnCandidate
	{
		int32 ColumnIndex = INDEX_NONE;
		FString MemberName;
		FString FieldName;
		bool bExplicitDotNotation = false;
	};

	static FString MakeSheetUrl(const FString &DocumentId, const FString &Gid)
	{
		return FString::Printf(TEXT("https://docs.google.com/spreadsheets/d/%s/export?format=csv&gid=%s"), *DocumentId, *Gid);
	}

	static FString MakePublicWorksheetFeedUrl(const FString& DocumentId)
	{
		return FString::Printf(TEXT("https://spreadsheets.google.com/feeds/worksheets/%s/public/basic?alt=json"), *DocumentId);
	}

	static FString MakeSpreadsheetEditUrl(const FString& DocumentId)
	{
		return FString::Printf(TEXT("https://docs.google.com/spreadsheets/d/%s/edit?usp=sharing"), *DocumentId);
	}

	static EGoogleSheetDefinitionType GuessDefinitionType(const FString& SheetName)
	{
		const FString Lower = SheetName.ToLower();
		if (Lower.StartsWith(TEXT("enum")) || Lower.StartsWith(TEXT("enums")) || Lower.StartsWith(TEXT("uenum")))
		{
			return EGoogleSheetDefinitionType::Enum;
		}
		return EGoogleSheetDefinitionType::Table;
	}

	static bool IsUserDefinedEnumSheetName(const FString& SheetName)
	{
		return SheetName.TrimStartAndEnd().ToLower().StartsWith(TEXT("uenum"));
	}

	static FString GetEnumSheetBaseName(const FString& SheetName)
	{
		FString Name = SheetName.TrimStartAndEnd();
		FString Lower = Name.ToLower();
		if (Lower.StartsWith(TEXT("uenum")))
		{
			int32 StartIndex = 5; // "uenum"
			if (Name.Len() > StartIndex && Name[StartIndex] == TEXT('_'))
			{
				StartIndex++;
			}
			return Name.Mid(StartIndex);
		}
		if (Lower.StartsWith(TEXT("enum")))
		{
			int32 StartIndex = 4; // "enum"
			if (Name.Len() > StartIndex && Name[StartIndex] == TEXT('_'))
			{
				StartIndex++;
			}
			return Name.Mid(StartIndex);
		}
		return Name;
	}

	static FString ToSafeIdentifier(const FString &Input)
	{
		FString Result;
		Result.Reserve(Input.Len() + 2);

		for (int32 Index = 0; Index < Input.Len(); ++Index)
		{
			TCHAR C = Input[Index];
			if (FChar::IsAlnum(C) || C == TEXT('_'))
			{
				Result.AppendChar(C);
			}
			else if (!Result.IsEmpty() && Result[Result.Len() - 1] != TEXT('_'))
			{
				Result.AppendChar(TEXT('_'));
			}
		}

		while (!Result.IsEmpty() && Result.EndsWith(TEXT("_")))
		{
			Result.LeftChopInline(1, false);
		}

		if (Result.IsEmpty() || (!FChar::IsAlpha(Result[0]) && Result[0] != TEXT('_')))
		{
			Result = FString(TEXT("_")) + Result;
		}

		return Result;
	}

	static FString ToPascalCaseIdentifier(const FString &Input)
	{
		const FString Sanitized = ToSafeIdentifier(Input);
		TArray<FString> Parts;
		Sanitized.ParseIntoArray(Parts, TEXT("_"), true);

		FString Result;
		for (const FString &Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}

			FString Normalized = Part;
			Normalized[0] = FChar::ToUpper(Normalized[0]);
			Result += Normalized;
		}

		if (Result.IsEmpty())
		{
			Result = TEXT("Generated");
		}

		if (!FChar::IsAlpha(Result[0]))
		{
			Result = FString(TEXT("N")) + Result;
		}

		return Result;
	}

	static void AddDocumentIdIfValid(const FString& Candidate, TArray<FString>& OutDocumentIds, TSet<FString>& Seen)
	{
		const FString Trimmed = Candidate.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Seen.Contains(Trimmed))
		{
			return;
		}

		Seen.Add(Trimmed);
		OutDocumentIds.Add(Trimmed);
	}

	static TArray<FString> GetConfiguredDocumentIds(const UGoogleSheetImporterSettings* Settings)
	{
		TArray<FString> DocumentIds;
		TSet<FString> Seen;
		if (Settings == nullptr)
		{
			return DocumentIds;
		}

		for (const FString& Item : Settings->DocumentIds)
		{
			AddDocumentIdIfValid(Item, DocumentIds, Seen);
		}

		if (DocumentIds.Num() == 0)
		{
			return DocumentIds;
			// FString Legacy = Settings->DocumentId;
			// Legacy.ReplaceInline(TEXT(","), TEXT("\n"));
			// Legacy.ReplaceInline(TEXT(";"), TEXT("\n"));
			// Legacy.ReplaceInline(TEXT("\t"), TEXT("\n"));

			// TArray<FString> Tokens;
			// Legacy.ParseIntoArrayLines(Tokens, true);
			
			// for (const FString& Token : Tokens)
			// {
			// 	AddDocumentIdIfValid(Token, DocumentIds, Seen);
			// }

			// AddDocumentIdIfValid(Settings->DocumentId, DocumentIds, Seen);
		}

		return DocumentIds;
	}

	static FString ResolveSheetDocumentId(const FGoogleSheetDefinition& Definition, const FString& DefaultDocumentId)
	{
		const FString FromSheet = Definition.SourceDocumentId.TrimStartAndEnd();
		if (!FromSheet.IsEmpty())
		{
			return FromSheet;
		}
		return DefaultDocumentId.TrimStartAndEnd();
	}

	static bool ParseCsv(const FString &CsvContent, TArray<TArray<FString>> &OutRows)
	{
		OutRows.Reset();
		TArray<FString> CurrentRow;
		FString CurrentCell;
		bool bInQuotes = false;

		for (int32 Index = 0; Index < CsvContent.Len(); ++Index)
		{
			const TCHAR C = CsvContent[Index];

			if (bInQuotes)
			{
				if (C == TEXT('"'))
				{
					const bool bEscapedQuote = (Index + 1 < CsvContent.Len() && CsvContent[Index + 1] == TEXT('"'));
					if (bEscapedQuote)
					{
						CurrentCell.AppendChar(TEXT('"'));
						++Index;
					}
					else
					{
						bInQuotes = false;
					}
				}
				else
				{
					CurrentCell.AppendChar(C);
				}

				continue;
			}

			switch (C)
			{
			case TEXT('"'):
				bInQuotes = true;
				break;
			case TEXT(','):
				CurrentRow.Add(CurrentCell);
				CurrentCell.Empty();
				break;
			case TEXT('\r'):
				break;
			case TEXT('\n'):
				CurrentRow.Add(CurrentCell);
				CurrentCell.Empty();
				OutRows.Add(CurrentRow);
				CurrentRow.Reset();
				break;
			default:
				CurrentCell.AppendChar(C);
				break;
			}
		}

		CurrentRow.Add(CurrentCell);
		OutRows.Add(CurrentRow);
		return !bInQuotes;
	}

	static FString EscapeCsvCell(const FString &Value)
	{
		if (Value.Contains(TEXT(",")) || Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\"")) || Value.Contains(TEXT("\r")))
		{
			FString Escaped = Value.Replace(TEXT("\""), TEXT("\"\""));
			return FString::Printf(TEXT("\"%s\""), *Escaped);
		}

		return Value;
	}

	static bool IsSupportedType(const FString &TypeName)
	{
		const FString Trimmed = TypeName.TrimStartAndEnd();
		static const TSet<FString> BasicTypes = {
			TEXT("int32"),
			TEXT("float"),
			TEXT("bool"),
			TEXT("FName"),
			TEXT("FString"),
			TEXT("FText")};

		if (BasicTypes.Contains(Trimmed))
		{
			return true;
		}

		if (Trimmed.StartsWith(TEXT("TSoftObjectPtr<")) && Trimmed.EndsWith(TEXT(">")))
		{
			return true;
		}

		if (Trimmed.StartsWith(TEXT("E")))
		{
			return true;
		}

		return false;
	}

	static bool WriteFileIfChanged(const FString &FilePath, const FString &Content, FString &OutMessage)
	{
		FString Existing;
		if (FPaths::FileExists(FilePath) && FFileHelper::LoadFileToString(Existing, *FilePath) && Existing == Content)
		{
			OutMessage = FString::Printf(TEXT("Unchanged: %s"), *FilePath);
			return true;
		}

		if (!FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutMessage = FString::Printf(TEXT("Failed writing file: %s"), *FilePath);
			return false;
		}

		OutMessage = FString::Printf(TEXT("Written: %s"), *FilePath);
		return true;
	}

	static UScriptStruct *FindStructByName(const FString &StructName)
	{
		const FString Target = StructName.TrimStartAndEnd();
		TArray<FString> CandidateNames;
		CandidateNames.Reserve(3);
		CandidateNames.Add(Target);
		if (Target.StartsWith(TEXT("F")) && Target.Len() > 1)
		{
			CandidateNames.Add(Target.Mid(1));
		}
		else if (!Target.IsEmpty())
		{
			CandidateNames.Add(FString(TEXT("F")) + Target);
		}

		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			const FString ReflectionName = It->GetName();
			const FString CppName = It->GetStructCPPName();
			for (const FString& Candidate : CandidateNames)
			{
				if (ReflectionName.Equals(Candidate, ESearchCase::CaseSensitive) ||
					CppName.Equals(Candidate, ESearchCase::CaseSensitive))
				{
					return *It;
				}
			}
		}

		return nullptr;
	}

	static bool FetchTextSync(const FString& Url, FString& OutText, FString& OutError)
	{
		OutText.Empty();
		OutError.Empty();

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));

		bool bDone = false;
		bool bSuccess = false;

		Request->OnProcessRequestComplete().BindLambda(
			[&bDone, &bSuccess, &OutText, &OutError](FHttpRequestPtr /*RequestPtr*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				bDone = true;
				if (!bConnectedSuccessfully || !Response.IsValid())
				{
					OutError = TEXT("HTTP request failed.");
					return;
				}

				if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
				{
					OutError = FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode());
					return;
				}

				OutText = Response->GetContentAsString();
				bSuccess = true;
			});

		if (!Request->ProcessRequest())
		{
			OutError = TEXT("Failed to start HTTP request.");
			return false;
		}

		while (!bDone)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.02f);
			FPlatformProcess::Sleep(0.02f);
		}

		return bSuccess;
	}

	static bool FetchCsvSync(const FString& Url, FString& OutCsv, FString& OutError)
	{
		return FetchTextSync(Url, OutCsv, OutError);
	}

	static bool ExtractGidFromUrl(const FString& Url, FString& OutGid)
	{
		OutGid.Empty();

		const int32 GidIndex = Url.Find(TEXT("gid="), ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (GidIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 ValueStart = GidIndex + 4;
		int32 ValueEnd = ValueStart;
		while (ValueEnd < Url.Len())
		{
			const TCHAR C = Url[ValueEnd];
			if (!FChar::IsDigit(C))
			{
				break;
			}
			++ValueEnd;
		}

		if (ValueEnd <= ValueStart)
		{
			return false;
		}

		OutGid = Url.Mid(ValueStart, ValueEnd - ValueStart);
		return true;
	}

	static bool ParseWorksheetFeed(const FString& JsonText, TArray<FGoogleSheetDefinition>& OutSheets, FString& OutError)
	{
		OutSheets.Reset();
		OutError.Empty();

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			OutError = TEXT("Failed to parse worksheet feed JSON.");
			return false;
		}

		const TSharedPtr<FJsonObject>* FeedObjectPtr = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("feed"), FeedObjectPtr) || FeedObjectPtr == nullptr || !FeedObjectPtr->IsValid())
		{
			OutError = TEXT("Worksheet feed JSON has no 'feed' object.");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!(*FeedObjectPtr)->TryGetArrayField(TEXT("entry"), Entries) || Entries == nullptr)
		{
			OutError = TEXT("Worksheet feed contains no entries.");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
		{
			const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
			if (!EntryObject.IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* TitleObjectPtr = nullptr;
			FString SheetName;
			if (EntryObject->TryGetObjectField(TEXT("title"), TitleObjectPtr) && TitleObjectPtr && TitleObjectPtr->IsValid())
			{
				(*TitleObjectPtr)->TryGetStringField(TEXT("$t"), SheetName);
			}

			if (SheetName.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			FString Gid;
			const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
			if (EntryObject->TryGetArrayField(TEXT("link"), LinkArray) && LinkArray)
			{
				for (const TSharedPtr<FJsonValue>& LinkValue : *LinkArray)
				{
					const TSharedPtr<FJsonObject> LinkObject = LinkValue.IsValid() ? LinkValue->AsObject() : nullptr;
					if (!LinkObject.IsValid())
					{
						continue;
					}

					FString Href;
					if (LinkObject->TryGetStringField(TEXT("href"), Href) && ExtractGidFromUrl(Href, Gid))
					{
						break;
					}
				}
			}

			if (Gid.IsEmpty())
			{
				continue;
			}

			FGoogleSheetDefinition Definition;
			Definition.SheetName = SheetName.TrimStartAndEnd();
			Definition.Gid = Gid;
			Definition.DefinitionType = GuessDefinitionType(Definition.SheetName);
			OutSheets.Add(Definition);
		}

		return OutSheets.Num() > 0;
	}

	static bool ParseSheetDefinitionsFromEditHtml(const FString& HtmlText, TArray<FGoogleSheetDefinition>& OutSheets, FString& OutError)
	{
		OutSheets.Reset();
		OutError.Empty();

		// Captures entries like: [1,0,\"1251765706\",[{\"1\":[[0,0,\"monsters\"]...
		const FRegexPattern Pattern(TEXT("\\[\\d+,0,\\\\\\\"(\\d+)\\\\\\\",\\[\\{\\\\\\\"1\\\\\\\":\\[\\[0,0,\\\\\\\"([^\\\\\\\"]+)\\\\\\\"\\]"));
		FRegexMatcher Matcher(Pattern, HtmlText);

		TSet<FString> SeenGids;
		while (Matcher.FindNext())
		{
			const FString Gid = Matcher.GetCaptureGroup(1);
			const FString SheetName = Matcher.GetCaptureGroup(2).TrimStartAndEnd();

			if (Gid.IsEmpty() || SheetName.IsEmpty() || SeenGids.Contains(Gid))
			{
				continue;
			}
			SeenGids.Add(Gid);

			FGoogleSheetDefinition Definition;
			Definition.SheetName = SheetName;
			Definition.Gid = Gid;
			Definition.DefinitionType = GuessDefinitionType(SheetName);
			OutSheets.Add(Definition);
		}

		if (OutSheets.Num() == 0)
		{
			OutError = TEXT("Could not extract sheet definitions from spreadsheet page HTML.");
			return false;
		}

		return true;
	}

	static FString NormalizeCodeDirectory(const FString &RelativeOrAbsolute)
	{
		if (FPaths::IsRelative(RelativeOrAbsolute))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / RelativeOrAbsolute);
		}
		return RelativeOrAbsolute;
	}

	static bool ParseEnumSheet(
		const FGoogleSheetDefinition &Definition,
		const TArray<TArray<FString>> &Rows,
		FParsedEnumSheet &OutSheet,
		FGoogleSheetImportResult &OutResult)
	{
		if (Rows.Num() < 2)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Enum sheet is empty."), *Definition.SheetName));
			return false;
		}

		const TArray<FString> &Header = Rows[0];
		const int32 NameIdx = Header.IndexOfByPredicate([](const FString &V)
														{ return V.Equals(TEXT("Name"), ESearchCase::IgnoreCase); });
		const int32 ValueIdx = Header.IndexOfByPredicate([](const FString &V)
														 { return V.Equals(TEXT("Value"), ESearchCase::IgnoreCase); });
		const int32 DisplayIdx = Header.IndexOfByPredicate([](const FString &V)
														   { return V.Equals(TEXT("DisplayName"), ESearchCase::IgnoreCase); });
		const int32 CommentIdx = Header.IndexOfByPredicate([](const FString &V)
														   { return V.Equals(TEXT("Comment"), ESearchCase::IgnoreCase); });

		if (NameIdx == INDEX_NONE)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Missing Name column in enum sheet."), *Definition.SheetName));
			return false;
		}

		OutSheet.SheetName = Definition.SheetName;
		OutSheet.bCreateAsUserDefinedAsset = IsUserDefinedEnumSheetName(Definition.SheetName);
		const FString EnumBaseName = GetEnumSheetBaseName(Definition.SheetName);
		const FString PascalEnumBaseName = ToPascalCaseIdentifier(EnumBaseName);
		if (OutSheet.bCreateAsUserDefinedAsset)
		{
			OutSheet.EnumName = TEXT("E") + PascalEnumBaseName;
		}
		else
		{
			// Keep legacy native-enum naming (eg. Enum_Character -> EEnumCharacter).
			OutSheet.EnumName = TEXT("E") + ToPascalCaseIdentifier(Definition.SheetName);
		}
		OutSheet.AssetName = TEXT("Enum") + PascalEnumBaseName;
		OutSheet.AssetOutputPath = Definition.AssetOutputPath;
		if (OutSheet.AssetOutputPath.IsEmpty())
		{
			OutSheet.AssetOutputPath = GetDefault<UGoogleSheetImporterSettings>()->DefaultDataAssetOutputPath;
		}

		int32 NextAutoValue = 0;
		TSet<FString> EntryNames;
		TSet<int32> UsedValues;

		for (int32 RowIndex = 1; RowIndex < Rows.Num(); ++RowIndex)
		{
			const TArray<FString> &Row = Rows[RowIndex];
			if (Row.Num() == 0)
			{
				continue;
			}

			const FString RawName = Row.IsValidIndex(NameIdx) ? Row[NameIdx].TrimStartAndEnd() : FString();
			if (RawName.IsEmpty())
			{
				continue;
			}

			FParsedEnumEntry Entry;
			Entry.Name = ToSafeIdentifier(RawName);
			if (Entry.Name.IsEmpty())
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Invalid enum entry name at row %d."), *Definition.SheetName, RowIndex + 1));
				continue;
			}

			if (EntryNames.Contains(Entry.Name))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Duplicated enum entry name: %s"), *Definition.SheetName, *Entry.Name));
				continue;
			}
			EntryNames.Add(Entry.Name);

			if (ValueIdx != INDEX_NONE && Row.IsValidIndex(ValueIdx) && !Row[ValueIdx].TrimStartAndEnd().IsEmpty())
			{
				Entry.bHasValue = true;
				Entry.Value = FCString::Atoi(*Row[ValueIdx].TrimStartAndEnd());
			}
			else
			{
				while (UsedValues.Contains(NextAutoValue))
				{
					++NextAutoValue;
				}
				Entry.bHasValue = true;
				Entry.Value = NextAutoValue;
				++NextAutoValue;
			}

			if (UsedValues.Contains(Entry.Value))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Duplicate enum value: %d"), *Definition.SheetName, Entry.Value));
				continue;
			}
			UsedValues.Add(Entry.Value);

			if (DisplayIdx != INDEX_NONE && Row.IsValidIndex(DisplayIdx))
			{
				Entry.DisplayName = Row[DisplayIdx].TrimStartAndEnd();
			}
			if (CommentIdx != INDEX_NONE && Row.IsValidIndex(CommentIdx))
			{
				Entry.Comment = Row[CommentIdx].TrimStartAndEnd();
			}

			OutSheet.Entries.Add(Entry);
		}

		if (OutSheet.Entries.Num() == 0)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Enum sheet contains no entries."), *Definition.SheetName));
			return false;
		}

		OutSheet.Entries.Sort([](const FParsedEnumEntry &L, const FParsedEnumEntry &R)
							  { return L.Name < R.Name; });

		return true;
	}

	static bool ParseTableSheet(
		const FGoogleSheetDefinition &Definition,
		const TArray<TArray<FString>> &Rows,
		const TSet<FString> &KnownEnumNames,
		FParsedTableSheet &OutSheet,
		FGoogleSheetImportResult &OutResult)
	{
		if (Rows.Num() < 3)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Table sheet requires at least header/type/data rows."), *Definition.SheetName));
			return false;
		}

		const TArray<FString> &HeaderRow = Rows[0];
		const TArray<FString> &TypeRow = Rows[1];
		if (HeaderRow.Num() == 0 || TypeRow.Num() == 0)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Missing header or type row."), *Definition.SheetName));
			return false;
		}

		OutSheet.SheetName = Definition.SheetName;
		OutSheet.StructName = TEXT("F") + ToPascalCaseIdentifier(Definition.SheetName) + TEXT("Row");
		OutSheet.AssetOutputPath = Definition.AssetOutputPath;

		if (OutSheet.AssetOutputPath.IsEmpty())
		{
			OutSheet.AssetOutputPath = GetDefault<UGoogleSheetImporterSettings>()->DefaultDataAssetOutputPath;
		}

		TSet<FString> SeenHeaders;
		for (int32 Col = 0; Col < HeaderRow.Num(); ++Col)
		{
			const FString Header = ToSafeIdentifier(HeaderRow[Col].TrimStartAndEnd());
			const FString Type = TypeRow.IsValidIndex(Col) ? TypeRow[Col].TrimStartAndEnd() : FString();

			if (Header.IsEmpty())
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Empty header at column %d"), *Definition.SheetName, Col + 1));
				continue;
			}

			if (SeenHeaders.Contains(Header))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Duplicate header: %s"), *Definition.SheetName, *Header));
				continue;
			}
			SeenHeaders.Add(Header);

			if (!IsSupportedType(Type))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Unsupported type for %s: %s"), *Definition.SheetName, *Header, *Type));
				continue;
			}

			if (Type.StartsWith(TEXT("E")) && !KnownEnumNames.Contains(Type))
			{
				if (UEnum *ExistingEnum = FindObject<UEnum>(nullptr, *Type); ExistingEnum == nullptr)
				{
					OutResult.bSuccess = false;
					OutResult.ErrorCount++;
					OutResult.Messages.Add(FString::Printf(TEXT("[%s] Enum type not found: %s"), *Definition.SheetName, *Type));
				}
			}

			FParsedTableColumn Column;
			Column.Name = Header;
			Column.Type = Type;
			Column.SourceHeader = HeaderRow[Col].TrimStartAndEnd();
			OutSheet.Columns.Add(Column);
		}

		if (OutSheet.Columns.Num() == 0)
		{
			return false;
		}

		OutSheet.RowNameColumnIndex = OutSheet.Columns.IndexOfByPredicate([](const FParsedTableColumn &Col)
																		  { return Col.Name.Equals(TEXT("RowName"), ESearchCase::IgnoreCase); });
		OutSheet.bHasExplicitRowNameColumn = (OutSheet.RowNameColumnIndex != INDEX_NONE);

		for (int32 RowIndex = 2; RowIndex < Rows.Num(); ++RowIndex)
		{
			const TArray<FString> &InputRow = Rows[RowIndex];
			if (InputRow.Num() == 0)
			{
				continue;
			}

			TArray<FString> Row;
			Row.SetNum(HeaderRow.Num());
			bool bAnyValue = false;
			for (int32 Col = 0; Col < Row.Num(); ++Col)
			{
				if (InputRow.IsValidIndex(Col))
				{
					Row[Col] = InputRow[Col];
					if (!Row[Col].TrimStartAndEnd().IsEmpty())
					{
						bAnyValue = true;
					}
				}
			}

			if (!bAnyValue)
			{
				continue;
			}

			if (OutSheet.bHasExplicitRowNameColumn)
			{
				const FString RowNameValue = Row.IsValidIndex(OutSheet.RowNameColumnIndex) ? Row[OutSheet.RowNameColumnIndex].TrimStartAndEnd() : FString();
				if (RowNameValue.IsEmpty())
				{
					OutResult.bSuccess = false;
					OutResult.ErrorCount++;
					OutResult.Messages.Add(FString::Printf(TEXT("[%s] Empty RowName at data row %d"), *Definition.SheetName, RowIndex + 1));
					continue;
				}
			}

			OutSheet.Rows.Add(Row);
		}

		return OutSheet.Rows.Num() > 0;
	}

	static bool TryExtractNestedParts(const FParsedTableColumn& Column, FString& OutMember, FString& OutField, bool& bOutExplicitDotNotation)
	{
		OutMember.Empty();
		OutField.Empty();
		bOutExplicitDotNotation = false;

		const FString TrimmedSourceHeader = Column.SourceHeader.TrimStartAndEnd();
		const int32 DotIndex = TrimmedSourceHeader.Find(TEXT("."));
		if (DotIndex != INDEX_NONE && DotIndex > 0 && DotIndex + 1 < TrimmedSourceHeader.Len())
		{
			const FString RawMember = TrimmedSourceHeader.Left(DotIndex);
			const FString RawField = TrimmedSourceHeader.Mid(DotIndex + 1);
			const FString Member = ToSafeIdentifier(RawMember);
			const FString Field = ToSafeIdentifier(RawField);
			if (!Member.IsEmpty() && !Field.IsEmpty())
			{
				OutMember = Member;
				OutField = Field;
				bOutExplicitDotNotation = true;
				return true;
			}
		}

		const int32 UnderscoreIndex = Column.Name.Find(TEXT("_"));
		if (UnderscoreIndex != INDEX_NONE && UnderscoreIndex > 0 && UnderscoreIndex + 1 < Column.Name.Len())
		{
			OutMember = ToSafeIdentifier(Column.Name.Left(UnderscoreIndex));
			OutField = ToSafeIdentifier(Column.Name.Mid(UnderscoreIndex + 1));
			return !OutMember.IsEmpty() && !OutField.IsEmpty();
		}

		return false;
	}

	static const FParsedTableColumn* FindColumnByName(const FParsedTableSheet& Table, const FString& ColumnName)
	{
		for (int32 Col = 0; Col < Table.Columns.Num(); ++Col)
		{
			if (Col == Table.RowNameColumnIndex)
			{
				continue;
			}

			if (Table.Columns[Col].Name.Equals(ColumnName, ESearchCase::IgnoreCase))
			{
				return &Table.Columns[Col];
			}
		}

		return nullptr;
	}

	static void ApplyBaseTableComposition(FParsedWorkbook& Workbook, FGoogleSheetImportResult& OutResult)
	{
		TMap<FString, int32> TableIndexBySafeSheetName;
		for (int32 TableIndex = 0; TableIndex < Workbook.Tables.Num(); ++TableIndex)
		{
			const FString SafeSheetName = ToSafeIdentifier(Workbook.Tables[TableIndex].SheetName).ToLower();
			if (!SafeSheetName.IsEmpty() && !TableIndexBySafeSheetName.Contains(SafeSheetName))
			{
				TableIndexBySafeSheetName.Add(SafeSheetName, TableIndex);
			}
		}

		for (int32 TableIndex = 0; TableIndex < Workbook.Tables.Num(); ++TableIndex)
		{
			FParsedTableSheet& Table = Workbook.Tables[TableIndex];
			TMap<FString, TArray<FNestedColumnCandidate>> GroupedCandidates;

			for (int32 Col = 0; Col < Table.Columns.Num(); ++Col)
			{
				if (Col == Table.RowNameColumnIndex)
				{
					continue;
				}

				FString MemberName;
				FString FieldName;
				bool bExplicitDotNotation = false;
				if (!TryExtractNestedParts(Table.Columns[Col], MemberName, FieldName, bExplicitDotNotation))
				{
					continue;
				}

				FNestedColumnCandidate Candidate;
				Candidate.ColumnIndex = Col;
				Candidate.MemberName = MemberName;
				Candidate.FieldName = FieldName;
				Candidate.bExplicitDotNotation = bExplicitDotNotation;
				GroupedCandidates.FindOrAdd(MemberName).Add(Candidate);
			}

			for (const TPair<FString, TArray<FNestedColumnCandidate>>& Pair : GroupedCandidates)
			{
				const FString& MemberName = Pair.Key;
				const TArray<FNestedColumnCandidate>& Candidates = Pair.Value;

				const bool bAnyDotNotation = Candidates.ContainsByPredicate([](const FNestedColumnCandidate& Candidate)
				{
					return Candidate.bExplicitDotNotation;
				});
				if (!bAnyDotNotation && Candidates.Num() < 2)
				{
					continue;
				}

				const int32* BaseTableIndexPtr = TableIndexBySafeSheetName.Find(MemberName.ToLower());
				if (BaseTableIndexPtr == nullptr || *BaseTableIndexPtr == TableIndex)
				{
					continue;
				}

				const FParsedTableSheet& BaseTable = Workbook.Tables[*BaseTableIndexPtr];
				bool bAllMatch = true;
				for (const FNestedColumnCandidate& Candidate : Candidates)
				{
					const FParsedTableColumn* MatchingBaseColumn = FindColumnByName(BaseTable, Candidate.FieldName);
					if (MatchingBaseColumn == nullptr)
					{
						bAllMatch = false;
						break;
					}

					const FParsedTableColumn& SourceColumn = Table.Columns[Candidate.ColumnIndex];
					if (!SourceColumn.Type.Equals(MatchingBaseColumn->Type, ESearchCase::CaseSensitive))
					{
						bAllMatch = false;
						break;
					}
				}

				if (!bAllMatch)
				{
					continue;
				}

				for (const FNestedColumnCandidate& Candidate : Candidates)
				{
					FParsedTableColumn& SourceColumn = Table.Columns[Candidate.ColumnIndex];
					SourceColumn.bIsNestedField = true;
					SourceColumn.NestedMemberName = MemberName;
					SourceColumn.NestedFieldName = Candidate.FieldName;
					SourceColumn.NestedStructType = BaseTable.StructName;
					SourceColumn.NestedStructSheetName = BaseTable.SheetName;
				}

				OutResult.Messages.Add(FString::Printf(
					TEXT("[%s] Applied BaseTable composition: %s -> %s"),
					*Table.SheetName,
					*MemberName,
					*BaseTable.StructName));
			}
		}
	}

	static FString BuildEnumsHeader(const TArray<FParsedEnumSheet> &EnumSheets)
	{
		FString Out;
		Out += TEXT("// AUTO-GENERATED FILE. DO NOT EDIT.\n");
		Out += TEXT("#pragma once\n\n");
		Out += TEXT("#include \"CoreMinimal.h\"\n");
		Out += TEXT("#include \"GS_Enums.generated.h\"\n\n");

		for (const FParsedEnumSheet &Sheet : EnumSheets)
		{
			Out += TEXT("UENUM(BlueprintType)\n");
			Out += FString::Printf(TEXT("enum class %s : uint8\n"), *Sheet.EnumName);
			Out += TEXT("{\n");
			for (int32 Index = 0; Index < Sheet.Entries.Num(); ++Index)
			{
				const FParsedEnumEntry &Entry = Sheet.Entries[Index];
				FString Meta;
				if (!Entry.DisplayName.IsEmpty())
				{
					Meta += FString::Printf(TEXT("DisplayName=\"%s\""), *Entry.DisplayName.ReplaceCharWithEscapedChar());
				}
				if (!Entry.Comment.IsEmpty())
				{
					if (!Meta.IsEmpty())
					{
						Meta += TEXT(", ");
					}
					Meta += FString::Printf(TEXT("ToolTip=\"%s\""), *Entry.Comment.ReplaceCharWithEscapedChar());
				}

				Out += FString::Printf(TEXT("\t%s = %d"), *Entry.Name, Entry.Value);
				if (!Meta.IsEmpty())
				{
					Out += FString::Printf(TEXT(" UMETA(%s)"), *Meta);
				}
				Out += (Index == Sheet.Entries.Num() - 1) ? TEXT("\n") : TEXT(",\n");
			}
			Out += TEXT("};\n\n");
		}

		return Out;
	}

	static FString BuildTableHeader(const FParsedTableSheet &TableSheet)
	{
		FString Out;
		Out += TEXT("// AUTO-GENERATED FILE. DO NOT EDIT.\n");
		Out += TEXT("#pragma once\n\n");
		Out += TEXT("#include \"CoreMinimal.h\"\n");
		Out += TEXT("#include \"Engine/DataTable.h\"\n");
		Out += TEXT("#include \"SheetGenerated/GS_Enums.h\"\n");
		TArray<FString> NestedIncludes;
		TSet<FString> NestedIncludeSet;
		for (const FParsedTableColumn& Column : TableSheet.Columns)
		{
			if (!Column.bIsNestedField || Column.NestedStructSheetName.IsEmpty())
			{
				continue;
			}

			if (Column.NestedStructType.Equals(TableSheet.StructName, ESearchCase::CaseSensitive))
			{
				continue;
			}

			const FString IncludeLine = FString::Printf(TEXT("#include \"GS_%sRow.h\"\n"), *ToPascalCaseIdentifier(Column.NestedStructSheetName));
			NestedIncludeSet.Add(IncludeLine);
		}
		for (const FString& IncludeLine : NestedIncludeSet)
		{
			NestedIncludes.Add(IncludeLine);
		}
		NestedIncludes.Sort();
		for (const FString& IncludeLine : NestedIncludes)
		{
			Out += IncludeLine;
		}
		Out += FString::Printf(TEXT("#include \"GS_%sRow.generated.h\"\n\n"), *ToPascalCaseIdentifier(TableSheet.SheetName));

		Out += TEXT("USTRUCT(BlueprintType)\n");
		Out += FString::Printf(TEXT("struct %s : public FTableRowBase\n"), *TableSheet.StructName);
		Out += TEXT("{\n");
		Out += TEXT("\tGENERATED_BODY()\n\n");

		TSet<FString> EmittedNestedMembers;
		for (int32 Col = 0; Col < TableSheet.Columns.Num(); ++Col)
		{
			if (Col == TableSheet.RowNameColumnIndex)
			{
				continue;
			}

			const FParsedTableColumn &Column = TableSheet.Columns[Col];
			if (Column.bIsNestedField)
			{
				if (Column.NestedMemberName.IsEmpty() || Column.NestedStructType.IsEmpty() || EmittedNestedMembers.Contains(Column.NestedMemberName))
				{
					continue;
				}

				EmittedNestedMembers.Add(Column.NestedMemberName);
				Out += TEXT("\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n");
				Out += FString::Printf(TEXT("\t%s %s;\n\n"), *Column.NestedStructType, *Column.NestedMemberName);
				continue;
			}

			Out += TEXT("\tUPROPERTY(EditAnywhere, BlueprintReadWrite)\n");
			Out += FString::Printf(TEXT("\t%s %s;\n\n"), *Column.Type, *Column.Name);
		}

		Out += TEXT("};\n");
		return Out;
	}

	static FString BuildCsvForDataTable(const FParsedTableSheet &TableSheet)
	{
		struct FExportColumn
		{
			bool bNestedStruct = false;
			FString HeaderName;
			int32 SourceColumnIndex = INDEX_NONE;
			TArray<int32> NestedColumnIndices;
		};

		auto EscapeStructImportString = [](const FString& InValue) -> FString
		{
			FString Escaped = InValue;
			Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
			Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
			return Escaped;
		};

		auto IsNumericOrBoolOrEnumType = [](const FString& InType) -> bool
		{
			const FString Type = InType.TrimStartAndEnd();
			return Type.Equals(TEXT("int32"), ESearchCase::CaseSensitive) ||
				Type.Equals(TEXT("int64"), ESearchCase::CaseSensitive) ||
				Type.Equals(TEXT("float"), ESearchCase::CaseSensitive) ||
				Type.Equals(TEXT("double"), ESearchCase::CaseSensitive) ||
				Type.Equals(TEXT("bool"), ESearchCase::CaseSensitive) ||
				Type.StartsWith(TEXT("E"));
		};

		auto FormatNestedFieldValue = [&EscapeStructImportString, &IsNumericOrBoolOrEnumType](const FString& InRawValue, const FString& InType) -> FString
		{
			const FString Trimmed = InRawValue.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				return FString();
			}

			if (IsNumericOrBoolOrEnumType(InType))
			{
				return Trimmed;
			}

			return FString::Printf(TEXT("\"%s\""), *EscapeStructImportString(Trimmed));
		};

		TArray<FExportColumn> ExportColumns;
		TMap<FString, int32> NestedColumnByMember;
		for (int32 Col = 0; Col < TableSheet.Columns.Num(); ++Col)
		{
			if (TableSheet.bHasExplicitRowNameColumn && Col == TableSheet.RowNameColumnIndex)
			{
				continue;
			}

			const FParsedTableColumn& Column = TableSheet.Columns[Col];
			if (Column.bIsNestedField && !Column.NestedMemberName.IsEmpty())
			{
				const int32* ExistingIndex = NestedColumnByMember.Find(Column.NestedMemberName);
				if (ExistingIndex == nullptr)
				{
					FExportColumn NewColumn;
					NewColumn.bNestedStruct = true;
					NewColumn.HeaderName = Column.NestedMemberName;
					NewColumn.NestedColumnIndices.Add(Col);
					const int32 NewIndex = ExportColumns.Add(MoveTemp(NewColumn));
					NestedColumnByMember.Add(Column.NestedMemberName, NewIndex);
				}
				else
				{
					ExportColumns[*ExistingIndex].NestedColumnIndices.Add(Col);
				}
			}
			else
			{
				FExportColumn NewColumn;
				NewColumn.bNestedStruct = false;
				NewColumn.HeaderName = Column.Name;
				NewColumn.SourceColumnIndex = Col;
				ExportColumns.Add(MoveTemp(NewColumn));
			}
		}

		FString Out;
		Out += TEXT(",");
		bool bFirst = true;
		for (const FExportColumn& ExportColumn : ExportColumns)
		{
			if (!bFirst)
			{
				Out += TEXT(",");
			}
			bFirst = false;
			Out += EscapeCsvCell(ExportColumn.HeaderName);
		}
		Out += TEXT("\n");

		for (int32 RowIndex = 0; RowIndex < TableSheet.Rows.Num(); ++RowIndex)
		{
			const TArray<FString>& Row = TableSheet.Rows[RowIndex];
			FString RowNameValue;
			if (TableSheet.bHasExplicitRowNameColumn)
			{
				RowNameValue = Row.IsValidIndex(TableSheet.RowNameColumnIndex) ? ToSafeIdentifier(Row[TableSheet.RowNameColumnIndex]) : FString();
			}
			else
			{
				RowNameValue = FString::FromInt(RowIndex + 1);
			}

			Out += EscapeCsvCell(RowNameValue);
			for (const FExportColumn& ExportColumn : ExportColumns)
			{
				FString CellValue;
				if (!ExportColumn.bNestedStruct)
				{
					CellValue = Row.IsValidIndex(ExportColumn.SourceColumnIndex) ? Row[ExportColumn.SourceColumnIndex] : FString();
				}
				else
				{
					FString StructLiteral = TEXT("(");
					bool bFirstField = true;
					for (const int32 NestedColIndex : ExportColumn.NestedColumnIndices)
					{
						if (!TableSheet.Columns.IsValidIndex(NestedColIndex))
						{
							continue;
						}

						const FParsedTableColumn& NestedColumn = TableSheet.Columns[NestedColIndex];
						if (NestedColumn.NestedFieldName.IsEmpty())
						{
							continue;
						}

						const FString RawFieldValue = Row.IsValidIndex(NestedColIndex) ? Row[NestedColIndex] : FString();
						const FString FormattedValue = FormatNestedFieldValue(RawFieldValue, NestedColumn.Type);
						if (FormattedValue.IsEmpty())
						{
							continue;
						}

						if (!bFirstField)
						{
							StructLiteral += TEXT(",");
						}
						bFirstField = false;
						StructLiteral += FString::Printf(TEXT("%s=%s"), *NestedColumn.NestedFieldName, *FormattedValue);
					}
					StructLiteral += TEXT(")");
					CellValue = StructLiteral;
				}

				Out += TEXT(",");
				Out += EscapeCsvCell(CellValue);
			}
			Out += TEXT("\n");
		}

		return Out;
	}

	static bool CreateOrUpdateUserDefinedEnumAsset(const FParsedEnumSheet& EnumSheet, FGoogleSheetImportResult& OutResult)
	{
		if (!EnumSheet.bCreateAsUserDefinedAsset)
		{
			return true;
		}

		FString AssetPath = EnumSheet.AssetOutputPath;
		if (AssetPath.IsEmpty())
		{
			AssetPath = TEXT("/Game/Data/Generated");
		}

		if (!AssetPath.StartsWith(TEXT("/Game")))
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Invalid asset path for UEnum: %s"), *EnumSheet.SheetName, *AssetPath));
			return false;
		}

		const FString EnumAssetName = EnumSheet.AssetName.IsEmpty() ? (TEXT("Enum") + ToPascalCaseIdentifier(GetEnumSheetBaseName(EnumSheet.SheetName))) : EnumSheet.AssetName;
		const FString PackageName = AssetPath / EnumAssetName;
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package == nullptr)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (Package == nullptr)
		{
			Package = CreatePackage(*PackageName);
		}
		if (Package == nullptr)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Failed creating enum package: %s"), *EnumSheet.SheetName, *PackageName));
			return false;
		}
		if (!Package->IsFullyLoaded())
		{
			Package->FullyLoad();
		}

		UUserDefinedEnum* UserEnum = FindObject<UUserDefinedEnum>(Package, *EnumAssetName);
		const bool bIsNewAsset = (UserEnum == nullptr);
		if (UserEnum == nullptr)
		{
			UserEnum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, *EnumAssetName, RF_Public | RF_Standalone));
		}

		if (UserEnum == nullptr)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Failed creating UUserDefinedEnum: %s"), *EnumSheet.SheetName, *EnumAssetName));
			return false;
		}

		TArray<TPair<FName, int64>> EnumNames;
		EnumNames.Reserve(EnumSheet.Entries.Num());
		for (const FParsedEnumEntry& Entry : EnumSheet.Entries)
		{
			const FString EntryName = ToSafeIdentifier(Entry.Name);
			if (EntryName.IsEmpty())
			{
				continue;
			}

			const FString FullName = UserEnum->GenerateFullEnumName(*EntryName);
			EnumNames.Emplace(*FullName, Entry.Value);
		}

		UserEnum->SetEnums(EnumNames, UEnum::ECppForm::Namespaced);
		FEnumEditorUtils::EnsureAllDisplayNamesExist(UserEnum);

		for (int32 Index = 0; Index < EnumSheet.Entries.Num(); ++Index)
		{
			const FString& DisplayName = EnumSheet.Entries[Index].DisplayName;
			if (!DisplayName.IsEmpty())
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(UserEnum, Index, FText::FromString(DisplayName));
			}
		}

		UserEnum->MarkPackageDirty();
		if (bIsNewAsset)
		{
			FAssetRegistryModule::AssetCreated(UserEnum);
		}

		OutResult.SuccessCount++;
		OutResult.Messages.Add(FString::Printf(TEXT("[%s] UserDefinedEnum updated: %s"), *EnumSheet.SheetName, *PackageName));
		return true;
	}

	static bool CreateOrUpdateDataTable(const FParsedTableSheet &TableSheet, FGoogleSheetImportResult &OutResult)
	{
		UScriptStruct *RowStruct = FindStructByName(TableSheet.StructName);
		if (RowStruct == nullptr)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Missing struct %s. Rebuild editor first."), *TableSheet.SheetName, *TableSheet.StructName));
			return false;
		}

		FString AssetPath = TableSheet.AssetOutputPath;
		if (AssetPath.IsEmpty())
		{
			AssetPath = TEXT("/Game/Data/Generated");
		}

		if (!AssetPath.StartsWith(TEXT("/Game")))
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Invalid asset path: %s"), *TableSheet.SheetName, *AssetPath));
			return false;
		}

		const FString DataTableName = TEXT("DT_") + ToPascalCaseIdentifier(TableSheet.SheetName);
		const FString PackageName = AssetPath / DataTableName;
		UPackage *Package = FindPackage(nullptr, *PackageName);
		if (Package == nullptr)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (Package == nullptr)
		{
			Package = CreatePackage(*PackageName);
		}
		if (Package == nullptr)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Failed creating package: %s"), *TableSheet.SheetName, *PackageName));
			return false;
		}
		if (!Package->IsFullyLoaded())
		{
			Package->FullyLoad();
		}

		UDataTable *DataTable = FindObject<UDataTable>(Package, *DataTableName);
		const bool bIsNewAsset = (DataTable == nullptr);
		if (DataTable == nullptr)
		{
			DataTable = NewObject<UDataTable>(Package, *DataTableName, RF_Public | RF_Standalone);
		}

		if (DataTable == nullptr)
		{
			OutResult.bSuccess = false;
			OutResult.ErrorCount++;
			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Failed creating UDataTable object."), *TableSheet.SheetName));
			return false;
		}

		DataTable->RowStruct = RowStruct;
		const FString CsvForImport = BuildCsvForDataTable(TableSheet);
		TArray<FString> ImportProblems = DataTable->CreateTableFromCSVString(CsvForImport);

		if (ImportProblems.Num() > 0)
		{
			OutResult.bSuccess = false;
			for (const FString &Problem : ImportProblems)
			{
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] %s"), *TableSheet.SheetName, *Problem));
			}
			return false;
		}

		DataTable->MarkPackageDirty();
		if (bIsNewAsset)
		{
			FAssetRegistryModule::AssetCreated(DataTable);
		}

		OutResult.SuccessCount++;
		OutResult.Messages.Add(FString::Printf(TEXT("[%s] DataTable updated: %s"), *TableSheet.SheetName, *PackageName));
		return true;
	}
} // namespace GoogleSheetImporter

bool FGoogleSheetImporterService::SyncSheetsFromDocument(FGoogleSheetImportResult& OutResult)
{
	using namespace GoogleSheetImporter;

	OutResult = FGoogleSheetImportResult();

	UGoogleSheetImporterSettings* Settings = GetMutableDefault<UGoogleSheetImporterSettings>();
	if (Settings == nullptr)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorCount++;
		OutResult.Messages.Add(TEXT("Failed to access Google Sheet Importer settings."));
		return false;
	}

	const TArray<FString> DocumentIds = GetConfiguredDocumentIds(Settings);
	if (DocumentIds.Num() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorCount++;
		OutResult.Messages.Add(TEXT("No DocumentId configured."));
		return false;
	}

	TArray<FGoogleSheetDefinition> DiscoveredAll;
	for (const FString& DocumentId : DocumentIds)
	{
		const FString FeedUrl = MakePublicWorksheetFeedUrl(DocumentId);
		FString FeedText;
		FString RequestError;
		TArray<FGoogleSheetDefinition> Discovered;
		FString ParseError;
		const bool bFeedLoaded = FetchTextSync(FeedUrl, FeedText, RequestError);
		const bool bFeedParsed = bFeedLoaded && ParseWorksheetFeed(FeedText, Discovered, ParseError);
		if (!bFeedParsed)
		{
			if (!bFeedLoaded)
			{
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Worksheet feed unavailable: %s (%s)"), *DocumentId, *RequestError, *FeedUrl));
			}
			else
			{
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Worksheet feed parse failed: %s"), *DocumentId, *ParseError));
			}

			const FString HtmlUrl = MakeSpreadsheetEditUrl(DocumentId);
			FString HtmlText;
			FString HtmlError;
			if (!FetchTextSync(HtmlUrl, HtmlText, HtmlError))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Fallback HTML load failed: %s (%s)"), *DocumentId, *HtmlError, *HtmlUrl));
				OutResult.Messages.Add(TEXT("If this spreadsheet is private, publish it to web or configure sheets manually."));
				continue;
			}

			if (!ParseSheetDefinitionsFromEditHtml(HtmlText, Discovered, ParseError))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorCount++;
				OutResult.Messages.Add(FString::Printf(TEXT("[%s] Fallback HTML parse failed: %s"), *DocumentId, *ParseError));
				OutResult.Messages.Add(TEXT("Use manual sheet configuration if auto-discovery is unavailable."));
				continue;
			}

			OutResult.Messages.Add(FString::Printf(TEXT("[%s] Discovered sheets using HTML fallback."), *DocumentId));
		}

		for (FGoogleSheetDefinition& Found : Discovered)
		{
			Found.SourceDocumentId = DocumentId;
			DiscoveredAll.Add(Found);
		}
	}
	if (DiscoveredAll.Num() == 0)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorCount++;
		OutResult.Messages.Add(TEXT("No sheets discovered from configured documents."));
		return false;
	}

	const FString DefaultDocumentId = DocumentIds[0];

	TArray<FGoogleSheetDefinition> Merged;
	Merged.Reserve(DiscoveredAll.Num());
	for (const FGoogleSheetDefinition& Found : DiscoveredAll)
	{
		FGoogleSheetDefinition Definition = Found;
		const FGoogleSheetDefinition* Existing = Settings->Sheets.FindByPredicate([&Found, &DefaultDocumentId](const FGoogleSheetDefinition& Item)
		{
			return Item.Gid == Found.Gid &&
				ResolveSheetDocumentId(Item, DefaultDocumentId).Equals(ResolveSheetDocumentId(Found, DefaultDocumentId), ESearchCase::CaseSensitive);
		});
		if (Existing == nullptr)
		{
			Existing = Settings->Sheets.FindByPredicate([&Found, &DefaultDocumentId](const FGoogleSheetDefinition& Item)
			{
				return Item.SheetName.Equals(Found.SheetName, ESearchCase::IgnoreCase) &&
					ResolveSheetDocumentId(Item, DefaultDocumentId).Equals(ResolveSheetDocumentId(Found, DefaultDocumentId), ESearchCase::CaseSensitive);
			});
		}

		if (Existing != nullptr)
		{
			Definition.DefinitionType = Existing->DefinitionType;
			Definition.AssetOutputPath = Existing->AssetOutputPath;
		}
		else
		{
			Definition.AssetOutputPath = Settings->DefaultDataAssetOutputPath;
		}

		Merged.Add(Definition);
	}

	Merged.Sort([](const FGoogleSheetDefinition& A, const FGoogleSheetDefinition& B)
	{
		const int32 DocCompare = A.SourceDocumentId.Compare(B.SourceDocumentId, ESearchCase::CaseSensitive);
		return DocCompare == 0 ? (A.SheetName < B.SheetName) : (DocCompare < 0);
	});

	Settings->Sheets = Merged;
	Settings->SaveConfig();

	OutResult.SuccessCount += Merged.Num();
	OutResult.Messages.Add(FString::Printf(TEXT("Synchronized %d sheets from %d document(s)."), Merged.Num(), DocumentIds.Num()));
	return true;
}

TArray<FGoogleSheetDefinition> FGoogleSheetImporterService::GetConfiguredSheets()
{
	const UGoogleSheetImporterSettings* Settings = GetDefault<UGoogleSheetImporterSettings>();
	return Settings ? Settings->Sheets : TArray<FGoogleSheetDefinition>();
}

FGoogleSheetImportResult FGoogleSheetImporterService::Execute(const EGoogleSheetImportAction Action)
{
	using namespace GoogleSheetImporter;

	FGoogleSheetImportResult Result;
	const UGoogleSheetImporterSettings *Settings = GetDefault<UGoogleSheetImporterSettings>();
	if (Settings == nullptr)
	{
		Result.bSuccess = false;
		Result.ErrorCount++;
		Result.Messages.Add(TEXT("Failed to access Google Sheet Importer settings."));
		return Result;
	}

	if (Settings->bAutoDiscoverSheets && Settings->bAutoSyncBeforeImport)
	{
		FGoogleSheetImportResult SyncResult;
		const bool bSyncOk = SyncSheetsFromDocument(SyncResult);
		for (const FString& Message : SyncResult.Messages)
		{
			Result.Messages.Add(FString::Printf(TEXT("[Sync] %s"), *Message));
		}
		if (!bSyncOk && Settings->Sheets.Num() == 0)
		{
			Result.bSuccess = false;
			Result.ErrorCount++;
			Result.Messages.Add(TEXT("No sheets configured and auto-sync failed."));
			return Result;
		}

		Settings = GetDefault<UGoogleSheetImporterSettings>();
	}

	const TArray<FString> DocumentIds = GetConfiguredDocumentIds(Settings);
	if (DocumentIds.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorCount++;
		Result.Messages.Add(TEXT("No DocumentId configured in Project Settings > Plugins > Google Sheet Importer."));
		return Result;
	}
	const FString DefaultDocumentId = DocumentIds[0];

	if (Settings->Sheets.Num() == 0)
	{
		Result.bSuccess = false;
		Result.ErrorCount++;
		Result.Messages.Add(TEXT("No sheet definitions configured."));
		return Result;
	}

	FParsedWorkbook Workbook;
	{
		FScopedSlowTask DownloadTask(static_cast<float>(Settings->Sheets.Num()), FText::FromString(TEXT("Downloading Google Sheets...")));
		DownloadTask.MakeDialog();

		for (const FGoogleSheetDefinition &Definition : Settings->Sheets)
		{
			DownloadTask.EnterProgressFrame(1.0f, FText::FromString(FString::Printf(TEXT("Downloading %s"), *Definition.SheetName)));

			if (Definition.SheetName.TrimStartAndEnd().IsEmpty() || Definition.Gid.TrimStartAndEnd().IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorCount++;
				Result.Messages.Add(TEXT("Sheet definition requires SheetName and Gid."));
				continue;
			}

			const FString SheetDocumentId = ResolveSheetDocumentId(Definition, DefaultDocumentId);
			if (SheetDocumentId.IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorCount++;
				Result.Messages.Add(FString::Printf(TEXT("[%s] Missing SourceDocumentId and no default DocumentId is available."), *Definition.SheetName));
				continue;
			}

			const FString Url = MakeSheetUrl(SheetDocumentId, Definition.Gid.TrimStartAndEnd());
			FString CsvContent;
			FString Error;
			if (!FetchCsvSync(Url, CsvContent, Error))
			{
				Result.bSuccess = false;
				Result.ErrorCount++;
				Result.Messages.Add(FString::Printf(TEXT("[%s] Download failed: %s (%s) [DocumentId=%s]"), *Definition.SheetName, *Error, *Url, *SheetDocumentId));
				continue;
			}

			TArray<TArray<FString>> Rows;
			if (!ParseCsv(CsvContent, Rows))
			{
				Result.bSuccess = false;
				Result.ErrorCount++;
				Result.Messages.Add(FString::Printf(TEXT("[%s] CSV parse failed."), *Definition.SheetName));
				continue;
			}

			if (Definition.DefinitionType == EGoogleSheetDefinitionType::Enum)
			{
				FParsedEnumSheet EnumSheet;
				if (ParseEnumSheet(Definition, Rows, EnumSheet, Result))
				{
					if (EnumSheet.bCreateAsUserDefinedAsset)
					{
						Workbook.UserDefinedEnums.Add(EnumSheet);
						Result.Messages.Add(FString::Printf(TEXT("[%s] UserDefinedEnum parsed (%d entries)."), *Definition.SheetName, EnumSheet.Entries.Num()));
					}
					else
					{
						Workbook.NativeEnums.Add(EnumSheet);
						Result.Messages.Add(FString::Printf(TEXT("[%s] Native enum parsed (%d entries)."), *Definition.SheetName, EnumSheet.Entries.Num()));
					}
				}
			}
			else
			{
				TSet<FString> KnownEnumNames;
				for (const FParsedEnumSheet &EnumSheet : Workbook.NativeEnums)
				{
					KnownEnumNames.Add(EnumSheet.EnumName);
				}

				FParsedTableSheet TableSheet;
				if (ParseTableSheet(Definition, Rows, KnownEnumNames, TableSheet, Result))
				{
					Workbook.Tables.Add(TableSheet);
					Result.Messages.Add(FString::Printf(TEXT("[%s] Table parsed (%d rows)."), *Definition.SheetName, TableSheet.Rows.Num()));
				}
			}
		}
	}

	ApplyBaseTableComposition(Workbook, Result);

	Workbook.NativeEnums.Sort([](const FParsedEnumSheet &L, const FParsedEnumSheet &R)
						{ return L.EnumName < R.EnumName; });
	Workbook.UserDefinedEnums.Sort([](const FParsedEnumSheet &L, const FParsedEnumSheet &R)
						{ return L.AssetName < R.AssetName; });
	Workbook.Tables.Sort([](const FParsedTableSheet &L, const FParsedTableSheet &R)
						 { return L.StructName < R.StructName; });

	if (Action == EGoogleSheetImportAction::ValidateOnly)
	{
		if (Result.ErrorCount == 0)
		{
			Result.Messages.Add(TEXT("Validation completed successfully."));
		}
		return Result;
	}

	const FString OutputDirectory = NormalizeCodeDirectory(Settings->CodeOutputDirectory);
	IPlatformFile &PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*OutputDirectory))
	{
		Result.bSuccess = false;
		Result.ErrorCount++;
		Result.Messages.Add(FString::Printf(TEXT("Failed creating output directory: %s"), *OutputDirectory));
		return Result;
	}

	{
		const FString EnumHeaderPath = OutputDirectory / TEXT("GS_Enums.h");
		FString Message;
		const FString EnumHeader = BuildEnumsHeader(Workbook.NativeEnums);
		if (!WriteFileIfChanged(EnumHeaderPath, EnumHeader, Message))
		{
			Result.bSuccess = false;
			Result.ErrorCount++;
		}
		else
		{
			Result.SuccessCount++;
		}
		Result.Messages.Add(Message);

		for (const FParsedTableSheet &Table : Workbook.Tables)
		{
			const FString TableHeaderPath = OutputDirectory / FString::Printf(TEXT("GS_%sRow.h"), *ToPascalCaseIdentifier(Table.SheetName));
			const FString TableHeader = BuildTableHeader(Table);
			if (!WriteFileIfChanged(TableHeaderPath, TableHeader, Message))
			{
				Result.bSuccess = false;
				Result.ErrorCount++;
			}
			else
			{
				Result.SuccessCount++;
			}
			Result.Messages.Add(Message);
		}
	}

	if (Action == EGoogleSheetImportAction::GenerateCodeOnly)
	{
		Result.Messages.Add(TEXT("Code generation completed. Rebuild editor before creating DataTables."));
		return Result;
	}

	const bool bNeedAssets = (Action == EGoogleSheetImportAction::CreateOrUpdateAssets || Action == EGoogleSheetImportAction::FullImport);
	if (bNeedAssets)
	{
		for (const FParsedEnumSheet& EnumSheet : Workbook.UserDefinedEnums)
		{
			CreateOrUpdateUserDefinedEnumAsset(EnumSheet, Result);
		}

		for (const FParsedTableSheet &Table : Workbook.Tables)
		{
			CreateOrUpdateDataTable(Table, Result);
		}
	}

	return Result;
}

bool FGoogleSheetImporterService::RegenerateProjectFiles(FString &OutMessage)
{
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString UbtPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Build/BatchFiles/RunUBT.bat"));
	const FString Args = FString::Printf(
		TEXT("-projectfiles -vscode -project=\"%s\" -game -engine -dotnet"),
		*ProjectFile);

	FProcHandle Handle = FPlatformProcess::CreateProc(*UbtPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
	if (!Handle.IsValid())
	{
		OutMessage = TEXT("Failed to launch RunUBT.bat for project file generation.");
		return false;
	}

	OutMessage = TEXT("Started project file regeneration.");
	return true;
}

bool FGoogleSheetImporterService::BuildEditorTarget(FString &OutMessage)
{
	const FString ProjectName = FApp::GetProjectName();
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString BuildBatPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Build/BatchFiles/Build.bat"));
	const FString Args = FString::Printf(
		TEXT("%sEditor Win64 Development -Project=\"%s\" -WaitMutex -FromMsBuild"),
		*ProjectName,
		*ProjectFile);

	FProcHandle Handle = FPlatformProcess::CreateProc(*BuildBatPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
	if (!Handle.IsValid())
	{
		OutMessage = TEXT("Failed to launch Build.bat for editor target.");
		return false;
	}

	OutMessage = TEXT("Started editor build process.");
	return true;
}
