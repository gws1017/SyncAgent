Add-Type -AssemblyName System.Drawing

# 16x16 픽셀아트 그리드로 검(칼) 아이콘을 그린 뒤 각 아이콘 사이즈에 맞게
# 블록 단위로 확대한다 (안티앨리어싱 없이 — 도트 느낌을 살리기 위해 일부러 각지게).
$grid = @(
    ".......H........",
    ".......H........",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    ".......HB.......",
    "....GGGGGGGG.....",
    "....GGGGGGGG.....",
    ".......WW........",
    ".......WW........",
    "......PPPP.......",
    "................."
)
$gridSize = 16

$colors = @{
    'H' = [System.Drawing.Color]::FromArgb(255, 235, 235, 240) # 블레이드 하이라이트 (밝은 은색)
    'B' = [System.Drawing.Color]::FromArgb(255, 170, 178, 190) # 블레이드 본체 (은색)
    'G' = [System.Drawing.Color]::FromArgb(255, 214, 168, 62)  # 가드 (금색)
    'W' = [System.Drawing.Color]::FromArgb(255, 120, 78, 46)   # 손잡이 (갈색)
    'P' = [System.Drawing.Color]::FromArgb(255, 214, 168, 62)  # 폼멜 (금색)
}

[int[]]$sizes = 16, 32, 48, 256
$images = New-Object System.Collections.Generic.List[object]

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap([int]$size, [int]$size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.Clear([System.Drawing.Color]::Transparent)

    [double]$cell = $size / $gridSize

    for ($row = 0; $row -lt $gridSize; $row++) {
        $line = $grid[$row]
        for ($col = 0; $col -lt $gridSize; $col++) {
            $ch = $line[$col]
            if ($ch -eq '.') { continue }
            $color = $colors[[string]$ch]
            $brush = New-Object System.Drawing.SolidBrush($color)
            $x = [Math]::Floor($col * $cell)
            $y = [Math]::Floor($row * $cell)
            $w = [Math]::Ceiling($cell)
            $h = [Math]::Ceiling($cell)
            $g.FillRectangle($brush, $x, $y, $w, $h)
            $brush.Dispose()
        }
    }

    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add([PSCustomObject]@{ Size = [int]$size; Bytes = $ms.ToArray() })
    $bmp.Dispose()
}

$outPath = "C:\Source\etc\idlegame\src\app_fantasy.ico"
$fs = New-Object System.IO.FileStream($outPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$images.Count)

$headerSize = 6 + 16 * $images.Count
$offset = $headerSize
foreach ($img in $images) {
    $dim = if ($img.Size -eq 256) { 0 } else { $img.Size }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$img.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) {
    $bw.Write($img.Bytes)
}

$bw.Flush()
$bw.Close()
$fs.Close()

Write-Host "Icon written to $outPath ($($images.Count) sizes)"
