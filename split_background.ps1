# Split a wide background image into tiles for Flipper Zero
# Usage: .\split_background.ps1 images\map_01.png

param(
    [Parameter(Mandatory=$true)]
    [string]$InputPath,
    [int]$TileWidth = 128
)

# Load System.Drawing assembly
Add-Type -AssemblyName System.Drawing

# Resolve to absolute path
$InputPath = (Resolve-Path $InputPath).Path

# Check if input file exists
if (-not (Test-Path $InputPath)) {
    Write-Error "Error: File '$InputPath' not found"
    exit 1
}

Write-Host "Loading image: $InputPath"

# Load the image
try {
    $img = [System.Drawing.Image]::FromFile($InputPath)
} catch {
    Write-Error "Error loading image: $_"
    exit 1
}

$width = $img.Width
$height = $img.Height

Write-Host "Input image: ${width}x${height} pixels"

# Calculate number of tiles needed
$numTiles = [Math]::Ceiling($width / $TileWidth)

Write-Host "Creating $numTiles tiles of ${TileWidth}px width"

# Get directory of input file (as absolute path)
$directory = Split-Path -Parent $InputPath

# Ensure directory exists
if (-not (Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

# Create tiles
for ($i = 0; $i -lt $numTiles; $i++) {
    # Calculate crop rectangle
    $left = $i * $TileWidth
    $right = [Math]::Min(($i + 1) * $TileWidth, $width)
    $actualWidth = $right - $left
    
    # Create rectangle for cropping
    $rect = New-Object System.Drawing.Rectangle($left, 0, $actualWidth, $height)
    
    # Create new bitmap for the tile
    $tile = New-Object System.Drawing.Bitmap($actualWidth, $height)
    
    # Draw the cropped portion onto the new bitmap
    $graphics = [System.Drawing.Graphics]::FromImage($tile)
    $graphics.DrawImage($img, 0, 0, $rect, [System.Drawing.GraphicsUnit]::Pixel)
    $graphics.Dispose()
    
    # Save the tile (use absolute path)
    $outputPath = Join-Path $directory "map_tile_$i.png"
    
    # Delete old file if it exists (prevents GDI+ lock issues)
    if (Test-Path $outputPath) {
        Remove-Item $outputPath -Force
    }
    
    try {
        $tile.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "  Tile ${i}: ${actualWidth}x${height}px -> $outputPath"
    } catch {
        Write-Error "Failed to save tile $i to $outputPath : $_"
        $tile.Dispose()
        $img.Dispose()
        exit 1
    }
    
    $tile.Dispose()
}

# Clean up
$img.Dispose()

Write-Host ""
Write-Host "Done! Created $numTiles tiles"
Write-Host "Tiles cover pixels 0-$width"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Verify tiles are in the images/ directory"
Write-Host "  2. Run: ufbt"
Write-Host "  3. Run: ufbt launch"