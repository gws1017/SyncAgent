Add-Type -AssemblyName System.Drawing

# 플레이스토어 등록용 에셋: 512x512 고해상도 아이콘, 1024x500 피처 그래픽.
# ic_badge와 같은 추상 젬 디자인을 재사용 (스토어 아이콘 = 실제 배지와 동일해야
# 사용자가 헷갈리지 않음).

$gemGrid = @(
    "................",
    ".......LL.......",
    "......LLDD......",
    ".....LLDDDD.....",
    "....LLDDDDDD....",
    "...LLDDDDDDDD...",
    "..LLDDDDDDDDDD..",
    ".LLDDDDDDDDDDDD.",
    ".DDDDDDDDDDDDDD.",
    "..DDDDDDDDDDDD..",
    "...DDDDDDDDDD...",
    "....DDDDDDDD....",
    ".....DDDDDD.....",
    "......DDDD......",
    ".......DD.......",
    "................"
)
$gemColors = @{
    'L' = [System.Drawing.Color]::FromArgb(255, 250, 210, 120)
    'D' = [System.Drawing.Color]::FromArgb(255, 200, 140, 30)
}
$bg = [System.Drawing.Color]::FromArgb(255, 30, 30, 34)

function New-GemIcon {
    param($size, $outPath, $bgColor)
    $gridSize = 16
    $bmp = New-Object System.Drawing.Bitmap([int]$size, [int]$size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $g.Clear($bgColor)
    [double]$cell = $size / $gridSize
    for ($row = 0; $row -lt $gridSize; $row++) {
        $line = $gemGrid[$row]
        for ($col = 0; $col -lt $gridSize; $col++) {
            $ch = $line[$col]
            if ($ch -eq '.') { continue }
            $color = $gemColors[[string]$ch]
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
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$outDir = "C:\Source\etc\idlegame\dist\store"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# 512x512 하이레조 아이콘
New-GemIcon -size 512 -outPath "$outDir\icon-512.png" -bgColor $bg

# 1024x500 피처 그래픽 — 중앙에 젬 아이콘 + 타이틀 텍스트
$fw = 1024; $fh = 500
$fbmp = New-Object System.Drawing.Bitmap($fw, $fh)
$fg = [System.Drawing.Graphics]::FromImage($fbmp)
$fg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$fg.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

# 배경 그라디언트
$rect = New-Object System.Drawing.Rectangle(0, 0, $fw, $fh)
$c1 = [System.Drawing.Color]::FromArgb(255, 24, 24, 28)
$c2 = [System.Drawing.Color]::FromArgb(255, 44, 36, 24)
$gradBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $c1, $c2, 45.0)
$fg.FillRectangle($gradBrush, $rect)
$gradBrush.Dispose()

# 젬 아이콘 (왼쪽)
$gemSize = 300
$gemBmp = New-Object System.Drawing.Bitmap($gemSize, $gemSize)
$gemG = [System.Drawing.Graphics]::FromImage($gemBmp)
$gemG.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
$gemG.Clear([System.Drawing.Color]::Transparent)
[double]$cell = $gemSize / 16
for ($row = 0; $row -lt 16; $row++) {
    $line = $gemGrid[$row]
    for ($col = 0; $col -lt 16; $col++) {
        $ch = $line[$col]
        if ($ch -eq '.') { continue }
        $color = $gemColors[[string]$ch]
        $brush = New-Object System.Drawing.SolidBrush($color)
        $x = [Math]::Floor($col * $cell)
        $y = [Math]::Floor($row * $cell)
        $w = [Math]::Ceiling($cell)
        $h = [Math]::Ceiling($cell)
        $gemG.FillRectangle($brush, $x, $y, $w, $h)
        $brush.Dispose()
    }
}
$gemG.Dispose()
$fg.DrawImage($gemBmp, 60, ($fh - $gemSize) / 2)
$gemBmp.Dispose()

# 타이틀 텍스트 (오른쪽)
$titleFont = New-Object System.Drawing.Font("Segoe UI", 64, [System.Drawing.FontStyle]::Bold)
$subFont   = New-Object System.Drawing.Font("Segoe UI", 26, [System.Drawing.FontStyle]::Regular)
$titleBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 240, 240, 245))
$subBrush   = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 214, 168, 62))

$fg.DrawString("TEXT RPG", $titleFont, $titleBrush, 400, 175)
$fg.DrawString("idle adventure, one line at a time", $subFont, $subBrush, 402, 270)

$titleFont.Dispose(); $subFont.Dispose(); $titleBrush.Dispose(); $subBrush.Dispose()
$fg.Dispose()
$fbmp.Save("$outDir\feature-graphic-1024x500.png", [System.Drawing.Imaging.ImageFormat]::Png)
$fbmp.Dispose()

Write-Host "Store assets written to $outDir"
