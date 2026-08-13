#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate an Excel workbook from the authoritative Modbus map JSON.

The input is produced by ``modbus-map-dump --format json``.  The workbook is
written directly as an Office Open XML archive using only Python's standard
library, keeping this build-time documentation tool independent of a large
spreadsheet package and its transitive dependencies.
"""

from __future__ import annotations

import argparse
import json
import os
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


EXPECTED_SCHEMA = "mnc.modbus-register-map.v1"
HEADERS = (
    "Register Starting Address (Hex)",
    "Register Ending Address (Hex)",
    "Register Starting Address (Dec)",
    "Register Ending Address (Dec)",
    "Register Data Type",
    "Register Function Code",
    "Register Read/Write Type",
    "Data Attribute Name",
)


@dataclass(frozen=True)
class RegisterRow:
    """One contiguous register-map definition rendered as one Excel row."""

    start_address: int
    end_address: int
    data_type: str
    function_code: int
    access: str
    attribute_name: str


def _required_integer(entry: dict[str, Any], key: str, row_number: int) -> int:
    value = entry.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"register {row_number}: {key} must be an integer")
    return value


def _required_text(entry: dict[str, Any], key: str, row_number: int) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"register {row_number}: {key} must be a non-empty string")
    return value.strip()


def _register_access(entry: dict[str, Any], function_code: int, row_number: int) -> str:
    explicit = entry.get("access")
    if explicit is not None:
        if explicit not in ("R", "R/W"):
            raise ValueError(f"register {row_number}: access must be R or R/W")
        return explicit

    if function_code in (0x01, 0x02, 0x03, 0x04):
        return "R"
    if function_code in (0x05, 0x06, 0x0F, 0x10, 0x16, 0x17):
        return "R/W"
    raise ValueError(
        f"register {row_number}: cannot infer access for function code "
        f"0x{function_code:02X}"
    )


def _attribute_name(entry: dict[str, Any], row_number: int) -> str:
    # ``source`` is the exporter's canonical, fully qualified identity.  It
    # already includes array indexes where needed, unlike the optional base
    # ``attribute`` field.
    for key in ("source", "attribute", "special_register"):
        value = entry.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
    raise ValueError(f"register {row_number}: no data attribute identity is present")


def load_register_rows(input_path: Path) -> list[RegisterRow]:
    """Load and strictly validate register rows from a map-dump JSON file."""

    with input_path.open("r", encoding="utf-8") as stream:
        document = json.load(stream)

    if not isinstance(document, dict):
        raise ValueError("top-level Modbus map JSON must be an object")
    if document.get("schema") != EXPECTED_SCHEMA:
        raise ValueError(
            f"unsupported Modbus map schema {document.get('schema')!r}; "
            f"expected {EXPECTED_SCHEMA!r}"
        )

    registers = document.get("registers")
    if not isinstance(registers, list) or not registers:
        raise ValueError("Modbus map JSON must contain a non-empty registers array")

    rows: list[RegisterRow] = []
    for row_number, entry in enumerate(registers, start=1):
        if not isinstance(entry, dict):
            raise ValueError(f"register {row_number}: entry must be an object")

        start_address = _required_integer(entry, "address", row_number)
        end_address = _required_integer(entry, "last_address", row_number)
        function_code = _required_integer(entry, "function_code", row_number)

        if not 0 <= start_address <= 0xFFFF:
            raise ValueError(f"register {row_number}: address is outside 0..65535")
        if not start_address <= end_address <= 0xFFFF:
            raise ValueError(
                f"register {row_number}: last_address precedes address or exceeds 65535"
            )
        if not 0 <= function_code <= 0xFF:
            raise ValueError(f"register {row_number}: function_code is outside 0..255")

        rows.append(
            RegisterRow(
                start_address=start_address,
                end_address=end_address,
                data_type=_required_text(entry, "data_type", row_number),
                function_code=function_code,
                access=_register_access(entry, function_code, row_number),
                attribute_name=_attribute_name(entry, row_number),
            )
        )

    return rows


def _column_name(index: int) -> str:
    result = ""
    while index:
        index, remainder = divmod(index - 1, 26)
        result = chr(ord("A") + remainder) + result
    return result


def _text_cell(reference: str, value: str, style: int = 0) -> str:
    style_attribute = f' s="{style}"' if style else ""
    return (
        f'<c r="{reference}" t="inlineStr"{style_attribute}>'
        f"<is><t>{escape(value)}</t></is></c>"
    )


def _number_cell(reference: str, value: int) -> str:
    return f'<c r="{reference}" t="n"><v>{value}</v></c>'


def _worksheet_xml(rows: Iterable[RegisterRow]) -> str:
    row_list = list(rows)
    sheet_rows: list[str] = []

    header_cells = "".join(
        _text_cell(f"{_column_name(column)}1", header, style=1)
        for column, header in enumerate(HEADERS, start=1)
    )
    sheet_rows.append(f'<row r="1" ht="30" customHeight="1">{header_cells}</row>')

    for excel_row, register in enumerate(row_list, start=2):
        cells = (
            _text_cell(f"A{excel_row}", f"0x{register.start_address:04X}"),
            _text_cell(f"B{excel_row}", f"0x{register.end_address:04X}"),
            _number_cell(f"C{excel_row}", register.start_address),
            _number_cell(f"D{excel_row}", register.end_address),
            _text_cell(f"E{excel_row}", register.data_type),
            _text_cell(f"F{excel_row}", f"0x{register.function_code:02X}"),
            _text_cell(f"G{excel_row}", register.access),
            _text_cell(f"H{excel_row}", register.attribute_name),
        )
        sheet_rows.append(f'<row r="{excel_row}">{"".join(cells)}</row>')

    final_row = len(row_list) + 1
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <dimension ref="A1:H{final_row}"/>
  <sheetViews>
    <sheetView workbookViewId="0">
      <pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/>
      <selection pane="bottomLeft" activeCell="A2" sqref="A2"/>
    </sheetView>
  </sheetViews>
  <sheetFormatPr defaultRowHeight="18"/>
  <cols>
    <col min="1" max="2" width="30" customWidth="1"/>
    <col min="3" max="4" width="29" customWidth="1"/>
    <col min="5" max="5" width="24" customWidth="1"/>
    <col min="6" max="7" width="28" customWidth="1"/>
    <col min="8" max="8" width="42" customWidth="1"/>
  </cols>
  <sheetData>{''.join(sheet_rows)}</sheetData>
  <autoFilter ref="A1:H{final_row}"/>
  <pageMargins left="0.25" right="0.25" top="0.5" bottom="0.5" header="0.2" footer="0.2"/>
  <pageSetup orientation="landscape" fitToWidth="1" fitToHeight="0"/>
</worksheet>
'''


def _archive_members(worksheet: str) -> dict[str, str]:
    return {
        "[Content_Types].xml": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
  <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
  <Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>
''',
        "_rels/.rels": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
''',
        "docProps/app.xml": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
  <Application>Monutchee Modbus Register Documentation Generator</Application>
</Properties>
''',
        "docProps/core.xml": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <dc:title>MSAP1 Modbus Register Map</dc:title>
  <dc:creator>Monutchee Yocto Build</dc:creator>
</cp:coreProperties>
''',
        "xl/workbook.xml": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <bookViews><workbookView/></bookViews>
  <sheets><sheet name="Registers" sheetId="1" r:id="rId1"/></sheets>
</workbook>
''',
        "xl/_rels/workbook.xml.rels": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>
''',
        "xl/styles.xml": '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="2">
    <font><sz val="11"/><name val="Aptos"/></font>
    <font><b/><color rgb="FFFFFFFF"/><sz val="11"/><name val="Aptos Display"/></font>
  </fonts>
  <fills count="3">
    <fill><patternFill patternType="none"/></fill>
    <fill><patternFill patternType="gray125"/></fill>
    <fill><patternFill patternType="solid"><fgColor rgb="FF156B52"/><bgColor indexed="64"/></patternFill></fill>
  </fills>
  <borders count="2">
    <border><left/><right/><top/><bottom/><diagonal/></border>
    <border><left/><right/><top/><bottom style="thin"><color rgb="FFB7C9C3"/></bottom><diagonal/></border>
  </borders>
  <cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
  <cellXfs count="2">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
    <xf numFmtId="0" fontId="1" fillId="2" borderId="1" xfId="0" applyAlignment="1"><alignment horizontal="center" vertical="center" wrapText="1"/></xf>
  </cellXfs>
  <cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>
''',
        "xl/worksheets/sheet1.xml": worksheet,
    }


def write_workbook(rows: Iterable[RegisterRow], output_path: Path) -> None:
    """Atomically write a deterministic single-sheet XLSX workbook."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    worksheet = _worksheet_xml(rows)

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)

    try:
        with zipfile.ZipFile(
            temporary_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for member_name, contents in _archive_members(worksheet).items():
                info = zipfile.ZipInfo(member_name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, contents.encode("utf-8"))
        os.replace(temporary_path, output_path)
    finally:
        temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a one-sheet XLSX Modbus register map"
    )
    parser.add_argument("--input", required=True, type=Path, help="map-dump JSON")
    parser.add_argument("--output", required=True, type=Path, help="output .xlsx")
    arguments = parser.parse_args()

    if arguments.output.suffix.lower() != ".xlsx":
        parser.error("the output filename must use the standard .xlsx extension")

    rows = load_register_rows(arguments.input)
    write_workbook(rows, arguments.output)
    print(f"Generated {arguments.output} with {len(rows)} register definitions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
