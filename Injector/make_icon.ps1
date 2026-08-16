# Generates Injector.ico - dark square background + white syringe outline.
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$icoPath = Join-Path $root 'Injector.ico'
function Draw-Syringe([System.Drawing.Graphics]$g) {
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point(0, 0)), (New-Object System.Drawing.Point(0, 256)),
        [System.Drawing.Color]::FromArgb(255, 46, 46, 50), [System.Drawing.Color]::FromArgb(255, 16, 16, 18))
    $g.FillRectangle($bgBrush, 0, 0, 256, 256)
    $bgBrush.Dispose()
    $g.TranslateTransform(128, 128); $g.RotateTransform(-45); $g.TranslateTransform(-128, -128)
    $white = [System.Drawing.Color]::White
    $needlePen = New-Object System.Drawing.Pen($white, 4)
    $needlePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Flat
    $needlePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($needlePen, 14, 128, 64, 128); $needlePen.Dispose()
    $barrelRect = New-Object System.Drawing.Rectangle(64, 96, 112, 64)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 12
    $path.AddArc($barrelRect.X, $barrelRect.Y, $r*2, $r*2, 180, 90)
    $path.AddArc($barrelRect.X + $barrelRect.Width - $r*2, $barrelRect.Y, $r*2, $r*2, 270, 90)
    $path.AddArc($barrelRect.X + $barrelRect.Width - $r*2, $barrelRect.Y + $barrelRect.Height - $r*2, $r*2, $r*2, 0, 90)
    $path.AddArc($barrelRect.X, $barrelRect.Y + $barrelRect.Height - $r*2, $r*2, $r*2, 90, 90)
    $path.CloseFigure()
    $barrelPen = New-Object System.Drawing.Pen($white, 7)
    $g.DrawPath($barrelPen, $path); $barrelPen.Dispose(); $path.Dispose()
    $markPen = New-Object System.Drawing.Pen($white, 4)
    foreach ($mx in 92, 110, 128, 146) { $g.DrawLine($markPen, $mx, 103, $mx, 115) }
    $markPen.Dispose()
    $rodPen = New-Object System.Drawing.Pen($white, 4)
    $g.DrawRectangle($rodPen, 176, 120, 38, 16); $rodPen.Dispose()
    $flangeRect = New-Object System.Drawing.Rectangle(214, 104, 30, 48)
    $flangePath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $fr = 8
    $flangePath.AddArc($flangeRect.X, $flangeRect.Y, $fr*2, $fr*2, 180, 90)
    $flangePath.AddArc($flangeRect.X + $flangeRect.Width - $fr*2, $flangeRect.Y, $fr*2, $fr*2, 270, 90)
    $flangePath.AddArc($flangeRect.X + $flangeRect.Width - $fr*2, $flangeRect.Y + $flangeRect.Height - $fr*2, $fr*2, $fr*2, 0, 90)
    $flangePath.AddArc($flangeRect.X, $flangeRect.Y + $flangeRect.Height - $fr*2, $fr*2, $fr*2, 90, 90)
    $flangePath.CloseFigure()
    $flangePen = New-Object System.Drawing.Pen($white, 5)
    $g.DrawPath($flangePen, $flangePath); $flangePen.Dispose(); $flangePath.Dispose()
}
$sizes = 16, 24, 32, 48, 64, 128, 256
$entries = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.ScaleTransform($s / 256.0, $s / 256.0)
    Draw-Syringe $g
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $entries += , @($s, $ms.ToArray())
    $ms.Dispose(); $bmp.Dispose()
}
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $dim = if ($e[0] -ge 256) { 0 } else { $e[0] }
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim); $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$e[1].Length); $bw.Write([UInt32]$offset)
    $offset += $e[1].Length
}
foreach ($e in $entries) { $bw.Write($e[1]) }
$bw.Flush(); $bw.Close()
Write-Output ("OK: $icoPath")
