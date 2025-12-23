# mitzi-panis
A grumpy bread walking and jumping around.

## Controls
- **Left/Right:** Move Panis left or right
- **Up (single press):** Small jump (~25px high)
- **Up (hold):** Big jump (~50px high)
- **Back (hold):** Exit game

## Game dynamics  
Panis starts at the left side of the screen. He can move freely from 0px to 64px on the x-axis. Once the, the background starts scrolling instead of Panis moving.
When the right edge of the map reaches the screen edge, Panis can continue moving right. Same logic applies when moving left.

- *Grid System* (6 rows × 39 columns for 3 background image tiles of 60px height, 128px width each):
   * Row 0 is in the sky, Row 5 is on the ground
   * Cell types: 0=empty, 1=block, 2=pill
- Initialization `init_grid`
   * 0.5% random blocks floating in air (rows 0-4)
   * 5% blocks on/near ground (stacked from row 5)
   * 2% collectable pills distributed randomly

