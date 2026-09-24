# Generate 16x16, column-major SSD1306 glyphs for the GB2312 repertoire.
# Each entry is a sorted little-endian uint16 Unicode codepoint followed by 32 bytes.
Add-Type -AssemblyName System.Drawing
$encoding = [System.Text.Encoding]::GetEncoding(936)
$characters = [System.Collections.Generic.SortedSet[int]]::new()
for ($lead = 0xA1; $lead -le 0xF7; $lead++) {
    for ($trail = 0xA1; $trail -le 0xFE; $trail++) {
        $s = $encoding.GetString([byte[]]@($lead, $trail))
        if ($s.Length -eq 1 -and [int][char]$s[0] -ge 0x80 -and [int][char]$s[0] -ne 0xFFFD) {
            [void]$characters.Add([int][char]$s[0])
        }
    }
}
$font = [System.Drawing.Font]::new('SimHei', 15, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$bitmap = [System.Drawing.Bitmap]::new(16, 16)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
$format = [System.Drawing.StringFormat]::GenericTypographic
$format.FormatFlags = $format.FormatFlags -bor [System.Drawing.StringFormatFlags]::NoClip
$output = Join-Path $PSScriptRoot '..\main\oled_font16.bin'
$stream = [System.IO.File]::Create([System.IO.Path]::GetFullPath($output))
try {
    foreach ($codepoint in $characters) {
        $stream.WriteByte($codepoint -band 0xFF)
        $stream.WriteByte(($codepoint -shr 8) -band 0xFF)
        $graphics.Clear([System.Drawing.Color]::Black)
        $graphics.DrawString([string][char]$codepoint, $font, [System.Drawing.Brushes]::White, -1, -2, $format)
        for ($page = 0; $page -lt 2; $page++) {
            for ($x = 0; $x -lt 16; $x++) {
                $column = 0
                for ($bit = 0; $bit -lt 8; $bit++) {
                    if ($bitmap.GetPixel($x, $page * 8 + $bit).R -ge 128) {
                        $column = $column -bor (1 -shl $bit)
                    }
                }
                $stream.WriteByte($column)
            }
        }
    }
} finally {
    $stream.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    $font.Dispose()
}
Write-Output "Generated $($characters.Count) glyphs at $output"
