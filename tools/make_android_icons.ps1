Add-Type -AssemblyName System.Drawing

# PC용 app_fantasy.ico/app.ico와 같은 톤의 픽셀아트를 안드로이드 런처/알림/최근앱 카드용
# PNG로 생성.
# - ic_badge: 런처 아이콘 + OS가 강제로 보여주는 알림/최근앱 배지 (고정, 항상 이걸로 노출됨).
#   프라이버시 모드를 켜도 이 배지는 안 바뀌는 게 실측 확인됐으므로, 게임인지 티 안 나는
#   추상 젬 모양으로 함.
# - ic_game / ic_sync: 최근앱 TaskDescription 동적 전환용 (일부 OEM/런처에서는 반영됨).
# - ic_game_notif: 상태바 알림 아이콘용 (이건 실측으로 정상 전환 확인됨).

function New-PixelPng {
    param($grid, $colors, $size, $outPath, $bgColor)

    $gridSize = 16
    $bmp = New-Object System.Drawing.Bitmap([int]$size, [int]$size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $g.Clear($bgColor)

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
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$swordGrid = @(
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
$swordColors = @{
    'H' = [System.Drawing.Color]::FromArgb(255, 235, 235, 240)
    'B' = [System.Drawing.Color]::FromArgb(255, 170, 178, 190)
    'G' = [System.Drawing.Color]::FromArgb(255, 214, 168, 62)
    'W' = [System.Drawing.Color]::FromArgb(255, 120, 78, 46)
    'P' = [System.Drawing.Color]::FromArgb(255, 214, 168, 62)
}

$syncGrid = @(
    "................",
    "......AAAA......",
    "....AA....AA....",
    "...A........AA..",
    "..A...........A.",
    ".A.............A",
    ".A..............",
    ".A..............",
    "..............A.",
    "..............A.",
    "A.............A.",
    "A............A..",
    ".A..........A...",
    "..AA......AA....",
    "......AAAA......",
    "................"
)
$syncColors = @{
    'A' = [System.Drawing.Color]::FromArgb(255, 90, 170, 230)
}

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
$transparent = [System.Drawing.Color]::Transparent
$outDir = "C:\Source\etc\idlegame\android\app\src\main\res\drawable"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

New-PixelPng -grid $gemGrid   -colors $gemColors   -size 192 -outPath "$outDir\ic_badge.png" -bgColor $bg
New-PixelPng -grid $swordGrid -colors $swordColors -size 192 -outPath "$outDir\ic_game.png" -bgColor $bg
New-PixelPng -grid $syncGrid  -colors $syncColors  -size 192 -outPath "$outDir\ic_sync.png" -bgColor $bg
New-PixelPng -grid $swordGrid -colors $swordColors -size 96  -outPath "$outDir\ic_game_notif.png" -bgColor $transparent

Write-Host "Android icons written to $outDir"
