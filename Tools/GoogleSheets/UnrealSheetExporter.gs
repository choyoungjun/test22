/**
 * Google Apps Script template for Unreal data export.
 *
 * Sheet format:
 * - Row 1: headers (supports nested keys like "Common.Id")
 * - Row 2: types (optional but recommended: int32, float, bool, FString, FText, FName, EMyEnum...)
 * - Row 3+: data rows
 *
 * Example headers:
 * RowName | Common.Id | Common.Name | Common.Price | Attack
 */

const UNREAL_EXPORT_CONFIG = {
  // If true, row 2 is treated as type row.
  hasTypeRow: true,
  headerRowIndex: 1,
  typeRowIndex: 2,
  dataStartRowIndex: 3,

  // Row key priority: first existing column in this list is used.
  rowNameCandidates: ["RowName", "Name", "Id", "Common.Id"],

  // Output folder under My Drive.
  outputFolderName: "UnrealSheetExport",

  // If true, blank cells are omitted from JSON.
  omitBlankValues: true,
};

/**
 * Export all visible sheets in current spreadsheet to JSON files.
 * File names: <SheetName>.json
 */
function exportAllSheetsToJson() {
  const spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  const sheets = spreadsheet.getSheets().filter((s) => !s.isSheetHidden());
  const folder = getOrCreateOutputFolder_(UNREAL_EXPORT_CONFIG.outputFolderName);

  sheets.forEach((sheet) => {
    const payload = buildSheetPayload_(sheet, UNREAL_EXPORT_CONFIG);
    const fileName = `${sheet.getName()}.json`;
    upsertDriveFile_(folder, fileName, JSON.stringify(payload, null, 2));
  });

  SpreadsheetApp.getUi().alert(`Export complete: ${sheets.length} sheet(s).`);
}

/**
 * Export only active sheet to JSON file.
 */
function exportActiveSheetToJson() {
  const sheet = SpreadsheetApp.getActiveSheet();
  const folder = getOrCreateOutputFolder_(UNREAL_EXPORT_CONFIG.outputFolderName);
  const payload = buildSheetPayload_(sheet, UNREAL_EXPORT_CONFIG);
  const fileName = `${sheet.getName()}.json`;
  upsertDriveFile_(folder, fileName, JSON.stringify(payload, null, 2));
  SpreadsheetApp.getUi().alert(`Exported: ${fileName}`);
}

/**
 * Preview 5 parsed rows from active sheet in logs.
 */
function previewActiveSheetJson() {
  const sheet = SpreadsheetApp.getActiveSheet();
  const payload = buildSheetPayload_(sheet, UNREAL_EXPORT_CONFIG);
  const preview = {
    sheet: payload.sheet,
    rowCount: payload.rowCount,
    rows: payload.rows.slice(0, 5),
  };
  console.log(JSON.stringify(preview, null, 2));
}

function buildSheetPayload_(sheet, config) {
  const values = sheet.getDataRange().getDisplayValues();
  if (values.length < config.headerRowIndex) {
    throw new Error(`Sheet "${sheet.getName()}" has no header row.`);
  }

  const headerRow = values[config.headerRowIndex - 1] || [];
  const typeRow = config.hasTypeRow ? (values[config.typeRowIndex - 1] || []) : [];
  const rows = [];

  for (let r = config.dataStartRowIndex - 1; r < values.length; r += 1) {
    const rowValues = values[r] || [];
    if (isBlankRow_(rowValues)) continue;

    const rowObject = {};
    let rowNameValue = "";

    for (let c = 0; c < headerRow.length; c += 1) {
      const header = (headerRow[c] || "").trim();
      if (!header) continue;

      const raw = rowValues[c] ?? "";
      const type = (typeRow[c] || "").trim();
      const typedValue = convertValueByType_(raw, type, config.omitBlankValues);

      if (typedValue === undefined) continue;
      setNestedValue_(rowObject, header, typedValue);
    }

    rowNameValue = resolveRowName_(rowObject, config.rowNameCandidates);
    if (rowNameValue) {
      rowObject.RowName = rowNameValue;
    }

    rows.push(rowObject);
  }

  return {
    sheet: sheet.getName(),
    generatedAt: new Date().toISOString(),
    rowCount: rows.length,
    rows,
  };
}

function resolveRowName_(rowObject, candidates) {
  for (let i = 0; i < candidates.length; i += 1) {
    const key = candidates[i];
    const value = getNestedValue_(rowObject, key);
    if (value !== null && value !== undefined && String(value).trim() !== "") {
      return String(value).trim();
    }
  }
  return "";
}

function convertValueByType_(rawValue, unrealType, omitBlankValues) {
  const text = String(rawValue ?? "").trim();
  if (text === "") {
    return omitBlankValues ? undefined : "";
  }

  const t = (unrealType || "").trim();
  if (t === "" || t === "FString" || t === "FText" || t === "FName") {
    return text;
  }

  if (t === "int32" || t === "int64") {
    const n = Number(text);
    return Number.isFinite(n) ? Math.trunc(n) : text;
  }

  if (t === "float" || t === "double") {
    const n = Number(text);
    return Number.isFinite(n) ? n : text;
  }

  if (t === "bool") {
    const lower = text.toLowerCase();
    if (["1", "true", "yes", "y"].includes(lower)) return true;
    if (["0", "false", "no", "n"].includes(lower)) return false;
    return text;
  }

  // Unreal enums are usually E*; keep as string token.
  if (t.startsWith("E")) {
    return text;
  }

  // Fallback: keep original text for unsupported/custom types.
  return text;
}

function setNestedValue_(obj, dottedPath, value) {
  const keys = dottedPath.split(".").map((k) => k.trim()).filter(Boolean);
  if (keys.length === 0) return;

  let current = obj;
  for (let i = 0; i < keys.length; i += 1) {
    const key = keys[i];
    const isLeaf = i === keys.length - 1;
    if (isLeaf) {
      current[key] = value;
      return;
    }
    if (typeof current[key] !== "object" || current[key] === null || Array.isArray(current[key])) {
      current[key] = {};
    }
    current = current[key];
  }
}

function getNestedValue_(obj, dottedPath) {
  const keys = dottedPath.split(".").map((k) => k.trim()).filter(Boolean);
  if (keys.length === 0) return null;

  let current = obj;
  for (let i = 0; i < keys.length; i += 1) {
    const key = keys[i];
    if (current === null || current === undefined || !(key in current)) {
      return null;
    }
    current = current[key];
  }
  return current;
}

function isBlankRow_(rowValues) {
  for (let i = 0; i < rowValues.length; i += 1) {
    if (String(rowValues[i] ?? "").trim() !== "") return false;
  }
  return true;
}

function getOrCreateOutputFolder_(folderName) {
  const found = DriveApp.getFoldersByName(folderName);
  if (found.hasNext()) return found.next();
  return DriveApp.createFolder(folderName);
}

function upsertDriveFile_(folder, fileName, content) {
  const existing = folder.getFilesByName(fileName);
  if (existing.hasNext()) {
    const file = existing.next();
    file.setContent(content);
    return file;
  }
  return folder.createFile(fileName, content, MimeType.PLAIN_TEXT);
}

