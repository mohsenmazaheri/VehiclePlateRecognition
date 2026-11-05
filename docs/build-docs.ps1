# Builds docs/<lang>/*.docx from docs/md/<lang>/*.md. It writes the Word file format directly,
# so it takes seconds and does not need Microsoft Word.
# Supported Markdown: "# ", "## ", "### ", "- ", a whole line in *italics* (subtitle), pipe tables.

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = $PSScriptRoot
$languages = @(
    @{ Code = "en"; Font = "Calibri"; Lang = "en-US"; Rtl = $false },
    @{ Code = "fa"; Font = "Segoe UI"; Lang = "en-US"; Rtl = $true }
)
$script:Rtl = $false
$leftToRightMark = [string][char]0x200E
# Latin words, code names and URLs stay left to right inside Persian text.
$latinPattern = '[A-Za-z0-9][A-Za-z0-9+#._/:\-]*(?:[ ,]+[A-Za-z0-9][A-Za-z0-9+#._/:\-]*)*(?<=[A-Za-z0-9+#])'
$pageContentWidth = 9638   # A4 width minus 2 cm margins, in twentieths of a point
$wordNs = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'
$xmlHead = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'

function Escape-Xml([string]$text) {
    return [System.Security.SecurityElement]::Escape($text)
}

function Run-Xml([string]$text, [string]$format, [bool]$rtl) {
    $inner = $format + $(if ($rtl) { "<w:rtl/>" } else { "" })
    $rPr = if ($inner) { "<w:rPr>$inner</w:rPr>" } else { "" }
    return "<w:r>$rPr<w:t xml:space=`"preserve`">$(Escape-Xml $text)</w:t></w:r>"
}

function Runs-Xml([string]$text, [string]$format) {
    # A value marked with a left to right mark, such as a plate number, stays one run.
    if (-not $script:Rtl -or $text.Contains($leftToRightMark)) { return Run-Xml $text $format $false }
    $sb = New-Object System.Text.StringBuilder
    $position = 0
    foreach ($match in [regex]::Matches($text, $latinPattern)) {
        if ($match.Index -gt $position) { [void]$sb.Append((Run-Xml $text.Substring($position, $match.Index - $position) $format $true)) }
        [void]$sb.Append((Run-Xml $match.Value $format $false))
        $position = $match.Index + $match.Length
    }
    if ($position -lt $text.Length) { [void]$sb.Append((Run-Xml $text.Substring($position) $format $true)) }
    return $sb.ToString()
}

function Paragraph-Xml([string]$text, [string]$style, [string]$format = "") {
    $direction = if ($script:Rtl -and $text.Contains($leftToRightMark)) { '<w:bidi w:val="0"/>' } else { "" }
    $styleXml = if ($style) { "<w:pStyle w:val=`"$style`"/>" } else { "" }
    $pPr = if ($styleXml -or $direction) { "<w:pPr>$styleXml$direction</w:pPr>" } else { "" }
    return "<w:p>$pPr$(Runs-Xml $text $format)</w:p>"
}

function Split-Row([string]$line) {
    return @($line.Trim().Trim("|").Split("|") | ForEach-Object { $_.Trim() })
}

function Table-Xml($rows) {
    $columns = $rows[0].Count
    # Column width follows the longest text, within limits, so short columns stay narrow.
    $weights = New-Object 'double[]' $columns
    foreach ($row in $rows) {
        for ($c = 0; $c -lt $columns; $c++) {
            $weights[$c] = [Math]::Max($weights[$c], [Math]::Sqrt([Math]::Min(120, [Math]::Max(6, $row[$c].Length))))
        }
    }
    $sum = ($weights | Measure-Object -Sum).Sum
    $widths = @($weights | ForEach-Object { [int]($pageContentWidth * $_ / $sum) })

    $sb = New-Object System.Text.StringBuilder
    $tableDirection = if ($script:Rtl) { "<w:bidiVisual/>" } else { "" }
    [void]$sb.Append("<w:tbl><w:tblPr><w:tblStyle w:val=`"TableGrid`"/>$tableDirection<w:tblW w:w=`"$pageContentWidth`" w:type=`"dxa`"/><w:tblLayout w:type=`"fixed`"/></w:tblPr><w:tblGrid>")
    foreach ($w in $widths) { [void]$sb.Append("<w:gridCol w:w=`"$w`"/>") }
    [void]$sb.Append("</w:tblGrid>")
    for ($r = 0; $r -lt $rows.Count; $r++) {
        $isHeader = $r -eq 0
        $trPr = if ($isHeader) { "<w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>" } else { "<w:trPr><w:cantSplit/></w:trPr>" }
        [void]$sb.Append("<w:tr>$trPr")
        for ($c = 0; $c -lt $columns; $c++) {
            $shade = if ($isHeader) { "<w:shd w:val=`"clear`" w:color=`"auto`" w:fill=`"E8EEF4`"/>" } else { "" }
            $bold = if ($isHeader) { "<w:b/><w:bCs/>" } else { "" }
            [void]$sb.Append("<w:tc><w:tcPr><w:tcW w:w=`"$($widths[$c])`" w:type=`"dxa`"/>$shade</w:tcPr>$(Paragraph-Xml $rows[$r][$c] 'TableText' $bold)</w:tc>")
        }
        [void]$sb.Append("</w:tr>")
    }
    [void]$sb.Append("</w:tbl><w:p><w:pPr><w:pStyle w:val=`"TableText`"/></w:pPr></w:p>")
    return $sb.ToString()
}

function Body-Xml([string[]]$lines) {
    $sb = New-Object System.Text.StringBuilder
    $table = New-Object System.Collections.ArrayList
    foreach ($line in ($lines + "")) {
        if ($line.StartsWith("|")) {
            $isSeparator = $line -match '^\|[\s\-:|]+\|\s*$'
            if (-not $isSeparator) { [void]$table.Add((Split-Row $line)) }
            continue
        }
        if ($table.Count -gt 0) {
            [void]$sb.Append((Table-Xml $table))
            $table = New-Object System.Collections.ArrayList
        }
        if ($line.Trim() -eq "") { continue }
        if ($line.StartsWith("### ")) { [void]$sb.Append((Paragraph-Xml $line.Substring(4) "Heading2")) }
        elseif ($line.StartsWith("## ")) { [void]$sb.Append((Paragraph-Xml $line.Substring(3) "Heading1")) }
        elseif ($line.StartsWith("# ")) { [void]$sb.Append((Paragraph-Xml $line.Substring(2) "Title")) }
        elseif ($line.StartsWith("- ")) { [void]$sb.Append((Paragraph-Xml $line.Substring(2) "ListBullet")) }
        elseif ($line -match '^\*[^*]+\*$') { [void]$sb.Append((Paragraph-Xml $line.Trim("*") "Subtitle")) }
        else { [void]$sb.Append((Paragraph-Xml $line "")) }
    }
    return $sb.ToString()
}

function Styles-Xml([string]$font, [string]$lang) {
    $normalPr = if ($script:Rtl) { "<w:pPr><w:bidi/></w:pPr>" } else { "" }
    $fonts = "<w:rFonts w:ascii=`"$font`" w:hAnsi=`"$font`" w:cs=`"$font`" w:eastAsia=`"$font`"/>"
    function Style([string]$id, [string]$name, [string]$pPr, [string]$rPr) {
        return "<w:style w:type=`"paragraph`" w:styleId=`"$id`"><w:name w:val=`"$name`"/><w:basedOn w:val=`"Normal`"/><w:next w:val=`"Normal`"/><w:qFormat/><w:pPr>$pPr</w:pPr><w:rPr>$rPr</w:rPr></w:style>"
    }
    $blue = '<w:color w:val="1F4E79"/>'
    return "$xmlHead<w:styles $wordNs><w:docDefaults><w:rPrDefault><w:rPr>$fonts<w:sz w:val=`"22`"/><w:szCs w:val=`"22`"/><w:lang w:val=`"$lang`" w:bidi=`"fa-IR`"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after=`"120`" w:line=`"264`" w:lineRule=`"auto`"/></w:pPr></w:pPrDefault></w:docDefaults>" +
        "<w:style w:type=`"paragraph`" w:default=`"1`" w:styleId=`"Normal`"><w:name w:val=`"Normal`"/><w:qFormat/>$normalPr</w:style>" +
        (Style "Title" "Title" '<w:spacing w:after="60"/>' "<w:b/><w:bCs/>$blue<w:sz w:val=`"44`"/><w:szCs w:val=`"44`"/>") +
        (Style "Subtitle" "Subtitle" '<w:spacing w:after="240"/>' '<w:color w:val="595959"/><w:sz w:val="22"/>') +
        (Style "Heading1" "heading 1" '<w:keepNext/><w:spacing w:before="280" w:after="100"/><w:outlineLvl w:val="0"/>' "<w:b/><w:bCs/>$blue<w:sz w:val=`"30`"/><w:szCs w:val=`"30`"/>") +
        (Style "Heading2" "heading 2" '<w:keepNext/><w:spacing w:before="180" w:after="60"/><w:outlineLvl w:val="1"/>' "<w:b/><w:bCs/>$blue<w:sz w:val=`"24`"/><w:szCs w:val=`"24`"/>") +
        (Style "ListBullet" "List Bullet" '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr><w:spacing w:after="60"/>' '') +
        (Style "TableText" "Table Text" '<w:spacing w:before="20" w:after="20" w:line="240" w:lineRule="auto"/>' '<w:sz w:val="19"/><w:szCs w:val="19"/>') +
        "<w:style w:type=`"table`" w:styleId=`"TableGrid`"><w:name w:val=`"Table Grid`"/><w:tblPr><w:tblBorders><w:top w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/><w:left w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/><w:bottom w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/><w:right w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/><w:insideH w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/><w:insideV w:val=`"single`" w:sz=`"4`" w:color=`"A6A6A6`"/></w:tblBorders><w:tblCellMar><w:left w:w=`"80`" w:type=`"dxa`"/><w:right w:w=`"80`" w:type=`"dxa`"/></w:tblCellMar></w:tblPr></w:style></w:styles>"
}

function Add-Entry($zip, [string]$name, [string]$content) {
    $entry = $zip.CreateEntry($name)
    $stream = $entry.Open()
    $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($content)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Dispose()
}

function Write-Docx([string]$path, [string]$body, [string]$title, [string]$font, [string]$lang) {
    if (Test-Path $path) { Remove-Item $path -Force }
    $zip = [System.IO.Compression.ZipFile]::Open($path, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        Add-Entry $zip "[Content_Types].xml" "$xmlHead<Types xmlns=`"http://schemas.openxmlformats.org/package/2006/content-types`"><Default Extension=`"rels`" ContentType=`"application/vnd.openxmlformats-package.relationships+xml`"/><Default Extension=`"xml`" ContentType=`"application/xml`"/><Override PartName=`"/word/document.xml`" ContentType=`"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml`"/><Override PartName=`"/word/styles.xml`" ContentType=`"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml`"/><Override PartName=`"/word/numbering.xml`" ContentType=`"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml`"/><Override PartName=`"/word/footer1.xml`" ContentType=`"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml`"/><Override PartName=`"/docProps/core.xml`" ContentType=`"application/vnd.openxmlformats-package.core-properties+xml`"/></Types>"
        Add-Entry $zip "_rels/.rels" "$xmlHead<Relationships xmlns=`"http://schemas.openxmlformats.org/package/2006/relationships`"><Relationship Id=`"rId1`" Type=`"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument`" Target=`"word/document.xml`"/><Relationship Id=`"rId2`" Type=`"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties`" Target=`"docProps/core.xml`"/></Relationships>"
        Add-Entry $zip "word/_rels/document.xml.rels" "$xmlHead<Relationships xmlns=`"http://schemas.openxmlformats.org/package/2006/relationships`"><Relationship Id=`"rId1`" Type=`"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles`" Target=`"styles.xml`"/><Relationship Id=`"rId2`" Type=`"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering`" Target=`"numbering.xml`"/><Relationship Id=`"rId3`" Type=`"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer`" Target=`"footer1.xml`"/></Relationships>"
        Add-Entry $zip "docProps/core.xml" "$xmlHead<cp:coreProperties xmlns:cp=`"http://schemas.openxmlformats.org/package/2006/metadata/core-properties`" xmlns:dc=`"http://purl.org/dc/elements/1.1/`"><dc:title>$(Escape-Xml $title)</dc:title></cp:coreProperties>"
        Add-Entry $zip "word/styles.xml" (Styles-Xml $font $lang)
        Add-Entry $zip "word/numbering.xml" "$xmlHead<w:numbering $wordNs><w:abstractNum w:abstractNumId=`"0`"><w:multiLevelType w:val=`"singleLevel`"/><w:lvl w:ilvl=`"0`"><w:start w:val=`"1`"/><w:numFmt w:val=`"bullet`"/><w:lvlText w:val=`"&#8226;`"/><w:lvlJc w:val=`"left`"/><w:pPr><w:ind w:left=`"360`" w:hanging=`"240`"/></w:pPr></w:lvl></w:abstractNum><w:num w:numId=`"1`"><w:abstractNumId w:val=`"0`"/></w:num></w:numbering>"
        Add-Entry $zip "word/footer1.xml" "$xmlHead<w:ftr $wordNs><w:p><w:pPr><w:jc w:val=`"center`"/></w:pPr><w:fldSimple w:instr=`" PAGE `"><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p></w:ftr>"
        Add-Entry $zip "word/document.xml" "$xmlHead<w:document $wordNs><w:body>$body<w:sectPr><w:footerReference w:type=`"default`" r:id=`"rId3`"/><w:pgSz w:w=`"11906`" w:h=`"16838`"/><w:pgMar w:top=`"1134`" w:right=`"1134`" w:bottom=`"1134`" w:left=`"1134`" w:header=`"567`" w:footer=`"567`" w:gutter=`"0`"/></w:sectPr></w:body></w:document>"
    }
    finally {
        $zip.Dispose()
    }
}

foreach ($language in $languages) {
    $script:Rtl = $language.Rtl
    $outDir = Join-Path $root $language.Code
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    foreach ($source in Get-ChildItem (Join-Path $root ("md\" + $language.Code)) -Filter *.md) {
        $lines = [string[]](Get-Content -LiteralPath $source.FullName -Encoding UTF8)
        $target = Join-Path $outDir ($source.BaseName + ".docx")
        Write-Docx $target (Body-Xml $lines) ($lines[0] -replace "^# ", "") $language.Font $language.Lang
        Write-Output "Built $target"
    }
}
