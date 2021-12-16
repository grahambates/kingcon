//Image to raw converter

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include "FreeImage.h"

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

const unsigned int PreviewImageBGColor = 0x102030;

#define B_0000 0x0
#define B_0001 0x1
#define B_0010 0x2
#define B_0011 0x3
#define B_0100 0x4
#define B_0101 0x5
#define B_0110 0x6
#define B_0111 0x7
#define B_1000 0x8
#define B_1001 0x9
#define B_1010 0xa
#define B_1011 0xb
#define B_1100 0xc
#define B_1101 0xd
#define B_1110 0xe
#define B_1111 0xf
#define Bits(left, right) ((B_##left) << 4) | (B_##right)
const char hardcodedFont6x6[][6] =
	{
		//0
		{Bits(0001, 1110), Bits(0011, 0011), Bits(0011, 0011), Bits(0011, 0011), Bits(0011, 0011), Bits(0001, 1110)},
		//1
		{Bits(0001, 1100), Bits(0011, 1100), Bits(0000, 1100), Bits(0000, 1100), Bits(0000, 1100), Bits(0011, 1111)},
		//2
		{Bits(0001, 1110), Bits(0011, 0011), Bits(0000, 0110), Bits(0000, 1100), Bits(0001, 1000), Bits(0011, 1111)},
		//3
		{Bits(0001, 1110), Bits(0011, 0011), Bits(0000, 0110), Bits(0000, 0011), Bits(0011, 0011), Bits(0001, 1110)},
		//4
		{Bits(0011, 0110), Bits(0011, 0110), Bits(0011, 0110), Bits(0011, 1111), Bits(0000, 0110), Bits(0000, 0110)},
		//5
		{Bits(0011, 1111), Bits(0011, 0000), Bits(0011, 1110), Bits(0000, 0011), Bits(0011, 0011), Bits(0001, 1110)},
		//6
		{Bits(0001, 1111), Bits(0011, 0000), Bits(0011, 1110), Bits(0011, 0011), Bits(0011, 0011), Bits(0001, 1110)},
		//7
		{Bits(0011, 1111), Bits(0000, 0011), Bits(0000, 0110), Bits(0000, 1100), Bits(0001, 1000), Bits(0011, 0000)},
		//8
		{Bits(0001, 1110), Bits(0011, 0011), Bits(0001, 1110), Bits(0011, 0011), Bits(0011, 0011), Bits(0001, 1110)},
		//9
		{Bits(0001, 1110), Bits(0011, 0011), Bits(0011, 0011), Bits(0001, 1111), Bits(0000, 0011), Bits(0011, 1110)}};

const char *imageModeNames[] = {"Single Frame", "Animation", "Bob", "Monospace Font", "Proportional Font"};
enum SaveFileType
{
	SFT_Binary,
	SFT_UChar,
	SFT_UShort,
	SFT_DollarUChar,
	SFT_DollarUShort,
	SFT_0xUChar,
	SFT_0xUShort,
};
struct LineColorEntry
{
	unsigned short color;
};
struct SourceImage
{
	FIBITMAP *bitmap;
	FIBITMAP *maskBitmap;
	int width;
	int height;
	LineColorEntry *lineColorEntriesForEachLine;
};

struct Image
{
	enum Format
	{
		IF_Bitplanes,
		IF_Chunky,
		IF_Sprite,
		IF_AttachedSprite,
		IF_VerticalFillTable,
		//		IF_SpriteImage
	};
	class CFormatSaver *saver;
	int numBitplanes;
	int spriteWidth;
	bool extraHalfBrite;
	bool isHAM;
	short spriteStartX;
	short spriteStartY;
	enum Mode
	{
		IM_SingleFrame,
		IM_Anim,
		IM_Bob,
		IM_MonospaceFont,
		IM_ProportionalFont
	};
	Mode mode;
	char fontCharacterList[1024];
	int fontLineLengths[1024];
	int numFontLines;
	int gapBetweenTextLines;
	int numBobs;
	int numAnimFrames;
	int x;
	int y;
	int width;
	int height;
	bool trim;
	bool saveRawPalette;
	bool saveCopper;
	bool is24Bit;
	bool lineColors;
	bool doubleCopperWaits;
	int lineColorMaxChangesPerLine;
	int copperColorIndex;
	bool interleaved;
	bool addExtraBlitterWord;
	bool invertMask;
	bool mask;
	int maskColorIndex;
	bool previewMaskColor0;
	int rotate;
	bool flipX;
	SaveFileType mainFileType;
	SaveFileType paletteFileType;
	SaveFileType bobFileType;
	SaveFileType fontFileType;
	int numSourceImages;
	SourceImage *sourceImages;
};

struct Data
{
	enum ConverterType
	{
		CT_None,
		CT_Image
	};
	char srcFileName[PATH_MAX];
	char destFileName[PATH_MAX];
	ConverterType type;

	Image image;
};

//Bob is the actual structure saved out to disk
#pragma pack(push, 1)
struct Bob
{
	union
	{
		unsigned short widthInWords;
		unsigned short numSprites;
	};
	unsigned short height;
	unsigned short width;
	unsigned long offset;
	short anchorX;
	short anchorY;
};

#define MAXBITPLANES 8

//Cutout is the internal working structure
struct Cutout
{
	int imageIndex;
	int x;
	int y;
	union
	{
		char *buffer;
		struct FillTableLineReference *fillTableLineReferenceBuffer;
		struct SpriteListEntry *spriteList;
	};
	int bufferSize;

	//Bob is the structure actually saved out in the bob file
	Bob bob;
};

#pragma pack(pop)

void MyExit(int result)
{
#ifdef _DEBUG
	if (result)
		Sleep(10000);
#endif

	exit(result);
}
#define exit MyExit

#define Fatal(...) printf("Error: Kingcon: "), printf(__VA_ARGS__), printf("\n"), MyExit(1)

/** Generic image loader
@param lpszPathName Pointer to the full file name
@param flag Optional load flag constant
@return Returns the loaded dib if successful, returns NULL otherwise
*/
FIBITMAP *GenericLoader(const char *lpszPathName, int flag)
{
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;

	// check the file signature and deduce its format
	// (the second argument is currently not used by FreeImage)
	fif = FreeImage_GetFileType(lpszPathName, 0);
	if (fif == FIF_UNKNOWN)
	{
		// no signature ?
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName);
	}
	// check that the plugin has reading capabilities ...
	if ((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif))
	{
		// ok, let's load the file
		FIBITMAP *dib = FreeImage_Load(fif, lpszPathName, flag);
		// unless a bad file format, we are done !
		return dib;
	}
	Fatal("Couldn't load %s\n", lpszPathName);
	return NULL;
}

/** Generic image writer
@param dib Pointer to the dib to be saved
@param lpszPathName Pointer to the full file name
@param flag Optional save flag constant
@return Returns true if successful, returns false otherwise
*/
bool GenericWriter(FIBITMAP *dib, const char *lpszPathName, int flag)
{
	FREE_IMAGE_FORMAT fif = FIF_UNKNOWN;
	BOOL bSuccess = FALSE;

	if (dib)
	{
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName);
		if (fif != FIF_UNKNOWN)
		{
			// check that the plugin has sufficient writing and export capabilities ...
			WORD bpp = FreeImage_GetBPP(dib);
			if (FreeImage_FIFSupportsWriting(fif) && FreeImage_FIFSupportsExportBPP(fif, bpp))
			{
				// ok, we can save the file
				bSuccess = FreeImage_Save(fif, dib, lpszPathName, flag);
				// unless an abnormal bug, we are done !
			}
		}
	}
	return (bSuccess == TRUE) ? true : false;
}

// ----------------------------------------------------------

/**
FreeImage error handler
@param fif Format / Plugin responsible for the error
@param message Error message
*/

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message)
{
	printf("\n*** ");
	if (fif != FIF_UNKNOWN)
	{
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	Fatal(message);
}

void Help()
{
	printf("KingCon V1.2 - Command Line Image to Big Endian Raw Converter.\n");
	printf("Written by Soren Hannibal/Lemon.\n");
	printf("\n");
	printf("KINGCON sourcefile destinationfile format [mode] [options...]\n");
	printf("or KINGCON @assetconversionlistfile\n");

	printf("\n");

	printf("Image Conversion Output Format\n");
	printf("-F/-Format=x = format\n");
	printf("Valid Formats are\n");
	printf("     c=chunky (.CHK)\n");
	printf("     s[16;32;64]=sprite format(.SPR/.S32/.S64)\n");
	printf("     a[16;32;64]=attachedsprite aka 15 color sprite format(.ASP/.A32/.A64)\n");
	printf("     1-8=bitplane format(.BPL)\n");
	printf("     v = vertical fill table format(.VFT)\n");
	printf("     e = extra half brite bitplane format(.EHB)\n");
	//TODO HAM:	printf("     h = HAM-6 bitplane format(.HAM)\n");
	//TODO HAM:	printf("     h8 = HAM-8 bitplane format(.HAM)\n");
	//TODO CHUNKY: printf("     c = 12-bit chunky format(.CHK)\n");
	printf("\n");

	printf("Image Conversion Mode\n");
	printf("-A/-Anim[=numFrames] = anim mode, finds the last decimal number in the sourcefilename, and increases by one for each frame. If NumFrames is not passed in, keeps going as long as there are more frames. Generates a bob list (.BOB)\n");
	printf("-B/-Bob=numBobs = bob mode, cut out each bob surrounded with a box and generate a bob list with offsetx/offsety/width/height/byteoffset (.BOB)\n");
	printf("-N/-MonospaceFont \"character list\" = monospace font mode, take each font with fixed width X (and fixed height X) a font ascii remap table (.FAR) and a bob list with offsetx/offsety/width/height/byteoffset per letter (.BOB). Note: to use the character \" put in the characters \\\'. For newline put in \n. For \\ put in \\\\. To exclude dummy characters, use a space \n");
	printf("-P/-ProportionalFont \"character list\" = proportional font mode, uses bob convert to generate font data (.BOB). Note: to use the character \" put in the characters \\\'. For newline put in \n. For \\ put in \\\\. To exclude dummy characters, use a space \n");
	printf("\n");

	printf("Image Conversion Options\n");
	printf("-G/-Gap=pixels = number of pixel lines to ignore between each font line (default=0) (only allowed in font mode)\n");
	printf("-X/-Left=x = start X position (default=0) (not allowed in bob mode)\n");
	printf("-Y/-Top=y = start Y position (default=0) (not allowed in bob mode). Note that Y=0 at top of image, not bottom!\n");
	printf("-W/-Width=width = width (default=width of source image) (not allowed in bob or proportional font mode), required for monospace font mode)\n");
	printf("-H/-Height=height = height (default=height of source image (not allowed in bob mode, required for monospace and proportional font mode))\n");
	printf("\n");
	printf("-L/-LineColors[=n] = generate color changes per line (n=number of colors changes allowed per line). Information is included into the copper. Only works with 12 bits\n");

	printf("-DW/-DoubleCopperWaits = Make sure that linecolor copperlists have 2 copwait commands on every line ($xxe1 $fffe $xy07 $fffe) - default is to only add a second copwait on line 255, if needed. (Only useful with linecolors option)\n");

	printf("-T/-Trim = trim image on x and y - it will trim based on a mask color index, or 0 if no mask color has been used\n");
	printf("-C/-CopperPalette[=n] = save copper-list ready palette (12 bits only). (not allowed with chunky format) (n=color start index. Default is 0 for bitmaps, 17 for sprites and attached sprites) (color count is determined by bitplane count. sprites don't save out the empty color) (.COP)\n");
	printf("-RP[24]/-RawPalette[24] = save raw palette in 12 or 24 bits. Note that all repalletizing code uses only 12 bits (color count is determined by bitplane count. sprites don't save out the empty color) (.PAL)\n");
	printf("-I/-Interleaved = interleaved (only allowed for bitplane format)\n");
	printf("-M/-Mask[=n] = add a mask bitplane (n=mask color index - default=0). (Only allowed for bitplane format) If image is interleaved, mask is also duplicated to match number of bitplanes, and is interleaved in with the image\n");
	printf("-IM/-InvertMask = invert the mask bitplane (only valid when -Mask optin is used\n");
	printf("-AW/-AddWord= insert an extra word to the right of each line in the bitplane (Only allowed for bitplanes).\n");
	printf("-PM/-PreviewMaskColor0 = when saving out preview image, when set it will use color 0 as a mask \n");
	printf("-SX/-SpriteX=n = start X position for sprite control word (Default is 129 ($81)) (Only allowed for sprites)\n");
	printf("-SY/-SpriteY=n = start Y position for sprite control word and linecolor copper list (Default is 44 ($2c)) (Only used for sprites, and for linecolor mode)\n");
	printf("-FX/-FlipX = flip the image on X (over the Y axis)\n");
	printf("-R/-Rotate=n = rotate the image. Rotation is applied after the graphics have been cut out of the source image. 1=90 degrees clockwise, 2=180 degrees, 3=270 degrees \n");
	printf("-FT/-FileType{Main;Palette;Bob;FontTable}=x = change the output file from raw to comma separated text file. Valid formats are {uchar;ushort;0xushort;0xuchar;dc.w;dc.b} (unsigned chars or shorts, either decimal or hex). adds the extension _UChar.INL, _UShort.INL,_dcw.i, or _dcb.i\n");
	printf("\n");
	printf("Asset Conversion List Files are single-byte text files containing one or more assets to batch process. each new line is a new asset. // at the beginning of a line means that the line is commented out\n");
}

unsigned long HalfBrite(unsigned long color)
{
	//This assumes that 0xfff and 0xeee both 0x777. This tries to be correct for OCS, not AGA (which may be idfferent)
	//This matches the expectation from the EAB community about EHB, though they didn't seem sure.
	return (color & 0xeeeeee) >> 1;
}
unsigned short HalfBrite12Bit(unsigned short color)
{
	//This assumes that 0xfff and 0xeee both 0x777. This tries to be correct for OCS, not AGA (which may be idfferent)
	//This matches the expectation from the EAB community about EHB, though they didn't seem sure.
	return (color & 0xeee) >> 1;
}
unsigned short DoubleBrite12Bit(unsigned short color)
{
	if (color & 0x888)
		Fatal("Something went wrong in halfbrite conversion");
	return (color & 0x777) << 1;
}

unsigned short MaskToByteOffset(unsigned int mask)
{
	if (mask == 0xff)
		return 0;
	if (mask == 0xff00)
		return 1;
	if (mask == 0xff0000)
		return 2;
	Fatal("Color Mask returned unexpected result");
	return 0;
}
RGBQUAD Convert12BitToRGBQUAD(unsigned short color)
{
	RGBQUAD result;
	result.rgbReserved = 0;
	result.rgbRed = ((color & 0xf00) >> 8) * 17;
	result.rgbGreen = ((color & 0xf0) >> 4) * 17;
	result.rgbBlue = ((color & 0xf) >> 0) * 17;
	return result;
}
unsigned short ConvertRGBQUADTo12Bit(const RGBQUAD &color)
{
	//	unsigned short result = (((color.rgbRed + 8) / 17) << 8) | (((color.rgbGreen + 8) / 17) << 4) | (((color.rgbBlue + 8) / 17) << 0);
	//make sure that 0xff and 0x f0 both get converted to 0xf
	unsigned short result = (((color.rgbRed) / 16) << 8) | (((color.rgbGreen) / 16) << 4) | (((color.rgbBlue) / 16) << 0);
	if (result > 0xfff)
		Fatal("Internal error in ConvertRGBQUADTo12Bit");
	return result;
}
unsigned long ConvertRGBQUADTo24Bit(const RGBQUAD &color)
{
	unsigned long result = (color.rgbRed << 16) | (color.rgbGreen << 8) | color.rgbBlue;
	if (result > 0xffffff)
		Fatal("Internal error in ConvertRGBQUADTo24Bit");
	return result;
}
unsigned long Convert12BitTo24Bit(unsigned short color)
{
	//convert 0xf to 0xff
	unsigned long result = (((color & 0xf00) << 8) * 17) + (((color & 0xf0) << 4) * 17) + ((color & 0xf) * 17);
	return result;
}
unsigned short Convert24BitTo12Bit(unsigned long color)
{
	//make sure that 0xff and 0x f0 both get converted to 0xf
	unsigned short result = ((((color & 0xff0000) >> 16) / 16) << 8) +
							((((color & 0xff00) >> 8) / 16) << 4) +
							((color & 0xff) / 16);
	return result;
}

SaveFileType ConvertOptionToFileType(const char *parameter, const char *option)
{
	if (!strcasecmp(parameter, "ushort"))
	{
		return SFT_UShort;
	}
	if (!strcasecmp(parameter, "uchar"))
	{
		return SFT_UChar;
	}
	if (!strcasecmp(parameter, "dc.w"))
	{
		return SFT_DollarUShort;
	}
	if (!strcasecmp(parameter, "dc.b"))
	{
		return SFT_DollarUChar;
	}
	if (!strcasecmp(parameter, "0Xushort"))
	{
		return SFT_0xUShort;
	}
	if (!strcasecmp(parameter, "0xuchar"))
	{
		return SFT_0xUChar;
	}
	Fatal("unknown parameter for option %s\n", option);
	return SFT_Binary;
}
int GetInteger(const char *parameter, const char *option)
{
	int result = 0;
	if (!*parameter)
	{
		Fatal("Internal error - got zero length parameter for option %s\n", option);
	}
	if (*parameter == '$')
	{
		parameter++;
		while (*parameter)
		{
			if (*parameter >= '0' && *parameter <= '9')
			{
				result = result * 16 + (*parameter - '0');
			}
			else if (*parameter >= 'a' && *parameter <= 'f')
			{
				result = result * 16 + (*parameter - 'a') + 0xa;
			}
			else if (*parameter >= 'A' && *parameter <= 'F')
			{
				result = result * 16 + (*parameter - 'A') + 0xa;
			}
			else
			{
				Fatal("Invalid integer parameter for option %s\n", option);
			}
			parameter++;
		}
	}
	else
	{
		while (*parameter)
		{
			if (*parameter < '0' || *parameter > '9')
			{
				Fatal("Invalid integer parameter for option %s\n", option);
			}
			result = result * 10 + (*parameter - '0');
			parameter++;
		}
	}
	return result;
}
const char *GetParameterForOption(const char *option, const char *parameter)
{
	if (!parameter)
	{
		Fatal("Missing parameter for option %s\n", option);
	}
	return parameter;
}

//use this in the special case where we can't use \x=y, and instead use \x y
const char *GetParameterForOptionAlternate(int &i, int &argc, char *argv[], const char *option)
{
	i++;
	if (i >= argc)
	{
		Fatal("Missing parameter for option %s\n", option);
	}
	return argv[i];
}

int CheckLineVertical(const unsigned char *sourcePtr, int sourceImageHeight, int sourceImagePitch, int x, int startX, int endX, int startY, int endY, bool mustHaveAnchor)
{
	int anchorPos = -1;
	//check if there is an unbroken, single-colored line along one edge, vertical or horizontal. Easiest way to check is to just check a box

	const unsigned char *cornerPosPtr = sourcePtr + startY * sourceImagePitch + x;
	for (int y = startY; y < endY; y++)
	{
		const unsigned char *posPtr = sourcePtr + y * sourceImagePitch + x;
		if (*posPtr != *cornerPosPtr)
		{
			//if mustHaveAnchor is true, this edge has to have a single dot indicating an anchor position, otherwise it must be single colored
			if (mustHaveAnchor && anchorPos == -1)
			{
				anchorPos = y;
			}
			else
				Fatal("The bob cutout process found a bounding box that was not single-colored with an anchor dot at bottom and left edges for the box at (%d,%d) (%d,%d)", startX, startY, endX - 1, endY - 1);
		}
	}

	if (anchorPos == -1)
	{
		if (mustHaveAnchor)
			Fatal("The bob cutout process did not find anchor dots at bottom or left edges for the box at (%d,%d) (%d,%d)", startX, startY, endX - 1, endY - 1);
		return 0;
	}
	return anchorPos - (startY + 1);
}

int CheckLineHorizontal(const unsigned char *sourcePtr, int sourceImageHeight, int sourceImagePitch, int y, int startX, int endX, int startY, int endY, bool mustHaveAnchor)
{
	int anchorPos = -1;
	//check if there is an unbroken, single-colored line along one edge, vertical or horizontal. Easiest way to check is to just check a box

	const unsigned char *cornerPosPtr = sourcePtr + y * sourceImagePitch + startX;
	for (int x = startX; x < endX; x++)
	{
		const unsigned char *posPtr = sourcePtr + y * sourceImagePitch + x;
		if (*posPtr != *cornerPosPtr)
		{
			//if mustHaveAnchor is true, this edge has to have a single dot indicating an anchor position, otherwise it must be single colored
			if (mustHaveAnchor && anchorPos == -1)
			{
				anchorPos = x;
			}
			else
				Fatal("The bob cutout process found a bounding box that was not single-colored with an anchor dot at bottom and left edges for the box at (%d,%d) (%d,%d)", startX, startY, endX - 1, endY - 1);
		}
	}
	if (anchorPos == -1)
	{
		if (mustHaveAnchor)
			Fatal("The bob cutout process did not find anchor dots at bottom or left edges for the box at (%d,%d) (%d,%d)", startX, startY, endX - 1, endY - 1);
		return 0;
	}
	return anchorPos - (startX + 1);
}

void StoreFontCharacterList(Data &data, int &i, int &argc, char *argv[], const char *option, bool isFromConversionList)
{
	const char *fontCharacterList = GetParameterForOptionAlternate(i, argc, argv, option);
	sprintf(data.image.fontCharacterList, "%s", fontCharacterList);
	if (!strlen(data.image.fontCharacterList) && strlen(fontCharacterList))
	{
		Fatal("Font character list contains characters above 0xff - that is not supported. Sometimes this is caused by batch files having their non-ascii characters converted incorrectly. Use a Conversion File instead.");
	}

	//strcpy(data.image.fontCharacterList, GetParameterForOptionAlternate(i, argc, argv, option));
	if (!isFromConversionList)
	{
		const char *tempPtr = data.image.fontCharacterList;
		while (*tempPtr)
		{
			if ((*tempPtr) & 0x80)
			{
				Fatal("Font character list contains non-ascii characters - that doens't work from command line. Use a Conversion File instead.");
			}
			tempPtr++;
		}
	}
}
int FindCutoutsLine(Image &image, const SourceImage &sourceImage, int rowStartY, int x, int height, Cutout *cutouts, int numCutoutsExpected, const char *itemName, bool performBoxCheck)
{
	const unsigned char *sourcePtr = FreeImage_GetBits(sourceImage.bitmap);
	const unsigned char *maskPtr = FreeImage_GetBits(sourceImage.maskBitmap);
	int sourceImagePitch = FreeImage_GetPitch(sourceImage.bitmap);
	int maskImagePitch = FreeImage_GetPitch(sourceImage.maskBitmap);
	int rowEndY = rowStartY + height;
	int numBobsFoundInLine = 0;
	//we have located a number of non-empty rows, in which we can look for sprite boxes
	int startX = x - 1;
	for (int j = x; j < sourceImage.width + 1; j++)
	{
		//scan left to right to find boxes to find out whih columns are empty, using the same method as above to find start and end rows
		bool isColumnEmpty = true;

		//special case: right edge +1 is empty
		if (j < sourceImage.width)
		{
			for (int k = rowStartY; k < rowEndY; k++)
			{
				const unsigned char *rowPtr = sourcePtr + k * sourceImagePitch + j;
				const unsigned char *maskRowPtr = maskPtr + k * maskImagePitch + j;
				//note: searching upwards here???
				if (*maskRowPtr)
					//				if (*rowPtr != image.maskColorIndex)
					isColumnEmpty = false;
			}
		}

		if (!isColumnEmpty && startX == x - 1)
			startX = j;
		else if (isColumnEmpty && startX != x - 1)
		{
			int endX = j;

			//narrow down to the proper Y positions for this sprite by finding which rows are empty
			int startY = -1;
			int endY = -1;
			for (int k = rowStartY; k < rowEndY; k++)
			{
				const unsigned char *rowPtr = sourcePtr + k * sourceImagePitch;
				const unsigned char *maskRowPtr = maskPtr + k * maskImagePitch;
				//check if row is empty, and if not, update startY and endY
				for (int l = startX; l < endX; l++)
				{
					if (maskRowPtr[l])
					//					if (rowPtr[l] != image.maskColorIndex)
					{
						if (startY == -1)
							startY = k;
						endY = k + 1;
						break;
					}
				}
			}
			if (startY == -1 || endY == -1)
				Fatal("Internal error in %s cutout process", itemName);

			if (endY - startY <= 2 || endX - startX <= 2)
			{
				Fatal("%s cutout at (%d,%d) is less than 1 pixel large", itemName, startX, startY);
			}

			int anchorX = 0;
			int anchorY = 0;
			if (performBoxCheck)
			{
				//verify that each bob is surrounded by a box that has center points. if not, print error
				CheckLineHorizontal(sourcePtr, sourceImage.height, sourceImagePitch, startY, startX, endX, startY, endY, false);			//top
				anchorX = CheckLineHorizontal(sourcePtr, sourceImage.height, sourceImagePitch, endY - 1, startX, endX, startY, endY, true); //bottom

				CheckLineVertical(sourcePtr, sourceImage.height, sourceImagePitch, endX - 1, startX, endX, startY, endY, false);		//right
				anchorY = CheckLineVertical(sourcePtr, sourceImage.height, sourceImagePitch, startX, startX, endX, startY, endY, true); //left
				//remove the box  order
				startX++;
				startY++;
				endX--;
				endY--;
			}
			else
			{
				startY = rowStartY;
				endY = rowStartY + height;
			}

			//store that box, if we are not out of box spaces...
			if (numBobsFoundInLine >= numCutoutsExpected)
			{
				Fatal("%s cutout process failed - expected to find %d cutouts, but found more than that", itemName, numCutoutsExpected);
			}

			cutouts[numBobsFoundInLine].imageIndex = 0;
			cutouts[numBobsFoundInLine].x = startX;
			cutouts[numBobsFoundInLine].y = startY;
			cutouts[numBobsFoundInLine].bob.width = (endX - startX);
			cutouts[numBobsFoundInLine].bob.height = (endY - startY);
			cutouts[numBobsFoundInLine].bob.anchorX = anchorX;
			cutouts[numBobsFoundInLine].bob.anchorY = anchorY;

			numBobsFoundInLine++;

			startX = -1;
		}
	}
	return numBobsFoundInLine;
}

int FindCutouts(Image &image, const SourceImage &sourceImage, Cutout *cutouts, int numCutoutsExpected, const char *itemName)
{
	int numBobsFound = 0;
	//find cutouts for bobs
	int rowStartY = -1;
	int rowNo = 0;
	for (int i = 0; i < sourceImage.height + 1; i++)
	{
		const unsigned char *rowPtr = FreeImage_GetScanLine(sourceImage.bitmap, i);
		const unsigned char *maskRowPtr = FreeImage_GetScanLine(sourceImage.maskBitmap, i);
		//scan top to bottom which rows are not empty to find where cutouts start, then find out first empty row where the where cutouts end
		bool isRowEmpty = true;
		//special case: bottom edge +1 is always empty
		if (i < sourceImage.height)
		{
			for (int j = 0; j < sourceImage.width; j++)
			{
				if (maskRowPtr[j])
					//				if (rowPtr[j] != image.maskColorIndex)
					isRowEmpty = false;
			}
		}
		if (!isRowEmpty && rowStartY == -1)
			rowStartY = i;
		else if (isRowEmpty && rowStartY != -1)
		{
			int rowEndY = i;

			//			printf("Finding %ss in row %d - between line %d and %d\n",itemName,rowNo,rowStartY,rowEndY-1);
			rowNo++;
			int numBobsFoundInLine = FindCutoutsLine(image, sourceImage, rowStartY, 0, rowEndY - rowStartY, cutouts + numBobsFound, numCutoutsExpected - numBobsFound, itemName, true);
			numBobsFound += numBobsFoundInLine;
			if (numBobsFound > numCutoutsExpected)
			{
				Fatal("%s cutout process failed - expected to find %d cutouts, but found more than that", itemName, numCutoutsExpected);
			}
			rowStartY = -1;
		}
	}

	return numBobsFound;
}

const char *GetFileTypeExtension(SaveFileType saveFileType)
{
	if (saveFileType == SFT_UChar || saveFileType == SFT_0xUChar)
	{
		return "_UChar.INL";
	}
	if (saveFileType == SFT_UShort || saveFileType == SFT_0xUShort)
	{
		return "_UShort.INL";
	}
	if (saveFileType == SFT_DollarUChar)
	{
		return "_dcb.i";
	}
	if (saveFileType == SFT_DollarUShort)
	{
		return "_dcw.i";
	}
	return "";
}
bool FileExists(const char *fileName)
{
	FILE *handle;
	if (handle = fopen(fileName, "rb"))
	{
		fclose(handle);
		return true;
	}
	return false;
}

void FileCreate(FILE *&handle, const char *fileName)
{
	handle = fopen(fileName, "wb");
	if (!handle)
		Fatal("Couldn't create %s", fileName);
}
void FileWrite(const void *ptr, size_t size, FILE *handle, const char *fileName, SaveFileType saveFileType)
{
	if (saveFileType == SFT_Binary)
	{
		if (fwrite(ptr, 1, size, handle) != (unsigned int)(size))
			Fatal("Couldn't write to %s", fileName);
	}
	else
	{
		long curPosInFile = ftell(handle);
		int outputBufferSize = size * 16 + 1;
		char *outputBuffer = new char[outputBufferSize];
		outputBuffer[0] = 0;
		char *outputBufferTemp = outputBuffer;

		const char *formatString = "%d";
		const char *formatStringFirstLineStart = "";
		const char *formatStringLineStart = "\n, ";
		const char *formatStringFirstLineStartInBlock = ", ";
		const char *formatStringSeparator = ", ";
		int dataSize = 2;
		switch (saveFileType)
		{
		case SFT_DollarUShort:
			formatString = "$%04x";
			formatStringFirstLineStart = "	dc.w	";
			formatStringFirstLineStartInBlock = "	dc.w	";
			formatStringLineStart = "\n	dc.w	";
			break;
		case SFT_DollarUChar:
			formatString = "$%02x";
			formatStringFirstLineStart = "	dc.b	";
			formatStringFirstLineStartInBlock = "	dc.b	";
			formatStringLineStart = "\n	dc.b	";
			dataSize = 1;
			break;
		case SFT_0xUShort:
			formatString = "0x%04x";
			break;
		case SFT_0xUChar:
			formatString = "0x%02x";
			dataSize = 1;
			break;
		case SFT_UShort:
			break;
		case SFT_UChar:
			dataSize = 1;
			break;
		default:
			Fatal("Internal error: invalid or improperly implemented save file type");
			break;
		}
		unsigned short *shortPtr = (unsigned short *)ptr;
		unsigned char *charPtr = (unsigned char *)ptr;
		if (size & (dataSize - 1))
		{
			Fatal("Internal Error: Was trying to save a non-even number of bytes in short-format to file %s", fileName);
		}
		int entriesPerLine = 16 / dataSize;
		for (size_t i = 0; i < (size / dataSize); i++)
		{
			unsigned short data = 0;
			if (dataSize == 1)
			{
				data = *(charPtr++);
			}
			else
			{
				data = ntohs(*(shortPtr++));
			}
			if (!(i % entriesPerLine))
			{
				if (!curPosInFile)
				{
					sprintf(outputBufferTemp, formatStringFirstLineStart);
					curPosInFile = 1;
				}
				else if (!i)
				{
					sprintf(outputBufferTemp, formatStringFirstLineStartInBlock);
				}
				else
				{
					sprintf(outputBufferTemp, formatStringLineStart);
				}
			}
			else
			{
				sprintf(outputBufferTemp, formatStringSeparator);
			}
			outputBufferTemp += strlen(outputBufferTemp);

			sprintf(outputBufferTemp, formatString, data);
			outputBufferTemp += strlen(outputBufferTemp);
		};

		sprintf(outputBufferTemp, "\n");
		outputBufferTemp += strlen(outputBufferTemp);

		if ((outputBufferTemp - outputBuffer) > outputBufferSize)
			Fatal("Internal error: Buffer overrun in FileWrite to %s", fileName);
		if (fwrite(outputBuffer, 1, outputBufferTemp - outputBuffer, handle) != (unsigned int)(outputBufferTemp - outputBuffer))
			Fatal("Couldn't write to %s", fileName);
		delete outputBuffer;
	}
}
void FileClose(FILE *handle, const char *fileName)
{
	if (fclose(handle))
		Fatal("Failed closing %s", fileName);
}

class CFormatSaver
{
public:
	CFormatSaver()
	{
	}
	virtual ~CFormatSaver()
	{
	}
	virtual Image::Format Format() = 0;
	virtual const char *FormatName() = 0;
	virtual bool ReserveColor0() { return false; }
	virtual bool SupportsLineColors() { return false; }
	virtual int FirstCopperColorIndex() { return 0; }
	virtual bool RequiresPalletizedImage() { return false; }
	virtual bool Requires32BitImage() { return false; }
	virtual void AllocateConversionBuffers(Image &image, int numCutouts, const Cutout *cutouts) = 0;
	virtual void PrepareCutout(Image &image, Cutout *cutout) = 0;
	virtual void PerformCutout(Image &image, const Cutout *cutout, const SourceImage &sourceImage) = 0;
	virtual void SaveImage(const Image &image, const char *destFileName, int numCutouts, const Cutout *cutouts) = 0;
	virtual void ExtractToPreviewImage(const Image &image, const Cutout *cutout, const unsigned long *palette24, unsigned int *cutoutBitmapPtr, int previewImagePitch) = 0;
};

class CBitplaneFormatSaver : public CFormatSaver
{
	int numMaskPlanes;

public:
	CBitplaneFormatSaver()
	{
		numMaskPlanes = 0;
	}
	Image::Format Format() override { return Image::IF_Bitplanes; }
	const char *FormatName() override { return "Bitplanes"; }
	bool SupportsLineColors() override { return true; }
	bool RequiresPalletizedImage() override { return true; }

	void AllocateConversionBuffers(Image &image, int numCutouts, const Cutout *cutouts) override
	{
		if (image.mask)
		{
			if (image.interleaved)
				numMaskPlanes = image.numBitplanes;
			else
				numMaskPlanes = 1;
		}
	}
	int perLineOffset = 0;
	int perPlaneOffset = 0;
	int maskPlaneOffset = 0;
	virtual void PrepareCutout(Image &image, Cutout *cutout) override
	{
		int widthBytes = ((cutout->bob.width + 15) / 16) * 2;
		if (image.addExtraBlitterWord)
		{
			widthBytes += 2;
		}
		cutout->bufferSize = widthBytes * cutout->bob.height * (image.numBitplanes + numMaskPlanes);
		cutout->buffer = (char *)malloc(cutout->bufferSize);
		memset(cutout->buffer, 0, cutout->bufferSize);
		perLineOffset = widthBytes;
		perPlaneOffset = widthBytes * cutout->bob.height;
		maskPlaneOffset = perPlaneOffset * image.numBitplanes;
		if (image.interleaved)
		{
			perLineOffset = widthBytes * image.numBitplanes;
			perPlaneOffset = widthBytes;
			if (image.mask)
			{
				maskPlaneOffset = widthBytes;
				perPlaneOffset *= 2;
				perLineOffset *= 2;
			}
		}
		cutout->bob.widthInWords = widthBytes / 2;
	}
	virtual void PerformCutout(Image &image, const Cutout *cutout, const SourceImage &sourceImage) override
	{
		for (int y = 0; y < cutout->bob.height; y++)
		{

			const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, cutout->y + y);
			sourcePtr += cutout->x;
			const unsigned char *maskPtr = FreeImage_GetScanLine(sourceImage.maskBitmap, cutout->y + y);
			maskPtr += cutout->x;

			char *destPtr = cutout->buffer + perLineOffset * y;
			for (int x = 0; x < cutout->bob.width; x++)
			{
				//read pixel
				char colorIndex = sourcePtr[x];

				//convert color, find mask, and crop values outside color range
				int bitFlag = 1 << ((x & 7) ^ 7);
				//write image to the right format
				for (int j = 0; j < image.numBitplanes; j++)
				{
					if (colorIndex & (1 << j))
						destPtr[j * perPlaneOffset + x / 8] |= bitFlag;
				}
				//write mask (to n bitplanes)
				if (maskPtr[x])
				{
					for (int j = 0; j < numMaskPlanes; j++)
						destPtr[maskPlaneOffset + j * perPlaneOffset + x / 8] |= bitFlag;
				}
			}
			if (image.invertMask)
			{
				for (int x = 0; x < cutout->bob.widthInWords * 2; x++)
				{
					for (int j = 0; j < numMaskPlanes; j++)
						destPtr[maskPlaneOffset + j * perPlaneOffset + x] = ~destPtr[maskPlaneOffset + j * perPlaneOffset + x];
				}
			}
		}
	}
	void SaveImage(const Image &image, const char *destFileName, int numCutouts, const Cutout *cutouts) override
	{
		char tempstr[PATH_MAX];
		strcpy(tempstr, destFileName);
		if (image.extraHalfBrite)
		{
			strcat(tempstr, ".EHB");
		}
		else if (image.isHAM)
		{
			strcat(tempstr, ".HAM");
		}
		else
		{
			strcat(tempstr, ".BPL");
		}
		strcat(tempstr, GetFileTypeExtension(image.mainFileType));

		//save bitplane data
		FILE *handle = 0;
		FileCreate(handle, tempstr);

		for (int i = 0; i < numCutouts; i++)
		{
			FileWrite(cutouts[i].buffer, cutouts[i].bufferSize, handle, tempstr, image.mainFileType);
		}

		FileClose(handle, tempstr);
	}
	void ExtractToPreviewImage(const Image &image, const Cutout *cutout, const unsigned long *palette24, unsigned int *cutoutBitmapPtr, int previewImagePitch) override
	{
		int widthBytes = cutout->bob.widthInWords * 2;

		int perLineOffset = widthBytes;
		int perPlaneOffset = widthBytes * cutout->bob.height;
		int maskPlaneOffset = perPlaneOffset * image.numBitplanes;
		if (image.interleaved)
		{
			perLineOffset = widthBytes * image.numBitplanes;
			perPlaneOffset = widthBytes;
			if (image.mask)
			{
				maskPlaneOffset = widthBytes;
				perPlaneOffset *= 2;
				perLineOffset *= 2;
			}
		}
		unsigned char maskFlip = 0;
		if (image.invertMask)
			maskFlip = 0xff;
		unsigned long lineColorPerLinePalette[1 << MAXBITPLANES];

		for (int y = 0; y < cutout->bob.height; y++)
		{
			const unsigned long *actualPalette24 = palette24;
			if (image.lineColors)
			{
				actualPalette24 = lineColorPerLinePalette;

				int maxColorsPerLine = (1 << image.numBitplanes);
				memset(lineColorPerLinePalette, 0, sizeof(unsigned long) * (maxColorsPerLine));
				for (int index = 0; index < maxColorsPerLine; index++)
				{
					lineColorPerLinePalette[index] = Convert12BitTo24Bit(image.sourceImages[cutout->imageIndex].lineColorEntriesForEachLine[(y + cutout->y) * maxColorsPerLine + index].color);
				}
			}
			for (int x = 0; x < widthBytes * 8; x++)
			{
				int xBit = (1 << ((x & 7) ^ 7));
				int xByte = x >> 3;

				unsigned long color = 0;
				if (image.mask && !((cutout->buffer[xByte + y * perLineOffset + maskPlaneOffset] ^ maskFlip) & xBit))
				{
					color = PreviewImageBGColor;
				}
				else
				{
					int colorIndex = 0;
					for (int k = 0; k < image.numBitplanes; k++)
					{
						if (cutout->buffer[xByte + y * perLineOffset + k * perPlaneOffset] & xBit)
							colorIndex |= 1 << k;
					}
					if (image.isHAM)
					{
						//TODO HAM: convert colors using HAM logic here
						color = 0;
					}
					else if (image.extraHalfBrite && colorIndex >= 32)
					{
						color = actualPalette24[colorIndex - 32];
						color = HalfBrite(color);
					}
					else
					{
						color = actualPalette24[colorIndex];
					}
					if (!image.previewMaskColor0 || colorIndex != 0)
					{
						color |= 0xff000000;
					}
				}
				cutoutBitmapPtr[x + y * previewImagePitch] = color;
			}
		}
	}
};

struct SpriteListEntry
{
	unsigned short spriteOffset;
};
class CSpriteFormatSaver : public CFormatSaver
{
	char *spriteBuffer;
	int spriteBufferSize;
	int spriteBufferUsed;

public:
	CSpriteFormatSaver()
	{
		spriteBuffer = 0;
		spriteBufferSize = 0;
		spriteBufferUsed = 0;
	}
	~CSpriteFormatSaver() override
	{
		if (spriteBuffer)
		{
			free(spriteBuffer);
		}
	}
	Image::Format Format() override { return Image::IF_Sprite; }
	const char *FormatName() override { return "Sprite"; }
	bool ReserveColor0() override { return true; }
	int FirstCopperColorIndex() override { return 17; }
	bool RequiresPalletizedImage() override { return true; }
	virtual bool IsAttached() { return false; }

	void AllocateConversionBuffers(Image &image, int numCutouts, const Cutout *cutouts) override
	{
		for (int i = 0; i < numCutouts; i++)
		{
			int numSpritesInCutout = (cutouts[i].bob.width + image.spriteWidth - 1) / image.spriteWidth;
			if (IsAttached())
			{
				numSpritesInCutout *= 2;
			}
			spriteBufferSize += numSpritesInCutout * (image.spriteWidth / 8 * 2) * (cutouts[i].bob.height + 2);
		}
		spriteBuffer = (char *)malloc(spriteBufferSize);
		memset(spriteBuffer, 0, spriteBufferSize);
	}
	virtual void PrepareCutout(Image &image, Cutout *cutout) override
	{
		int numSpritesInCutout = (cutout->bob.width + image.spriteWidth - 1) / image.spriteWidth;
		if (IsAttached())
		{
			numSpritesInCutout *= 2;
		}
		if (numSpritesInCutout > 8)
			Fatal("More than 8 sprites required - please trim down the image width");

		cutout->bufferSize = numSpritesInCutout * sizeof(SpriteListEntry);
		cutout->spriteList = (SpriteListEntry *)malloc(cutout->bufferSize);
		cutout->bob.numSprites = numSpritesInCutout;
		memset(cutout->spriteList, 0, cutout->bufferSize);
	}
	virtual void PerformCutout(Image &image, const Cutout *cutout, const SourceImage &sourceImage) override
	{
		int spriteIndex = 0;
		for (int spriteStartX = 0; spriteStartX < cutout->bob.width; spriteStartX += image.spriteWidth)
		{
			int spriteCount = (IsAttached()) ? 2 : 1;
			for (int isOddAttachedSprite = 0; isOddAttachedSprite < spriteCount; isOddAttachedSprite++)
			{
				cutout->spriteList[spriteIndex].spriteOffset = spriteBufferUsed;
				spriteIndex++;

				short yPos = cutout->y + image.spriteStartY - image.y;
				short xPos = cutout->x + image.spriteStartX + spriteStartX - image.x;
				short yPosEnd = yPos + cutout->bob.height;
				unsigned short sprpos = ((yPos & 0xff) << 8) + ((xPos & 0x1fe) >> 1);
				unsigned short sprctl = ((yPosEnd & 0xff) << 8) + ((yPos & 0x100) >> 6) + ((yPosEnd & 0x100) >> 7) + (xPos & 0x1);
				if (isOddAttachedSprite)
					sprctl |= 0x80;
				*(unsigned short *)(spriteBuffer + spriteBufferUsed) = htons(sprpos);
				spriteBufferUsed += image.spriteWidth / 8;
				*(unsigned short *)(spriteBuffer + spriteBufferUsed) = htons(sprctl);
				spriteBufferUsed += image.spriteWidth / 8;

				for (int y = 0; y < cutout->bob.height; y++)
				{

					const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, cutout->y + y);
					sourcePtr += cutout->x + spriteStartX;

					for (int x = 0; x < cutout->bob.width - spriteStartX && x < image.spriteWidth; x++)
					{
						//read pixel
						char colorIndex = sourcePtr[x];

						//convert color, find mask, and crop values outside color range
						int bitFlag = 1 << ((x & 7) ^ 7);
						//write image to the right format - for the 2 bitplanes
						for (int j = 0; j < 2; j++)
						{
							if (colorIndex & (1 << (j + 2 * isOddAttachedSprite)))
							{
								spriteBuffer[spriteBufferUsed + j * (image.spriteWidth / 8) + x / 8] |= bitFlag;
							}
						}
					}
					spriteBufferUsed += 2 * image.spriteWidth / 8;
				}

				//control word end
				spriteBufferUsed += 2 * image.spriteWidth / 8;

				if (spriteBufferUsed > spriteBufferSize)
					Fatal("Internal error in sprite process");
			}
		}
	}
	void SaveImage(const Image &image, const char *destFileName, int numCutouts, const Cutout *cutouts) override
	{
		char tempstr[PATH_MAX];
		strcpy(tempstr, destFileName);
		if (IsAttached())
		{
			strcat(tempstr, ".ASP");
		}
		else
		{
			strcat(tempstr, ".SPR");
		}
		if (image.spriteWidth == 32)
		{
			tempstr[strlen(tempstr) - 2] = '3';
			tempstr[strlen(tempstr) - 1] = '2';
		}
		if (image.spriteWidth == 64)
		{
			tempstr[strlen(tempstr) - 2] = '6';
			tempstr[strlen(tempstr) - 1] = '4';
		}
		strcat(tempstr, GetFileTypeExtension(image.mainFileType));

		//save sprite data
		FILE *handle = 0;
		FileCreate(handle, tempstr);

		int totalCutoutSize = 0;
		//1. sum up width of all sprite lists
		for (int i = 0; i < numCutouts; i++)
		{
			totalCutoutSize += cutouts[i].bufferSize;
		}

		for (int i = 0; i < numCutouts; i++)
		{
			int numSpritesInCutout = (cutouts[i].bob.width + image.spriteWidth - 1) / image.spriteWidth;
			if (IsAttached())
			{
				numSpritesInCutout *= 2;
			}
			for (int j = 0; j < numSpritesInCutout; j++)
			{
				cutouts[i].spriteList[j].spriteOffset += totalCutoutSize;
				cutouts[i].spriteList[j].spriteOffset = htons(cutouts[i].spriteList[j].spriteOffset);
			}
			//2. write individual sprite lists
			FileWrite(cutouts[i].spriteList, cutouts[i].bufferSize, handle, tempstr, image.mainFileType);

			//remove the the buffer offsets again
			for (int j = 0; j < numSpritesInCutout; j++)
			{
				cutouts[i].spriteList[j].spriteOffset = ntohs(cutouts[i].spriteList[j].spriteOffset);
				cutouts[i].spriteList[j].spriteOffset -= totalCutoutSize;
			}
		}

		//3. write actual sprite data
		FileWrite(spriteBuffer, spriteBufferUsed, handle, tempstr, image.mainFileType);

		FileClose(handle, tempstr);
	}
	void ExtractToPreviewImage(const Image &image, const Cutout *cutout, const unsigned long *palette24, unsigned int *cutoutBitmapPtr, int previewImagePitch) override
	{
		for (int sprNo = 0, sprIndex = 0; sprIndex < cutout->bob.numSprites; sprIndex += ((IsAttached()) ? 2 : 1), sprNo++)
		{
			const char *spritePtr = spriteBuffer + cutout->spriteList[sprIndex].spriteOffset + image.spriteWidth * 2 / 8;
			const char *attachedSpritePtr = NULL;
			if (IsAttached())
				attachedSpritePtr = spriteBuffer + cutout->spriteList[sprIndex + 1].spriteOffset + image.spriteWidth * 2 / 8;

			for (int y = 0; y < cutout->bob.height; y++)
			{
				for (int x = 0; x < image.spriteWidth && x < cutout->bob.width - sprNo * image.spriteWidth; x++)
				{
					int xBit = (1 << ((x & 7) ^ 7));
					int xByte = x >> 3;

					int colorIndex = 0;
					if (spritePtr[xByte + y * 2 * image.spriteWidth / 8] & xBit)
						colorIndex |= 1;
					if (spritePtr[xByte + (y * 2 + 1) * image.spriteWidth / 8] & xBit)
						colorIndex |= 2;
					if (attachedSpritePtr)
					{
						if (attachedSpritePtr[xByte + y * 2 * image.spriteWidth / 8] & xBit)
							colorIndex |= 4;
						if (attachedSpritePtr[xByte + (y * 2 + 1) * image.spriteWidth / 8] & xBit)
							colorIndex |= 8;
					}

					unsigned long color = PreviewImageBGColor;
					if (colorIndex)
					{
						color = palette24[colorIndex] | 0xff000000;
					}
					cutoutBitmapPtr[sprNo * image.spriteWidth + x + y * previewImagePitch] = color;
				}
			}
		}
	}
};

class CAttachedSpriteFormatSaver : public CSpriteFormatSaver
{
public:
	Image::Format Format() override { return Image::IF_AttachedSprite; }
	const char *FormatName() override { return "Attached Sprite"; }
	bool SupportsLineColors() override { return true; }
	bool IsAttached() override { return true; }
};

struct FillTableLineReference
{
	unsigned short fillTableLineOffset;
};
struct FillTableLineHeader
{
	unsigned char numEntries;
};
struct FillTableLineEntry
{
	unsigned char position;
	//TODO: fill table color: store bits here? Or should it be part of position - top x bits are the eor color, and if position is too large then use color=0
};
struct FillTableLine
{
	FillTableLineHeader header;
	FillTableLineEntry entries[9999];
};
class CVerticalFillTableSaver : public CFormatSaver
{
	int maxNrFillTableLines;
	int maxNrFillTablePixels;
	FillTableLineReference *uniqueFillTableLineReferences;
	int nrFillTableLines;
	char *fillTableLineBuffer;
	int fillTableLineBufferSize;
	int fillTableLineBufferUsed;

public:
	CVerticalFillTableSaver()
	{
		maxNrFillTableLines = 0;
		maxNrFillTablePixels = 0;
		uniqueFillTableLineReferences = 0;
		nrFillTableLines = 0;
		fillTableLineBuffer = 0;
		fillTableLineBufferSize = 0;
		fillTableLineBufferUsed = 0;
	}
	~CVerticalFillTableSaver() override
	{
		if (fillTableLineBuffer)
		{
			free(fillTableLineBuffer);
		}
	}

	Image::Format Format() override { return Image::IF_VerticalFillTable; }
	const char *FormatName() override { return "VerticalFillTable"; }
	bool RequiresPalletizedImage() override { return true; }
	bool ReserveColor0() override { return true; }

	void AllocateConversionBuffers(Image &image, int numCutouts, const Cutout *cutouts) override
	{
		for (int i = 0; i < numCutouts; i++)
		{
			maxNrFillTableLines += cutouts[i].bob.width;
			maxNrFillTablePixels += cutouts[i].bob.width * cutouts[i].bob.height;
		}
		uniqueFillTableLineReferences = new FillTableLineReference[maxNrFillTableLines];
		fillTableLineBufferSize = maxNrFillTableLines * sizeof(FillTableLineHeader) + maxNrFillTablePixels * sizeof(FillTableLineEntry);
		fillTableLineBuffer = (char *)malloc(fillTableLineBufferSize);
	}
	virtual void PrepareCutout(Image &image, Cutout *cutout) override
	{
		cutout->bufferSize = cutout->bob.width * sizeof(FillTableLineReference);
		cutout->fillTableLineReferenceBuffer = (FillTableLineReference *)malloc(cutout->bufferSize);
		memset(cutout->fillTableLineReferenceBuffer, 0, cutout->bufferSize);
	}
	virtual void PerformCutout(Image &image, const Cutout *cutout, const SourceImage &sourceImage) override
	{
		int sourceImagePitch = FreeImage_GetPitch(sourceImage.bitmap);
		int tempFillTableLineMaxLength = sizeof(FillTableLineHeader) + cutout->bob.height * sizeof(FillTableLineEntry);
		FillTableLine *tempFillTableLine = (FillTableLine *)malloc(tempFillTableLineMaxLength);
		for (int x = 0; x < cutout->bob.width; x++)
		{
			tempFillTableLine->header.numEntries = 0;

			const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, cutout->y);
			sourcePtr += cutout->x + x;

			//generate local line
			int previousColorIndex = 0;
			for (int y = 0; y < cutout->bob.height; y++)
			{
				char colorIndex = sourcePtr[sourceImagePitch * y];
				if (colorIndex != previousColorIndex)
				{
					int pixelColorDifference = colorIndex ^ previousColorIndex;
					tempFillTableLine->entries[tempFillTableLine->header.numEntries].position = y;
					//TODO: fill table color: Store color here
					//						tempFillTableLine->entries[tempFillTableLine->header.numEntries].color=pixelColorDifference;
					tempFillTableLine->header.numEntries++;
				}
				previousColorIndex = colorIndex;
			}

			int fillTableLineLength = sizeof(FillTableLineHeader) + tempFillTableLine->header.numEntries * sizeof(FillTableLineEntry);
			if (fillTableLineLength > tempFillTableLineMaxLength)
				Fatal("Internal error in fill table line");

			int j;
			for (j = 0; j < nrFillTableLines; j++)
			{
				//see if line is already found, and if so, just reuse that index
				if (!memcmp(fillTableLineBuffer + uniqueFillTableLineReferences[j].fillTableLineOffset, tempFillTableLine, fillTableLineLength))
				{
					break;
				}
			}

			if (j >= nrFillTableLines)
			{
				if (fillTableLineBufferUsed + fillTableLineLength > fillTableLineBufferSize)
					Fatal("Internal error in fill table");
				if (fillTableLineBufferUsed > 32767)
					Fatal("image too complex to store in fill table - data is over 15 bits");

				//line was not found - store it;
				uniqueFillTableLineReferences[j].fillTableLineOffset = fillTableLineBufferUsed;
				memcpy(fillTableLineBuffer + uniqueFillTableLineReferences[j].fillTableLineOffset, tempFillTableLine, fillTableLineLength);
				fillTableLineBufferUsed += fillTableLineLength;
				nrFillTableLines++;
			}
			cutout->fillTableLineReferenceBuffer[x].fillTableLineOffset = uniqueFillTableLineReferences[j].fillTableLineOffset;
		}
		free(tempFillTableLine);
	}
	void SaveImage(const Image &image, const char *destFileName, int numCutouts, const Cutout *cutouts) override
	{
		char tempstr[PATH_MAX];
		strcpy(tempstr, destFileName);
		strcat(tempstr, ".VFT");
		strcat(tempstr, GetFileTypeExtension(image.mainFileType));

		//save filltable file
		FILE *handle = 0;
		FileCreate(handle, tempstr);

		int totalCutoutSize = 0;
		//1. sum up width of all reference buffers
		for (int i = 0; i < numCutouts; i++)
		{
			totalCutoutSize += cutouts[i].bufferSize;
		}

		for (int i = 0; i < numCutouts; i++)
		{
			//update the buffer offsets to be relative inside the file
			for (int j = 0; j < cutouts[i].bob.width; j++)
			{
				cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset += totalCutoutSize;
				cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset = htons(cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset);
			}
			//2. write cutout line references, with offset into cutout line data (from start of file, so add the size of cutout line references
			FileWrite(cutouts[i].fillTableLineReferenceBuffer, cutouts[i].bufferSize, handle, tempstr, image.mainFileType);

			//remove the the buffer offsets again
			for (int j = 0; j < cutouts[i].bob.width; j++)
			{
				cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset = ntohs(cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset);
				cutouts[i].fillTableLineReferenceBuffer[j].fillTableLineOffset -= totalCutoutSize;
			}
		}

		//3. write cutout line data
		FileWrite(fillTableLineBuffer, fillTableLineBufferUsed, handle, tempstr, image.mainFileType);

		FileClose(handle, tempstr);
	}
	void ExtractToPreviewImage(const Image &image, const Cutout *cutout, const unsigned long *palette24, unsigned int *cutoutBitmapPtr, int previewImagePitch) override
	{
		for (int x = 0; x < cutout->bob.width; x++)
		{
			const FillTableLine *fillTableLine = (const FillTableLine *)(fillTableLineBuffer + cutout->fillTableLineReferenceBuffer[x].fillTableLineOffset);
			int colorIndex = 0;
			int y = 0;
			for (int i = 0; i < fillTableLine->header.numEntries; i++)
			{
				for (; y < fillTableLine->entries[i].position; y++)
				{
					unsigned long color = PreviewImageBGColor;
					if (colorIndex)
					{
						color = palette24[colorIndex] | 0xff000000;
					}
					cutoutBitmapPtr[x + y * previewImagePitch] = color;
				}
				//TODO: fill table color: use proper color (fillTableLine->entries[i].color?) here
				colorIndex ^= 1;
			}
		}
	}
};
class CChunkyFormatSaver : public CFormatSaver
{
public:
	Image::Format Format() override { return Image::IF_Chunky; }
	const char *FormatName() override { return "Chunky"; }
	bool Requires32BitImage() override { return true; }

	void AllocateConversionBuffers(Image &image, int numCutouts, const Cutout *cutouts) override
	{
	}
	virtual void PrepareCutout(Image &image, Cutout *cutout) override
	{
		//TODO Chunky: calc buffersizes and alloc buffers for chunky
		Fatal("not done yet: saving chunky format");
	}
	virtual void PerformCutout(Image &image, const Cutout *cutout, const SourceImage &sourceImage) override
	{
		//TODO Chunky: fill out output buffer for chunky
		Fatal("Not yet supported - chunky ");
	}
	void SaveImage(const Image &image, const char *destFileName, int numCutouts, const Cutout *cutouts) override
	{
		//TODO Chunky: save .chk file
		Fatal("not done yet: saving chunky file");
	}
	void ExtractToPreviewImage(const Image &image, const Cutout *cutout, const unsigned long *palette24, unsigned int *cutoutBitmapPtr, int previewImagePitch) override
	{
		//TODO Chunky: copy to preview image
		Fatal("TODO: copy chunky to preview image ");
	}
};

void SavePreviewImage(const Image &image, int numCutouts, const Cutout *cutouts, const unsigned long *palette24, const char *destFileName)
{
	//save preview image of all cutouts
#define PREVIEW_IMAGE_MARGIN_TOP 20
#define PREVIEW_IMAGE_MARGIN_BOTTOM 5
#define PREVIEW_IMAGE_MARGIN_LEFT 5
#define PREVIEW_IMAGE_MARGIN_RIGHT 5
#define PREVIEW_IMAGE_MIN_WIDTH 32

	//calc preview image width
	int numImagesPerRow = (int)sqrtf((float)numCutouts);
	int numImageRows = numImagesPerRow ? (numCutouts + (numImagesPerRow - 1)) / numImagesPerRow : 0;
	int previewImageWidth = 0;
	int previewImageHeight = 0;
	for (int i = 0; i < numImageRows; i++)
	{
		int rowWidth = 0;
		int rowHeight = 0;
		for (int j = 0; j < numImagesPerRow; j++)
		{
			if (i * numImagesPerRow + j >= numCutouts)
				break;
			int width = ((cutouts[i * numImagesPerRow + j].bob.width + 15) & ~15) + PREVIEW_IMAGE_MARGIN_LEFT + PREVIEW_IMAGE_MARGIN_RIGHT;
			width = max(width, PREVIEW_IMAGE_MIN_WIDTH);
			rowWidth += width;
			rowHeight = max(rowHeight, (cutouts[i * numImagesPerRow + j].bob.height + PREVIEW_IMAGE_MARGIN_TOP + PREVIEW_IMAGE_MARGIN_BOTTOM));
		}
		previewImageWidth = max(previewImageWidth, rowWidth);
		previewImageHeight += rowHeight;
	}

	//allocate 32 bit image buffer of target size
	FIBITMAP *previewBitmap = FreeImage_Allocate(previewImageWidth, previewImageHeight, 32, 0xff0000, 0x00ff00, 0x0000ff);
	if (!previewBitmap)
		Fatal("Internal error in Image_Allocate");
	//fill the background
	for (int y = 0; y < previewImageHeight; y++)
	{
		unsigned int *bits = (unsigned int *)FreeImage_GetScanLine(previewBitmap, y);
		for (int x = 0; x < previewImageWidth; x++)
		{
			bits[x] = PreviewImageBGColor;
		}
	}

	unsigned int *previewImageBitmap = (unsigned int *)FreeImage_GetBits(previewBitmap);

	//work around the fact that image is stored bottom to top
	int previewImagePitch = (int)(FreeImage_GetPitch(previewBitmap) / sizeof(unsigned int));

	//copy back cutouts
	int previewImageY = 0;
	for (int i = 0; i < numImageRows; i++)
	{
		int previewImageX = 0;
		int rowHeight = 0;
		for (int j = 0; j < numImagesPerRow; j++)
		{
			if (i * numImagesPerRow + j >= numCutouts)
				break;
			const Cutout *currentCutout = &cutouts[i * numImagesPerRow + j];

			int alignedWidth = ((currentCutout->bob.width + 15) & ~15);

			unsigned int *cutoutBitmapPtr = previewImageBitmap + ((previewImageY + PREVIEW_IMAGE_MARGIN_TOP) * previewImagePitch) + previewImageX + PREVIEW_IMAGE_MARGIN_LEFT;

			//draw cutout outline
			int colorOutline = 0xff006600;
			int colorAnchor = 0xffffffff;
			for (int k = -1; k < alignedWidth + 1; k++)
			{
				cutoutBitmapPtr[k - 1 * previewImagePitch] = colorOutline;
				if (k == currentCutout->bob.anchorX)
				{
					cutoutBitmapPtr[k + currentCutout->bob.height * previewImagePitch] = colorAnchor;
				}
				else
				{
					cutoutBitmapPtr[k + currentCutout->bob.height * previewImagePitch] = colorOutline;
				}
			}

			for (int k = 0; k < currentCutout->bob.height; k++)
			{
				if (k == currentCutout->bob.anchorY)
				{
					cutoutBitmapPtr[k * previewImagePitch - 1] = colorAnchor;
				}
				else
				{
					cutoutBitmapPtr[k * previewImagePitch - 1] = colorOutline;
				}
				cutoutBitmapPtr[k * previewImagePitch + alignedWidth] = colorOutline;
			}

			//copy to previewImage
			image.saver->ExtractToPreviewImage(image, currentCutout, palette24, cutoutBitmapPtr, previewImagePitch);

			//draw number above each item
			char tempstr[5];
			sprintf(tempstr, "%2d", i * numImagesPerRow + j);
			for (int k = 0; tempstr[k] != 0; k++)
			{
				char asciiVal = tempstr[k];
				if (asciiVal >= '0' && asciiVal <= '9')
				{
					const char *letter = hardcodedFont6x6[asciiVal - '0'];
					for (int x = 0; x < 6; x++)
					{
						for (int y = 0; y < 6; y++)
						{
							if (letter[y] & (1 << (5 - x)))
								cutoutBitmapPtr[x + k * 7 + (y - 8) * previewImagePitch] = 0xffffffff;
						}
					}
				}
			}

			int width = ((currentCutout->bob.width + 15) & ~15) + PREVIEW_IMAGE_MARGIN_LEFT + PREVIEW_IMAGE_MARGIN_RIGHT;
			width = max(width, PREVIEW_IMAGE_MIN_WIDTH);
			previewImageX += width;
			rowHeight = max(rowHeight, (currentCutout->bob.height + PREVIEW_IMAGE_MARGIN_TOP + PREVIEW_IMAGE_MARGIN_BOTTOM));
		}
		previewImageY += rowHeight;
	}

	//save image (_preview.bmp) or gif or tga or ???s
	char tempstr[PATH_MAX];
	strcpy(tempstr, destFileName);
	strcat(tempstr, "_preview.TGA");
	//writer needs bottom left to be 0,0 - so flip image
	FreeImage_FlipVertical(previewBitmap);
	GenericWriter(previewBitmap, tempstr, 0);
	FreeImage_Unload(previewBitmap);
}
void SaveFiles(const Image &image, int numCutouts, const Cutout *cutouts, const unsigned short *palette, const unsigned long *palette24, const char *destFileName, int fontCharacterListLength)
{
	if (image.saveRawPalette || image.saveCopper)
	{
		unsigned short paletteFlipped[1 << MAXBITPLANES];
		unsigned long paletteFlipped24[1 << MAXBITPLANES];
		//turn to bigendian
		for (int i = 0; i < (1 << image.numBitplanes); i++)
		{
			paletteFlipped[i] = htons(palette[i]);
			paletteFlipped24[i] = htonl(palette24[i]);
		}

		int size = (1 << image.numBitplanes);
		int firstIndex = 0;
		if (image.extraHalfBrite)
		{
			size /= 2;
		}
		if (image.isHAM)
		{
			size /= 4;
		}
		bool reserveColor0 = image.saver->ReserveColor0();
		if (reserveColor0)
		{
			firstIndex = 1;
			size--;
		}
		if (image.saveRawPalette)
		{
			char tempstr[PATH_MAX];
			strcpy(tempstr, destFileName);
			strcat(tempstr, ".PAL");
			strcat(tempstr, GetFileTypeExtension(image.paletteFileType));

			//save palette
			FILE *handle = 0;
			FileCreate(handle, tempstr);
			if (image.is24Bit)
			{
				FileWrite(paletteFlipped24 + firstIndex, size * sizeof(unsigned long), handle, tempstr, image.paletteFileType);
			}
			else
			{
				FileWrite(paletteFlipped + firstIndex, size * sizeof(unsigned short), handle, tempstr, image.paletteFileType);
			}
			FileClose(handle, tempstr);
		}
		if (image.saveCopper)
		{
			int copperColorIndex = image.copperColorIndex;
			if (copperColorIndex < 0)
			{
				copperColorIndex = image.saver->FirstCopperColorIndex();
			}

			char tempstr[PATH_MAX];
			strcpy(tempstr, destFileName);
			strcat(tempstr, ".COP");
			strcat(tempstr, GetFileTypeExtension(image.paletteFileType));

			//save palette
			FILE *handle = 0;
			FileCreate(handle, tempstr);

			if (!image.lineColors)
			{
				if (copperColorIndex + size > 32)
				{
					Fatal("Error: Copper Color Index out of range");
				}
				short copperList[2 * 256];
				for (int i = 0; i < size; i++)
				{
					copperList[i * 2 + 0] = htons(((i + copperColorIndex) * 2) + 0x180);
					copperList[i * 2 + 1] = paletteFlipped[firstIndex + i];
				}
				FileWrite(copperList, size * 2 * sizeof(short), handle, tempstr, image.paletteFileType);
			}
			else
			{
				if (numCutouts != 1)
				{
					Fatal("Linecolors option can only be done with one cutout - anims, fonts, or bobs are not supported");
				}
				int maxColorsPerLine = (1 << image.numBitplanes);
				const Cutout &cutout = cutouts[0];
				LineColorEntry *lineColors = image.sourceImages[cutout.imageIndex].lineColorEntriesForEachLine + copperColorIndex;
				lineColors += cutout.y * maxColorsPerLine;
				int screenStartY = cutout.y + image.spriteStartY - image.y;
				for (int y = 0; y < cutout.bob.height; y++)
				{
					unsigned short copperList[2 * (256 + 4)];
					int copperListEntries = 0;
					if (y)
					{
						if (image.doubleCopperWaits || screenStartY + y == 0x100)
						{
							copperList[copperListEntries * 2 + 0] = htons(0x00e1 + ((screenStartY + y - 1) << 8));
							copperList[copperListEntries * 2 + 1] = htons(0xfffe);
							copperListEntries++;
						}
						copperList[copperListEntries * 2 + 0] = htons(0x0007 + ((screenStartY + y) << 8));
						copperList[copperListEntries * 2 + 1] = htons(0xfffe);
						copperListEntries++;
					}

					const LineColorEntry *actualPalette = lineColors + (y)*maxColorsPerLine + copperColorIndex;
					const LineColorEntry *actualPalettePrevious = lineColors + (y - 1) * maxColorsPerLine + copperColorIndex;

					for (int i = 0; i < size; i++)
					{
						if ((!y || actualPalette[firstIndex + i].color != actualPalettePrevious[firstIndex + i].color) &&
							actualPalette[firstIndex + i].color != 0xffff)
						{
							copperList[copperListEntries * 2 + 0] = htons(((i + copperColorIndex) * 2) + 0x180);
							copperList[copperListEntries * 2 + 1] = htons(actualPalette[firstIndex + i].color);
							copperListEntries++;
						}
					}
					FileWrite(copperList, copperListEntries * 2 * sizeof(unsigned short), handle, tempstr, image.paletteFileType);
				}
			}
			FileClose(handle, tempstr);
		}
	}
	//save image file
	image.saver->SaveImage(image, destFileName, numCutouts, cutouts);

	if (image.mode == Image::IM_Bob || image.mode == Image::IM_MonospaceFont || image.mode == Image::IM_ProportionalFont)
	{
		char tempstr[PATH_MAX];
		strcpy(tempstr, destFileName);
		strcat(tempstr, ".BOB");
		strcat(tempstr, GetFileTypeExtension(image.bobFileType));

		//save bob file
		FILE *handle = 0;
		FileCreate(handle, tempstr);

		for (int i = 0; i < numCutouts; i++)
		{
			Bob bob;
			//turn to bigendian
			bob.widthInWords = htons(cutouts[i].bob.widthInWords);
			bob.height = htons(cutouts[i].bob.height);
			bob.width = htons(cutouts[i].bob.width);
			bob.offset = htonl(cutouts[i].bob.offset);
			bob.anchorX = htons(cutouts[i].bob.anchorX);
			bob.anchorY = htons(cutouts[i].bob.anchorY);
			FileWrite(&bob, sizeof(Bob), handle, tempstr, image.bobFileType);
		}

		FileClose(handle, tempstr);
	}
	if (image.mode == Image::IM_MonospaceFont || image.mode == Image::IM_ProportionalFont)
	{
		//generate ascii remap table
		unsigned char asciiRemapTable[256];
		memset(asciiRemapTable, 0xff, sizeof(asciiRemapTable));
		for (int i = fontCharacterListLength - 1; i >= 0; i--)
		{
			asciiRemapTable[*(unsigned char *)(&image.fontCharacterList[i])] = i;
			//for convenience: auto-remap lower to upper and vice versa, if it has not explicitly set both versions.
			if (image.fontCharacterList[i] >= 'a' && image.fontCharacterList[i] <= 'z' && asciiRemapTable[*(unsigned char *)(&image.fontCharacterList[i]) + 'A' - 'a'] == 0xff)
				asciiRemapTable[*(unsigned char *)(&image.fontCharacterList[i]) + 'A' - 'a'] = i;
			if (image.fontCharacterList[i] >= 'A' && image.fontCharacterList[i] <= 'Z' && asciiRemapTable[*(unsigned char *)(&image.fontCharacterList[i]) + 'a' - 'A'] == 0xff)
				asciiRemapTable[*(unsigned char *)(&image.fontCharacterList[i]) + 'a' - 'A'] = i;
		}

		char tempstr[PATH_MAX];
		strcpy(tempstr, destFileName);
		strcat(tempstr, ".FAR");
		strcat(tempstr, GetFileTypeExtension(image.fontFileType));

		//save .far file
		FILE *handle = 0;
		FileCreate(handle, tempstr);

		FileWrite(asciiRemapTable, sizeof(asciiRemapTable), handle, tempstr, image.fontFileType);

		FileClose(handle, tempstr);
	}
}

void ConvertTo32Bit(FIBITMAP *&bitmap)
{
	int numBPP = FreeImage_GetBPP(bitmap);
	if (numBPP != 32)
	{
		FIBITMAP *oldBitmap = bitmap;
		bitmap = FreeImage_ConvertTo32Bits(bitmap);
		if (FreeImage_GetImageType(bitmap) != FIT_BITMAP)
			Fatal("Internal error. ConvertTo32Bit didn't behave as expected.");
		FreeImage_Unload(oldBitmap);
	}
}
void ConvertTo24Bit(FIBITMAP *&bitmap)
{
	int numBPP = FreeImage_GetBPP(bitmap);
	if (numBPP != 24)
	{
		FIBITMAP *oldBitmap = bitmap;
		bitmap = FreeImage_ConvertTo24Bits(bitmap);
		if (FreeImage_GetImageType(bitmap) != FIT_BITMAP)
			Fatal("Internal error. ConvertTo24Bit didn't behave as expected.");
		FreeImage_Unload(oldBitmap);
	}
}

void RemapPaletteToHaveBGColorInEntry0(SourceImage &sourceImage, FIBITMAP *bitmap, RGBQUAD *originalPalette, int numColors)
{
	short bgColor = ConvertRGBQUADTo12Bit(originalPalette[0]);
	//The Color quantize may store the original palette color in a different index than 0.
	//Therefore, Find bg color index, and swap it to be index 0
	RGBQUAD *newPalette = FreeImage_GetPalette(bitmap);
	int postQuantizeBgColorIndex = -1;
	for (int i = 0; i < numColors; i++)
	{
		if (ConvertRGBQUADTo12Bit(newPalette[i]) == bgColor)
		{
			postQuantizeBgColorIndex = i;
			break;
		}
	}
	if (postQuantizeBgColorIndex == -1)
		Fatal("Internal error: color quantize didn't preserve the palette as expected");

	//swap all pixels that are index 0 to postquantizebgcolorindex, and any that are the bg color to be index 0
	for (int y = 0; y < sourceImage.height; y++)
	{
		unsigned char *sourcePtr = FreeImage_GetScanLine(bitmap, y);
		for (int x = 0; x < sourceImage.width; x++)
		{
			//read pixel
			unsigned char colorIndex = sourcePtr[x];
			if (ConvertRGBQUADTo12Bit(newPalette[colorIndex]) == bgColor)
			{
				sourcePtr[x] = 0;
			}
			else if (colorIndex == 0)
			{
				sourcePtr[x] = postQuantizeBgColorIndex;
			}
		}
	}

	//swap palette entries
	if (postQuantizeBgColorIndex != 0)
	{
		newPalette[postQuantizeBgColorIndex] = newPalette[0];
		newPalette[0] = originalPalette[0];
	}
}
FIBITMAP *AttemptLosslessPalletizing(SourceImage &sourceImage, const Image &image, short &numUsedColors, bool hasOriginalPalette, const RGBQUAD *originalPalette, int maxColors, int startY, int height, bool reserveColor0)
{
	if (image.extraHalfBrite)
	{
		maxColors /= 2;
	}

	//TODO HAM LineColor: This may eventually also do HAM per line.

	FIBITMAP *&bitmap = sourceImage.bitmap;
	//First, try a lossless conversion to palletized mode
	unsigned short colorsUsedIndices[1 << 12];
	memset(colorsUsedIndices, 0xff, sizeof(colorsUsedIndices));
	unsigned int redMask = FreeImage_GetRedMask(bitmap);
	unsigned int greenMask = FreeImage_GetGreenMask(bitmap);
	unsigned int blueMask = FreeImage_GetBlueMask(bitmap);
	unsigned short redMaskByteOffset = MaskToByteOffset(redMask);
	unsigned short greenMaskByteOffset = MaskToByteOffset(greenMask);
	unsigned short blueMaskByteOffset = MaskToByteOffset(blueMask);
	int firstUsableColorIndex = 0;
	if (reserveColor0)
	{
		//reserve color 0, but never actually use it
		firstUsableColorIndex = 1;
		numUsedColors++;
	}

	//First, mark all used colors.
	for (int y = startY; y < startY + height; y++)
	{

		const unsigned char *sourcePtr = FreeImage_GetScanLine(bitmap, y);
		RGBQUAD color;
		color.rgbReserved = 0;
		for (int x = 0; x < sourceImage.width; x++)
		{
			//read pixel
			color.rgbRed = sourcePtr[x * 4 + redMaskByteOffset];
			color.rgbGreen = sourcePtr[x * 4 + greenMaskByteOffset];
			color.rgbBlue = sourcePtr[x * 4 + blueMaskByteOffset];
			unsigned short color12Bit = ConvertRGBQUADTo12Bit(color);
			if (colorsUsedIndices[color12Bit] == 0xffff)
			{
				colorsUsedIndices[color12Bit] = numUsedColors;
				numUsedColors++;
			}
		}
	}
	//EHB: after converting, see which palette entries can be used as EHB instead, to lower the number of palette entries
	//so remove all the colorsusedindices that are set, if its' half color is found

	//EHB NOTE: This is not quite optimal for linecolors, as it may not be reusing colors if only the halfbrite entry of an existing color is used in a line.
	//I can work around this by storing for each color if it also is used for both EHB and non-EHB, and if not (and if the color is less than 777, of course), the color line reuser gets to consider if it can find one of the 8 matching doublebrite colors
	if (image.extraHalfBrite)
	{
		//note: it is not doing color 0x000 on purpose - because halfbrite of color 0x000 is 0x000
		for (unsigned short color = (1 << 12) - 1; color > 0; color--)
		{
			if (colorsUsedIndices[color] != 0xffff && colorsUsedIndices[HalfBrite12Bit(color)] != 0xffff)
			{
				unsigned short thisColorUsedIndex = colorsUsedIndices[HalfBrite12Bit(color)];
				for (unsigned short colorTemp = (1 << 12) - 1; colorTemp > 0; colorTemp--)
				{
					if (colorsUsedIndices[colorTemp] > thisColorUsedIndex && colorsUsedIndices[colorTemp] != 0xffff)
						colorsUsedIndices[colorTemp]--;
				}

				colorsUsedIndices[HalfBrite12Bit(color)] = 0xffff;
				numUsedColors--;
			}
		}
	}

	//Check if there are too many colors after this process
	if (numUsedColors <= maxColors)
	{
		unsigned short colorsFinalOrder[1 << 12];
		//generate 12-bit palette and a second lookup-table
		if (hasOriginalPalette)
		{
			//if it was palletized then try to reorder based on regular palette, but move unused colors up.
			memset(colorsFinalOrder, 0xff, sizeof(colorsFinalOrder));
			//so first store the indvidual colors based on the order of the original palette, if they are actually used
			int numFinalOrderColors = firstUsableColorIndex;
			for (int i = 0; i < 1 << MAXBITPLANES; i++)
			{
				unsigned short color12Bit = ConvertRGBQUADTo12Bit(originalPalette[i]);
				if (colorsUsedIndices[color12Bit] != 0xffff)
				{

					if (colorsFinalOrder[color12Bit] == 0xffff)
					{
						colorsFinalOrder[color12Bit] = numFinalOrderColors;
						numFinalOrderColors++;
					}
				}
			}
			if (numUsedColors != numFinalOrderColors)
			{
				//this is just in case I missed something here - there shouldn't be any more colors, but just checking.
				Fatal("Error: Internal problem in palette reordering");
			}
		}
		else
		{
			//no palette found - order doesn't matter
			memcpy(colorsFinalOrder, colorsUsedIndices, sizeof(colorsFinalOrder));
		}

		FIBITMAP *losslessPalletizedBitmap = FreeImage_Allocate(sourceImage.width, height, 8);
		if (!losslessPalletizedBitmap)
			Fatal("Internal error in Image_Allocate");

		RGBQUAD *losslessPalletizedBitmapPalette = FreeImage_GetPalette(losslessPalletizedBitmap);
		memset(losslessPalletizedBitmapPalette, 0, sizeof(RGBQUAD) * 1 << MAXBITPLANES);
		//store palette here
		for (unsigned short i = 0; i < 1 << 12; i++)
		{
			if (colorsFinalOrder[i] != 0xffff)
			{
				losslessPalletizedBitmapPalette[colorsFinalOrder[i]] = Convert12BitToRGBQUAD(i);
			}
		}

		//store pixels
		RGBQUAD *paletteSource = FreeImage_GetPalette(bitmap);
		//First, mark all used colors.
		for (int y = startY; y < startY + height; y++)
		{

			const unsigned char *sourcePtr = FreeImage_GetScanLine(bitmap, y);
			unsigned char *destPtr = FreeImage_GetScanLine(losslessPalletizedBitmap, y - startY);
			RGBQUAD color;
			color.rgbReserved = 0;
			for (int x = 0; x < sourceImage.width; x++)
			{
				//read pixel
				color.rgbRed = sourcePtr[x * 4 + redMaskByteOffset];
				color.rgbGreen = sourcePtr[x * 4 + greenMaskByteOffset];
				color.rgbBlue = sourcePtr[x * 4 + blueMaskByteOffset];
				unsigned short color12Bit = ConvertRGBQUADTo12Bit(color);
				if (image.extraHalfBrite && colorsFinalOrder[color12Bit] == 0xffff)
				{
					unsigned short foundColor = 0xffff;
					short doubleColor12BitBase = DoubleBrite12Bit(color12Bit);
					//go through the 8 "double brite" colors that output this pixel's color as halfbrite,
					for (int j = 0; j < 8; j++)
					{
						short doubleColor12Bit = doubleColor12BitBase + (j & 1) + ((j & 2) << 3) + ((j & 4) << 6);
						if (colorsFinalOrder[doubleColor12Bit] != 0xffff)
							foundColor = colorsFinalOrder[doubleColor12Bit];
					}
					if (foundColor == 0xffff)
						Fatal("Internal error in HalfBrite lossless palletizing routine");
					destPtr[x] = (foundColor & 0xff) + 0x20;
					//					destPtr[x] = colorsFinalOrder[doubleColor12Bit] & 0xff;
				}
				else
					destPtr[x] = colorsFinalOrder[color12Bit] & 0xff;
			}
		}
		return losslessPalletizedBitmap;
	}
	return NULL;
}

FIBITMAP *ExtractMaskBitmap(const SourceImage &sourceImage, const Image &image)
{
	FIBITMAP *maskBitmap = 0;
	int numBPP = 0;
	if (FreeImage_GetImageType(sourceImage.bitmap) == FIT_BITMAP)
	{
		numBPP = FreeImage_GetBPP(sourceImage.bitmap);
	}
	if (numBPP == 8)
	{
		maskBitmap = FreeImage_Allocate(sourceImage.width, sourceImage.height, 8);
		for (int y = 0; y < sourceImage.height; y++)
		{
			const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, y);
			unsigned char *destPtr = FreeImage_GetScanLine(maskBitmap, y);
			for (int x = 0; x < sourceImage.width; x++)
			{
				if (sourcePtr[x] == image.maskColorIndex)
					destPtr[x] = 0;
				else
					destPtr[x] = 255;
			}
		}
	}
	else
	{
		if (image.saver->ReserveColor0())
		{
			Fatal("Image format %s requires a palletized image, as it reserves color index 0", image.saver->Format());
		}
		if (image.mask)
		{
			Fatal("Mask option requires a palletized image");
		}
		if (numBPP == 32)
		{
			maskBitmap = FreeImage_Allocate(sourceImage.width, sourceImage.height, 8);
			for (int y = 0; y < sourceImage.height; y++)
			{
				const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, y);
				unsigned char *destPtr = FreeImage_GetScanLine(maskBitmap, y);
				for (int x = 0; x < sourceImage.width; x++)
				{
					if (sourcePtr[x * 4 + 3])
						destPtr[x] = 255;
					else
						destPtr[x] = 0;
				}
			}
		}
		else
		{
			if (image.mode == Image::IM_Bob || image.mode == Image::IM_ProportionalFont)
			{
				Fatal("Bob mode and proportional font mode requires a palletized image or image with an alpha channel");
			}
			maskBitmap = FreeImage_Allocate(sourceImage.width, sourceImage.height, 8);
			const unsigned char *topLeftPixelPtr = FreeImage_GetScanLine(sourceImage.bitmap, 0);
			for (int y = 0; y < sourceImage.height; y++)
			{
				unsigned char *destPtr = FreeImage_GetScanLine(maskBitmap, y);
				const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, y);
				for (int x = 0; x < sourceImage.width; x++)
				{
					if (numBPP == 24 &&
						sourcePtr[x * 3] == topLeftPixelPtr[0] &&
						sourcePtr[x * 3 + 1] == topLeftPixelPtr[1] &&
						sourcePtr[x * 3 + 2] == topLeftPixelPtr[2])
						destPtr[x] = 0;
					else
						destPtr[x] = 255;
				}
			}
		}
	}
	return maskBitmap;
}

void ConvertToPallettized(SourceImage &sourceImage, Image &image)
{
	int maxColors = 1 << image.numBitplanes;
	bool hasOriginalPalette = false;
	RGBQUAD originalPalette[1 << MAXBITPLANES];
	FIBITMAP *&bitmap = sourceImage.bitmap;
	bool needsQuantizing = false;
	if (FreeImage_GetImageType(bitmap) == FIT_BITMAP)
	{
		int numBPP = FreeImage_GetBPP(bitmap);
		if (numBPP > 8)
		{
			printf("WARNING: Image is not palletized, so this step is done before converting. It will have to guess the background color. This is your problem, not mine. You should consider manually converting to a palletized format\n");
			needsQuantizing = true;
		}
		else
		{
			RGBQUAD *paletteSource = FreeImage_GetPalette(bitmap);
			hasOriginalPalette = true;
			memcpy(originalPalette, paletteSource, sizeof(originalPalette));

			for (int y = 0; y < sourceImage.height; y++)
			{
				const unsigned char *sourcePtr = FreeImage_GetScanLine(bitmap, y);
				for (int x = 0; x < sourceImage.width; x++)
				{
					//read pixel
					unsigned char colorIndex = sourcePtr[x];
					if (colorIndex >= maxColors && colorIndex != image.maskColorIndex)
					{
						printf("WARNING: Image is using more than the first %d colors, - will try to automatically optimize. You should consider manually rearranging the palette\n", maxColors);
						needsQuantizing = true;
						break;
					}
				}
				if (needsQuantizing)
					break;
			}
		}
	}
	else
	{
		needsQuantizing = true;
		printf("Image is in a weird format, trying to convert it to a bitmap so it can be processed. It will have to guess the background color, and it may lose some quality.\n");
	}

	if (needsQuantizing)
	{
		ConvertTo32Bit(bitmap);
		short numUsedColors = 0;
		FIBITMAP *newBitmap = AttemptLosslessPalletizing(sourceImage, image, numUsedColors, hasOriginalPalette, originalPalette, maxColors, 0, sourceImage.height, false);
		if (newBitmap)
		{
			FreeImage_Unload(bitmap);
			bitmap = newBitmap;
		}
		else
		{
			if (image.extraHalfBrite)
			{
				Fatal("ERROR: Even after palette remapping, it is using %d colors, but is only allowed to use %d colors. For Extra Halfbrite, lossy conversion isn't allowed\n", numUsedColors, maxColors / (image.extraHalfBrite ? 2 : 1));
			}
			printf("WARNING: Even after palette remapping, it is using %d colors, but is only allowed to use %d colors. Performing quantizing process instead\n", numUsedColors, maxColors);
			if (!hasOriginalPalette)
			{
				//get top left pixel, and use that as BG color. it's better than no guess.
				if (!FreeImage_GetPixelColor(bitmap, 0, 0, &originalPalette[0]))
					Fatal("Internal error. GetPixelColor didn't behave as expected.");
			}

			ConvertTo24Bit(bitmap);
			FIBITMAP *oldBitmap = bitmap;
			bitmap = FreeImage_ColorQuantizeEx(bitmap, FIQ_NNQUANT, maxColors, 1, &originalPalette[0]);
			if (FreeImage_GetImageType(bitmap) != FIT_BITMAP)
				Fatal("Internal error. Quantize didn't behave as expected.");
			FreeImage_Unload(oldBitmap);

			RemapPaletteToHaveBGColorInEntry0(sourceImage, bitmap, &originalPalette[0], maxColors);
		}
	}
	if (FreeImage_GetColorType(bitmap) != FIC_PALETTE && FreeImage_GetColorType(bitmap) != FIC_MINISBLACK && FreeImage_GetColorType(bitmap) != FIC_MINISWHITE)
	{
		Fatal("Internal error. Colortype is not Palette.");
	}
}

void BuildCutoutList(Image &image, Cutout *cutouts, int &numCutouts)
{
	if (image.mode == Image::IM_Anim || image.mode == Image::IM_SingleFrame)
	{
		for (int i = 0; i < image.numSourceImages; i++)
		{
			cutouts[i].imageIndex = i;
			cutouts[i].x = image.x;
			cutouts[i].y = image.y;
			cutouts[i].bob.width = image.width;
			cutouts[i].bob.height = image.height;
			if (cutouts[i].bob.width >= image.sourceImages[i].width - image.x)
				cutouts[i].bob.width = image.sourceImages[i].width - image.x;
			if (cutouts[i].bob.height >= image.sourceImages[i].height - image.y)
				cutouts[i].bob.height = image.sourceImages[i].height - image.y;
			cutouts[i].bob.anchorX = 0; // cutouts[i].bob.width / 2;
			cutouts[i].bob.anchorY = 0; // cutouts[i].bob.height / 2;
		}
	}
	else if (image.mode == Image::IM_Bob)
	{
		int numBobsFound = FindCutouts(image, image.sourceImages[0], cutouts, image.numBobs, "Bob");
		if (image.numBobs != numBobsFound)
		{
			Fatal("Bob cutout process failed - expected to find %d cutouts, but found only %d", image.numBobs, numCutouts);
		}
	}
	else if (image.mode == Image::IM_MonospaceFont)
	{

		//check if there are fewer or more characters than expected. This will help to make sure the font character list is valid
		//It's not exactly precise, but it checks that all rows of characters are used by the font, so it at east indicates if things are completely off
		int maxCharactersPerLine = 0;
		for (int i = 0; i < image.numFontLines; i++)
		{
			if (image.sourceImages[0].width - image.x < image.fontLineLengths[i] * image.width)
				Fatal("The font character list has more characters on line %d than fits on the image", i);
		}
		if (image.sourceImages[0].height - image.y < (image.numFontLines * image.height + (image.numFontLines - 1) * image.gapBetweenTextLines))
			Fatal("The font character list has more lines than fits on the image");

		//find fixed font cutouts
		int cutoutIndex = 0;
		for (int i = 0; i < image.numFontLines; i++)
		{
			for (int j = 0; j < image.fontLineLengths[i]; j++, cutoutIndex++)
			{
				cutouts[cutoutIndex].imageIndex = 0;
				cutouts[cutoutIndex].x = j * image.width + image.x;
				cutouts[cutoutIndex].y = i * (image.height + image.gapBetweenTextLines) + image.y;
				cutouts[cutoutIndex].bob.width = image.width;
				cutouts[cutoutIndex].bob.height = image.height;
				cutouts[cutoutIndex].bob.anchorX = 0; // cutouts[cutoutIndex].bob.width / 2;
				cutouts[cutoutIndex].bob.anchorY = 0; // cutouts[cutoutIndex].bob.height / 2;
			}
		}

		int fontCharacterListLength = strlen(image.fontCharacterList);
		if (fontCharacterListLength != numCutouts)
		{
			Fatal("Font character cutout process failed - expected to find %d characters, but found %d", image.numBobs, numCutouts);
		}
	}
	else if (image.mode == Image::IM_ProportionalFont)
	{
		if (image.sourceImages[0].height - image.y < (image.numFontLines * image.height + (image.numFontLines - 1) * image.gapBetweenTextLines))
			Fatal("The font character list has more lines than fits on the image");

		int cutoutIndex = 0;
		for (int i = 0; i < image.numFontLines; i++)
		{
			int numBobsFoundInLine = FindCutoutsLine(image, image.sourceImages[0], i * (image.height + image.gapBetweenTextLines) + image.y, image.x, image.height, cutouts + cutoutIndex, image.fontLineLengths[i], "Font", false);
			cutoutIndex += image.fontLineLengths[i];
			if (numBobsFoundInLine != image.fontLineLengths[i])
				Fatal("Font character cutout process failed - expected to find %d characters on line %d, but found %d", image.fontLineLengths[i], i, numBobsFoundInLine);
		}
	}
	else
	{
		Fatal("Internal Error: Unknown mode");
	}
}
bool ImageHasPixels(SourceImage &sourceImage, int xStart, int yStart, int xEnd, int yEnd)
{
	for (int y = yStart; y <= yEnd; y++)
	{

		const unsigned char *sourcePtr = FreeImage_GetScanLine(sourceImage.bitmap, y);
		const unsigned char *maskPtr = FreeImage_GetScanLine(sourceImage.maskBitmap, y);
		for (int x = xStart; x <= xEnd; x++)
		{
			char colorIndex = sourcePtr[x];
			if (maskPtr[x])
				//			if (colorIndex != maskColorIndex)
				return true;
		}
	}
	return false;
}

int PaletteSortFunc(const void *a, const void *b)
{
	unsigned short *aShort = (unsigned short *)a;
	unsigned short *bShort = (unsigned short *)b;
	return *aShort - *bShort;
}
void ConvertImage(const char *srcFileName, const char *destFileName, Image &image)
{
	if (!image.saver)
		Fatal("No format specified for %s", srcFileName);
	//print what we are doing
	printf("Converting %s, format: %s, mode: %s%s%s%s%s.\n",
		   srcFileName,
		   image.saver->FormatName(),
		   imageModeNames[image.mode],
		   image.mask ? ", creating mask" : "",
		   image.interleaved ? ", interleaving" : "",
		   image.saveRawPalette ? ", saving raw palette" : "", image.saveCopper ? ", saving copper palette" : "");

	//check for option conflicts
	if ((image.x != 0 || image.y != 0) && (image.mode == Image::IM_Bob))
	{
		Fatal("X and Y settings are not allowed in bob mode");
	}
	if (image.saveRawPalette && image.lineColors)
	{
		Fatal("RawPalette and LineColors options are mutually exclusive");
	}
	if ((image.gapBetweenTextLines != 0) && (image.mode != Image::IM_ProportionalFont && image.mode != Image::IM_MonospaceFont))
	{
		Fatal("Gap setting is only allowed in monospace font and proportional font modes");
	}
	if ((image.width != -1) && (image.mode == Image::IM_Bob || image.mode == Image::IM_ProportionalFont))
	{
		Fatal("Width setting is not allowed in bob or proportional font modes");
	}
	if ((image.width == -1) && (image.mode == Image::IM_MonospaceFont))
	{
		Fatal("Width setting is required in monospace font modes");
	}
	if ((image.height != -1) && (image.mode == Image::IM_Bob))
	{
		Fatal("Height setting is not allowed in bob mode");
	}
	if ((image.height == -1) && (image.mode == Image::IM_MonospaceFont || image.mode == Image::IM_ProportionalFont))
	{
		Fatal("Height setting is required in monospace and proportional font modes");
	}
	if (image.isHAM && (image.mask || image.trim))
	{
		Fatal("HAM format is not compatible with Trim or Mask options");
	}
	if (image.isHAM && (image.mode == Image::IM_Bob || image.mode == Image::IM_MonospaceFont || image.mode == Image::IM_ProportionalFont))
	{
		Fatal("HAM format is not compatible with bob or font modes");
	}
	if ((image.interleaved || image.mask) && image.saver->Format() != Image::IF_Bitplanes)
	{
		Fatal("Interleaved and Mask options are only allowed for bitplane format");
	}
	if (image.previewMaskColor0 && image.saver->Format() != Image::IF_Bitplanes)
	{
		Fatal("PreviewMaskColor0 option is only allowed for bitplane format");
	}
	if (image.numBitplanes > MAXBITPLANES)
	{
		Fatal("Only up to %d bitplanes allowed", MAXBITPLANES);
	}

	if (image.fontCharacterList)
	{
		char *fontCharacterListConverted = image.fontCharacterList;
		int lineLength = 0;
		int fontCharacterListOriginal = strlen(image.fontCharacterList);
		int convertedLength = 0;
		for (int i = 0; i < fontCharacterListOriginal; i++)
		{
			char nextChar = image.fontCharacterList[i];
			if (nextChar == '\\')
			{
				i++;
				nextChar = image.fontCharacterList[i];
				if (nextChar == 'n')
				{
					image.fontLineLengths[image.numFontLines] = lineLength;
					image.numFontLines++;
					lineLength = 0;
					continue;
				}
				else if (nextChar == '\\')
				{
				}
				else if (nextChar == '\'')
				{
					nextChar = '"';
				}
				else
				{
					Fatal("Unknown character code \\%c in font list", nextChar);
				}
			}
			fontCharacterListConverted[convertedLength] = nextChar;
			convertedLength++;
			lineLength++;
		}
		image.fontLineLengths[image.numFontLines] = lineLength;
		image.numFontLines++;
		fontCharacterListConverted[convertedLength] = 0;
	}

	int fontCharacterListLength = strlen(image.fontCharacterList);
	if (image.mode == Image::IM_MonospaceFont || image.mode == Image::IM_ProportionalFont)
	{
		//		printf("Character List:%s\n", image.fontCharacterList);

		for (int i = 0; i < fontCharacterListLength; i++)
		{
			for (int j = 0; j < i; j++)
			{
				if (image.fontCharacterList[i] == image.fontCharacterList[j] && image.fontCharacterList[i] != ' ')
					Fatal("Character %c found twice in font character list", image.fontCharacterList[i]);
			}
		}
	}

	if (image.mode == Image::IM_Anim)
	{
		//find srcfilename's number idex
		int lastDecimalIndex = -1;
		for (int i = 0; srcFileName[i]; i++)
		{
			if (srcFileName[i] >= '0' && srcFileName[i] <= '9')
				lastDecimalIndex = i;
		}
		if (lastDecimalIndex < 0)
			Fatal("Tried to convert animation, but no decimals was found in the filename %s", srcFileName);
		int firstDecimalIndex = lastDecimalIndex;
		while (firstDecimalIndex && srcFileName[firstDecimalIndex - 1] >= '0' && srcFileName[firstDecimalIndex - 1] <= '9')
			firstDecimalIndex--;
		char firstHalfOfName[PATH_MAX];
		strncpy(firstHalfOfName, srcFileName, firstDecimalIndex);
		firstHalfOfName[firstDecimalIndex] = '\0';
		const char *lastHalfOfName = srcFileName + lastDecimalIndex + 1;

		int animStartingNumber = 0;
		for (int i = firstDecimalIndex; i <= lastDecimalIndex; i++)
		{
			animStartingNumber = animStartingNumber * 10 + srcFileName[i] - '0';
		}

		if (!image.numAnimFrames)
		{
			//count anim images
			char fileName[PATH_MAX];
			do
			{
				image.numAnimFrames++;
				sprintf(fileName, "%s%d%s", firstHalfOfName, animStartingNumber + image.numAnimFrames, lastHalfOfName);
			} while (FileExists(fileName));
			printf("%d animation frames found\n", image.numAnimFrames);
		}
		//load anim images
		image.numSourceImages = image.numAnimFrames;
		image.sourceImages = new SourceImage[image.numSourceImages];
		memset(image.sourceImages, 0, sizeof(SourceImage) * image.numSourceImages);
		for (int i = 0; i < image.numSourceImages; i++)
		{
			char fileName[PATH_MAX];
			sprintf(fileName, "%s%d%s", firstHalfOfName, animStartingNumber + i, lastHalfOfName);
			image.sourceImages[i].bitmap = GenericLoader(fileName, 0);
			//change 0,0 to be top-left instead of bottom-left
			FreeImage_FlipVertical(image.sourceImages[i].bitmap);
		}
	}
	else
	{
		image.numSourceImages = 1;
		image.sourceImages = new SourceImage[image.numSourceImages];
		memset(image.sourceImages, 0, sizeof(SourceImage) * image.numSourceImages);
		//load single image
		image.sourceImages[0].bitmap = GenericLoader(srcFileName, 0);
		//change 0,0 to be top-left instead of bottom-left
		FreeImage_FlipVertical(image.sourceImages[0].bitmap);
	}
	int numCutouts = image.numSourceImages;
	if (image.mode == Image::IM_Bob)
		numCutouts = image.numBobs;
	else if (image.mode == Image::IM_ProportionalFont || image.mode == Image::IM_MonospaceFont)
		numCutouts = fontCharacterListLength;

	Cutout *cutouts = new Cutout[numCutouts];
	for (int i = 0; i < image.numSourceImages; i++)
	{
		image.sourceImages[i].maskBitmap = NULL;
		image.sourceImages[i].width = FreeImage_GetWidth(image.sourceImages[i].bitmap);
		image.sourceImages[i].height = FreeImage_GetHeight(image.sourceImages[i].bitmap);
		if (image.sourceImages[i].width != image.sourceImages[0].width ||
			image.sourceImages[i].height != image.sourceImages[0].height)
		{
			Fatal("Animation frames should all be same width and height");
		}

		if (FreeImage_GetImageType(image.sourceImages[i].bitmap) == FIT_BITMAP)
		{
			int numBPP = FreeImage_GetBPP(image.sourceImages[i].bitmap);
			if (numBPP < 8)
			{
				FIBITMAP *oldBitmap = image.sourceImages[i].bitmap;
				image.sourceImages[i].bitmap = FreeImage_ConvertTo8Bits(image.sourceImages[i].bitmap);
				FreeImage_Unload(oldBitmap);
				if (FreeImage_GetBPP(image.sourceImages[i].bitmap) != 8)
					Fatal("Internal error. FreeImage_ConvertTo8Bits didn't behave as expected.");
			}
		}
		image.sourceImages[i].maskBitmap = ExtractMaskBitmap(image.sourceImages[i], image);

		//Convert to the proper source image type (8-bit pallettized or raw 24-bit)
		if (image.saver->RequiresPalletizedImage())
		{
			if (image.lineColors)
			{
				//Generate linecolor table for the source image here.
				if (image.saver->SupportsLineColors())
				{
					int maxColorsPerLine = (1 << image.numBitplanes);
					short *colorsLockedForEachLine = new short[image.sourceImages[i].height];
					memset(colorsLockedForEachLine, -1, sizeof(short) * image.sourceImages[i].height);
					LineColorEntry *lineColorEntriesForEachLine = new LineColorEntry[image.sourceImages[i].height * maxColorsPerLine];
					memset(lineColorEntriesForEachLine, -1, sizeof(LineColorEntry) * image.sourceImages[i].height * maxColorsPerLine);

					ConvertTo32Bit(image.sourceImages[i].bitmap);
					for (int y = 0; y < image.sourceImages[i].height; y++)
					{
						short numUsedColors = 0;
						//convert line losslessly to an image (lock color 0 if it is needed, though).
						bool reserveColor0 = image.saver->ReserveColor0();
						FIBITMAP *lineBitmap = AttemptLosslessPalletizing(image.sourceImages[i], image, numUsedColors, false, NULL, maxColorsPerLine, y, 1, reserveColor0);
						if (!lineBitmap)
							Fatal("Too many colors found on line %d (expected %d, but found %d)", y, maxColorsPerLine / (image.extraHalfBrite ? 2 : 1), numUsedColors);
						//sort the colors

						RGBQUAD *paletteSource = FreeImage_GetPalette(lineBitmap);
						unsigned short palette[1 << MAXBITPLANES];
						memset(palette, 0, sizeof(palette));
						for (int i = 0; i < numUsedColors; i++)
						{
							palette[i] = ConvertRGBQUADTo12Bit(paletteSource[i]);
						}
						qsort(palette, numUsedColors, sizeof(unsigned short), PaletteSortFunc);
						//fail if more than n colors
						if (numUsedColors > maxColorsPerLine)
							Fatal("Too many colors found on line %d", y);

						//count and store how many colors are used in the locked counter.
						colorsLockedForEachLine[y] = numUsedColors;

						//store just the color palette for that line in the lineColorEntry
						for (int i = 0; i < numUsedColors; i++)
						{
							lineColorEntriesForEachLine[y * maxColorsPerLine + i].color = palette[i];
						}

						FreeImage_Unload(lineBitmap);
					}

					short lastLineColorWasUsed[1 << 12];
					memset(lastLineColorWasUsed, -1, sizeof(lastLineColorWasUsed));
					short *colorsChangedForEachLine = new short[image.sourceImages[i].height];
					memset(colorsChangedForEachLine, 0, sizeof(short) * image.sourceImages[i].height);
					int lastLineWithAllColorsLocked = 0;

					//for each color
					for (int y = 1; y < image.sourceImages[i].height; y++)
					{
						//If a color is used on a line, and it was used earlier, try to reuse it (by reserving a color on every line between those two lines)
						for (int index = 0; index < colorsLockedForEachLine[y]; index++)
						{
							unsigned short color = lineColorEntriesForEachLine[y * maxColorsPerLine + index].color;
							if (color == 0xffff)
								continue;
							//if color was used before and there are empty slots all the way back to that line then reuse that color
							if (lastLineColorWasUsed[color] >= 0 && lastLineColorWasUsed[color] >= lastLineWithAllColorsLocked)
							{
								//to reuse the color between two separate lines, add it to every line in between those two lines.
								for (int y2 = lastLineColorWasUsed[color] + 1; y2 < y; y2++)
								{
									if (colorsLockedForEachLine[y2] > maxColorsPerLine)
										Fatal("Internal error in LineColors mode");
									//store the color
									lineColorEntriesForEachLine[y2 * maxColorsPerLine + colorsLockedForEachLine[y2]].color = color;

									//increase count of locked colors for this line
									colorsLockedForEachLine[y2]++;
									//if there are enough locked colors on this line, set the "last line with all colors locked" flag.
									if (colorsLockedForEachLine[y2] == maxColorsPerLine)
										lastLineWithAllColorsLocked = y2;
								}
							}
							lastLineColorWasUsed[color] = y;
						}
						if (colorsLockedForEachLine[y] == maxColorsPerLine)
							lastLineWithAllColorsLocked = y;

						//count how many colors changed this line, as they may be over the limit
						for (int index = 0; index < maxColorsPerLine; index++)
						{
							unsigned short color = lineColorEntriesForEachLine[y * maxColorsPerLine + index].color;
							if (color == 0xffff)
								continue;
							bool isNewColor = true;
							for (int index2 = 0; index2 < maxColorsPerLine; index2++)
							{
								if (lineColorEntriesForEachLine[(y - 1) * maxColorsPerLine + index2].color == color)
								{
									isNewColor = false;
									break;
								}
							}
							if (isNewColor)
							{
								//we found a unique color that wasn't used on the previous line
								//now check if there are too many color changes on a line, and if so, try to push some of them to an earlier line, if earlier lines have room.
								//if not possible then fail loudly and fatally
								if (image.lineColorMaxChangesPerLine && colorsChangedForEachLine[y] >= image.lineColorMaxChangesPerLine)
								{
									for (short ytemp = y - 1; ytemp >= 0; ytemp--)
									{
										if (colorsLockedForEachLine[ytemp] == maxColorsPerLine)
										{
											Fatal("Too many color changes on line %d and it wasn't possible to move the color changes to earlier lines", y);
										}
										if (ytemp == 0 || colorsChangedForEachLine[ytemp] < image.lineColorMaxChangesPerLine)
										{
											colorsChangedForEachLine[ytemp]++;
											for (int y2 = ytemp; y2 < y; y2++)
											{
												if (colorsLockedForEachLine[y2] > maxColorsPerLine)
													Fatal("Internal error in LineColors mode");
												//store the color
												lineColorEntriesForEachLine[y2 * maxColorsPerLine + colorsLockedForEachLine[y2]].color = color;

												//increase count of locked colors for this line
												colorsLockedForEachLine[y2]++;
												//if there are enough locked colors on this line, set the "last line with all colors locked" flag.
												if (colorsLockedForEachLine[y2] == maxColorsPerLine)
													lastLineWithAllColorsLocked = y2;
											}
											break;
										}

										//there wasn't room in this line - keep going through the for-loop to try an earlier line.
									}
								}
								else
								{
									colorsChangedForEachLine[y]++;
								}
							}
						}
					}

					//Second step is to try to reuse palette indices per line. Basically reorder the LineColorIndices to be in the right order
					for (int y = 1; y < image.sourceImages[i].height; y++)
					{
						int colorChangesThisLine = 0;
						for (int index = 0; index < maxColorsPerLine; index++)
						{
							unsigned short lastLineColor = lineColorEntriesForEachLine[(y - 1) * maxColorsPerLine + index].color;
							if (lastLineColor == 0xffff)
								continue;
							int matchingIndexInThisLine = -1;
							for (int index2 = 0; index2 < maxColorsPerLine; index2++)
							{
								if (lineColorEntriesForEachLine[y * maxColorsPerLine + index2].color == lastLineColor)
								{
									matchingIndexInThisLine = index2;
									break;
								}
							}
							if (matchingIndexInThisLine >= 0 && matchingIndexInThisLine != index)
							{
								LineColorEntry swapCopy = lineColorEntriesForEachLine[y * maxColorsPerLine + matchingIndexInThisLine];
								lineColorEntriesForEachLine[y * maxColorsPerLine + matchingIndexInThisLine] = lineColorEntriesForEachLine[y * maxColorsPerLine + index];
								lineColorEntriesForEachLine[y * maxColorsPerLine + index] = swapCopy;
							}
							else
								colorChangesThisLine++;
						}
					}

					image.sourceImages[i].lineColorEntriesForEachLine = lineColorEntriesForEachLine;

					//Repalletize image, pixel by pixel, based on the new palette.
					FIBITMAP *newBitmap = FreeImage_Allocate(image.sourceImages[i].width, image.sourceImages[i].height, 8);
					if (!newBitmap)
						Fatal("Internal error in Image_Allocate");

					//store the new bitmap here.
					unsigned int redMask = FreeImage_GetRedMask(image.sourceImages[i].bitmap);
					unsigned int greenMask = FreeImage_GetGreenMask(image.sourceImages[i].bitmap);
					unsigned int blueMask = FreeImage_GetBlueMask(image.sourceImages[i].bitmap);
					unsigned short redMaskByteOffset = MaskToByteOffset(redMask);
					unsigned short greenMaskByteOffset = MaskToByteOffset(greenMask);
					unsigned short blueMaskByteOffset = MaskToByteOffset(blueMask);
					for (int y = 0; y < image.sourceImages[i].height; y++)
					{
						unsigned char *newBits = FreeImage_GetScanLine(newBitmap, y);
						unsigned char *originalBits = FreeImage_GetScanLine(image.sourceImages[i].bitmap, y);
						//for each pixel, find the color in the line, and store that index.
						RGBQUAD color;
						color.rgbReserved = 0;
						for (int x = 0; x < image.sourceImages[i].width; x++)
						{
							color.rgbRed = originalBits[x * 4 + redMaskByteOffset];
							color.rgbGreen = originalBits[x * 4 + greenMaskByteOffset];
							color.rgbBlue = originalBits[x * 4 + blueMaskByteOffset];
							unsigned short color12Bit = ConvertRGBQUADTo12Bit(color);
							int index;

							if (image.extraHalfBrite)
							{
								for (index = 0; index < maxColorsPerLine / 2; index++)
								{
									if (lineColorEntriesForEachLine[y * maxColorsPerLine + index].color == color12Bit)
										break;
									if (HalfBrite(lineColorEntriesForEachLine[y * maxColorsPerLine + index].color) == color12Bit)
									{
										index += 0x20;
										break;
									}
								}
							}
							else
							{
								for (index = 0; index < maxColorsPerLine; index++)
								{
									if (lineColorEntriesForEachLine[y * maxColorsPerLine + index].color == color12Bit)
										break;
								}
							}
							if (index == maxColorsPerLine)
								Fatal("Internal error in Linecolors conversion");
							newBits[x] = index;
						}
					}

					//compare image with original image, in 24-bit mode they should be identical!
					FreeImage_Unload(image.sourceImages[i].bitmap);
					image.sourceImages[i].bitmap = newBitmap;

					delete[] colorsLockedForEachLine;
				}
				else
				{
					Fatal("LineColors option is only supported with bitplanes and attachedsprite formats");
				}
			}
			else
			{
				if (image.isHAM)
				{
					//TODO HAM: convert image here
					//					FIBITMAP* newBitmap = AttemptLosslessPalletizing(image.sourceImages[i], image, numUsedColors, false, NULL, numBitplanes, 0, sourceImage.height, false);
					//					if (!newBitmap)
					{
						//WTF this should never fail!
					}
				}
				else
				{
					ConvertToPallettized(image.sourceImages[i], image);
				}
				RGBQUAD *pal0 = FreeImage_GetPalette(image.sourceImages[0].bitmap);
				RGBQUAD *pali = FreeImage_GetPalette(image.sourceImages[i].bitmap);
				for (int j = 0; j < 256; j++)
				{
					if (pal0[j].rgbRed != pali[j].rgbRed ||
						pal0[j].rgbGreen != pali[j].rgbGreen ||
						pal0[j].rgbBlue != pali[j].rgbBlue)
					{
						Fatal("Palettes don't match up across animation frames");
					}
				}
			}
		}
		else if (image.saver->Requires32BitImage())
		{
			ConvertTo32Bit(image.sourceImages[i].bitmap);
		}
		else
		{
			Fatal("Unknown format");
		}

		if (image.isHAM)
		{
			RGBQUAD *quad = FreeImage_GetPalette(image.sourceImages[i].bitmap);
			for (int i = (1 << image.numBitplanes) / 4; i < 256; i++)
			{
				quad[i].rgbRed = 255;
				quad[i].rgbGreen = 0;
				quad[i].rgbBlue = 0;
			}
		}
		if (image.extraHalfBrite)
		{
			RGBQUAD *quad = FreeImage_GetPalette(image.sourceImages[i].bitmap);
			for (int i = 32; i < 256; i++)
			{
				quad[i].rgbRed = 255;
				quad[i].rgbGreen = 0;
				quad[i].rgbBlue = 0;
			}
		}
	}

	BuildCutoutList(image, cutouts, numCutouts);

	//flip source image here
	bool flipX = image.flipX;
	bool flipY = false;
	if (image.rotate & 2)
	{
		flipX = !flipX;
		flipY = !flipY;
	}
	bool rot90 = (image.rotate & 1) ? true : false;
	if (flipX)
	{
		for (int i = 0; i < image.numSourceImages; i++)
		{
			FreeImage_FlipHorizontal(image.sourceImages[i].bitmap);
		}
	}
	if (flipY)
	{
		for (int i = 0; i < image.numSourceImages; i++)
		{
			FreeImage_FlipVertical(image.sourceImages[i].bitmap);
		}
	}
	if (rot90)
	{
		for (int i = 0; i < image.numSourceImages; i++)
		{
			//rotate cutouts x/y/width/height/anchorx/anchory here
			FIBITMAP *oldBitmap = image.sourceImages[i].bitmap;
			FIBITMAP *newBitmap = FreeImage_Allocate(image.sourceImages[i].height, image.sourceImages[i].width, 8);
			int oldPitch = FreeImage_GetPitch(oldBitmap);
			int newPitch = FreeImage_GetPitch(newBitmap);
			const unsigned char *oldBits = FreeImage_GetBits(oldBitmap);
			unsigned char *newBits = FreeImage_GetBits(newBitmap);
			for (int y = 0; y < image.sourceImages[i].height; y++)
			{
				for (int x = 0; x < image.sourceImages[i].width; x++)
				{
					newBits[x * newPitch + y] = oldBits[y * oldPitch + x];
				}
			}
			RGBQUAD *oldPalette = FreeImage_GetPalette(oldBitmap);
			RGBQUAD *newPalette = FreeImage_GetPalette(newBitmap);
			memcpy(newPalette, oldPalette, 256 * sizeof(RGBQUAD));

			FreeImage_Unload(image.sourceImages[i].bitmap);
			image.sourceImages[i].bitmap = newBitmap;
			image.sourceImages[i].width = FreeImage_GetWidth(image.sourceImages[i].bitmap);
			image.sourceImages[i].height = FreeImage_GetHeight(image.sourceImages[i].bitmap);
			FreeImage_FlipVertical(image.sourceImages[i].bitmap);
		}
	}
	for (int i = 0; i < numCutouts; i++)
	{
		//flip cutouts x/anchor here
		if (flipX)
		{
			cutouts[i].x = (image.sourceImages[cutouts[i].imageIndex].width - cutouts[i].x) - cutouts[i].bob.width;
			cutouts[i].bob.anchorX = (cutouts[i].bob.width - cutouts[i].bob.anchorX) - 1;
		}
		if (flipY)
		{
			cutouts[i].y = (image.sourceImages[cutouts[i].imageIndex].height - cutouts[i].y) - cutouts[i].bob.height;
			cutouts[i].bob.anchorY = (cutouts[i].bob.height - cutouts[i].bob.anchorY) - 1;
		}
		//rotate cutouts x/y/width/height/anchorx/anchory here
		if (rot90)
		{
			int temp = cutouts[i].x;
			cutouts[i].x = cutouts[i].y;
			cutouts[i].y = temp;

			temp = cutouts[i].bob.height;
			cutouts[i].bob.height = cutouts[i].bob.width;
			cutouts[i].bob.width = temp;

			temp = cutouts[i].bob.anchorX;
			cutouts[i].bob.anchorX = cutouts[i].bob.anchorY;
			cutouts[i].bob.anchorY = temp;

			cutouts[i].x = (image.sourceImages[cutouts[i].imageIndex].width - cutouts[i].x) - cutouts[i].bob.width;
			cutouts[i].bob.anchorX = (cutouts[i].bob.width - cutouts[i].bob.anchorX) - 1;
		}

		if (image.trim)
		{
			while (cutouts[i].bob.width > 1)
			{
				if (ImageHasPixels(image.sourceImages[cutouts[i].imageIndex], cutouts[i].x, cutouts[i].y, cutouts[i].x, cutouts[i].y + cutouts[i].bob.height - 1))
					break;
				cutouts[i].x++;
				cutouts[i].bob.width--;
				cutouts[i].bob.anchorX--;
			}
			while (cutouts[i].bob.width > 1)
			{
				if (ImageHasPixels(image.sourceImages[cutouts[i].imageIndex], cutouts[i].x + cutouts[i].bob.width - 1, cutouts[i].y, cutouts[i].x + cutouts[i].bob.width - 1, cutouts[i].y + cutouts[i].bob.height - 1))
					break;
				cutouts[i].bob.width--;
			}
			while (cutouts[i].bob.height > 1)
			{
				if (ImageHasPixels(image.sourceImages[cutouts[i].imageIndex], cutouts[i].x, cutouts[i].y, cutouts[i].x + cutouts[i].bob.width - 1, cutouts[i].y))
					break;
				cutouts[i].y++;
				cutouts[i].bob.height--;
				cutouts[i].bob.anchorY--;
			}
			while (cutouts[i].bob.height > 1)
			{
				if (ImageHasPixels(image.sourceImages[cutouts[i].imageIndex], cutouts[i].x, cutouts[i].y + cutouts[i].bob.height - 1, cutouts[i].x + cutouts[i].bob.width - 1, cutouts[i].y + cutouts[i].bob.height - 1))
					break;
				cutouts[i].bob.height--;
			}
		}
	}

	//allocate work buffers and do initial calculations
	image.saver->AllocateConversionBuffers(image, numCutouts, cutouts);

	int totalFileOffset = 0;
	for (int i = 0; i < numCutouts; i++)
	{

		//calculate pitches and sizes for the converted image data (interleaved or not, mask or not), and allocate memory
		image.saver->PrepareCutout(image, &cutouts[i]);

		//store offsets in bob table - as that will be saved out
		cutouts[i].bob.offset = totalFileOffset;

		SourceImage &sourceImage = image.sourceImages[cutouts[i].imageIndex];

		//generate the final pixel data
		image.saver->PerformCutout(image, &cutouts[i], sourceImage);
		totalFileOffset += cutouts[i].bufferSize;
	}
	RGBQUAD *paletteSource = FreeImage_GetPalette(image.sourceImages[0].bitmap);
	unsigned short palette[1 << MAXBITPLANES];
	unsigned long palette24[1 << MAXBITPLANES];
	for (int i = 0; i < (1 << image.numBitplanes); i++)
	{
		palette[i] = ConvertRGBQUADTo12Bit(paletteSource[i]);
		if (image.is24Bit)
		{
			palette24[i] = ConvertRGBQUADTo24Bit(paletteSource[i]);
		}
		else
		{
			palette24[i] = Convert12BitTo24Bit(palette[i]);
		}
	}
	SaveFiles(image, numCutouts, cutouts, palette, palette24, destFileName, fontCharacterListLength);
	SavePreviewImage(image, numCutouts, cutouts, palette24, destFileName);
	for (int i = 0; i < image.numSourceImages; i++)
	{
		if (image.sourceImages[i].maskBitmap)
			FreeImage_Unload(image.sourceImages[i].maskBitmap);
		if (image.sourceImages[i].lineColorEntriesForEachLine)
			delete[] image.sourceImages[i].lineColorEntriesForEachLine;

		FreeImage_Unload(image.sourceImages[i].bitmap);
	}
}

void InitTextFileReader(char *fileName, char *&textReaderCurPosition, int &lineNumber, char *&textReaderStartPosition)
{
	FILE *handle = fopen(fileName, "rb");

	if (!handle)
	{
		Fatal("Failed opening %s for reading", fileName);
	}
	fseek(handle, 0, SEEK_END);
	long size = ftell(handle);
	fseek(handle, 0, SEEK_SET);
	if (size < 0)
	{
		Fatal("Problem occurred while reading %s", fileName);
	}

	textReaderStartPosition = (char *)malloc(size + 1);
	textReaderStartPosition[size] = 0;
	int bytesRead = fread(textReaderStartPosition, 1, size, handle);
	if (bytesRead != size)
	{
		Fatal("Failed reading from %s", fileName);
	}
	fclose(handle);

	textReaderCurPosition = textReaderStartPosition;
	lineNumber = 0;
}
void CloseTextFileReader(char *&textReaderStartPosition)
{
	free(textReaderStartPosition);
}

char *GetNextTextLine(char *&textReaderCurPosition, int &lineNumber)
{
	char *result = 0;
	if (textReaderCurPosition)
	{
		result = textReaderCurPosition;
		lineNumber++;
		for (;;)
		{
			if (!*textReaderCurPosition)
			{
				textReaderCurPosition = 0;
				break;
			}
			else if (*textReaderCurPosition == 0xa)
			{
				*textReaderCurPosition = 0;
				textReaderCurPosition++;
				break;
			}
			else if (*textReaderCurPosition == 0xd && *(textReaderCurPosition + 1) == 0xa)
			{
				*textReaderCurPosition = 0;
				textReaderCurPosition += 2;
				break;
			}
			textReaderCurPosition++;
		}
	}
	return result;
}
bool IsWhiteSpace(char character)
{
	if (character == ' ' || character == '	')
		return true;
	return false;
}
bool IsWhiteSpace(wchar_t character)
{
	if (character == ' ' || character == '	')
		return true;
	return false;
}
char *TrimWhiteSpace(char *ptr)
{
	while (IsWhiteSpace(*ptr))
		ptr++;
	return ptr;
}

int ProcessSingleLine(int argc, char *argv[], bool isFromConversionList, int recursion)
{
	if (argc > 1 && argv[1][0] == '@')
	{
		if (recursion > 5)
		{
			Fatal("Too many recursions");
		}
		wprintf(L"Processing Asset Conversion List %s\n", argv[1] + 1);
		char *textReaderCurPosition = 0;
		int lineNumber = 0;
		char *textReaderStartPosition = 0;
		char fileName[1024];
		sprintf(fileName, "%s", argv[1] + 1);
		InitTextFileReader(fileName, textReaderCurPosition, lineNumber, textReaderStartPosition);
		char *textLine;
		while (textLine = GetNextTextLine(textReaderCurPosition, lineNumber))
		{
			char *textLineAfterWhiteSpace = TrimWhiteSpace(textLine);
			if (!textLine[0] || (textLine[0] == '/' && textLine[1] == '/'))
				continue;
			int newargc = 0;
			char *newargv[1024];
			char tempstr[1024];
			sprintf(tempstr, "%s", textLineAfterWhiteSpace);
			int charPos = 0;

			newargv[newargc] = argv[0];
			newargc++;
			newargv[newargc] = &tempstr[charPos];
			newargc++;
			while (tempstr[charPos])
			{
				if (tempstr[charPos] == ' ')
				{
					tempstr[charPos] = 0;
					charPos++;
					//skipping all whitespace
					while (IsWhiteSpace(tempstr[charPos]))
					{
						charPos++;
					}

					//starting a new argument
					if (tempstr[charPos] == '"')
					{
						charPos++;
						newargv[newargc] = &tempstr[charPos];
						newargc++;
						while (tempstr[charPos])
						{
							if (tempstr[charPos] == '"')
							{
								tempstr[charPos] = ' ';
								break;
							}
							charPos++;
						}
					}
					else if (tempstr[charPos])
					{
						newargv[newargc] = &tempstr[charPos];
						newargc++;
					}
				}
				else if (tempstr[charPos] == '"')
				{
					charPos++;
					while (tempstr[charPos])
					{
						if (tempstr[charPos] == '"')
						{
							charPos++;
							break;
						}
						charPos++;
					}
				}
				else
					charPos++;
			}
			//			printf("Processing Line: %s\n", textLine);
			ProcessSingleLine(newargc, newargv, true, recursion + 1);
		}
		CloseTextFileReader(textReaderStartPosition);
		//
		return 0;
	}
	Data data;
	memset(&data, 0, sizeof(data));
	data.image.height = -1;
	data.image.width = -1;
	data.image.spriteStartX = 0x81;
	data.image.spriteStartY = 0x2c;
	int numFileNames = 0;
	for (int i = 1; i < argc; i++)
	{
		char currentArg[1024];
		sprintf(currentArg, "%s", argv[i]);
		// if (currentArg[0] != '-' && currentArg[0] != '/')
		if (currentArg[0] != '-')
		{
			// Convert Windows directory separators
			int i = 0;
			while (currentArg[i] != '\0')
			{
				if (currentArg[i] == '\\')
				{
					currentArg[i] = '/';
				}
				i++;
			}

			if (numFileNames == 0)
			{
				strcpy(data.srcFileName, currentArg);
				numFileNames++;
			}
			else if (numFileNames == 1)
			{
				strcpy(data.destFileName, currentArg);
				numFileNames++;
			}
			else
			{
				Fatal("Unexpected argument %s\n", currentArg);
			}
			//for non-image converters: read filename extension - if known then it's special converter, otherwise image converter
			data.type = Data::CT_Image;
		}
		else
		{
			const char *option = currentArg;
			char tempstr[512];
			const char *parameter = strchr(option, '=');
			if (parameter)
			{
				strncpy(tempstr, option, parameter - option);
				tempstr[parameter - option] = '\0'; // Null terminate
				option = tempstr;
				parameter++;
			}

			//parse options
			if (numFileNames < 2)
			{
				Fatal("Option %s was found before source and destination filenames\n", option);
			}
			switch (data.type)
			{
			case Data::CT_Image:
			{

				if (!strcasecmp(option + 1, "f") || !strcasecmp(option + 1, "format"))
				{
					const char *parm = GetParameterForOption(option, parameter);
					if (!strcasecmp(parameter, "c") || !strcasecmp(parameter, "chunky"))
					{
						data.image.saver = new CChunkyFormatSaver;
						data.image.numBitplanes = 0;
					}
					else if (!strcasecmp(parameter, "s") || !strcasecmp(parameter, "s16") || !strcasecmp(parameter, "sprite") || !strcasecmp(parameter, "sprite16"))
					{
						data.image.saver = new CSpriteFormatSaver;
						data.image.numBitplanes = 2;
						data.image.spriteWidth = 16;
					}
					else if (!strcasecmp(parameter, "a") || !strcasecmp(parameter, "a16") || !strcasecmp(parameter, "attachedsprite") || !strcasecmp(parameter, "attachedsprite16"))
					{
						data.image.saver = new CAttachedSpriteFormatSaver;
						data.image.numBitplanes = 4;
						data.image.spriteWidth = 16;
					}
					else if (!strcasecmp(parameter, "s32") || !strcasecmp(parameter, "sprite32"))
					{
						data.image.saver = new CSpriteFormatSaver;
						data.image.numBitplanes = 2;
						data.image.spriteWidth = 32;
					}
					else if (!strcasecmp(parameter, "a32") || !strcasecmp(parameter, "attachedsprite32"))
					{
						data.image.saver = new CAttachedSpriteFormatSaver;
						data.image.numBitplanes = 4;
						data.image.spriteWidth = 32;
					}
					else if (!strcasecmp(parameter, "s64") || !strcasecmp(parameter, "sprite64"))
					{
						data.image.saver = new CSpriteFormatSaver;
						data.image.numBitplanes = 2;
						data.image.spriteWidth = 64;
					}
					else if (!strcasecmp(parameter, "a32") || !strcasecmp(parameter, "attachedsprite64"))
					{
						data.image.saver = new CAttachedSpriteFormatSaver;
						data.image.numBitplanes = 4;
						data.image.spriteWidth = 64;
					}
					else if (!strcasecmp(parameter, "v") || !strcasecmp(parameter, "verticalfilltable"))
					{
						data.image.saver = new CVerticalFillTableSaver;
						//TODO: fill table color: support a variable number of bitplanes
						data.image.numBitplanes = 1;
					}
					else if (!strcasecmp(parameter, "e") || !strcasecmp(parameter, "extrahalfbrite"))
					{
						data.image.saver = new CBitplaneFormatSaver;
						data.image.numBitplanes = 6;
						data.image.extraHalfBrite = true;
					}
					else if (!strcasecmp(parameter, "h") || !strcasecmp(parameter, "ham"))
					{
						data.image.saver = new CBitplaneFormatSaver;
						data.image.numBitplanes = 6;
						data.image.isHAM = true;
					}
					else if (!strcasecmp(parameter, "h8") || !strcasecmp(parameter, "ham8"))
					{
						data.image.saver = new CBitplaneFormatSaver;
						data.image.numBitplanes = 8;
						data.image.isHAM = true;
					}
					else if (parm[0] >= '0' && parm[0] <= '9')
					{
						data.image.saver = new CBitplaneFormatSaver;
						data.image.numBitplanes = GetInteger(parm, option);
					}
					else
					{
						Fatal("unknown parameter for option %s\n", option);
					}
				}
				else if (!strcasecmp(option + 1, "a") || !strcasecmp(option + 1, "anim"))
				{
					if (data.image.mode != Image::IM_SingleFrame)
					{
						Fatal("only one mode selection is allowed. option %s conflicts with previous mode option\n", option);
					}
					data.image.mode = Image::IM_Anim;
					if (parameter)
					{
						data.image.numAnimFrames = GetInteger(GetParameterForOption(option, parameter), option);
					}
				}
				else if (!strcasecmp(option + 1, "b") || !strcasecmp(option + 1, "bob"))
				{
					if (data.image.mode != Image::IM_SingleFrame)
					{
						Fatal("only one mode selection is allowed. option %s conflicts with previous mode option\n", option);
					}
					data.image.mode = Image::IM_Bob;
					data.image.numBobs = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "n") || !strcasecmp(option + 1, "monospacefont"))
				{
					if (data.image.mode != Image::IM_SingleFrame)
					{
						Fatal("only one mode selection is allowed. option %s conflicts with previous mode option\n", option);
					}
					data.image.mode = Image::IM_MonospaceFont;
					StoreFontCharacterList(data, i, argc, argv, option, isFromConversionList);
				}
				else if (!strcasecmp(option + 1, "p") || !strcasecmp(option + 1, "proportionalfont"))
				{
					if (data.image.mode != Image::IM_SingleFrame)
					{
						Fatal("only one mode selection is allowed. option %s conflicts with previous mode option\n", option);
					}
					data.image.mode = Image::IM_ProportionalFont;
					StoreFontCharacterList(data, i, argc, argv, option, isFromConversionList);
				}
				else if (!strcasecmp(option + 1, "g") || !strcasecmp(option + 1, "Gap"))
				{
					data.image.gapBetweenTextLines = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "x") || !strcasecmp(option + 1, "Left"))
				{
					data.image.x = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "x") || !strcasecmp(option + 1, "Left"))
				{
					data.image.x = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "y") || !strcasecmp(option + 1, "Top"))
				{
					data.image.y = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "W") || !strcasecmp(option + 1, "Width"))
				{
					data.image.width = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "H") || !strcasecmp(option + 1, "Height"))
				{
					data.image.height = GetInteger(GetParameterForOption(option, parameter), option);
				}

				else if (!strcasecmp(option + 1, "t") || !strcasecmp(option + 1, "Trim"))
				{
					data.image.trim = true;
				}
				else if (!strcasecmp(option + 1, "dw") || !strcasecmp(option + 1, "DoubleCopperWaits"))
				{
					data.image.doubleCopperWaits = true;
				}
				else if (!strcasecmp(option + 1, "L") || !strcasecmp(option + 1, "LineColors"))
				{
					data.image.lineColors = true;
					if (parameter)
						data.image.lineColorMaxChangesPerLine = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "RP") || !strcasecmp(option + 1, "RawPalette"))
				{
					data.image.saveRawPalette = true;
				}
				else if (!strcasecmp(option + 1, "RP24") || !strcasecmp(option + 1, "RawPalette24"))
				{
					data.image.saveRawPalette = true;
					data.image.is24Bit = true;
				}
				else if (!strcasecmp(option + 1, "c") || !strcasecmp(option + 1, "CopperPalette"))
				{
					data.image.saveCopper = true;
					if (!parameter)
						data.image.copperColorIndex = -1;
					else
						data.image.copperColorIndex = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "I") || !strcasecmp(option + 1, "Interleaved"))
				{
					data.image.interleaved = true;
				}
				else if (!strcasecmp(option + 1, "M") || !strcasecmp(option + 1, "Mask"))
				{
					data.image.mask = true;
					if (parameter)
						data.image.maskColorIndex = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "R") || !strcasecmp(option + 1, "Rotate"))
				{
					data.image.rotate = GetInteger(GetParameterForOption(option, parameter), option) & 3;
				}
				else if (!strcasecmp(option + 1, "FX") || !strcasecmp(option + 1, "FlipX"))
				{
					data.image.flipX = true;
				}
				else if (!strcasecmp(option + 1, "SX") || !strcasecmp(option + 1, "SpriteX"))
				{
					data.image.spriteStartX = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "SY") || !strcasecmp(option + 1, "SpriteY"))
				{
					data.image.spriteStartY = GetInteger(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "PM") || !strcasecmp(option + 1, "PreviewMaskColor0"))
				{
					data.image.previewMaskColor0 = true;
				}
				else if (!strcasecmp(option + 1, "AW") || !strcasecmp(option + 1, "AddWord"))
				{
					data.image.addExtraBlitterWord = true;
				}
				else if (!strcasecmp(option + 1, "IM") || !strcasecmp(option + 1, "InvertMask"))
				{
					data.image.invertMask = true;
				}
				else if (!strcasecmp(option + 1, "FTMain") || !strcasecmp(option + 1, "FileTypeMain"))
				{
					data.image.mainFileType = ConvertOptionToFileType(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "FTPalette") || !strcasecmp(option + 1, "FileTypePalette"))
				{
					data.image.paletteFileType = ConvertOptionToFileType(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "FTBob") || !strcasecmp(option + 1, "FileTypeBob"))
				{
					data.image.bobFileType = ConvertOptionToFileType(GetParameterForOption(option, parameter), option);
				}
				else if (!strcasecmp(option + 1, "FTFont") || !strcasecmp(option + 1, "FileTypeFont"))
				{
					data.image.fontFileType = ConvertOptionToFileType(GetParameterForOption(option, parameter), option);
				}
				else
				{
					Fatal("unknown option %s\n", option);
				}
				break;
			}
				//parse other types of converters here

			default:
				Fatal("Internal problem: unknown converter type\n");
				break;
			}
		}
	}

	if (numFileNames < 2)
	{
		Fatal("Missing source or destination filenames\n");
	}
	switch (data.type)
	{
	case Data::CT_Image:
	{
		ConvertImage(data.srcFileName, data.destFileName, data.image);
		break;
	}
	default:
		Fatal("Internal problem: unknown converter type\n");
	}
	if (data.image.saver)
	{
		delete data.image.saver;
		data.image.saver = 0;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc > 1 && (!strcmp(argv[1], "/?") || !strcmp(argv[1], "-?") || !strcmp(argv[1], "-h")))
	{
		Help();
	}
	else
	{
		FreeImage_SetOutputMessage(FreeImageErrorHandler);
		ProcessSingleLine(argc, argv, false, 0);
	}
	MyExit(0);
}
