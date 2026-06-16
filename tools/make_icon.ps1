Add-Type -AssemblyName System.Drawing

[int[]]$sizes = 16, 32, 48, 256
$images = New-Object System.Collections.Generic.List[object]

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap([int]$size, [int]$size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    [double]$dsize = $size
    [double]$margin = $dsize * 0.12
    [double]$rectX = $margin
    [double]$rectY = $margin
    [double]$rectW = $dsize - 2.0 * $margin
    [double]$rectH = $dsize - 2.0 * $margin
    $rect = New-Object System.Drawing.RectangleF($rectX, $rectY, $rectW, $rectH)

    [double]$penWidth = [Math]::Max(1.0, $dsize * 0.11)
    $color = [System.Drawing.Color]::FromArgb(255, 90, 170, 230)
    $pen = New-Object System.Drawing.Pen($color, $penWidth)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

    $g.DrawArc($pen, $rect, -20.0, 160.0)
    $g.DrawArc($pen, $rect, 160.0, 160.0)

    $brush = New-Object System.Drawing.SolidBrush($color)
    [double]$cx = $dsize / 2.0
    [double]$cy = $dsize / 2.0
    [double]$r = $rectW / 2.0
    [double]$aw = $dsize * 0.16

    foreach ($angleDeg in 140.0, -40.0) {
        [double]$rad = $angleDeg * [Math]::PI / 180.0
        [double]$tipX = $cx + $r * [Math]::Cos($rad)
        [double]$tipY = $cy + $r * [Math]::Sin($rad)
        $p1 = New-Object System.Drawing.PointF(($tipX + $aw * [Math]::Cos($rad + 2.5)), ($tipY + $aw * [Math]::Sin($rad + 2.5)))
        $p2 = New-Object System.Drawing.PointF(($tipX + $aw * [Math]::Cos($rad - 2.5)), ($tipY + $aw * [Math]::Sin($rad - 2.5)))
        $tip = New-Object System.Drawing.PointF($tipX, $tipY)
        $pts = [System.Drawing.PointF[]]@($tip, $p1, $p2)
        $g.FillPolygon($brush, $pts)
    }

    $g.Dispose()
    $pen.Dispose()
    $brush.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add([PSCustomObject]@{ Size = [int]$size; Bytes = $ms.ToArray() })
    $bmp.Dispose()
}

$outPath = "C:\Source\etc\idlegame\src\app.ico"
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
