# KingCon V1.2 - Command Line Image to Big Endian Raw Converter.

Written by Soren Hannibal/Lemon.

Original source from [WinUAEDemoToolchain5](https://www.pouet.net/prod.php?which=65625).
Experimental Mac / cross platform support by Graham Bates.

## Build:

Depends on [FreeImage](https://freeimage.sourceforge.io/) library. On Mac you can run `brew install freeimage`.

To complile run:
`make all`

## Usage:

```
kingcon sourcefile destinationfile format [mode] [options...]
```

### Image Conversion Output Format

```
-F/-Format=format
```
Valid Formats are:

Format | Description | Output extension
-|-|-
`c` | Chunky | .CHK
`s[16;32;64]` | Sprite format | .SPR/.S32/.S64
`a[16;32;64]` | Attachedsprite aka 15 color sprite format | .ASP/.A32/.A64
`1-8` | Bitplane format | .BPL
`v` | Vertical fill table format | .VFT
`e` | Extra half brite bitplane format | .EHB

### Image Conversion Mode

Mode | Description
-|-
`-A/-Anim[=numFrames]` | Anim mode, finds the last decimal number in the sourcefilename, and increases by one for each frame. If NumFrames is not passed in, keeps going as long as there are more frames. Generates a bob list (.BOB).
`-B/-Bob=numBobs` | Bob mode, cut out each bob surrounded with a box and generate a bob list with offsetx/offsety/width/height/byteoffset (.BOB).
`-N/-MonospaceFont "character list"` | Monospace font mode, take each font with fixed width X (and fixed height X) a font ascii remap table (.FAR) and a bob list with offsetx/offsety/width/height/byteoffset per letter (.BOB). Note: to use the character " put in the characters \'. For newline put in `.` For `\` put in `\\`. To exclude dummy characters, use a space.
`-P/-ProportionalFont "character list"` | Proportional font mode, uses bob convert to generate font data (.BOB). Note: to use the character `"` put in the characters `\'`. For newline put in `.` For `\` put in `\\`. To exclude dummy characters, use a space.

### Image Conversion Options

Option | Default | Description | Output extension
-|-|-|-
`-G/-Gap=pixels` | `0` | Number of pixel lines to ignore between each font line (only allowed in font mode).
`-X/-Left=x` | `0` | Start X position (not allowed in bob mode)
`-Y/-Top=y` | `0` | Start Y position (not allowed in bob mode). Note that Y=0 at top of image, not bottom!
`-W/-Width=width` | width of source image | Width (not allowed in bob or proportional font mode), required for monospace font mode).
`-H/-Height=height` | height of source image| Height (not allowed in bob mode, required for monospace and proportional font mode).
`-L/-LineColors[=n]` | | generate color changes per line (n=number of colors changes allowed per line). Information is included into the copper. Only works with 12 bits
`-DW/-DoubleCopperWaits` || Make sure that linecolor copperlists have 2 copwait commands on every line (`$xxe1 $fffe $xy07 $fffe`) - default is to only add a second copwait on line 255, if needed. (Only useful with linecolors option)
`-T/-Trim` || Trim image on x and y - it will trim based on a mask color index, or 0 if no mask color has been used.
`-C/-CopperPalette[=n]` | `0` for bitmaps, `17` for sprites and attached sprites | Save copper-list ready palette (12 bits only). (not allowed with chunky format) (n=color start index) Color count is determined by bitplane count. sprites don't save out the empty color. | .COP
`-RP[24]/-RawPalette[24]` || Save raw palette in 12 or 24 bits. Note that all repalletizing code uses only 12 bits (color count is determined by bitplane count. sprites don't save out the empty color) | .PAL
`-I/-Interleaved` || Interleaved (only allowed for bitplane format)
`-M/-Mask[=n]` | `0` | Add a mask bitplane (n=mask color index). (Only allowed for bitplane format) If image is interleaved, mask is also duplicated to match number of bitplanes, and is interleaved in with the image
`-IM/-InvertMask` || Invert the mask bitplane (only valid when `-Mask` option is used.
`-AW/-AddWord` || Insert an extra word to the right of each line in the bitplane (Only allowed for bitplanes).
`-PM/-PreviewMaskColor0` || When saving out preview image, when set it will use color 0 as a mask.
`-SX/-SpriteX=n` | `129` (`$18`)| Start X position for sprite control word (Only allowed for sprites).
`-SY/-SpriteY=n` | `44` (`$2c`) | Start Y position for sprite control word and linecolor copper list (Only used for sprites, and for linecolor mode).
`-FX/-FlipX` || flip the image on X (over the Y axis)
`-R/-Rotate=n` || rotate the image. Rotation is applied after the graphics have been cut out of the source image. `1`=90 degrees clockwise, `2`=180 degrees, `3`=270 degrees
`-FT/-FileType{Main;Palette;Bob;FontTable}=x` || change the output file from raw to comma separated text file. Valid formats are `{uchar;ushort;0xushort;0xuchar;dc.w;dc.b}` (unsigned chars or shorts, either decimal or hex). adds the extension `_UChar.INL`, `_UShort.INL`,`_dcw.i`, or `_dcb.i`


### Asset Conversion Lists

```
kingcon @assetconversionlistfile
```

Asset Conversion List Files are single-byte text files containing one or more
assets to batch process. each new line is a new asset. `//` at the beginning of
a line means that the line is commented out.
