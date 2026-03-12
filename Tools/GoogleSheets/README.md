# Google Sheets -> Unreal JSON Export

`UnrealSheetExporter.gs` is a Google Apps Script template that exports sheets to JSON with nested fields.

## Expected Sheet Format

- Row 1: header names
- Row 2: Unreal types (`int32`, `float`, `bool`, `FString`, `FText`, `FName`, enum like `EItemType`) - optional but recommended
- Row 3+: data

Example header row:

`RowName | Common.Id | Common.Name | Common.Price | Attack`

This becomes:

```json
{
  "RowName": "Sword_001",
  "Common": {
    "Id": "Sword_001",
    "Name": "Bronze Sword",
    "Price": 100
  },
  "Attack": 12.5
}
```

## Setup

1. Open Google Sheet.
2. Extensions -> Apps Script.
3. Paste `UnrealSheetExporter.gs` content.
4. Run `exportActiveSheetToJson` or `exportAllSheetsToJson`.
5. Check My Drive folder: `UnrealSheetExport`.

## Notes

- Dot headers (`Common.Id`) are converted into nested JSON objects.
- `RowName` is auto-resolved from `RowName`, `Name`, `Id`, `Common.Id` (first found).
- Blank cells are omitted by default (`omitBlankValues: true`).
