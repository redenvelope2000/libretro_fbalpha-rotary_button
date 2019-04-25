// Galaga & Dig-Dug driver for FB Alpha, based on the MAME driver by Nicola Salmoria & previous work by Martin Scragg, Mirko Buffoni, Aaron Giles
// Dig Dug added July 27, 2015
// Xevious added April 22, 2019

// notes: galaga freeplay mode doesn't display "freeplay" - need to investigate.

#include "tiles_generic.h"
#include "z80_intf.h"
#include "namco_snd.h"
#include "samples.h"
#include "earom.h"

/* Weird video definitions...
 *  +---+
 *  |   |
 *  |   |
 *  |   | height, y, tilemap_width
 *  |   |
 *  +---+
 *  width, x, tilemap_height
 */
#define NAMCO_SCREEN_WIDTH    224
#define NAMCO_SCREEN_HEIGHT   288

#define NAMCO_TMAP_WIDTH      36
#define NAMCO_TMAP_HEIGHT     28


static const INT32 Colour2Bit[4] = { 
   0x00, 0x47, 0x97, 0xde 
};
static const INT32 Colour3Bit[8] = { 
   0x00, 0x21, 0x47, 0x68,
   0x97, 0xb8, 0xde, 0xff 
};

static const INT32 Colour4Bit[16] = {
   0x00, 0x0e, 0x1f, 0x2d,
   0x43, 0x51, 0x62, 0x70,
   0x8f, 0x9d, 0xae, 0xbc,
   0xd2, 0xe0, 0xf1, 0xff
};

struct PortBits_Def
{
   UINT8 Current[8];
   UINT8 Last[8];
};

#define NAMCO_BRD_INP_COUNT      3

struct Input_Def
{
   struct PortBits_Def PortBits[NAMCO_BRD_INP_COUNT];
   UINT8 Dip[3];
   UINT8 Ports[NAMCO_BRD_INP_COUNT];
   UINT8 PrevInValue;
   UINT8 Reset;
};

static struct Input_Def input;

static const UINT8 namcoControls[16] = {
/* 0000, 0001, 0010, 0011, 0100, 0101, 0110, 0111, 1000, 1001, 1010, 1011, 1100, 1101, 1110, 1111  */
/* LDRU, LDRu, LDrU, LDru, LdRU, LdRu, LdrU, Ldru, lDRU, lDRu, lDrU, lDru, ldRU, ldRu, ldrU, ldru  */
   8,    8,    8,    5,    8,    8,    7,    6,    8,    3,    8,    4,    1,    2,    0,    8
};

struct Control_Def
{
   UINT8 player1Port;
   UINT8 player2Port;
};

static struct Control_Def controls;

struct Graphics_Def
{
   UINT8 *fgChars;
   UINT8 *Sprites;
   UINT8 *bgTiles;
   UINT32 *Palette;
};

static struct Graphics_Def graphics;

struct Memory_Def
{
   struct
   {
      UINT8  *Start;
      UINT32 Size;
   } All;
   struct
   {
      UINT8 *Start;
      UINT32 Size;
      UINT8 *Video;
      UINT8 *Shared1;
      UINT8 *Shared2;
      UINT8 *Shared3;
   } RAM;
   struct
   {
      UINT8 *Rom1;
      UINT8 *Rom2;
      UINT8 *Rom3;
   } Z80;
   struct
   {
      UINT8 *Palette;
      UINT8 *CharLookup;
      UINT8 *SpriteLookup;
   } PROM;
};

static struct Memory_Def memory;

static UINT8 *DrvTempRom = NULL;
static UINT8 *PlayFieldData; // digdug playfield data

enum
{
   CPU1 = 0,
   CPU2,
   CPU3,
   NAMCO_BRD_CPU_COUNT
};

struct CPU_Control_Def
{
   UINT8 FireIRQ;
   UINT8 Halt;
};

struct CPU_Def
{
   struct CPU_Control_Def CPU[NAMCO_BRD_CPU_COUNT];
};

static struct CPU_Def cpus = { 0 };

#define STARS_CTRL_NUM     6

struct Stars_Def
{
   UINT32 ScrollX;
   UINT32 ScrollY;
   UINT8 Control[STARS_CTRL_NUM];
};

static struct Stars_Def stars = { 0 };

#define IOCHIP_BUF_SIZE       16

struct IOChip_Def
{
   UINT8 CustomCommand;
   UINT8 CPU1FireNMI;
   UINT8 Mode;
   UINT8 Credits;
   UINT8 LeftCoinPerCredit;
   UINT8 LeftCreditPerCoin;
   UINT8 RightCoinPerCredit;
   UINT8 RightCreditPerCoin;
   UINT8 AuxCoinPerCredit;
   UINT8 AuxCreditPerCoin;
   UINT8 Buffer[IOCHIP_BUF_SIZE];
};

static struct IOChip_Def ioChip = { 0 };

#define NAMCO54XX_CFG1_SIZE   4
#define NAMCO54XX_CFG2_SIZE   4
#define NAMCO54XX_CFG3_SIZE   5

enum
{
   NAMCO54_WR_CFG1 = 1,
   NAMCO54_WR_CFG2,
   NAMCO54_WR_CFG3
};

#define NAMCO54_CMD_NOP       0x00
#define NAMCO54_CMD_SND4_7    0x10
#define NAMCO54_CMD_SND8_11   0x20
#define NAMCO54_CMD_CFG1_WR   0x30
#define NAMCO54_CMD_CFG2_WR   0x40
#define NAMCO54_CMD_SND17_20  0x50
#define NAMCO54_CMD_CFG3_WR   0x60
#define NAMCO54_CMD_FRQ_OUT   0x70


struct NAMCO54XX_Def
{
   INT32 Fetch;
   INT32 FetchMode;
   UINT8 Config1[NAMCO54XX_CFG1_SIZE];
   UINT8 Config2[NAMCO54XX_CFG2_SIZE]; 
   UINT8 Config3[NAMCO54XX_CFG3_SIZE];
};

static struct NAMCO54XX_Def namco54xx = { 0 };

enum GAMES_ON_MACHINE
{
   NAMCO_GALAGA = 0,
   NAMCO_DIGDUG,
   NAMCO_XEVIOUS,
   NAMCO_TOTAL_GAMES
};

//static UINT8 DrvFlipScreen;

struct CPU_Rd_Table
{
   UINT16 startAddr;
   UINT16 endAddr;
   UINT8 (*readFunc)(UINT16 offset);
};

struct CPU_Wr_Table
{
   UINT16 startAddr;
   UINT16 endAddr;
   void (*writeFunc)(UINT16 offset, UINT8 dta);
};

struct MachineDef
{
   INT32 Game;
   UINT8 bHasSamples;
   UINT8 FlipScreen;
   struct CPU_Wr_Table *wrAddrList;
   struct CPU_Rd_Table *rdAddrList;
};

static struct MachineDef machine = { 0 };

struct Button_Def
{
   INT32 Hold[NAMCO_BRD_INP_COUNT];
   INT32 Held[NAMCO_BRD_INP_COUNT];
   INT32 Last;
};

static struct Button_Def button = { 0 };

#define INP_GALAGA_COIN_TRIGGER     0x70
#define INP_GALAGA_COIN_MASK        0x70
#define INP_GALAGA_START_1          0x04
#define INP_GALAGA_START_2          0x08

#define INP_DIGDUG_COIN_TRIGGER     0x01
#define INP_DIGDUG_COIN_MASK        0x01
#define INP_DIGDUG_START_1          0x10
#define INP_DIGDUG_START_2          0x20

#define INP_XEVIOUS_COIN_TRIGGER    0x70
#define INP_XEVIOUS_COIN_MASK       0x70
#define INP_XEVIOUS_START_1         0x04
#define INP_XEVIOUS_START_2         0x08

static const struct CoinAndCredit_Def
{
   UINT8 portNumber;
   UINT8 coinTrigger;
   UINT8 coinMask;
   UINT8 start1Trigger;
   UINT8 start1Mask;
   UINT8 start2Trigger;
   UINT8 start2Mask;
} coinAndCreditParams[NAMCO_TOTAL_GAMES] = 
{
   {
      .portNumber =     0,
      .coinTrigger =    INP_GALAGA_COIN_TRIGGER,
      .coinMask =       INP_GALAGA_COIN_MASK,
      .start1Trigger =  INP_GALAGA_START_1,
      .start1Mask =     INP_GALAGA_START_1,
      .start2Trigger =  INP_GALAGA_START_2,
      .start2Mask =     INP_GALAGA_START_2,
   },
   {
      .portNumber =     0,
      .coinTrigger =    INP_DIGDUG_COIN_TRIGGER,
      .coinMask =       INP_DIGDUG_COIN_MASK,
      .start1Trigger =  INP_DIGDUG_START_1,
      .start1Mask =     INP_DIGDUG_START_1,
      .start2Trigger =  INP_DIGDUG_START_2,
      .start2Mask =     INP_DIGDUG_START_2,
   },
   {
      .portNumber =     2,
      .coinTrigger =    INP_XEVIOUS_COIN_TRIGGER,
      .coinMask =       INP_XEVIOUS_COIN_MASK,
      .start1Trigger =  INP_XEVIOUS_START_1,
      .start1Mask =     INP_XEVIOUS_START_1,
      .start2Trigger =  INP_XEVIOUS_START_2,
      .start2Mask =     INP_XEVIOUS_START_2,
   }
};

static struct Game_Params
{
// Dig Dug playfield stuff
   INT32 playfield;
   INT32 alphacolor;
   INT32 playenable;
   INT32 playcolor;
// Xevious start gate
   INT32 startEnable;
   UINT8 coinInserted;
} gameVars;

enum SpriteFlags
{
   X_FLIP = 0,
   Y_FLIP,
   X_SIZE,
   Y_SIZE
};

#define xFlip (1 << X_FLIP)
#define yFlip (1 << Y_FLIP)
#define xSize (1 << X_SIZE)
#define ySize (1 << Y_SIZE)
#define Orient (xFlip | yFlip)

struct Namco_Sprite_Params
{
   INT32 Sprite;
   INT32 Colour;
   INT32 xStart;
   INT32 yStart;
   INT32 xStep;
   INT32 yStep;
   INT32 Flags;
   INT32 PaletteBits;
   INT32 PaletteOffset;
};

static void machineReset(void);
static INT32 DrvDoReset(void);
static void Namco54XXWrite(INT32 Data);
static UINT8 NamcoZ80ReadDip(UINT16 Offset, UINT32 DipCount);
static UINT8 NamcoZ80ReadIoCmd(UINT16 Offset);
static UINT8 __fastcall NamcoZ80ProgRead(UINT16 addr);
static UINT8 updateCoinAndCredit(UINT8 gameID);
static UINT8 updateJoyAndButtons(UINT16 Offset, UINT8 jp);
static void NamcoZ80WriteSound(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteCPU1Irq(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteCPU2Irq(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteCPU3Irq(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteCPUReset(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteIoChip(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteIoCmd(UINT16 Offset, UINT8 dta);
static void NamcoZ80WriteFlipScreen(UINT16 Offset, UINT8 dta);
static void __fastcall NamcoZ80ProgWrite(UINT16 addr, UINT8 dta);
static void NamcoRenderSprites(
   UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3, 
   UINT32 GetSpriteParams(
      struct Namco_Sprite_Params *spriteParams, 
      UINT32 Offset, 
      UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3
   )
);
static INT32 DrvExit(void);
static void DrvPreMakeInputs(void);
static void DrvMakeInputs(void);
static INT32 DrvFrame(void);
static INT32 DrvScan(INT32 nAction, INT32 *pnMin);

/* === Galaga === */

static struct BurnInputInfo GalagaInputList[] =
{
	{"Coin 1"            , BIT_DIGITAL  , &input.PortBits[0].Current[4], "p1 coin"   },
	{"Start 1"           , BIT_DIGITAL  , &input.PortBits[0].Current[2], "p1 start"  },
	{"Coin 2"            , BIT_DIGITAL  , &input.PortBits[0].Current[5], "p2 coin"   },
	{"Start 2"           , BIT_DIGITAL  , &input.PortBits[0].Current[3], "p2 start"  },

	{"Left"              , BIT_DIGITAL  , &input.PortBits[1].Current[3], "p1 left"   },
	{"Right"             , BIT_DIGITAL  , &input.PortBits[1].Current[1], "p1 right"  },
	{"Fire 1"            , BIT_DIGITAL  , &input.PortBits[1].Current[4], "p1 fire 1" },
	
	{"Left (Cocktail)"   , BIT_DIGITAL  , &input.PortBits[2].Current[3], "p2 left"   },
	{"Right (Cocktail)"  , BIT_DIGITAL  , &input.PortBits[2].Current[1], "p2 right"  },
	{"Fire 1 (Cocktail)" , BIT_DIGITAL  , &input.PortBits[2].Current[4], "p2 fire 1" },

	{"Reset"             , BIT_DIGITAL  , &input.Reset,                  "reset"     },
	{"Service"           , BIT_DIGITAL  , &input.PortBits[0].Current[6], "service"   },
	{"Dip 1"             , BIT_DIPSWITCH, &input.Dip[0],                 "dip"       },
	{"Dip 2"             , BIT_DIPSWITCH, &input.Dip[1],                 "dip"       },
	{"Dip 3"             , BIT_DIPSWITCH, &input.Dip[2],                 "dip"       },
};

STDINPUTINFO(Galaga)

#define GALAGA_NUM_OF_DIPSWITCHES      3

static struct BurnDIPInfo GalagaDIPList[]=
{
	// Default Values
	{0x0c, 0xff, 0x80, 0x80, NULL                     },
	{0x0d, 0xff, 0xff, 0xf7, NULL                     },
	{0x0e, 0xff, 0xff, 0x97, NULL                     },
	
	// Dip 1
	{0   , 0xfe, 0   , 2   , "Service Mode"           },
	{0x0c, 0x01, 0x80, 0x80, "Off"                    },
	{0x0c, 0x01, 0x80, 0x00, "On"                     },

	// Dip 2
	{0   , 0xfe, 0   , 4   , "Difficulty"             },
	{0x0d, 0x01, 0x03, 0x03, "Easy"                   },
	{0x0d, 0x01, 0x03, 0x00, "Medium"                 },
	{0x0d, 0x01, 0x03, 0x01, "Hard"                   },
	{0x0d, 0x01, 0x03, 0x02, "Hardest"                },
	
	{0   , 0xfe, 0   , 2   , "Demo Sounds"            },
	{0x0d, 0x01, 0x08, 0x08, "Off"                    },
	{0x0d, 0x01, 0x08, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Freeze"                 },
	{0x0d, 0x01, 0x10, 0x10, "Off"                    },
	{0x0d, 0x01, 0x10, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Rack Test"              },
	{0x0d, 0x01, 0x20, 0x20, "Off"                    },
	{0x0d, 0x01, 0x20, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Cabinet"                },
	{0x0d, 0x01, 0x80, 0x80, "Upright"                },
	{0x0d, 0x01, 0x80, 0x00, "Cocktail"               },
	
	// Dip 3	
	{0   , 0xfe, 0   , 8   , "Coinage"                },
	{0x0e, 0x01, 0x07, 0x04, "4 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x02, "3 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x06, "2 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x07, "1 Coin  1 Play"         },
	{0x0e, 0x01, 0x07, 0x01, "2 Coins 3 Plays"        },
	{0x0e, 0x01, 0x07, 0x03, "1 Coin  2 Plays"        },
	{0x0e, 0x01, 0x07, 0x05, "1 Coin  3 Plays"        },
	{0x0e, 0x01, 0x07, 0x00, "Freeplay"               },
	
	{0   , 0xfe, 0   , 8   , "Bonus Life"             },
	{0x0e, 0x01, 0x38, 0x20, "20k  60k  60k"          },
	{0x0e, 0x01, 0x38, 0x18, "20k  60k"               },
	{0x0e, 0x01, 0x38, 0x10, "20k  70k  70k"          },
	{0x0e, 0x01, 0x38, 0x30, "20k  80k  80k"          },
	{0x0e, 0x01, 0x38, 0x38, "30k  80k"               },
	{0x0e, 0x01, 0x38, 0x08, "30k 100k 100k"          },
	{0x0e, 0x01, 0x38, 0x28, "30k 120k 120k"          },
	{0x0e, 0x01, 0x38, 0x00, "None"                   },
	
	{0   , 0xfe, 0   , 4   , "Lives"                  },
	{0x0e, 0x01, 0xc0, 0x00, "2"                      },
	{0x0e, 0x01, 0xc0, 0x80, "3"                      },
	{0x0e, 0x01, 0xc0, 0x40, "4"                      },
	{0x0e, 0x01, 0xc0, 0xc0, "5"                      },
};

STDDIPINFO(Galaga)

static struct BurnDIPInfo GalagamwDIPList[]=
{
	// Default Values
	{0x0c, 0xff, 0xff, 0x80, NULL                     },
	{0x0d, 0xff, 0xff, 0xf7, NULL                     },
	{0x0e, 0xff, 0xff, 0x97, NULL                     },
	
	// Dip 1
	{0   , 0xfe, 0   , 2   , "Service Mode"           },
	{0x0c, 0x01, 0x80, 0x80, "Off"                    },
	{0x0c, 0x01, 0x80, 0x00, "On"                     },

	// Dip 2
	{0   , 0xfe, 0   , 2   , "2 Credits Game"         },
	{0x0d, 0x01, 0x01, 0x00, "1 Player"               },
	{0x0d, 0x01, 0x01, 0x01, "2 Players"              },
	
	{0   , 0xfe, 0   , 4   , "Difficulty"             },
	{0x0d, 0x01, 0x06, 0x06, "Easy"                   },
	{0x0d, 0x01, 0x06, 0x00, "Medium"                 },
	{0x0d, 0x01, 0x06, 0x02, "Hard"                   },
	{0x0d, 0x01, 0x06, 0x04, "Hardest"                },
	
	{0   , 0xfe, 0   , 2   , "Demo Sounds"            },
	{0x0d, 0x01, 0x08, 0x08, "Off"                    },
	{0x0d, 0x01, 0x08, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Freeze"                 },
	{0x0d, 0x01, 0x10, 0x10, "Off"                    },
	{0x0d, 0x01, 0x10, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Rack Test"              },
	{0x0d, 0x01, 0x20, 0x20, "Off"                    },
	{0x0d, 0x01, 0x20, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Cabinet"                },
	{0x0d, 0x01, 0x80, 0x80, "Upright"                },
	{0x0d, 0x01, 0x80, 0x00, "Cocktail"               },
	
	// Dip 3	
	{0   , 0xfe, 0   , 8   , "Coinage"                },
	{0x0e, 0x01, 0x07, 0x04, "4 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x02, "3 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x06, "2 Coins 1 Play"         },
	{0x0e, 0x01, 0x07, 0x07, "1 Coin  1 Play"         },
	{0x0e, 0x01, 0x07, 0x01, "2 Coins 3 Plays"        },
	{0x0e, 0x01, 0x07, 0x03, "1 Coin  2 Plays"        },
	{0x0e, 0x01, 0x07, 0x05, "1 Coin  3 Plays"        },
	{0x0e, 0x01, 0x07, 0x00, "Freeplay"               },	
	
	{0   , 0xfe, 0   , 8   , "Bonus Life"             },
	{0x0e, 0x01, 0x38, 0x20, "20k  60k  60k"          },
	{0x0e, 0x01, 0x38, 0x18, "20k  60k"               },
	{0x0e, 0x01, 0x38, 0x10, "20k  70k  70k"          },
	{0x0e, 0x01, 0x38, 0x30, "20k  80k  80k"          },
	{0x0e, 0x01, 0x38, 0x38, "30k  80k"               },
	{0x0e, 0x01, 0x38, 0x08, "30k 100k 100k"          },
	{0x0e, 0x01, 0x38, 0x28, "30k 120k 120k"          },
	{0x0e, 0x01, 0x38, 0x00, "None"                   },	
	
	{0   , 0xfe, 0   , 4   , "Lives"                  },
	{0x0e, 0x01, 0xc0, 0x00, "2"                      },
	{0x0e, 0x01, 0xc0, 0x80, "3"                      },
	{0x0e, 0x01, 0xc0, 0x40, "4"                      },
	{0x0e, 0x01, 0xc0, 0xc0, "5"                      },
};

STDDIPINFO(Galagamw)

static struct BurnRomInfo GalagaRomDesc[] = {
	{ "gg1_1b.3p",     0x01000, 0xab036c9f, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "gg1_2b.3m",     0x01000, 0xd9232240, BRF_ESS | BRF_PRG   }, //	 1
	{ "gg1_3.2m",      0x01000, 0x753ce503, BRF_ESS | BRF_PRG   }, //	 2
	{ "gg1_4b.2l",     0x01000, 0x499fcc76, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "gg1_5b.3f",     0x01000, 0xbb5caae3, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "gg1_7b.2c",     0x01000, 0xd016686b, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "gg1_9.4l",      0x01000, 0x58b2f47c, BRF_GRA             },	//  6	Characters
	
	{ "gg1_11.4d",     0x01000, 0xad447c80, BRF_GRA             },	//  7	Sprites
	{ "gg1_10.4f",     0x01000, 0xdd6f1afc, BRF_GRA             },	//  8
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	//  9	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 10
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 11
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 12
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 13
};

STD_ROM_PICK(Galaga)
STD_ROM_FN(Galaga)

static struct BurnRomInfo GalagaoRomDesc[] = {
	{ "gg1-1.3p",      0x01000, 0xa3a0f743, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "gg1-2.3m",      0x01000, 0x43bb0d5c, BRF_ESS | BRF_PRG   }, //	 1
	{ "gg1-3.2m",      0x01000, 0x753ce503, BRF_ESS | BRF_PRG   }, //	 2
	{ "gg1-4.2l",      0x01000, 0x83874442, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "gg1-5.3f",      0x01000, 0x3102fccd, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "gg1-7.2c",      0x01000, 0x8995088d, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "gg1-9.4l",      0x01000, 0x58b2f47c, BRF_GRA             },	//  6	Characters
	
	{ "gg1-11.4d",     0x01000, 0xad447c80, BRF_GRA             },	//  7	Sprites
	{ "gg1-10.4f",     0x01000, 0xdd6f1afc, BRF_GRA             }, //  8
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	//  9	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 10
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 11
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 12
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 13
};

STD_ROM_PICK(Galagao)
STD_ROM_FN(Galagao)

static struct BurnRomInfo GalagamwRomDesc[] = {
	{ "3200a.bin",     0x01000, 0x3ef0b053, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "3300b.bin",     0x01000, 0x1b280831, BRF_ESS | BRF_PRG   }, //	 1
	{ "3400c.bin",     0x01000, 0x16233d33, BRF_ESS | BRF_PRG   }, //	 2
	{ "3500d.bin",     0x01000, 0x0aaf5c23, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "3600e.bin",     0x01000, 0xbc556e76, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "3700g.bin",     0x01000, 0xb07f0aa4, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "2600j.bin",     0x01000, 0x58b2f47c, BRF_GRA             },	//  6	Characters
	
	{ "2800l.bin",     0x01000, 0xad447c80, BRF_GRA             },	//  7	Sprites
	{ "2700k.bin",     0x01000, 0xdd6f1afc, BRF_GRA             },	//  8
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	//  9	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 10
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 11
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 12
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 13
};

STD_ROM_PICK(Galagamw)
STD_ROM_FN(Galagamw)

static struct BurnRomInfo GalagamfRomDesc[] = {
	{ "3200a.bin",     0x01000, 0x3ef0b053, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "3300b.bin",     0x01000, 0x1b280831, BRF_ESS | BRF_PRG   }, //	 1
	{ "3400c.bin",     0x01000, 0x16233d33, BRF_ESS | BRF_PRG   }, //	 2
	{ "3500d.bin",     0x01000, 0x0aaf5c23, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "3600fast.bin",  0x01000, 0x23d586e5, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "3700g.bin",     0x01000, 0xb07f0aa4, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "2600j.bin",     0x01000, 0x58b2f47c, BRF_GRA             },	//  6	Characters
	
	{ "2800l.bin",     0x01000, 0xad447c80, BRF_GRA             },	//  7	Sprites
	{ "2700k.bin",     0x01000, 0xdd6f1afc, BRF_GRA             },	//  8
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	//  9	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 10
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 11
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 12
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 13
};

STD_ROM_PICK(Galagamf)
STD_ROM_FN(Galagamf)

static struct BurnRomInfo GalagamkRomDesc[] = {
	{ "mk2-1",         0x01000, 0x23cea1e2, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "mk2-2",         0x01000, 0x89695b1a, BRF_ESS | BRF_PRG   }, //	 1
	{ "3400c.bin",     0x01000, 0x16233d33, BRF_ESS | BRF_PRG   }, //	 2
	{ "mk2-4",         0x01000, 0x24b767f5, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "gg1-5.3f",      0x01000, 0x3102fccd, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "gg1-7b.2c",     0x01000, 0xd016686b, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "gg1-9.4l",      0x01000, 0x58b2f47c, BRF_GRA             },	//  6	Characters
	
	{ "gg1-11.4d",     0x01000, 0xad447c80, BRF_GRA             },	//  7	Sprites
	{ "gg1-10.4f",     0x01000, 0xdd6f1afc, BRF_GRA             },	//  8
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	//  9	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 10
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 11
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 12
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 13
};

STD_ROM_PICK(Galagamk)
STD_ROM_FN(Galagamk)

static struct BurnRomInfo GallagRomDesc[] = {
	{ "gallag.1",      0x01000, 0xa3a0f743, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "gallag.2",      0x01000, 0x5eda60a7, BRF_ESS | BRF_PRG   }, //	 1
	{ "gallag.3",      0x01000, 0x753ce503, BRF_ESS | BRF_PRG   }, //	 2
	{ "gallag.4",      0x01000, 0x83874442, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "gallag.5",      0x01000, 0x3102fccd, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "gallag.7",      0x01000, 0x8995088d, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "gallag.6",      0x01000, 0x001b70bc, BRF_ESS | BRF_PRG   }, //  6	Z80 #4 Program Code
	
	{ "gallag.8",      0x01000, 0x169a98a4, BRF_GRA             },	//  7	Characters
	
	{ "gallag.a",      0x01000, 0xad447c80, BRF_GRA             },	//  8	Sprites
	{ "gallag.9",      0x01000, 0xdd6f1afc, BRF_GRA             },	//  9
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	// 10	PROMs
	{ "prom-4.2n",     0x00100, 0x59b6edab, BRF_GRA             },	// 11
	{ "prom-3.1c",     0x00100, 0x4a04bb6b, BRF_GRA             },	// 12
	{ "prom-1.1d",     0x00100, 0x7a2815b4, BRF_GRA             },	// 13
	{ "prom-2.5c",     0x00100, 0x77245b66, BRF_GRA             },	// 14
};

STD_ROM_PICK(Gallag)
STD_ROM_FN(Gallag)

static struct BurnRomInfo NebulbeeRomDesc[] = {
	{ "nebulbee.01",   0x01000, 0xf405f2c4, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "nebulbee.02",   0x01000, 0x31022b60, BRF_ESS | BRF_PRG   }, //	 1
	{ "gg1_3.2m",      0x01000, 0x753ce503, BRF_ESS | BRF_PRG   }, //	 2
	{ "nebulbee.04",   0x01000, 0xd76788a5, BRF_ESS | BRF_PRG   }, //	 3
	
	{ "gg1-5",         0x01000, 0x3102fccd, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	
	{ "gg1-7",         0x01000, 0x8995088d, BRF_ESS | BRF_PRG   }, //  5	Z80 #3 Program Code
	
	{ "nebulbee.07",   0x01000, 0x035e300c, BRF_ESS | BRF_PRG   }, //  6	Z80 #4 Program Code
	
	{ "gg1_9.4l",      0x01000, 0x58b2f47c, BRF_GRA             },	//  7	Characters
	
	{ "gg1_11.4d",     0x01000, 0xad447c80, BRF_GRA             },	//  8	Sprites
	{ "gg1_10.4f",     0x01000, 0xdd6f1afc, BRF_GRA             },	//  9
	
	{ "prom-5.5n",     0x00020, 0x54603c6b, BRF_GRA             },	// 10	PROMs
	{ "2n.bin",        0x00100, 0xa547d33b, BRF_GRA             },	// 11
	{ "1c.bin",        0x00100, 0xb6f585fb, BRF_GRA             },	// 12
	{ "1d.bin",        0x00100, 0x86d92b24, BRF_GRA             },	// 14
	{ "5c.bin",        0x00100, 0x8bd565f6, BRF_GRA             },	// 13
};

STD_ROM_PICK(Nebulbee)
STD_ROM_FN(Nebulbee)

static struct BurnSampleInfo GalagaSampleDesc[] = {
#if !defined (ROM_VERIFY)
   { "bang", SAMPLE_NOLOOP },
   { "init", SAMPLE_NOLOOP },
#endif
  { "", 0 }
};

STD_SAMPLE_PICK(Galaga)
STD_SAMPLE_FN(Galaga)

static INT32 GalagaInit(void);
static INT32 GallagInit(void);
static INT32 GalagaMemIndex(void);
static void GalagaMachineInit(void);
static UINT8 GalagaZ80ReadDip(UINT16 Offset);
static UINT8 GalagaZ80ReadInputs(UINT16 offset);
static void GalagaZ80Write7007(UINT16 offset, UINT8 dta);
static void GalagaZ80WriteStars(UINT16 Offset, UINT8 dta);
static INT32 GalagaDraw(void);
static void GalagaCalcPalette(void);
static void GalagaInitStars(void);
static void GalagaRenderStars(void);
static void GalagaRenderSprites(void);
static UINT32 GalagaGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3);

static struct CPU_Rd_Table GalagaZ80ReadList[] =
{
	{ 0x6800, 0x6807, GalagaZ80ReadDip        }, 
   { 0x7000, 0x7002, GalagaZ80ReadInputs     },
	{ 0x7100, 0x7100, NamcoZ80ReadIoCmd       },
	{ 0x0000, 0x0000, NULL                    },
};

static struct CPU_Wr_Table GalagaZ80WriteList[] = 
{
	{ 0x6800, 0x681f, NamcoZ80WriteSound      },
   { 0x6820, 0x6820, NamcoZ80WriteCPU1Irq    },
	{ 0x6821, 0x6821, NamcoZ80WriteCPU2Irq    },
	{ 0x6822, 0x6822, NamcoZ80WriteCPU3Irq    },
	{ 0x6823, 0x6823, NamcoZ80WriteCPUReset   },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
	{ 0x7000, 0x700f, NamcoZ80WriteIoChip     },
   { 0x7007, 0x7007, GalagaZ80Write7007      },
	{ 0x7100, 0x7100, NamcoZ80WriteIoCmd      },
	{ 0xa000, 0xa005, GalagaZ80WriteStars     },
	{ 0xa007, 0xa007, NamcoZ80WriteFlipScreen },
   { 0x0000, 0x0000, NULL                    },
};

#define GALAGA_NUM_OF_CHAR_PALETTE_BITS   2
#define GALAGA_NUM_OF_SPRITE_PALETTE_BITS 2

#define GALAGA_PALETTE_OFFSET_CHARS       0
#define GALAGA_PALETTE_OFFSET_SPRITE      0x100
#define GALAGA_PALETTE_OFFSET_BGSTARS     0x200
#define GALAGA_PALETTE_SIZE_CHARS         0x100
#define GALAGA_PALETTE_SIZE_SPRITES       0x100
#define GALAGA_PALETTE_SIZE_BGSTARS       0x040
#define GALAGA_PALETTE_SIZE (GALAGA_PALETTE_SIZE_CHARS + \
                             GALAGA_PALETTE_SIZE_SPRITES + \
                             GALAGA_PALETTE_SIZE_BGSTARS)
                             
#define GALAGA_NUM_OF_CHAR                0x100
#define GALAGA_SIZE_OF_CHAR_IN_BYTES      0x80

#define GALAGA_NUM_OF_SPRITE              0x80
#define GALAGA_SIZE_OF_SPRITE_IN_BYTES    0x200

static INT32 CharPlaneOffsets[GALAGA_NUM_OF_CHAR_PALETTE_BITS] = 
   { 0, 4 };
static INT32 CharXOffsets[8]       = { 64, 65, 66, 67, 0, 1, 2, 3 };
static INT32 CharYOffsets[8]       = { 0, 8, 16, 24, 32, 40, 48, 56 };
static INT32 SpritePlaneOffsets[GALAGA_NUM_OF_SPRITE_PALETTE_BITS] = 
   { 0, 4 };
static INT32 SpriteXOffsets[16]    = { 0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195 };
static INT32 SpriteYOffsets[16]    = { 0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312 };

struct Star_Def 
{
	UINT16 x;
   UINT16 y;
	UINT8 Colour;
   UINT8 Set;
};

#define MAX_STARS 252
static struct Star_Def *StarSeedTable;


static INT32 GalagaInit()
{
	// Allocate and Blank all required memory
	memory.All.Start = NULL;
	GalagaMemIndex();
	
   memory.All.Start = (UINT8 *)BurnMalloc(memory.All.Size);
	if (NULL == memory.All.Start) 
      return 1;
	memset(memory.All.Start, 0, memory.All.Size);
	
   GalagaMemIndex();

	DrvTempRom = (UINT8 *)BurnMalloc(0x02000);

	// Load Z80 #1 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x00000,    0, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x01000,    1, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x02000,    2, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x03000,    3, 1)) return 1;
	
	// Load Z80 #2 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x00000,    4, 1)) return 1;
	
	// Load Z80 #3 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom3 + 0x00000,    5, 1)) return 1;
	
	// Load and decode the chars
	if (0 != BurnLoadRom(DrvTempRom,                   6, 1)) return 1;
	GfxDecode(
      GALAGA_NUM_OF_CHAR, 
      GALAGA_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      CharPlaneOffsets, 
      CharXOffsets, 
      CharYOffsets, 
      GALAGA_SIZE_OF_CHAR_IN_BYTES, 
      DrvTempRom, 
      graphics.fgChars
   );
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         7, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         8, 1)) return 1;
	GfxDecode(
      GALAGA_NUM_OF_SPRITE, 
      GALAGA_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      SpritePlaneOffsets, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      GALAGA_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom, 
      graphics.Sprites
   );

	// Load the PROMs
	if (0 != BurnLoadRom(memory.PROM.Palette,          9, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.CharLookup,       10, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.SpriteLookup,     11, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm,               12, 1)) return 1;
	
	BurnFree(DrvTempRom);
	
	GalagaMachineInit();

	return 0;
}

static INT32 GallagInit()
{
	// Allocate and Blank all required memory
	memory.All.Start = NULL;
	GalagaMemIndex();
   
	memory.All.Start = (UINT8 *)BurnMalloc(memory.All.Size);
	if (NULL == memory.All.Start) 
      return 1;
	memset(memory.All.Start, 0, memory.All.Size);
	
   GalagaMemIndex();

	DrvTempRom = (UINT8 *)BurnMalloc(0x02000);

	// Load Z80 #1 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x00000,    0, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x01000,    1, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x02000,    2, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x03000,    3, 1)) return 1;
	
	// Load Z80 #2 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x00000,    4, 1)) return 1;
	
	// Load Z80 #3 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom3 + 0x00000,    5, 1)) return 1;
	
	// Load and decode the chars
	if (0 != BurnLoadRom(DrvTempRom,                   7, 1)) return 1;
	GfxDecode(
      GALAGA_NUM_OF_CHAR, 
      GALAGA_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      CharPlaneOffsets, 
      CharXOffsets, 
      CharYOffsets, 
      GALAGA_SIZE_OF_CHAR_IN_BYTES, 
      DrvTempRom, 
      graphics.fgChars
   );
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         8, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         9, 1)) return 1;
	GfxDecode(
      GALAGA_NUM_OF_SPRITE, 
      GALAGA_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      SpritePlaneOffsets, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      GALAGA_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom, 
      graphics.Sprites
   );

	// Load the PROMs
	if (0 != BurnLoadRom(memory.PROM.Palette,          10, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.CharLookup,       11, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.SpriteLookup,     12, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm,               13, 1)) return 1;
	
	BurnFree(DrvTempRom);
	
	GalagaMachineInit();

	return 0;
}

static INT32 GalagaMemIndex()
{
	UINT8 *Next = memory.All.Start;

	memory.Z80.Rom1            = Next; Next += 0x04000;
	memory.Z80.Rom2            = Next; Next += 0x04000;
	memory.Z80.Rom3            = Next; Next += 0x04000;
	memory.PROM.Palette        = Next; Next += 0x00020;
	memory.PROM.CharLookup     = Next; Next += 0x00100;
	memory.PROM.SpriteLookup   = Next; Next += 0x00100;
	NamcoSoundProm             = Next; Next += 0x00200;
	
	memory.RAM.Start           = Next;

	memory.RAM.Video           = Next; Next += 0x00800;
	memory.RAM.Shared1         = Next; Next += 0x00400;
	memory.RAM.Shared2         = Next; Next += 0x00400;
	memory.RAM.Shared3         = Next; Next += 0x00400;

	memory.RAM.Size            = Next - memory.RAM.Start;

	graphics.bgTiles           = Next; Next += sizeof(struct Star_Def) * MAX_STARS;
   StarSeedTable              = (struct Star_Def *)graphics.bgTiles;
	graphics.fgChars           = Next; Next += GALAGA_NUM_OF_CHAR * 8 * 8;
	graphics.Sprites           = Next; Next += GALAGA_NUM_OF_SPRITE * 16 * 16;
	graphics.Palette           = (UINT32*)Next; Next += GALAGA_PALETTE_SIZE * sizeof(UINT32);

	memory.All.Size            = Next - memory.All.Start;

	return 0;
}

static tilemap_scan ( galaga_fg )
{
	if ((col - 2) & 0x20)
   {
		return (row + 2 + (((col - 2) & 0x1f) << 5));
	}

   return col - 2 + ((row + 2) << 5);
}

static tilemap_callback ( galaga_fg )
{
   INT32 Code   = memory.RAM.Video[offs + 0x000] & 0x7f;
   INT32 Colour = memory.RAM.Video[offs + 0x400] & 0x3f;

	TILE_SET_INFO(0, Code, Colour, 0);
}

static void GalagaMachineInit()
{
	ZetInit(CPU1);
	ZetOpen(CPU1);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU2);
	ZetOpen(CPU2);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU3);
	ZetOpen(CPU3);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	NamcoSoundInit(18432000 / 6 / 32, 3, 0);
	NacmoSoundSetAllRoutes(0.90 * 10.0 / 16.0, BURN_SND_ROUTE_BOTH);
	BurnSampleInit(1);
	BurnSampleSetAllRoutesAllSamples(0.25, BURN_SND_ROUTE_BOTH);
	machine.bHasSamples = BurnSampleGetStatus(0) != -1;
   
   machine.rdAddrList = GalagaZ80ReadList;
   machine.wrAddrList = GalagaZ80WriteList;
   
	GenericTilesInit();

   GenericTilemapInit(
      0, 
      galaga_fg_map_scan, galaga_fg_map_callback, 
      8, 8, 
      NAMCO_TMAP_WIDTH, NAMCO_TMAP_HEIGHT
   );
	GenericTilemapSetGfx(
      0, 
      graphics.fgChars, 
      GALAGA_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      GALAGA_NUM_OF_CHAR * 8 * 8, 
      0x0, 
      GALAGA_PALETTE_SIZE_CHARS - 1
   );
	GenericTilemapSetTransparent(0, 0);
   
	GenericTilemapSetOffsets(TMAP_GLOBAL, 0, 0);

	earom_init();

	// Reset the driver
	DrvDoReset();
   
   gameVars.startEnable = 1;
   
   controls.player1Port = 1;
   controls.player2Port = 2;
}

static UINT8 GalagaZ80ReadDip(UINT16 offset)
{
   return NamcoZ80ReadDip(offset, GALAGA_NUM_OF_DIPSWITCHES);
}

static UINT8 GalagaZ80ReadInputs(UINT16 Offset)
{
   UINT8 retVal = 0xff;
   
   if ( (0x71 == ioChip.CustomCommand) ||
        (0xb1 == ioChip.CustomCommand) )
   {
      switch (Offset)
      {
         case 0:
         {
            if (ioChip.Mode) 
            {
               retVal = input.Ports[0];
            } 
            else 
            {
               retVal = updateCoinAndCredit(NAMCO_GALAGA);
            }
         }
         break;
            
         case 1:
         case 2:
         {
            retVal = updateJoyAndButtons(Offset, input.Ports[Offset]);
         }   
         break;
            
         default:
            break;
      }
   }
   
   return retVal;
}

static void GalagaZ80Write7007(UINT16 offset, UINT8 dta)
{
   if (0xe1 == ioChip.CustomCommand) 
   {
      ioChip.LeftCoinPerCredit = ioChip.Buffer[1];
      ioChip.LeftCreditPerCoin = ioChip.Buffer[2];
   }
   
   return;
}

static void GalagaZ80WriteStars(UINT16 Offset, UINT8 dta)
{
   stars.Control[Offset] = dta & 0x01;
}

static INT32 GalagaDraw()
{
	BurnTransferClear();

	GalagaCalcPalette();

	GenericTilemapSetScrollX(0, 0);
	GenericTilemapSetScrollY(0, 0);
   GenericTilemapSetEnable(0, 1);
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_TRANSPARENT);

	GalagaRenderStars();
	GalagaRenderSprites();	

	BurnTransferCopy(graphics.Palette);
	return 0;
}

#define GALAGA_3BIT_PALETTE_SIZE    32
#define GALAGA_2BIT_PALETTE_SIZE    64

static void GalagaCalcPalette()
{
	UINT32 Palette3Bit[GALAGA_3BIT_PALETTE_SIZE];
	
	for (INT32 i = 0; i < GALAGA_3BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.Palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.Palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.Palette[i] >> 5) & 0x06];
      
		Palette3Bit[i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_CHARS; i ++) 
   {
		graphics.Palette[GALAGA_PALETTE_OFFSET_CHARS + i] = 
         Palette3Bit[((memory.PROM.CharLookup[i]) & 0x0f) + 0x10];
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_SPRITES; i ++) 
   {
		graphics.Palette[GALAGA_PALETTE_OFFSET_SPRITE + i] = 
         Palette3Bit[memory.PROM.SpriteLookup[i] & 0x0f];
	}
	
	UINT32 Palette2Bit[GALAGA_2BIT_PALETTE_SIZE];

	for (INT32 i = 0; i < GALAGA_2BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour2Bit[(i >> 0) & 0x03];
      INT32 g = Colour2Bit[(i >> 2) & 0x03];
      INT32 b = Colour2Bit[(i >> 4) & 0x03];
      
		Palette2Bit[i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_BGSTARS; i ++) 
   {
		graphics.Palette[GALAGA_PALETTE_OFFSET_BGSTARS + i] = 
         Palette2Bit[i];
	}

	GalagaInitStars();
}

static void GalagaInitStars()
{
	/*
	  Galaga star line and pixel locations pulled directly from
	  a clocked stepping of the 05 starfield. The chip was clocked
	  on a test rig with hblank and vblank simulated, each X & Y
	  location of a star being recorded along with it's color value.

	  The lookup table is generated using a reverse engineered
	  linear feedback shift register + XOR boolean expression.

	  Because the starfield begins generating stars at the point
	  in time it's enabled the exact horiz location of the stars
	  on Galaga depends on the length of time of the POST for the
	  original board.

	  Two control bits determine which of two sets are displayed
	  set 0 or 1 and simultaneously 2 or 3.

	  There are 63 stars in each set, 126 displayed at any one time
	  Code: jmakovicka, based on info from http://www.pin4.at/pro_custom_05xx.php
	*/

	const UINT16 feed = 0x9420;

	INT32 idx = 0;
	for (UINT32 sf = 0; sf < 4; ++ sf)
	{
		// starfield select flags
		UINT16 sf1 = (sf >> 1) & 1;
		UINT16 sf2 = sf & 1;

		UINT16 i = 0x70cc;
		for (UINT32 cnt = 0; cnt < 65535; ++ cnt)
		{
			// output enable lookup
			UINT16 xor1 = i    ^ (i >> 3);
			UINT16 xor2 = xor1 ^ (i >> 2);
			UINT16 oe = (sf1 ? 0 : 0x4000) | ((sf1 ^ sf2) ? 0 : 0x1000);
			if ( ( 0x8007             == ( i   & 0x8007) ) && 
              ( 0x2008             == (~i   & 0x2008) ) && 
              ( (sf1 ? 0 : 0x0100) == (xor1 & 0x0100) ) && 
              ( (sf2 ? 0 : 0x0040) == (xor2 & 0x0040) ) && 
              ( oe                 == (i    & 0x5000) ) && 
              ( cnt                >= (256 * 4)       ) )
			{
				// color lookup
				UINT16 xor3 = (i >> 1) ^ (i >> 6);
				UINT16 clr  = ( 
                 (          (i >> 9)             & 0x07)
               | ( ( xor3 ^ (i >> 4) ^ (i >> 7)) & 0x08)
               |   (~xor3                        & 0x10)
               | (        ( (i >> 2) ^ (i >> 5)) & 0x20) )
					^ ( (i & 0x4000) ? 0 : 0x24)
					^ ( ( ((i >> 2) ^ i) & 0x1000) ? 0x21 : 0);

				StarSeedTable[idx].x = cnt % 256;
				StarSeedTable[idx].y = cnt / 256;
				StarSeedTable[idx].Colour = clr;
				StarSeedTable[idx].Set = sf;
				++ idx;
			}

			// update the LFSR
			if (i & 1)
				i = (i >> 1) ^ feed;
			else
				i = (i >> 1);
		}
	}
}

static void GalagaRenderStars()
{
	if (1 == stars.Control[5]) 
   {
		INT32 SetA = stars.Control[3];
		INT32 SetB = stars.Control[4] | 0x02;

		for (INT32 StarCounter = 0; StarCounter < MAX_STARS; StarCounter ++) 
      {
			if ( (SetA == StarSeedTable[StarCounter].Set) || 
              (SetB == StarSeedTable[StarCounter].Set) ) 
         {
				INT32 x = (                      StarSeedTable[StarCounter].x + stars.ScrollX) % 256 + 16;
				INT32 y = ((nScreenHeight / 2) + StarSeedTable[StarCounter].y + stars.ScrollY) % 256;

				if ( (x >= 0) && (x < nScreenWidth)  && 
                 (y >= 0) && (y < nScreenHeight) ) 
            {
					pTransDraw[(y * nScreenWidth) + x] = StarSeedTable[StarCounter].Colour + GALAGA_PALETTE_OFFSET_BGSTARS;
				}
			}

		}
	}
}

static UINT32 GalagaGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3)
{
   spriteParams->Sprite =    SpriteRam1[Offset + 0] & 0x7f;
   spriteParams->Colour =    SpriteRam1[Offset + 1] & 0x3f;
   
   spriteParams->xStart =    SpriteRam2[Offset + 1] - 40 + (0x100 * (SpriteRam3[Offset + 1] & 0x03));
   spriteParams->yStart =    NAMCO_SCREEN_WIDTH - SpriteRam2[Offset + 0] + 1;
   spriteParams->xStep =     16;
   spriteParams->yStep =     16;
   
   spriteParams->Flags =     SpriteRam3[Offset + 0] & 0x0f;
   
   if (spriteParams->Flags & ySize)
   {
      if (spriteParams->Flags & yFlip)
      {
         spriteParams->yStep = -16;
      }
      else
      {
         spriteParams->yStart -= 16;
      }
   }
   
   if (spriteParams->Flags & xSize)
   {
      if (spriteParams->Flags & xFlip)
      {
         spriteParams->xStart += 16;
         spriteParams->xStep  = -16;
      }
   }
   
   spriteParams->PaletteBits   = GALAGA_NUM_OF_SPRITE_PALETTE_BITS;
   spriteParams->PaletteOffset = GALAGA_PALETTE_OFFSET_SPRITE;
   
   return 1;
}

static void GalagaRenderSprites()
{
	UINT8 *SpriteRam1 = memory.RAM.Shared1 + 0x380;
	UINT8 *SpriteRam2 = memory.RAM.Shared2 + 0x380;
	UINT8 *SpriteRam3 = memory.RAM.Shared3 + 0x380;

   NamcoRenderSprites(SpriteRam1, SpriteRam2, SpriteRam3, GalagaGetSpriteParams);
}

/* === Dig Dug === */

static struct BurnInputInfo DigdugInputList[] =
{
	{"P1 Coin"              , BIT_DIGITAL  , &input.PortBits[0].Current[0], "p1 coin"   },
	{"P1 Start"             , BIT_DIGITAL  , &input.PortBits[0].Current[4], "p1 start"  },
	{"P2 Coin"              , BIT_DIGITAL  , &input.PortBits[0].Current[1], "p2 coin"   },
	{"P2 Start"             , BIT_DIGITAL  , &input.PortBits[0].Current[5], "p2 start"  },

	{"P1 Up"                , BIT_DIGITAL  , &input.PortBits[1].Current[0], "p1 up"     },
	{"P1 Down"              , BIT_DIGITAL  , &input.PortBits[1].Current[2], "p1 down"   },
	{"P1 Left"              , BIT_DIGITAL  , &input.PortBits[1].Current[3], "p1 left"   },
	{"P1 Right"             , BIT_DIGITAL  , &input.PortBits[1].Current[1], "p1 right"  },
	{"P1 Fire 1"            , BIT_DIGITAL  , &input.PortBits[1].Current[4], "p1 fire 1" },
	
	{"P2 Up"                , BIT_DIGITAL  , &input.PortBits[2].Current[0], "p2 up"     },
	{"P2 Down"              , BIT_DIGITAL  , &input.PortBits[2].Current[2], "p2 down"   },
	{"P2 Left (Cocktail)"   , BIT_DIGITAL  , &input.PortBits[2].Current[3], "p2 left"   },
	{"P2 Right (Cocktail)"  , BIT_DIGITAL  , &input.PortBits[2].Current[1], "p2 right"  },
	{"P2 Fire 1 (Cocktail)" , BIT_DIGITAL  , &input.PortBits[2].Current[4], "p2 fire 1" },

	{"Service"              , BIT_DIGITAL  , &input.PortBits[0].Current[7], "service"   },
	{"Reset"                , BIT_DIGITAL  , &input.Reset,                  "reset"     },
	{"Dip 1"                , BIT_DIPSWITCH, &input.Dip[0],                 "dip"       },
	{"Dip 2"                , BIT_DIPSWITCH, &input.Dip[1],                 "dip"       },
};

STDINPUTINFO(Digdug)

#define DIGDUG_NUM_OF_DIPSWITCHES      2

static struct BurnDIPInfo DigdugDIPList[]=
{
	{0x10, 0xff, 0xff, 0xa1, NULL		               },
	{0x11, 0xff, 0xff, 0x24, NULL		               },

	{0   , 0xfe, 0   ,    8, "Coin B"		         },
	{0x10, 0x01, 0x07, 0x07, "3 Coins 1 Credits"		},
	{0x10, 0x01, 0x07, 0x03, "2 Coins 1 Credits"		},
	{0x10, 0x01, 0x07, 0x01, "1 Coin  1 Credits"		},
	{0x10, 0x01, 0x07, 0x05, "2 Coins 3 Credits"		},
	{0x10, 0x01, 0x07, 0x06, "1 Coin  2 Credits"		},
	{0x10, 0x01, 0x07, 0x02, "1 Coin  3 Credits"		},
	{0x10, 0x01, 0x07, 0x04, "1 Coin  6 Credits"		},
	{0x10, 0x01, 0x07, 0x00, "1 Coin  7 Credits"		},

	{0   , 0xfe, 0   ,    16, "Bonus Life"		      },
	{0x10, 0x01, 0x38, 0x20, "10K, 40K, Every 40K"	},
	{0x10, 0x01, 0x38, 0x10, "10K, 50K, Every 50K"	},
	{0x10, 0x01, 0x38, 0x30, "20K, 60K, Every 60K"	},
	{0x10, 0x01, 0x38, 0x08, "20K, 70K, Every 70K"	},
	{0x10, 0x01, 0x38, 0x28, "10K and 40K Only"		},
	{0x10, 0x01, 0x38, 0x18, "20K and 60K Only"		},
	{0x10, 0x01, 0x38, 0x38, "10K Only"		         },
	{0x10, 0x01, 0x38, 0x00, "None"		            },
	{0x10, 0x01, 0x38, 0x20, "20K, 60K, Every 60K"	},
	{0x10, 0x01, 0x38, 0x10, "30K, 80K, Every 80K"	},
	{0x10, 0x01, 0x38, 0x30, "20K and 50K Only"		},
	{0x10, 0x01, 0x38, 0x08, "20K and 60K Only"		},
	{0x10, 0x01, 0x38, 0x28, "30K and 70K Only"		},
	{0x10, 0x01, 0x38, 0x18, "20K Only"		         },
	{0x10, 0x01, 0x38, 0x38, "30K Only"		         },
	{0x10, 0x01, 0x38, 0x00, "None"		            },

	{0   , 0xfe, 0   ,    4, "Lives"		            },
	{0x10, 0x01, 0xc0, 0x00, "1"		               },
	{0x10, 0x01, 0xc0, 0x40, "2"		               },
	{0x10, 0x01, 0xc0, 0x80, "3"		               },
	{0x10, 0x01, 0xc0, 0xc0, "5"		               },

	{0   , 0xfe, 0   ,    4, "Coin A"		         },
	{0x11, 0x01, 0xc0, 0x40, "2 Coins 1 Credits"		},
	{0x11, 0x01, 0xc0, 0x00, "1 Coin  1 Credits"		},
	{0x11, 0x01, 0xc0, 0xc0, "2 Coins 3 Credits"		},
	{0x11, 0x01, 0xc0, 0x80, "1 Coin  2 Credits"		},

	{0   , 0xfe, 0   ,    2, "Freeze"		         },
	{0x11, 0x01, 0x20, 0x20, "Off"		            },
	{0x11, 0x01, 0x20, 0x00, "On"		               },

	{0   , 0xfe, 0   ,    2, "Demo Sounds"		      },
	{0x11, 0x01, 0x10, 0x10, "Off"		            },
	{0x11, 0x01, 0x10, 0x00, "On"		               },

	{0   , 0xfe, 0   ,    2, "Allow Continue"		   },
	{0x11, 0x01, 0x08, 0x08, "No"		               },
	{0x11, 0x01, 0x08, 0x00, "Yes"		            },

	{0   , 0xfe, 0   ,    2, "Cabinet"		         },
	{0x11, 0x01, 0x04, 0x04, "Upright"		         },
	{0x11, 0x01, 0x04, 0x00, "Cocktail"		         },

	{0   , 0xfe, 0   ,    4, "Difficulty"		      },
	{0x11, 0x01, 0x03, 0x00, "Easy"		            },
	{0x11, 0x01, 0x03, 0x02, "Medium"		         },
	{0x11, 0x01, 0x03, 0x01, "Hard"		            },
	{0x11, 0x01, 0x03, 0x03, "Hardest"		         },
};

STDDIPINFO(Digdug)

// Dig Dug (rev 2)

static struct BurnRomInfo digdugRomDesc[] = {
	{ "dd1a.1",	      0x1000, 0xa80ec984, BRF_ESS | BRF_PRG  }, //  0 Z80 #1 Program Code
	{ "dd1a.2",	      0x1000, 0x559f00bd, BRF_ESS | BRF_PRG  }, //  1
	{ "dd1a.3",	      0x1000, 0x8cbc6fe1, BRF_ESS | BRF_PRG  }, //  2
	{ "dd1a.4",	      0x1000, 0xd066f830, BRF_ESS | BRF_PRG  }, //  3

	{ "dd1a.5",	      0x1000, 0x6687933b, BRF_ESS | BRF_PRG  }, //  4	Z80 #2 Program Code
	{ "dd1a.6",	      0x1000, 0x843d857f, BRF_ESS | BRF_PRG  }, //  5

	{ "dd1.7",	      0x1000, 0xa41bce72, BRF_ESS | BRF_PRG  }, //  6	Z80 #3 Program Code

	{ "dd1.9",	      0x0800, 0xf14a6fe1, BRF_GRA            }, //  7	Characters

	{ "dd1.15",	      0x1000, 0xe22957c8, BRF_GRA            }, //  8	Sprites
	{ "dd1.14",	      0x1000, 0x2829ec99, BRF_GRA            }, //  9
	{ "dd1.13",	      0x1000, 0x458499e9, BRF_GRA            }, // 10
	{ "dd1.12",	      0x1000, 0xc58252a0, BRF_GRA            }, // 11

	{ "dd1.11",	      0x1000, 0x7b383983, BRF_GRA            }, // 12	Characters 8x8 2bpp

	{ "dd1.10b",      0x1000, 0x2cf399c2, BRF_GRA            }, // 13 Playfield Data

	{ "136007.113",   0x0020, 0x4cb9da99, BRF_GRA            }, // 14 Palette Prom
	{ "136007.111",   0x0100, 0x00c7c419, BRF_GRA            }, // 15 Sprite Color Prom
	{ "136007.112",   0x0100, 0xe9b3e08e, BRF_GRA            }, // 16 Character Color Prom

	{ "136007.110",   0x0100, 0x7a2815b4, BRF_GRA            }, // 17 Namco Sound Proms
	{ "136007.109",   0x0100, 0x77245b66, BRF_GRA            }, // 18
};

STD_ROM_PICK(digdug)
STD_ROM_FN(digdug)

static INT32 DigdugInit(void);
static INT32 DigDugMemIndex(void);
static void DigDugMachineInit(void);
static UINT8 DigDugZ80ReadDip(UINT16 Offset);
static UINT8 DigDugZ80ReadInputs(UINT16 Offset);
static void DigDug_pf_latch_w(UINT16 offset, UINT8 data);
static void DigDugZ80Writeb840(UINT16 offset, UINT8 dta);
static void DigDugZ80WriteIoChip(UINT16 offset, UINT8 dta);
static INT32 DigDugDraw(void);
static void DigDugCalcPalette(void);
static UINT32 DigDugGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3);
static void DigDugRenderSprites(void);

static struct CPU_Rd_Table DigDugZ80ReadList[] =
{
	{ 0x6800, 0x6807, DigDugZ80ReadDip        }, 
   { 0x7000, 0x700f, DigDugZ80ReadInputs     }, 
	{ 0x7100, 0x7100, NamcoZ80ReadIoCmd       },
   // EAROM Read
	{ 0xb800, 0xb83f, earom_read              },
	{ 0x0000, 0x0000, NULL                    },
};

static struct CPU_Wr_Table DigDugZ80WriteList[] =
{
   // EAROM Write
	{ 0xb800, 0xb83f, earom_write             },
	{ 0x6800, 0x681f, NamcoZ80WriteSound      },
   { 0xb840, 0xb840, DigDugZ80Writeb840      },
   { 0x6820, 0x6820, NamcoZ80WriteCPU1Irq    },
	{ 0x6821, 0x6821, NamcoZ80WriteCPU2Irq    },
	{ 0x6822, 0x6822, NamcoZ80WriteCPU3Irq    },
	{ 0x6823, 0x6823, NamcoZ80WriteCPUReset   },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
	{ 0x7000, 0x700f, NamcoZ80WriteIoChip     },
   { 0x7008, 0x7008, DigDugZ80WriteIoChip    },
	{ 0x7100, 0x7100, NamcoZ80WriteIoCmd      },
	{ 0xa000, 0xa006, DigDug_pf_latch_w       },
	{ 0xa007, 0xa007, NamcoZ80WriteFlipScreen },
   { 0x0000, 0x0000, NULL                    },

};

#define DIGDUG_NUM_OF_CHAR_PALETTE_BITS   1
#define DIGDUG_NUM_OF_SPRITE_PALETTE_BITS 2
#define DIGDUG_NUM_OF_BGTILE_PALETTE_BITS 2

#define DIGDUG_PALETTE_OFFSET_BGTILES     0x0
#define DIGDUG_PALETTE_OFFSET_SPRITE      0x100
#define DIGDUG_PALETTE_OFFSET_CHARS       0x200
#define DIGDUG_PALETTE_SIZE_BGTILES       0x100
#define DIGDUG_PALETTE_SIZE_SPRITES       0x100
#define DIGDUG_PALETTE_SIZE_CHARS         0x20
#define DIGDUG_PALETTE_SIZE (DIGDUG_PALETTE_SIZE_CHARS + \
                             DIGDUG_PALETTE_SIZE_SPRITES + \
                             DIGDUG_PALETTE_SIZE_BGTILES)
                             
#define DIGDUG_NUM_OF_CHAR                0x80
#define DIGDUG_SIZE_OF_CHAR_IN_BYTES      0x40

#define DIGDUG_NUM_OF_SPRITE              0x100
#define DIGDUG_SIZE_OF_SPRITE_IN_BYTES    0x200

#define DIGDUG_NUM_OF_BGTILE              0x100
#define DIGDUG_SIZE_OF_BGTILE_IN_BYTES    0x80

static INT32 DigdugCharsPlaneOffsets[DIGDUG_NUM_OF_CHAR_PALETTE_BITS] = { 0 };
static INT32 DigdugCharsXOffsets[8] = { STEP8(7,-1) };
static INT32 DigdugCharsYOffsets[8] = { STEP8(0,8) };

static INT32 DigdugInit()
{
	// Allocate and Blank all required memory
	memory.All.Start = NULL;
	DigDugMemIndex();
	
   memory.All.Start = (UINT8 *)BurnMalloc(memory.All.Size);
	if (NULL == memory.All.Start) 
      return 1;
	memset(memory.All.Start, 0, memory.All.Size);
	
   DigDugMemIndex();

	DrvTempRom = (UINT8 *)BurnMalloc(0x4000);

	// Load Z80 #1 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x00000,    0, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x01000,    1, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x02000,    2, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x03000,    3, 1)) return 1;
	
	// Load Z80 #2 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x00000,    4, 1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x01000,    5, 1)) return 1;
	
	// Load Z80 #3 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom3 + 0x00000,    6, 1)) return 1;

	memset(DrvTempRom, 0, 0x4000);
	// Load and decode the chars 8x8 (in digdug)
	if (0 != BurnLoadRom(DrvTempRom,                   7, 1)) return 1;
	GfxDecode(
      DIGDUG_NUM_OF_CHAR, 
      DIGDUG_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      DigdugCharsPlaneOffsets, 
      DigdugCharsXOffsets, 
      DigdugCharsYOffsets, 
      DIGDUG_SIZE_OF_CHAR_IN_BYTES, 
      DrvTempRom, 
      graphics.fgChars
   );
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x4000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         8, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         9, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x02000,         10, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x03000,         11, 1)) return 1;
	GfxDecode(
      DIGDUG_NUM_OF_SPRITE, 
      DIGDUG_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      SpritePlaneOffsets, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      DIGDUG_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom, 
      graphics.Sprites
   );

	memset(DrvTempRom, 0, 0x4000);
	// Load and decode the chars 2bpp
	if (0 != BurnLoadRom(DrvTempRom,                   12, 1)) return 1;
	GfxDecode(
      DIGDUG_NUM_OF_BGTILE, 
      DIGDUG_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      CharPlaneOffsets, 
      CharXOffsets, 
      CharYOffsets, 
      DIGDUG_SIZE_OF_BGTILE_IN_BYTES, 
      DrvTempRom, 
      graphics.bgTiles
   );

	// Load gfx4 - the playfield data
	if (0 != BurnLoadRom(PlayFieldData,                13, 1)) return 1;

	// Load the PROMs
	if (0 != BurnLoadRom(memory.PROM.Palette,          14, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.SpriteLookup,     15, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.CharLookup,       16, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm,               17, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm + 0x0100,      18, 1)) return 1;
	
	BurnFree(DrvTempRom);

   machine.Game = NAMCO_DIGDUG;

	DigDugMachineInit();

	return 0;
}

static INT32 DigDugMemIndex()
{
	UINT8 *Next = memory.All.Start;

	memory.Z80.Rom1            = Next; Next += 0x04000;
	memory.Z80.Rom2            = Next; Next += 0x04000;
	memory.Z80.Rom3            = Next; Next += 0x04000;
	memory.PROM.Palette        = Next; Next += 0x00020;
	memory.PROM.CharLookup     = Next; Next += 0x00100;
	memory.PROM.SpriteLookup   = Next; Next += 0x00100;
	NamcoSoundProm             = Next; Next += 0x00200;
	
	memory.RAM.Start           = Next;

	memory.RAM.Video           = Next; Next += 0x00800;
	memory.RAM.Shared1         = Next; Next += 0x00400;
	memory.RAM.Shared2         = Next; Next += 0x00400;
	memory.RAM.Shared3         = Next; Next += 0x00400;

	memory.RAM.Size            = Next - memory.RAM.Start;

	PlayFieldData              = Next; Next += 0x01000;
	graphics.fgChars           = Next; Next += DIGDUG_NUM_OF_CHAR * 8 * 8;
	graphics.bgTiles           = Next; Next += DIGDUG_NUM_OF_BGTILE * 8 * 8;
	graphics.Sprites           = Next; Next += DIGDUG_NUM_OF_SPRITE * 16 * 16;
	graphics.Palette           = (UINT32*)Next; Next += DIGDUG_PALETTE_SIZE * sizeof(UINT32);

	memory.All.Size            = Next - memory.All.Start;

	return 0;
}

static tilemap_scan (digdug_bg )
{
	INT32 offs;

	row += 2;
	col -= 2;
	if (col & 0x20)
		offs = row + ((col & 0x1f) << 5);
	else
		offs = col + (row << 5);

	return offs;
}

static tilemap_callback ( digdug_bg )
{
	UINT8 *pf = PlayFieldData + (gameVars.playfield << 10);
   INT8 pfval = pf[offs & 0xfff];
   INT32 pfColour = (pfval >> 4) + (gameVars.playcolor << 4);
   
	TILE_SET_INFO(0, pfval, pfColour, 0);
}

static tilemap_scan (digdug_fg )
{
   INT32 row2 = row + 2;
   INT32 col2 = col - 2;

	if (col2 & 0x20)
   {
		return (row2 + ((col2 & 0x1f) << 5));
	}
   
   return (col2 + (row2 << 5));

}

static tilemap_callback ( digdug_fg )
{
   INT32 Code = memory.RAM.Video[offs];
   INT32 Colour = ((Code >> 4) & 0x0e) | ((Code >> 3) & 2);
   Code &= 0x7f;

	TILE_SET_INFO(1, Code, Colour, 0);
}

static void DigDugMachineInit()
{
	ZetInit(CPU1);
	ZetOpen(CPU1);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU2);
	ZetOpen(CPU2);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU3);
	ZetOpen(CPU3);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	NamcoSoundInit(18432000 / 6 / 32, 3, 0);
	NacmoSoundSetAllRoutes(0.90 * 10.0 / 16.0, BURN_SND_ROUTE_BOTH);
	BurnSampleInit(1);
	BurnSampleSetAllRoutesAllSamples(0.25, BURN_SND_ROUTE_BOTH);
	machine.bHasSamples = BurnSampleGetStatus(0) != -1;
   
   machine.rdAddrList = DigDugZ80ReadList;
   machine.wrAddrList = DigDugZ80WriteList;
   
	GenericTilesInit();
   GenericTilemapInit(
      0, 
      digdug_bg_map_scan, digdug_bg_map_callback, 
      8, 8, 
      NAMCO_TMAP_WIDTH, NAMCO_TMAP_HEIGHT
   );
	GenericTilemapSetGfx(
      0, 
      graphics.bgTiles, 
      DIGDUG_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      DIGDUG_NUM_OF_BGTILE * 8 * 8, 
      DIGDUG_PALETTE_OFFSET_BGTILES, 
      DIGDUG_PALETTE_SIZE_BGTILES - 1
   );
	GenericTilemapSetTransparent(0, 0);
	
   GenericTilemapInit(
      1, 
      digdug_fg_map_scan, digdug_fg_map_callback, 
      8, 8, 
      NAMCO_TMAP_WIDTH, NAMCO_TMAP_HEIGHT
   );
	GenericTilemapSetGfx(
      1, 
      graphics.fgChars, 
      DIGDUG_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      DIGDUG_NUM_OF_CHAR * 8 * 8, 
      DIGDUG_PALETTE_OFFSET_CHARS, 
      DIGDUG_PALETTE_SIZE_CHARS - 1
   );
	GenericTilemapSetTransparent(1, 0);
   
	GenericTilemapSetOffsets(TMAP_GLOBAL, 0, 0);

	earom_init();

	// Reset the driver
	DrvDoReset();
   
   gameVars.startEnable = 1;
   
   controls.player1Port = 1;
   controls.player2Port = 2;
}

static UINT8 DigDugZ80ReadDip(UINT16 offset)
{
   return NamcoZ80ReadDip(offset, DIGDUG_NUM_OF_DIPSWITCHES);
}

static UINT8 DigDugZ80ReadInputs(UINT16 Offset)
{
   switch (ioChip.CustomCommand) 
   {
      case 0xd2: 
      {
         if ( (0 == Offset) || (1 == Offset) )
            return input.Dip[Offset];
         break;
      }
      
      case 0x71:
      case 0xb1: 
      {
         if (0xb1 == ioChip.CustomCommand)
         {
            if (Offset <= 2) // status
               return 0;
            else
               return 0xff;
         }
         
         if (0 == Offset) 
         {
            if (ioChip.Mode) 
            {
               return input.Ports[0];
            } 
            else 
            {
               return updateCoinAndCredit(NAMCO_DIGDUG);
            }
         }
         
         if ( (1 == Offset) || (2 == Offset) ) 
         {
            INT32 jp = input.Ports[Offset];

            if (0 == ioChip.Mode)
            {
               /* check directions, according to the following 8-position rule */
               /*         0          */
               /*        7 1         */
               /*       6 8 2        */
               /*        5 3         */
               /*         4          
               if ((jp & 0x01) == 0)		// up 
                  jp = (jp & ~0x0f) | 0x00;
               else if ((jp & 0x02) == 0)	// right
                  jp = (jp & ~0x0f) | 0x02;
               else if ((jp & 0x04) == 0)	// down
                  jp = (jp & ~0x0f) | 0x04;
               else if ((jp & 0x08) == 0) // left 
                  jp = (jp & ~0x0f) | 0x06;
               else
                  jp = (jp & ~0x0f) | 0x08;*/
               jp = namcoControls[jp & 0x0f] | (jp & 0xf0);
            }

            return updateJoyAndButtons(Offset, jp);
         }
      }
   }
   
   return 0xff;
}

static void DigDug_pf_latch_w(UINT16 offset, UINT8 data)
{
	switch (offset)
	{
		case 0:
			gameVars.playfield = (gameVars.playfield & ~1) | (data & 1);
			break;

		case 1:
			gameVars.playfield = (gameVars.playfield & ~2) | ((data << 1) & 2);
			break;

		case 2:
			gameVars.alphacolor = data & 1;
			break;

		case 3:
			gameVars.playenable = data & 1;
			break;

		case 4:
			gameVars.playcolor = (gameVars.playcolor & ~1) | (data & 1);
			break;

		case 5:
			gameVars.playcolor = (gameVars.playcolor & ~2) | ((data << 1) & 2);
			break;
	}
}

static void DigDugZ80Writeb840(UINT16 offset, UINT8 dta)
{
   earom_ctrl_write(0xb840, dta);
}

static void DigDugZ80WriteIoChip(UINT16 offset, UINT8 dta)
{
   if (0xc1 == ioChip.CustomCommand) 
   {
      ioChip.LeftCoinPerCredit = ioChip.Buffer[2] & 0x0f;
      ioChip.LeftCreditPerCoin = ioChip.Buffer[3] & 0x0f;
   }
}

static INT32 DigDugDraw()
{
	BurnTransferClear();
	DigDugCalcPalette();

	GenericTilemapSetScrollX(0, 0);
	GenericTilemapSetScrollY(0, 0);
   GenericTilemapSetOffsets(0, 0, 0);
   GenericTilemapSetEnable(0, (0 == gameVars.playenable));
   GenericTilemapSetEnable(1, 1);
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_DRAWOPAQUE);
	GenericTilemapDraw(1, pTransDraw, 0 | TMAP_TRANSPARENT);

   DigDugRenderSprites();

	BurnTransferCopy(graphics.Palette);
	return 0;
}

#define DIGDUG_3BIT_PALETTE_SIZE    32

static void DigDugCalcPalette()
{
	UINT32 Palette[DIGDUG_3BIT_PALETTE_SIZE];
	
	for (INT32 i = 0; i < DIGDUG_3BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.Palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.Palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.Palette[i] >> 5) & 0x06];
      
		Palette[i] = BurnHighCol(r, g, b, 0);
	}

	/* bg_select */
	for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_BGTILES; i ++) 
   {
		graphics.Palette[DIGDUG_PALETTE_OFFSET_BGTILES + i] = 
         Palette[memory.PROM.CharLookup[i] & 0x0f];
	}

	/* sprites */
	for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_SPRITES; i ++) 
   {
		graphics.Palette[DIGDUG_PALETTE_OFFSET_SPRITE + i] = 
         Palette[(memory.PROM.SpriteLookup[i] & 0x0f) + 0x10];
	}

	/* characters - direct mapping */
	for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_CHARS; i += 2)
	{
		graphics.Palette[DIGDUG_PALETTE_OFFSET_CHARS + i + 0] = Palette[0];
		graphics.Palette[DIGDUG_PALETTE_OFFSET_CHARS + i + 1] = Palette[i/2];
	}

}

static UINT32 DigDugGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3)
{
   INT32 Sprite = SpriteRam1[Offset + 0];
   if (Sprite & 0x80) spriteParams->Sprite = (Sprite & 0xc0) | ((Sprite & ~0xc0) << 2);
   else               spriteParams->Sprite = Sprite;
   spriteParams->Colour = SpriteRam1[Offset + 1] & 0x3f;

   spriteParams->yStart = SpriteRam2[Offset + 1] - 40 + 1;
   if (8 > spriteParams->yStart) spriteParams->yStart += 0x100;
   spriteParams->xStart = NAMCO_SCREEN_WIDTH - SpriteRam2[Offset + 0] + 1;
   spriteParams->xStep = 16;
   spriteParams->yStep = 16;

   spriteParams->Flags = SpriteRam3[Offset + 0] & 0x03;
   spriteParams->Flags |= ((Sprite & 0x80) >> 4) | ((Sprite & 0x80) >> 5);

   spriteParams->PaletteBits = DIGDUG_NUM_OF_SPRITE_PALETTE_BITS;
   spriteParams->PaletteOffset = DIGDUG_PALETTE_OFFSET_SPRITE;
   
   return 1;
}

static void DigDugRenderSprites()
{
	UINT8 *SpriteRam1 = memory.RAM.Shared1 + 0x380;
	UINT8 *SpriteRam2 = memory.RAM.Shared2 + 0x380;
	UINT8 *SpriteRam3 = memory.RAM.Shared3 + 0x380;
	
   NamcoRenderSprites(SpriteRam1, SpriteRam2, SpriteRam3, DigDugGetSpriteParams);
   
}

/* === XEVIOUS === */

static struct BurnInputInfo XeviousInputList[] =
{
	{"Dip 1"             , BIT_DIPSWITCH,  &input.Dip[0],                 "dip"       },
	{"Dip 2"             , BIT_DIPSWITCH,  &input.Dip[1],                 "dip"       },

	{"Reset"             , BIT_DIGITAL,    &input.Reset,                  "reset"     },

	{"Up"                , BIT_DIGITAL,    &input.PortBits[0].Current[0], "p1 up"     },
	{"Right"             , BIT_DIGITAL,    &input.PortBits[0].Current[1], "p1 right"  },
	{"Down"              , BIT_DIGITAL,    &input.PortBits[0].Current[2], "p1 down"   },
	{"Left"              , BIT_DIGITAL,    &input.PortBits[0].Current[3], "p1 left"   },
	{"Fire 1"            , BIT_DIGITAL,    &input.PortBits[0].Current[4], "p1 fire 1" },
	{"Fire 2"            , BIT_DIGITAL,    &input.PortBits[0].Current[5], "p1 fire 2" },
	
	{"Up (Cocktail)"     , BIT_DIGITAL,    &input.PortBits[1].Current[0], "p2 up"     },
	{"Right (Cocktail)"  , BIT_DIGITAL,    &input.PortBits[1].Current[1], "p2 right"  },
	{"Down (Cocktail)"   , BIT_DIGITAL,    &input.PortBits[1].Current[2], "p2 down"   },
	{"Left (Cocktail)"   , BIT_DIGITAL,    &input.PortBits[1].Current[3], "p2 left"   },
	{"Fire 1 (Cocktail)" , BIT_DIGITAL,    &input.PortBits[1].Current[4], "p2 fire 1" },
	{"Fire 2 (Cocktail)" , BIT_DIGITAL,    &input.PortBits[1].Current[5], "p2 fire 2" },

	{"Coin 1"            , BIT_DIGITAL,    &input.PortBits[2].Current[4], "p1 coin"   },
	{"Start 1"           , BIT_DIGITAL,    &input.PortBits[2].Current[2], "p1 start"  },
	{"Coin 2"            , BIT_DIGITAL,    &input.PortBits[2].Current[5], "p2 coin"   },
	{"Start 2"           , BIT_DIGITAL,    &input.PortBits[2].Current[3], "p2 start"  },
	{"Service"           , BIT_DIGITAL,    &input.PortBits[2].Current[6], "service"   },

};

STDINPUTINFO(Xevious)

#define XEVIOUS_NUM_OF_DIPSWITCHES     2

static struct BurnDIPInfo XeviousDIPList[]=
{
	// Default Values
   // nInput, nFlags, nMask, nSettings, szInfo
	{0x00, 0xff, 0xff, 0xFF, NULL                     },
	{0x01, 0xff, 0xff, 0xFF, NULL                     },
	
	// Dip 1
	{0   , 0xfe, 0   , 2   , "Button 2"               },
	{0x00, 0x01, 0x01, 0x01, "Released"               },
	{0x00, 0x01, 0x01, 0x00, "Held"                   },

	{0   , 0xfe, 0   , 2   , "Flags Award Bonus Life" },
	{0x00, 0x01, 0x02, 0x02, "Yes"                    },
	{0x00, 0x01, 0x02, 0x00, "No"                     },

	{0   , 0xfe, 0   , 4   , "Coin B"                 },
	{0x00, 0x01, 0x0C, 0x04, "2 Coins 1 Play"         },
	{0x00, 0x01, 0x0C, 0x0C, "1 Coin  1 Play"         },
	{0x00, 0x01, 0x0C, 0x00, "2 Coins 3 Plays"        },
	{0x00, 0x01, 0x0C, 0x08, "1 Coin  2 Plays"        },

	{0   , 0xfe, 0   , 2   , "Button 2 (Cocktail)"    },
	{0x00, 0x01, 0x10, 0x10, "Released"               },
	{0x00, 0x01, 0x10, 0x00, "Held"                   },

	{0   , 0xfe, 0   , 4   , "Difficulty"             },
	{0x00, 0x01, 0x60, 0x40, "Easy"                   },
	{0x00, 0x01, 0x60, 0x60, "Normal"                 },
	{0x00, 0x01, 0x60, 0x20, "Hard"                   },
	{0x00, 0x01, 0x60, 0x00, "Hardest"                },
	
	{0   , 0xfe, 0   , 2   , "Freeze"                 },
	{0x00, 0x01, 0x80, 0x80, "Off"                    },
	{0x00, 0x01, 0x80, 0x00, "On"                     },
	
	// Dip 2	
	{0   , 0xfe, 0   , 4   , "Coin A"                 },
	{0x01, 0x01, 0x03, 0x01, "2 Coins 1 Play"         },
	{0x01, 0x01, 0x03, 0x03, "1 Coin  1 Play"         },
	{0x01, 0x01, 0x03, 0x00, "2 Coins 3 Plays"        },
	{0x01, 0x01, 0x03, 0x02, "1 Coin  2 Plays"        },
	
	{0   , 0xfe, 0   , 8   , "Bonus Life"             },
	{0x01, 0x01, 0x1C, 0x18, "10k  40k  40k"          },
	{0x01, 0x01, 0x1C, 0x14, "10k  50k  50k"          },
	{0x01, 0x01, 0x1C, 0x10, "20k  50k  50k"          },
	{0x01, 0x01, 0x1C, 0x1C, "20k  60k  60k"          },
	{0x01, 0x01, 0x1C, 0x0C, "20k  70k  70k"          },
	{0x01, 0x01, 0x1C, 0x08, "20k  80k  80k"          },
	{0x01, 0x01, 0x1C, 0x04, "20k  60k"               },
	{0x01, 0x01, 0x1C, 0x00, "None"                   },
	
	{0   , 0xfe, 0   , 4   , "Lives"                  },
	{0x01, 0x01, 0x60, 0x40, "1"                      },
	{0x01, 0x01, 0x60, 0x20, "2"                      },
	{0x01, 0x01, 0x60, 0x60, "3"                      },
	{0x01, 0x01, 0x60, 0x00, "5"                      },

	{0   , 0xfe, 0   , 2   , "Cabinet"                },
	{0x01, 0x01, 0x80, 0x80, "Upright"                },
	{0x01, 0x01, 0x80, 0x00, "Cocktail"               },
	
};

STDDIPINFO(Xevious)

static struct BurnRomInfo XeviousRomDesc[] = {
	{ "xvi_1.3p",      0x01000, 0x09964dda, BRF_ESS | BRF_PRG   }, //  0	Z80 #1 Program Code
	{ "xvi_2.3m",      0x01000, 0x60ecce84, BRF_ESS | BRF_PRG   }, //  1
	{ "xvi_3.2m",      0x01000, 0x79754b7d, BRF_ESS | BRF_PRG   }, //  2
	{ "xvi_4.2l",      0x01000, 0xc7d4bbf0, BRF_ESS | BRF_PRG   }, //  3
	
	{ "xvi_5.3f",      0x01000, 0xc85b703f, BRF_ESS | BRF_PRG   }, //  4	Z80 #2 Program Code
	{ "xvi_6.3j",      0x01000, 0xe18cdaad, BRF_ESS | BRF_PRG   }, //  5	
	
	{ "xvi_7.2c",      0x01000, 0xdd35cf1c, BRF_ESS | BRF_PRG   }, //  6	Z80 #3 Program Code
	
	{ "xvi_12.3b",     0x01000, 0x088c8b26, BRF_GRA             }, /*  7 background characters */
	{ "xvi_13.3c",     0x01000, 0xde60ba25, BRF_GRA             },	/*  8 bg pattern B0 */
	{ "xvi_14.3d",     0x01000, 0x535cdbbc, BRF_GRA             },	/*  9 bg pattern B1 */
	
	{ "xvi_15.4m",     0x02000, 0xdc2c0ecb, BRF_GRA             }, /* 10 sprite set #1, planes 0/1 */
	{ "xvi_18.4r",     0x02000, 0x02417d19, BRF_GRA             }, /* 11 sprite set #1, plane 2, set #2, plane 0 */
	{ "xvi_17.4p",     0x02000, 0xdfb587ce, BRF_GRA             }, /* 12 sprite set #2, planes 1/2 */
	{ "xvi_16.4n",     0x01000, 0x605ca889, BRF_GRA             },	/* 13 sprite set #3, planes 0/1 */

	{ "xvi_9.2a",      0x01000, 0x57ed9879, BRF_GRA             }, /* 14 */
	{ "xvi_10.2b",     0x02000, 0xae3ba9e5, BRF_GRA             }, /* 15 */
	{ "xvi_11.2c",     0x01000, 0x31e244dd, BRF_GRA             }, /* 16 */

	{ "xvi_8bpr.6a",   0x00100, 0x5cc2727f, BRF_GRA             }, /* 17 palette red component */
	{ "xvi_9bpr.6d",   0x00100, 0x5c8796cc, BRF_GRA             }, /* 18 palette green component */
	{ "xvi10bpr.6e",   0x00100, 0x3cb60975, BRF_GRA             }, /* 19 palette blue component */
	{ "xvi_7bpr.4h",   0x00200, 0x22d98032, BRF_GRA             }, /* 20 bg tiles lookup table low bits */
	{ "xvi_6bpr.4f",   0x00200, 0x3a7599f0, BRF_GRA             }, /* 21 bg tiles lookup table high bits */
	{ "xvi_4bpr.3l",   0x00200, 0xfd8b9d91, BRF_GRA             }, /* 22 sprite lookup table low bits */
	{ "xvi_5bpr.3m",   0x00200, 0xbf906d82, BRF_GRA             }, /* 23 sprite lookup table high bits */

	{ "xvi_2bpr.7n",   0x00100, 0x550f06bc, BRF_GRA             }, /* 24 */
	{ "xvi_1bpr.5n",   0x00100, 0x77245b66, BRF_GRA             }, /* 25 timing - not used */
};

STD_ROM_PICK(Xevious)
STD_ROM_FN(Xevious)


static struct BurnSampleInfo XeviousSampleDesc[] = {
#if !defined (ROM_VERIFY)
	{ "explo1.wav", SAMPLE_NOLOOP },	// ground target explosion 
	{ "explo2.wav", SAMPLE_NOLOOP },	// Solvalou explosion 
	{ "explo3.wav", SAMPLE_NOLOOP },	// credit 
	{ "explo4.wav", SAMPLE_NOLOOP },	// Garu Zakato explosion 
#endif
   { "",           0 }
};

STD_SAMPLE_PICK(Xevious)
STD_SAMPLE_FN(Xevious)


static INT32 XeviousInit(void);
static INT32 XeviousMemIndex(void);
static void XeviousMachineInit(void);
static UINT8 XeviousPlayFieldRead(UINT16 Offset);
static UINT8 XeviousWorkRAMRead(UINT16 Offset);
static UINT8 XeviousSharedRAM1Read(UINT16 Offset);
static UINT8 XeviousSharedRAM2Read(UINT16 Offset);
static UINT8 XeviousSharedRAM3Read(UINT16 Offset);
static UINT8 XeviousZ80ReadDip(UINT16 Offset);
static UINT8 XeviousZ80ReadInputs(UINT16 Offset);
static void Xevious_bs_wr(UINT16 Offset, UINT8 dta);
static void XeviousZ80WriteIoChip(UINT16 Offset, UINT8 dta);
static void Xevious_vh_latch_w(UINT16 Offset, UINT8 dta);
static void XeviousBGColorRAMWrite(UINT16 Offset, UINT8 Dta);
static void XeviousBGCharRAMWrite(UINT16 Offset, UINT8 Dta);
static void XeviousFGColorRAMWrite(UINT16 Offset, UINT8 Dta);
static void XeviousFGCharRAMWrite(UINT16 Offset, UINT8 Dta);
static void XeviousWorkRAMWrite(UINT16 Offset, UINT8 Dta);
static void XeviousSharedRAM1Write(UINT16 Offset, UINT8 Dta);
static void XeviousSharedRAM2Write(UINT16 Offset, UINT8 Dta);
static void XeviousSharedRAM3Write(UINT16 Offset, UINT8 Dta);
static INT32 XeviousDraw(void);
static void XeviousCalcPalette(void);
static UINT32 XeviousGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3);
static void XeviousRenderSprites(void);

static struct CPU_Rd_Table XeviousZ80ReadList[] =
{
	{ 0x6800, 0x6807, XeviousZ80ReadDip          }, 
   { 0x7000, 0x700f, XeviousZ80ReadInputs       },
	{ 0x7100, 0x7100, NamcoZ80ReadIoCmd          },
   { 0x7800, 0x7fff, XeviousWorkRAMRead         },
   { 0x8000, 0x8fff, XeviousSharedRAM1Read      },
   { 0x9000, 0x9fff, XeviousSharedRAM2Read      },
   { 0xa000, 0xafff, XeviousSharedRAM3Read      },
   { 0xf000, 0xffff, XeviousPlayFieldRead       },
   { 0x0000, 0x0000, NULL                       },
};

static struct CPU_Wr_Table XeviousZ80WriteList[] = 
{
	{ 0x6800, 0x681f, NamcoZ80WriteSound         },
   { 0x6820, 0x6820, NamcoZ80WriteCPU1Irq       },
	{ 0x6821, 0x6821, NamcoZ80WriteCPU2Irq       },
	{ 0x6822, 0x6822, NamcoZ80WriteCPU3Irq       },
	{ 0x6823, 0x6823, NamcoZ80WriteCPUReset      },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
	{ 0x7000, 0x700f, XeviousZ80WriteIoChip      },
	{ 0x7100, 0x7100, NamcoZ80WriteIoCmd         },
   { 0x7800, 0x7fff, XeviousWorkRAMWrite        },
   { 0x8000, 0x8fff, XeviousSharedRAM1Write     },
   { 0x9000, 0x9fff, XeviousSharedRAM2Write     },
   { 0xa000, 0xafff, XeviousSharedRAM3Write     },
   { 0xb000, 0xb7ff, XeviousFGColorRAMWrite     },
   { 0xb800, 0xbfff, XeviousBGColorRAMWrite     },
   { 0xc000, 0xc7ff, XeviousFGCharRAMWrite      },
   { 0xc800, 0xcfff, XeviousBGCharRAMWrite      },
   { 0xd000, 0xd07f, Xevious_vh_latch_w         },
   { 0xf000, 0xffff, Xevious_bs_wr              },
   { 0x0000, 0x0000, NULL                       },
};

#define XEVIOUS_NO_OF_COLS                   64
#define XEVIOUS_NO_OF_ROWS                   32

#define XEVIOUS_NUM_OF_CHAR_PALETTE_BITS     1
#define XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS   3
#define XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS   2

#define XEVIOUS_PALETTE_OFFSET_BGTILES       0x0
#define XEVIOUS_PALETTE_SIZE_BGTILES         (0x80 * 4)
#define XEVIOUS_PALETTE_OFFSET_SPRITE        (XEVIOUS_PALETTE_OFFSET_BGTILES + \
                                              XEVIOUS_PALETTE_SIZE_BGTILES)
#define XEVIOUS_PALETTE_SIZE_SPRITES         (0x40 * 8)
#define XEVIOUS_PALETTE_OFFSET_CHARS         (XEVIOUS_PALETTE_OFFSET_SPRITE + \
                                              XEVIOUS_PALETTE_SIZE_SPRITES)
#define XEVIOUS_PALETTE_SIZE_CHARS           (0x40 * 2)
#define XEVIOUS_PALETTE_SIZE (XEVIOUS_PALETTE_SIZE_CHARS + \
                              XEVIOUS_PALETTE_SIZE_SPRITES + \
                              XEVIOUS_PALETTE_SIZE_BGTILES)
                             
#define XEVIOUS_NUM_OF_CHAR                  0x200
#define XEVIOUS_SIZE_OF_CHAR_IN_BYTES        (8 * 8)

#define XEVIOUS_NUM_OF_SPRITE1               0x080
#define XEVIOUS_NUM_OF_SPRITE2               0x080
#define XEVIOUS_NUM_OF_SPRITE3               0x040
#define XEVIOUS_NUM_OF_SPRITE                (XEVIOUS_NUM_OF_SPRITE1 + \
                                              XEVIOUS_NUM_OF_SPRITE2 + \
                                              XEVIOUS_NUM_OF_SPRITE3)
#define XEVIOUS_SIZE_OF_SPRITE_IN_BYTES      0x200

#define XEVIOUS_NUM_OF_BGTILE                0x200
#define XEVIOUS_SIZE_OF_BGTILE_IN_BYTES      (8 * 8)

static INT32 XeviousCharXOffsets[8] = 	{ 0, 1, 2, 3, 4, 5, 6, 7 };
static INT32 XeviousCharYOffsets[8] = 	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 };

static struct PlaneOffsets
{
   INT32 fgChars[XEVIOUS_NUM_OF_CHAR_PALETTE_BITS];
   INT32 bgChars[XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS];
   INT32 sprites1[XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS];
   INT32 sprites2[XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS];
   INT32 sprites3[XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS];
} xeviousOffsets = {
   { 0 },   /* foreground characters */ 

/* background tiles */
   /* 512 characters */
   /* 2 bits per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
   { 0, 512 * 8 * 8 },

/* sprite set #1 */
   /* 128 sprites */
   /* 3 bits per pixel */
   /* 16 x 16 sprites */
   /* every sprite takes 64 consecutive bytes */
   { 0x10004, 0x00000, 0x00004 }, // 0x0000 + { 128*64*8+4, 0, 4 }

/* sprite set #2 */
   { 0x00000, 0x10000, 0x10004 }, // 0x2000 + { 0, 128*64*8, 128*64*8+4 }

/* sprite set #3 */
   { 0x08000, 0x00000, 0x00004 }, // 0x6000 + { 64*64*8, 0, 4 }
   
};

struct Xevious_RAM
{
   UINT8 bs[2];

   UINT8 *workram;
   UINT8 *fg_videoram;
   UINT8 *fg_colorram;
   UINT8 *bg_videoram;
   UINT8 *bg_colorram;
};

struct Xevious_ROM
{
   UINT8 *rom2a;
   UINT8 *rom2b;
   UINT8 *rom2c;
};

static struct Xevious_RAM xeviousRAM;
static struct Xevious_ROM xeviousROM;

static INT32 XeviousInit()
{
	// Allocate and Blank all required memory
	memory.All.Start = NULL;
	XeviousMemIndex();
	
   memory.All.Start = (UINT8 *)BurnMalloc(memory.All.Size);
	if (NULL == memory.All.Start) 
      return 1;
	
   memset(memory.All.Start, 0, memory.All.Size);
	XeviousMemIndex();

	DrvTempRom = (UINT8 *)BurnMalloc(0x08000);
   if (NULL == DrvTempRom) 
      return 1;
   
	// Load Z80 #1 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x00000,  0,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x01000,  1,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x02000,  2,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x03000,  3,  1)) return 1;
	
	// Load Z80 #2 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x00000,  4,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x01000,  5,  1)) return 1;
	
	// Load Z80 #3 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom3 + 0x00000,  6,  1)) return 1;
	
	// Load and decode the chars
   /* foreground characters: */
   /* 512 characters */
   /* 1 bit per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	if (0 != BurnLoadRom(DrvTempRom,                      7,  1)) return 1;
	GfxDecode(
      XEVIOUS_NUM_OF_CHAR, 
      XEVIOUS_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      xeviousOffsets.fgChars, 
      XeviousCharXOffsets, 
      XeviousCharYOffsets, 
      XEVIOUS_SIZE_OF_CHAR_IN_BYTES, 
      DrvTempRom, 
      graphics.fgChars
   );

   /* background tiles */
   /* 512 characters */
   /* 2 bits per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom,                      8,  1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,            9,  1)) return 1;
	GfxDecode(
      XEVIOUS_NUM_OF_BGTILE, 
      XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      xeviousOffsets.bgChars, 
      XeviousCharXOffsets, 
      XeviousCharYOffsets, 
      XEVIOUS_SIZE_OF_BGTILE_IN_BYTES, 
      DrvTempRom, 
      graphics.bgTiles
   );

	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x08000);

	if (0 != BurnLoadRom(DrvTempRom + 0x00000,            10, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x02000,            11, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x04000,            12, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x06000,            13, 1)) return 1;

   /* sprite set #1 */
   /* 128 sprites */
   /* 3 bits per pixel */
   /* 16 x 16 sprites */
   /* every sprite takes 128 (64?) consecutive bytes */
	GfxDecode(
      XEVIOUS_NUM_OF_SPRITE1, 
      XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      xeviousOffsets.sprites1, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom + (0x0000), 
      graphics.Sprites
   );

   /* sprite set #2 */
   /* 128 sprites */
   /* 3 bits per pixel */
   /* 16 x 16 sprites */
   /* every sprite takes 128 (64?) consecutive bytes */
	GfxDecode(
      XEVIOUS_NUM_OF_SPRITE2, 
      XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      xeviousOffsets.sprites2, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom + (0x2000), 
      graphics.Sprites + (XEVIOUS_NUM_OF_SPRITE1 * (16 * 16))
   );

   /* sprite set #3 */
   /* 64 sprites */
   /* 3 bits per pixel (one is always 0) */
   /* 16 x 16 sprites */
   /* every sprite takes 64 consecutive bytes */
	GfxDecode(
      XEVIOUS_NUM_OF_SPRITE3, 
      XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      xeviousOffsets.sprites3, 
      SpriteXOffsets, 
      SpriteYOffsets, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      DrvTempRom + (0x6000), 
      graphics.Sprites + ((XEVIOUS_NUM_OF_SPRITE1 + XEVIOUS_NUM_OF_SPRITE2) * (16 * 16))
   );

   // Load PlayFieldData

/*	if (0 != BurnLoadRom(PlayFieldData + 0x00000,  14,  1)) return 1;
	if (0 != BurnLoadRom(PlayFieldData + 0x01000,  15,  1)) return 1;
	if (0 != BurnLoadRom(PlayFieldData + 0x03000,  16,  1)) return 1;
*/   
	if (0 != BurnLoadRom(xeviousROM.rom2a,  14,  1)) return 1;
	if (0 != BurnLoadRom(xeviousROM.rom2b,  15,  1)) return 1;
	if (0 != BurnLoadRom(xeviousROM.rom2c,  16,  1)) return 1;
   
	// Load the PROMs
	if (0 != BurnLoadRom(memory.PROM.Palette,    17, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.Palette + 0x100, 18, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.Palette + 0x200, 19, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.CharLookup, 20, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.CharLookup + 0x200, 21, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.SpriteLookup, 22, 1)) return 1;
	if (0 != BurnLoadRom(memory.PROM.SpriteLookup + 0x200, 23, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm,          24, 1)) return 1;
	if (0 != BurnLoadRom(NamcoSoundProm + 0x100,  25, 1)) return 1;
	
   if (NULL != DrvTempRom)
   {
      BurnFree(DrvTempRom);
   }
   
   machine.Game = NAMCO_XEVIOUS;
   
	XeviousMachineInit();

	return 0;
}

static INT32 XeviousMemIndex()
{
	UINT8 *Next = memory.All.Start;

	memory.Z80.Rom1 = Next;             Next += 0x04000;
	memory.Z80.Rom2 = Next;             Next += 0x04000;
	memory.Z80.Rom3 = Next;             Next += 0x04000;
	memory.PROM.Palette = Next;         Next += 0x00300;
	memory.PROM.CharLookup = Next;      Next += 0x00400;
	memory.PROM.SpriteLookup = Next;    Next += 0x00400;
	NamcoSoundProm = Next;              Next += 0x00200;
	
	memory.RAM.Start = Next;
   xeviousRAM.workram = Next;          Next += 0x00800;
	memory.RAM.Shared1 = Next;          Next += 0x01000;
	memory.RAM.Shared2 = Next;          Next += 0x01000;
	memory.RAM.Shared3 = Next;          Next += 0x01000;
	memory.RAM.Video = Next;            Next += 0x02000;

	memory.RAM.Size = Next - memory.RAM.Start;

	graphics.bgTiles = Next;            Next += XEVIOUS_NUM_OF_BGTILE * XEVIOUS_SIZE_OF_BGTILE_IN_BYTES;
	xeviousROM.rom2a = Next;            Next += 0x01000;
	xeviousROM.rom2b = Next;            Next += 0x02000;
	xeviousROM.rom2c = Next;            Next += 0x01000;
	PlayFieldData = xeviousROM.rom2a;
	graphics.fgChars = Next;            Next += XEVIOUS_NUM_OF_CHAR * XEVIOUS_SIZE_OF_CHAR_IN_BYTES;
	graphics.Sprites = Next;            Next += XEVIOUS_NUM_OF_SPRITE * XEVIOUS_SIZE_OF_SPRITE_IN_BYTES;
	graphics.Palette = (UINT32*)Next;   Next += XEVIOUS_PALETTE_SIZE * sizeof(UINT32);

	memory.All.Size = Next - memory.All.Start;

   return 0;
}

static tilemap_scan ( xevious_bg )
{
   return (row) * XEVIOUS_NO_OF_COLS + col;
}

static tilemap_callback ( xevious_bg )
{
   UINT8 code = xeviousRAM.bg_videoram[offs];
   UINT8 attr = xeviousRAM.bg_colorram[offs];
   
   TILE_SET_INFO(
      0, 
      (UINT16)(code + ((attr & 0x01) << 8)), 
      ((attr & 0x3c) >> 2) | ((code & 0x80) >> 3) | ((attr & 0x03) << 5), 
      ((attr & 0xc0) >> 6)
   );
}

static tilemap_scan ( xevious_fg )
{
   return (row) * XEVIOUS_NO_OF_COLS + (col);
}

static tilemap_callback ( xevious_fg )
{
   UINT8 code = xeviousRAM.fg_videoram[offs];
   UINT8 attr = xeviousRAM.fg_colorram[offs];
   TILE_SET_INFO(
      1, 
      code, 
      ((attr & 0x03) << 4) | ((attr & 0x3c) >> 2), 
      ((attr & 0xc0) >> 6)
   );
   
}

static void XeviousMachineInit()
{
	ZetInit(CPU1);
	ZetOpen(CPU1);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
   ZetMapMemory(memory.RAM.Shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU2);
	ZetOpen(CPU2);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	ZetInit(CPU3);
	ZetOpen(CPU3);
	ZetSetReadHandler(NamcoZ80ProgRead);
	ZetSetWriteHandler(NamcoZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	NamcoSoundInit(18432000 / 6 / 32, 3, 0);
	NacmoSoundSetAllRoutes(0.90 * 10.0 / 16.0, BURN_SND_ROUTE_BOTH);
	BurnSampleInit(0);
	BurnSampleSetAllRoutesAllSamples(0.25, BURN_SND_ROUTE_BOTH);
	machine.bHasSamples = BurnSampleGetStatus(0) != -1;

   machine.rdAddrList = XeviousZ80ReadList;
   machine.wrAddrList = XeviousZ80WriteList;
   
   xeviousRAM.fg_colorram = memory.RAM.Video;            // 0xb000 - 0xb7ff
   xeviousRAM.bg_colorram = memory.RAM.Video + 0x0800;   // 0xb800 - 0xbfff
   xeviousRAM.fg_videoram = memory.RAM.Video + 0x1000;   // 0xc000 - 0xc7ff
   xeviousRAM.bg_videoram = memory.RAM.Video + 0x1800;   // 0xc800 - 0xcfff
   
	GenericTilesInit();

   GenericTilemapInit(
      0, 
      xevious_bg_map_scan, xevious_bg_map_callback, 
      8, 8, 
      XEVIOUS_NO_OF_COLS, XEVIOUS_NO_OF_ROWS
   );
	GenericTilemapSetGfx(
      0, 
      graphics.bgTiles, 
      XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      XEVIOUS_NUM_OF_BGTILE * XEVIOUS_SIZE_OF_BGTILE_IN_BYTES, 
      XEVIOUS_PALETTE_OFFSET_BGTILES, 
      0x7f //XEVIOUS_PALETTE_SIZE_BGTILES - 1
   );

   GenericTilemapInit(
      1, 
      xevious_fg_map_scan, xevious_fg_map_callback, 
      8, 8, 
      XEVIOUS_NO_OF_COLS, XEVIOUS_NO_OF_ROWS
   );
	GenericTilemapSetGfx(
      1, 
      graphics.fgChars, 
      XEVIOUS_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      XEVIOUS_NUM_OF_CHAR * XEVIOUS_SIZE_OF_CHAR_IN_BYTES, 
      XEVIOUS_PALETTE_OFFSET_CHARS, 
      0x3f // XEVIOUS_PALETTE_SIZE_CHARS - 1
   );
	GenericTilemapSetTransparent(1, 0);
	
   GenericTilemapSetOffsets(TMAP_GLOBAL, 0, 0);

	// Reset the driver
	DrvDoReset();
   
   controls.player1Port = 0;
   controls.player2Port = 1;

}

static UINT8 XeviousPlayFieldRead(UINT16 Offset)
{
   UINT16 addr_2b = ( ((xeviousRAM.bs[1] & 0x7e) << 6) | 
                      ((xeviousRAM.bs[0] & 0xfe) >> 1) );
   
   UINT8 dat_2b = xeviousROM.rom2b[addr_2b];

   UINT16 addr_2a = addr_2b >> 1;
   
   UINT8 dat_2a = xeviousROM.rom2a[addr_2a]; 
   if (addr_2b & 1)
   {
      dat_2a >>= 4; 
   }
   else
   {
      dat_2a &=  0x0f;
   }
   
   UINT16 addr_2c = (UINT16)dat_2b << 2;
   if (dat_2a & 1)
   {
      addr_2c += 0x0400;
   }
   if ((xeviousRAM.bs[0] & 1) ^ ((dat_2a >> 2) & 1) )
   {
      addr_2c |= 1;
   }
   if ((xeviousRAM.bs[1] & 1) ^ ((dat_2a >> 1) & 1) )
   {
      addr_2c |= 2;
   }

   UINT8 dat_2c = 0;
   if (Offset & 1)
   {
      dat_2c = xeviousROM.rom2c[addr_2c + 0x0800];
   }
   else
   {
      dat_2c = xeviousROM.rom2c[addr_2c];
      dat_2c = (dat_2c & 0x3f) | ((dat_2c & 0x80) >> 1) | ((dat_2c & 0x40) << 1);
      dat_2c ^= ((dat_2a << 4) & 0x40);
      dat_2c ^= ((dat_2a << 6) & 0x80);
   }
   
   return dat_2c;
}

static UINT8 XeviousZ80ReadDip(UINT16 Offset)
{
   return NamcoZ80ReadDip(Offset, XEVIOUS_NUM_OF_DIPSWITCHES);
}

static UINT8 XeviousZ80ReadInputs(UINT16 Offset)
{
   switch (ioChip.CustomCommand & 0x0f) 
   {
      case 0x01: 
      {
         if (0 == Offset) 
         {
            if (ioChip.Mode) 
            {
               return input.Ports[2];        // service switch
            } 
            else 
            {
               return updateCoinAndCredit(NAMCO_XEVIOUS);
            }
         }
         
         if ( (1 == Offset) || (2 == Offset) ) 
         {
            INT32 jp = input.Ports[Offset - 1];

            if (0 == ioChip.Mode)
            {
               /* check directions, according to the following 8-position rule */
               /*         0          */
               /*        7 1         */
               /*       6 8 2        */
               /*        5 3         */
               /*         4          
               if ((jp & 0x01) == 0)		// up
                  jp = (jp & ~0x0f) | 0x00;
               else if ((jp & 0x02) == 0)	// right
                  jp = (jp & ~0x0f) | 0x02;
               else if ((jp & 0x04) == 0)	// down 
                  jp = (jp & ~0x0f) | 0x04;
               else if ((jp & 0x08) == 0) // left 
                  jp = (jp & ~0x0f) | 0x06;
               else
                  jp = (jp & ~0x0f) | 0x08; */
               jp = namcoControls[jp & 0x0f] | (jp & 0xf0);
               
               switch (jp)
               {
                  case 0:
                     BurnSamplePlay(0);
                     break;
                  case 2:
                     BurnSamplePlay(1);
                     break;
                  case 4:
                     BurnSamplePlay(2);
                     break;
                  case 6:
                     BurnSamplePlay(3);
                     break;
               }
            }

            return jp; //updateJoyAndButtons(Offset, jp);
         }
         break;
      }
      
      case 0x04:
      {
         if (3 == Offset)
         {
            if ((0x80 == ioChip.Buffer[0]) || (0x10 == ioChip.Buffer[0]))
               return 0x05;
            else
               return 0x95;
         }
         else
            return 0;
         break;
      }
      
      default:
      {
         break;
      }
   }
   
   return 0xff;
}
		
static UINT8 XeviousWorkRAMRead(UINT16 Offset)
{
   if (0x0800 > Offset) // 0x7800 - 0x7fff
      return xeviousRAM.workram[Offset];
   
   return 0;
}

static UINT8 XeviousSharedRAM1Read(UINT16 Offset)
{
   if (0x1000 > Offset) // 0x8000 - 0x87ff
      return memory.RAM.Shared1[Offset & 0x07ff];
   
   return 0;
}

static UINT8 XeviousSharedRAM2Read(UINT16 Offset)
{
   if (0x1000 > Offset) // 0x9000 - 0x97ff
      return memory.RAM.Shared2[Offset & 0x07ff];
   
   return 0;
}

static UINT8 XeviousSharedRAM3Read(UINT16 Offset)
{
   if (0x1000 > Offset) // 0xa000 - 0xa7ff
      return memory.RAM.Shared3[Offset & 0x07ff];
   
   return 0;
}

static void Xevious_bs_wr(UINT16 Offset, UINT8 dta)
{
   xeviousRAM.bs[Offset & 0x01] = dta;
}

static void XeviousZ80WriteIoChip(UINT16 Offset, UINT8 dta)
{
   ioChip.Buffer[Offset & 0x0f] = dta;
   
   switch (ioChip.CustomCommand & 0x0f)
   {
      case 0x01:
      {
         if (0 == Offset)
         {
            switch (dta & 0x0f)
            {
               case 0x00:
                  /* nop */
                  break;
               case 0x01:
                  ioChip.Credits = 0;
                  ioChip.Mode = 0;
                  gameVars.startEnable = 1;
                  break;
               case 0x02:
                  gameVars.startEnable = 1;
                  break;
               case 0x03:
                  ioChip.Mode = 1;
                  break;
               case 0x04:
                  ioChip.Mode = 0;
                  break;
               case 0x05:
                  gameVars.startEnable = 0;
                  ioChip.Mode = 1;
                  break;
            }
         }
         if (7 == Offset)
         {
            ioChip.AuxCoinPerCredit = ioChip.Buffer[1] & 0x0f;
            ioChip.AuxCreditPerCoin = ioChip.Buffer[2] & 0x0f;
            ioChip.LeftCoinPerCredit = ioChip.Buffer[3] & 0x0f;
            ioChip.LeftCreditPerCoin = ioChip.Buffer[4] & 0x0f;
            ioChip.RightCoinPerCredit = ioChip.Buffer[5] & 0x0f;
            ioChip.RightCreditPerCoin = ioChip.Buffer[6] & 0x0f;
         }
         break;
      }
      
      case 0x04:
         break;
         
      case 0x08:
      {
         if (6 == Offset)
         {
            // it is not known how the parameters control the explosion. 
				// We just use samples. 
				if (memcmp(ioChip.Buffer,"\x40\x40\x40\x01\xff\x00\x20",7) == 0)
				{
					BurnSamplePlay(0); //sample_start (0, 0, 0);
            }
				else if (memcmp(ioChip.Buffer,"\x30\x40\x00\x02\xdf\x00\x10",7) == 0)
				{
					BurnSamplePlay(1); //sample_start (0, 1, 0);
				}
				else if (memcmp(ioChip.Buffer,"\x30\x10\x00\x80\xff\x00\x10",7) == 0)
				{
					BurnSamplePlay(2); //sample_start (0, 2, 0);
				}
				else if (memcmp(ioChip.Buffer,"\x30\x80\x80\x01\xff\x00\x10",7) == 0)
				{
					BurnSamplePlay(3); //sample_start (0, 3, 0);
				}
         }
         break;
      }
   }
   
}

static void Xevious_vh_latch_w(UINT16 Offset, UINT8 dta)
{
   UINT16 dta16 = dta + ((Offset & 1) << 8);
   UINT16 reg = (Offset & 0xf0) >> 4;
   
   switch (reg)
   {
      case 0:
      {
         // set bg tilemap x
         GenericTilemapSetScrollX(0, dta16+20);
         break;
      }
      case 1:
      {
         // set fg tilemap x
         GenericTilemapSetScrollX(1, dta16+32);
         break;
      }
      case 2:
      {
         // set bg tilemap y
         GenericTilemapSetScrollY(0, dta16+16);
         break;
      }
      case 3:
      {
         // set fg tilemap y
         GenericTilemapSetScrollY(1, dta16+18);
         break;
      }
      case 7:
      {
         // flipscreen
         machine.FlipScreen = dta & 1;
         break;
      }
      default:
      {
         break;
      }
   }
   
}

static void XeviousBGColorRAMWrite(UINT16 Offset, UINT8 Dta)
{
   *(xeviousRAM.bg_colorram + (Offset & 0x7ff)) = Dta;
}

static void XeviousBGCharRAMWrite(UINT16 Offset, UINT8 Dta)
{
   *(xeviousRAM.bg_videoram + (Offset & 0x7ff)) = Dta;
}

static void XeviousFGColorRAMWrite(UINT16 Offset, UINT8 Dta)
{
   *(xeviousRAM.fg_colorram + (Offset & 0x7ff)) = Dta;
}

static void XeviousFGCharRAMWrite(UINT16 Offset, UINT8 Dta)
{
   *(xeviousRAM.fg_videoram + (Offset & 0x7ff)) = Dta;
}

static void XeviousWorkRAMWrite(UINT16 Offset, UINT8 Dta)
{
   if ((0x0000 <= Offset) && (0x0800 > Offset)) // 0x7800 - 0x7fff
      xeviousRAM.workram[Offset] = Dta;
}

static void XeviousSharedRAM1Write(UINT16 Offset, UINT8 Dta)
{
   if ((0x0000 <= Offset) && (0x1000 > Offset)) // 0x8000 - 0x87ff
      memory.RAM.Shared1[Offset & 0x07ff] = Dta;
}

static void XeviousSharedRAM2Write(UINT16 Offset, UINT8 Dta)
{
   if ((0x0000 <= Offset) && (0x1000 > Offset)) // 0x9000 - 0x97ff
      memory.RAM.Shared2[Offset & 0x07ff] = Dta;
}

static void XeviousSharedRAM3Write(UINT16 Offset, UINT8 Dta)
{
   if ((0x0000 <= Offset) && (0x1000 > Offset)) // 0xa000 - 0xa7ff
      memory.RAM.Shared3[Offset & 0x07ff] = Dta;
}

static INT32 XeviousDraw()
{
	BurnTransferClear();
	XeviousCalcPalette();
   
   GenericTilemapSetEnable(0, 1);
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_DRAWOPAQUE);

   XeviousRenderSprites();

   GenericTilemapSetEnable(1, 1);
   GenericTilemapDraw(1, pTransDraw, 0 | TMAP_TRANSPARENT);

	BurnTransferCopy(graphics.Palette);
   
	return 0;
}

#define XEVIOUS_BASE_PALETTE_SIZE   128

static void XeviousCalcPalette()
{
	UINT32 Palette[XEVIOUS_BASE_PALETTE_SIZE + 1];
   UINT32 code = 0;
	
	for (INT32 i = 0; i < XEVIOUS_BASE_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour4Bit[(memory.PROM.Palette[0x0000 + i]) & 0x0f];
      INT32 g = Colour4Bit[(memory.PROM.Palette[0x0100 + i]) & 0x0f];
      INT32 b = Colour4Bit[(memory.PROM.Palette[0x0200 + i]) & 0x0f];
      
		Palette[i] = BurnHighCol(r, g, b, 0);
	}
   
   Palette[XEVIOUS_BASE_PALETTE_SIZE] = BurnHighCol(0, 0, 0, 0); // Transparency Colour for Sprites

	/* bg_select */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_BGTILES; i ++) 
   {
      code = ( (memory.PROM.CharLookup[                               i] & 0x0f)       | 
              ((memory.PROM.CharLookup[XEVIOUS_PALETTE_SIZE_BGTILES + i] & 0x0f) << 4) );
		graphics.Palette[XEVIOUS_PALETTE_OFFSET_BGTILES + i] = Palette[code];
	}

	/* sprites */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_SPRITES; i ++) 
   {
      code = ( (memory.PROM.SpriteLookup[i                               ] & 0x0f)       |
              ((memory.PROM.SpriteLookup[XEVIOUS_PALETTE_SIZE_SPRITES + i] & 0x0f) << 4) );
      if (code & 0x80)
         graphics.Palette[XEVIOUS_PALETTE_OFFSET_SPRITE + i] = Palette[code & 0x7f];
      else
         graphics.Palette[XEVIOUS_PALETTE_OFFSET_SPRITE + i] = Palette[XEVIOUS_BASE_PALETTE_SIZE];
	}

	/* characters - direct mapping */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_CHARS; i += 2)
	{
		graphics.Palette[XEVIOUS_PALETTE_OFFSET_CHARS + i + 0] = Palette[XEVIOUS_BASE_PALETTE_SIZE];
		graphics.Palette[XEVIOUS_PALETTE_OFFSET_CHARS + i + 1] = Palette[i / 2];
	}

}

static UINT32 XeviousGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3)
{
   if (0 == (SpriteRam1[Offset + 1] & 0x40))
   {
      INT32 Sprite =      SpriteRam1[Offset + 0];
      
      if (SpriteRam3[Offset + 0] & 0x80)
      {
         Sprite &= 0x3f;
         Sprite += 0x100;
      }
      spriteParams->Sprite = Sprite;
      spriteParams->Colour = SpriteRam1[Offset + 1] & 0x7f;

      spriteParams->yStart = ((SpriteRam2[Offset + 1] - 40) + (SpriteRam3[Offset + 1] & 1 ) * 0x100);
      spriteParams->xStart = NAMCO_SCREEN_WIDTH - (SpriteRam2[Offset + 0] - 1);
      spriteParams->xStep = 16;
      spriteParams->yStep = 16;
      
      spriteParams->Flags = SpriteRam3[Offset + 0] & 0x0f;
      
      spriteParams->PaletteBits = XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS;
      spriteParams->PaletteOffset = XEVIOUS_PALETTE_OFFSET_SPRITE;

      return 1;
   }
   
   return 0;
}

static void XeviousRenderSprites(void)
{
	UINT8 *SpriteRam2 = memory.RAM.Shared1 + 0x780;
	UINT8 *SpriteRam3 = memory.RAM.Shared2 + 0x780;
	UINT8 *SpriteRam1 = memory.RAM.Shared3 + 0x780;
  
   NamcoRenderSprites(SpriteRam1, SpriteRam2, SpriteRam3, XeviousGetSpriteParams);
}


/* === Common === */

static void machineReset()
{
	cpus.CPU[CPU1].FireIRQ = 0;
	cpus.CPU[CPU2].FireIRQ = 0;
	cpus.CPU[CPU3].FireIRQ = 0;
	cpus.CPU[CPU2].Halt = 0;
	cpus.CPU[CPU3].Halt = 0;
   
	machine.FlipScreen = 0;
	
   for (INT32 i = 0; i < STARS_CTRL_NUM; i++) {
		stars.Control[i] = 0;
	}
	stars.ScrollX = 0;
	stars.ScrollY = 0;
	
	ioChip.CustomCommand = 0;
	ioChip.CPU1FireNMI = 0;
	ioChip.Mode = 0;
	ioChip.Credits = 0;
	ioChip.LeftCoinPerCredit = 0;
	ioChip.LeftCreditPerCoin = 0;
	ioChip.RightCoinPerCredit = 0;
	ioChip.RightCreditPerCoin = 0;
	ioChip.AuxCoinPerCredit = 0;
	ioChip.AuxCreditPerCoin = 0;
	for (INT32 i = 0; i < IOCHIP_BUF_SIZE; i ++) 
   {
		ioChip.Buffer[i] = 0;
	}
   
}

static INT32 DrvDoReset()
{
	for (INT32 i = 0; i < NAMCO_BRD_CPU_COUNT; i ++) 
   {
		ZetOpen(i);
		ZetReset();
		ZetClose();
	}
	
   BurnSampleReset();
   NamcoSoundReset();
   
   machineReset();
   
	input.PrevInValue = 0xff;

   memset(&namco54xx, 0, sizeof(namco54xx));
   
	gameVars.playfield = 0;
	gameVars.alphacolor = 0;
	gameVars.playenable = 0;
	gameVars.playcolor = 0;
   gameVars.startEnable = 0;
   gameVars.coinInserted = 0;

	earom_reset();

	HiscoreReset();

	return 0;
}

static void Namco54XXWrite(INT32 Data)
{
	if (namco54xx.Fetch) 
   {
		switch (namco54xx.FetchMode) 
      {
			case NAMCO54_WR_CFG3:
				namco54xx.Config3[NAMCO54XX_CFG3_SIZE - (namco54xx.Fetch --)] = Data;
				break;
            
			case NAMCO54_WR_CFG2:
				namco54xx.Config2[NAMCO54XX_CFG2_SIZE - (namco54xx.Fetch --)] = Data;
				break;

			case NAMCO54_WR_CFG1:
				namco54xx.Config1[NAMCO54XX_CFG1_SIZE - (namco54xx.Fetch --)] = Data;
				break;
            
         default:
            if (NAMCO54XX_CFG1_SIZE <= namco54xx.Fetch)
            {
               namco54xx.Fetch = 1;
            }
            namco54xx.FetchMode = NAMCO54_WR_CFG1;
				namco54xx.Config1[NAMCO54XX_CFG1_SIZE - (namco54xx.Fetch --)] = Data;
            break;
		}
	} 
   else 
   {
		switch (Data & 0xf0) 
      {
			case NAMCO54_CMD_NOP:
				break;

			case NAMCO54_CMD_SND4_7:	// output sound on pins 4-7 only
				if (0 == memcmp(namco54xx.Config1,"\x40\x00\x02\xdf",NAMCO54XX_CFG1_SIZE))
					// bosco
					// galaga
					// xevious
               BurnSamplePlay(0);
//				else if (memcmp(namco54xx.Config1,"\x10\x00\x80\xff",4) == 0)
					// xevious
//					sample_start(0, 1, 0);
//				else if (memcmp(namco54xx.Config1,"\x80\x80\x01\xff",4) == 0)
					// xevious
//					sample_start(0, 2, 0);
				break;

			case NAMCO54_CMD_SND8_11:	// output sound on pins 8-11 only
//				if (memcmp(namco54xx.Config2,"\x40\x40\x01\xff",4) == 0)
					// xevious
//					sample_start(1, 3, 0);
//					BurnSamplePlay(1);
				/*else*/ if (0 == memcmp(namco54xx.Config2,"\x30\x30\x03\xdf",NAMCO54XX_CFG2_SIZE))
					// bosco
					// galaga
               BurnSamplePlay(1);
//				else if (memcmp(namco54xx.Config2,"\x60\x30\x03\x66",4) == 0)
					// polepos
//					sample_start( 0, 0, 0 );
				break;

			case NAMCO54_CMD_CFG1_WR:
				namco54xx.Fetch = NAMCO54XX_CFG1_SIZE;
				namco54xx.FetchMode = NAMCO54_WR_CFG1;
				break;

			case NAMCO54_CMD_CFG2_WR:
				namco54xx.Fetch = NAMCO54XX_CFG2_SIZE;
				namco54xx.FetchMode = NAMCO54_WR_CFG2;
				break;

			case NAMCO54_CMD_SND17_20:	// output sound on pins 17-20 only
//				if (memcmp(namco54xx.Config3,"\x08\x04\x21\x00\xf1",5) == 0)
					// bosco
//					sample_start(2, 2, 0);
				break;

			case NAMCO54_CMD_CFG3_WR:
				namco54xx.Fetch = NAMCO54XX_CFG3_SIZE;
				namco54xx.FetchMode = NAMCO54_WR_CFG3;
				break;

			case NAMCO54_CMD_FRQ_OUT:
				// polepos
				/* 0x7n = Screech sound. n = pitch (if 0 then no sound) */
				/* followed by 0x60 command? */
				if (0 == ( Data & 0x0f )) 
            {
//					if (sample_playing(1))
//						sample_stop(1);
				} 
            else 
            {
//					INT32 freq = (INT32)( ( 44100.0f / 10.0f ) * (float)(Data & 0x0f) );

//					if (!sample_playing(1))
//						sample_start(1, 1, 1);
//					sample_set_freq(1, freq);
				}
				break;
            
         default:
            break;
		}
	}
}

static UINT8 NamcoZ80ReadDip(UINT16 Offset, UINT32 DipCount)
{
   UINT8 retVal = 0;
   
   for (UINT32 count = 0; count < DipCount; count ++)
   {
      retVal <<= 1;
      retVal |=  ((input.Dip[count] >> Offset) & 0x01);
   }

   return retVal;
}

static UINT8 NamcoZ80ReadIoCmd(UINT16 Offset)
{
   return ioChip.CustomCommand;
}
      
static UINT8 __fastcall NamcoZ80ProgRead(UINT16 addr)
{
   struct CPU_Rd_Table *rdEntry = machine.rdAddrList;
   UINT8 dta = 0;
   
   if (NULL != rdEntry)
   {
      while (NULL != rdEntry->readFunc)
      {
         if ( (addr >= rdEntry->startAddr) && 
              (addr <= rdEntry->endAddr)      )
         {
            dta = rdEntry->readFunc(addr - rdEntry->startAddr);
         }
         rdEntry ++;
      }
   }
   
	return dta;
}

static UINT8 updateCoinAndCredit(UINT8 gameID)
{
   const struct CoinAndCredit_Def *signals = &coinAndCreditParams[gameID];
   
   UINT8 In = input.Ports[signals->portNumber];
   if (In != input.PrevInValue) 
   {
      if (0 < ioChip.LeftCoinPerCredit) 
      {
         if ( (signals->coinTrigger != (In & signals->coinMask) ) && 
              (99 > ioChip.Credits) ) 
         {
            gameVars.coinInserted ++;
            if (gameVars.coinInserted >= ioChip.LeftCoinPerCredit) 
            {
               ioChip.Credits += ioChip.LeftCreditPerCoin;
               gameVars.coinInserted = 0;
            }
         }
      } 
      else 
      {
         ioChip.Credits = 2;
      }
      
      if (gameVars.startEnable)
      {
         if ( signals->start1Trigger != (In & signals->start1Mask) ) 
         {
            if (ioChip.Credits >= 1) ioChip.Credits --;
         }
         
         if ( signals->start2Trigger != (In & signals->start2Mask) ) 
         {
            if (ioChip.Credits >= 2) ioChip.Credits -= 2;
         }
      }
   }
   
   input.PrevInValue = In;
   
   return (ioChip.Credits / 10) * 16 + ioChip.Credits % 10;
}

static UINT8 updateJoyAndButtons(UINT16 Offset, UINT8 jp)
{
   UINT8 joy = jp & 0x0f;
   UINT8 in, toggle;

   in = ~((jp & 0xf0) >> 4);

   toggle = in ^ button.Last;
   button.Last = (button.Last & 2) | (in & 1);

   /* fire */
   joy |= ((toggle & in & 0x01)^1) << 4;
   joy |= ((in & 0x01)^1) << 5;

   return joy;
}

static void NamcoZ80WriteSound(UINT16 Offset, UINT8 dta)
{
   NamcoSoundWrite(Offset, dta);
}

static void NamcoZ80WriteCPU1Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU1].FireIRQ = dta & 0x01;
   if (!cpus.CPU[CPU1].FireIRQ) 
   {
      INT32 nActive = ZetGetActive();
      ZetClose();
      ZetOpen(CPU1);
      ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
      ZetClose();
      ZetOpen(nActive);
   }

}

static void NamcoZ80WriteCPU2Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU2].FireIRQ = dta & 0x01;
   if (!cpus.CPU[CPU2].FireIRQ) 
   {
      INT32 nActive = ZetGetActive();
      ZetClose();
      ZetOpen(CPU2);
      ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
      ZetClose();
      ZetOpen(nActive);
   }

}

static void NamcoZ80WriteCPU3Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU3].FireIRQ = !(dta & 0x01);

}
		
static void NamcoZ80WriteCPUReset(UINT16 Offset, UINT8 dta)
{
   if (!(dta & 0x01)) 
   {
      INT32 nActive = ZetGetActive();
      ZetClose();
      ZetOpen(CPU2);
      ZetReset();
      ZetClose();
      ZetOpen(CPU3);
      ZetReset();
      ZetClose();
      ZetOpen(nActive);
      cpus.CPU[CPU2].Halt = 1;
      cpus.CPU[CPU3].Halt = 1;
      return;
   } 
   else 
   {
      cpus.CPU[CPU2].Halt = 0;
      cpus.CPU[CPU3].Halt = 0;
   }
}
		
static void NamcoZ80WriteIoChip(UINT16 Offset, UINT8 dta)
{
   ioChip.Buffer[Offset] = dta;
   Namco54XXWrite(dta);
   
}
	
static void NamcoZ80WriteIoCmd(UINT16 Offset, UINT8 dta)
{
   ioChip.CustomCommand = dta;
   ioChip.CPU1FireNMI = 1;
   
   switch (ioChip.CustomCommand) 
   {
      case 0x10: 
      {
         ioChip.CPU1FireNMI = 0;
         return;
      }
      
      case 0xa1: 
      {
         ioChip.Mode = 1;
         return;
      }
      case 0xb1: 
      {
         ioChip.Credits = 0;
         return;
      }
      case 0xc1:
      case 0xe1: 
      {
         ioChip.Credits = 0;
         ioChip.Mode = 0;
         return;
      }
   }
   
}

static void NamcoZ80WriteFlipScreen(UINT16 Offset, UINT8 dta)
{
   machine.FlipScreen = dta & 0x01;
}

static void __fastcall NamcoZ80ProgWrite(UINT16 addr, UINT8 dta)
{
   struct CPU_Wr_Table *wrEntry = machine.wrAddrList;
   
   if (NULL != wrEntry)
   {
      while (NULL != wrEntry->writeFunc)
      {
         if ( (addr >= wrEntry->startAddr) &&
              (addr <= wrEntry->endAddr)      )
         {
            wrEntry->writeFunc(addr - wrEntry->startAddr, dta);
         }
         
         wrEntry ++;
      }
   }
}

static const INT32 GfxOffset[2][2] = {
   { 0, 1 },
   { 2, 3 }
};

static void NamcoRenderSprites(UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3, UINT32 GetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 Offset, UINT8 *SpriteRam1, UINT8 *SpriteRam2, UINT8 *SpriteRam3))
{
   struct Namco_Sprite_Params spriteParams;
   
	for (INT32 Offset = 0; Offset < 0x80; Offset += 2) 
   {
      if (GetSpriteParams(&spriteParams, Offset, SpriteRam1, SpriteRam2, SpriteRam3))
      {
         INT32 spriteRows = ((spriteParams.Flags & ySize) != 0);
         INT32 spriteCols = ((spriteParams.Flags & xSize) != 0);
         
         for (INT32 y = 0; y <= spriteRows; y ++) 
         {
            for (INT32 x = 0; x <= spriteCols; x ++) 
            {
               INT32 Code = spriteParams.Sprite;
               if (spriteRows | spriteCols)
                  //Code += GfxOffset[y ^ (spriteRows * ((spriteParams.Flags & yFlip) != 0))][x ^ (spriteCols * ((spriteParams.Flags & xFlip) != 0))];
                  Code += ((y * 2 + x) ^ (spriteParams.Flags & Orient));
               INT32 xPos = spriteParams.xStart + spriteParams.xStep * x;
               INT32 yPos = spriteParams.yStart + spriteParams.yStep * y;

               if ((xPos < -15) || (xPos >= nScreenWidth) ) continue;
               if ((yPos < -15) || (yPos >= nScreenHeight)) continue;

               switch (spriteParams.Flags & Orient)
               {
                  case 3:
                     Render16x16Tile_Mask_FlipXY_Clip(
                        pTransDraw, 
                        Code, 
                        xPos, yPos, 
                        spriteParams.Colour, 
                        spriteParams.PaletteBits, 
                        0, 
                        spriteParams.PaletteOffset, 
                        graphics.Sprites
                     );
                     break;
                  case 2:
                     Render16x16Tile_Mask_FlipY_Clip(
                        pTransDraw, 
                        Code, 
                        xPos, yPos, 
                        spriteParams.Colour, 
                        spriteParams.PaletteBits, 
                        0, 
                        spriteParams.PaletteOffset, 
                        graphics.Sprites
                     );
                     break;
                  case 1:
                     Render16x16Tile_Mask_FlipX_Clip(
                        pTransDraw, 
                        Code, 
                        xPos, yPos, 
                        spriteParams.Colour, 
                        spriteParams.PaletteBits, 
                        0, 
                        spriteParams.PaletteOffset, 
                        graphics.Sprites
                     );
                     break;
                  case 0:
                  default:
                     Render16x16Tile_Mask_Clip(
                        pTransDraw, 
                        Code, 
                        xPos, yPos, 
                        spriteParams.Colour, 
                        spriteParams.PaletteBits, 
                        0, 
                        spriteParams.PaletteOffset, 
                        graphics.Sprites
                     );
                     break;
               }
            }
         }
      }
	}
}

static INT32 DrvExit()
{
	GenericTilesExit();
   
   NamcoSoundExit();
   BurnSampleExit();
   
	ZetExit();

	earom_exit();

   machineReset();
   
	BurnFree(memory.All.Start);
	
	machine.Game = NAMCO_GALAGA; // digdugmode = 0;

	return 0;
}

static void DrvPreMakeInputs() {
	// silly bit of code to keep the joystick button pressed for only 1 frame
	// needed for proper pumping action in digdug & highscore name entry.
	memcpy(&input.PortBits[controls.player1Port].Last[0], &input.PortBits[controls.player1Port].Current[0], sizeof(input.PortBits[controls.player1Port].Current));
	memcpy(&input.PortBits[controls.player2Port].Last[0], &input.PortBits[controls.player2Port].Current[0], sizeof(input.PortBits[controls.player2Port].Current));

	{
		input.PortBits[controls.player1Port].Last[4] = 0;
		input.PortBits[controls.player2Port].Last[4] = 0;
		for (INT32 i = 0; i < 2; i++) {
			if(((!i) ? input.PortBits[controls.player1Port].Current[4] : input.PortBits[controls.player2Port].Current[4]) && !button.Held[i]) 
         {
				button.Hold[i] = 2; // number of frames to be held + 1.
				button.Held[i] = 1;
			} else {
				if (((!i) ? !input.PortBits[controls.player1Port].Current[4] : !input.PortBits[controls.player2Port].Current[4])) {
					button.Held[i] = 0;
				}
			}

			if(button.Hold[i]) 
         {
				button.Hold[i] --;
				((!i) ? input.PortBits[controls.player1Port].Current[4] : input.PortBits[controls.player1Port].Current[4]) = ((button.Hold[i]) ? 1 : 0);
			} else {
				(!i) ? input.PortBits[controls.player1Port].Current[4] : input.PortBits[controls.player2Port].Current[4] = 0;
			}
		}
		//bprintf(0, _T("%X:%X,"), DrvInputPort1r[4], DrvButtonHold[0]);
	}
}

static void DrvMakeInputs()
{
	// Reset Inputs
	input.Ports[0] = 0xff;
	input.Ports[1] = 0xff;
	input.Ports[2] = 0xff;

	// Compile Digital Inputs
	for (INT32 i = 0; i < 8; i ++) 
   {
		input.Ports[0] -= (input.PortBits[0].Current[i] & 1) << i;
		input.Ports[1] -= (input.PortBits[1].Current[i] & 1) << i;
		input.Ports[2] -= (input.PortBits[2].Current[i] & 1) << i;
	}

   // galaga only - service mode
	if (NAMCO_GALAGA == machine.Game) 
		input.Ports[0] = (input.Ports[0] & ~0x80) | (input.Dip[0] & 0x80);
}

static INT32 DrvFrame()
{
	
	if (input.Reset) DrvDoReset();

	DrvPreMakeInputs();
	DrvMakeInputs();

	INT32 nSoundBufferPos = 0;
	INT32 nInterleave = 400;
	INT32 nCyclesTotal[3];

	nCyclesTotal[0] = (18432000 / 6) / 60;
	nCyclesTotal[1] = (18432000 / 6) / 60;
	nCyclesTotal[2] = (18432000 / 6) / 60;
	
	ZetNewFrame();

	for (INT32 i = 0; i < nInterleave; i++) 
   {
		INT32 nCurrentCPU;
		
		nCurrentCPU = CPU1;
		ZetOpen(nCurrentCPU);
		ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
		if (i == (nInterleave-1) && cpus.CPU[CPU1].FireIRQ) 
      {
			ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
		}
		if ( (9 == (i % 10)) && 
           ioChip.CPU1FireNMI ) 
      {
			ZetNmi();
		}
		ZetClose();
		
		if (!cpus.CPU[CPU2].Halt) 
      {
			nCurrentCPU = CPU2;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (i == (nInterleave-1) && cpus.CPU[CPU2].FireIRQ) 
         {
				ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
			}
			ZetClose();
		}
		
		if (!cpus.CPU[CPU3].Halt) 
      {
			nCurrentCPU = CPU3;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (((i == ((64 + 000) * nInterleave) / 272) ||
				 (i == ((64 + 128) * nInterleave) / 272)) && cpus.CPU[CPU3].FireIRQ) 
         {
				ZetNmi();
			}
			ZetClose();
		}

		if (pBurnSoundOut) 
      {
			INT32 nSegmentLength = nBurnSoundLen / nInterleave;
			INT16* pSoundBuf = pBurnSoundOut + (nSoundBufferPos << 1);
			
			if (nSegmentLength) 
         {
				NamcoSoundUpdate(pSoundBuf, nSegmentLength);
				if (machine.bHasSamples)
					BurnSampleRender(pSoundBuf, nSegmentLength);
			}
			nSoundBufferPos += nSegmentLength;
		}
	}
	
	if (pBurnSoundOut) 
   {
		INT32 nSegmentLength = nBurnSoundLen - nSoundBufferPos;
		INT16* pSoundBuf = pBurnSoundOut + (nSoundBufferPos << 1);

		if (nSegmentLength) 
      {
			NamcoSoundUpdate(pSoundBuf, nSegmentLength);
			if (machine.bHasSamples)
				BurnSampleRender(pSoundBuf, nSegmentLength);
		}
	}

	if (pBurnDraw)
		BurnDrvRedraw();

	if (NAMCO_GALAGA == machine.Game) 
   {
		static const INT32 Speeds[8] = { -1, -2, -3, 0, 3, 2, 1, 0 };

		stars.ScrollX += Speeds[stars.Control[0] + (stars.Control[1] * 2) + (stars.Control[2] * 4)];
	}

	return 0;
}

static INT32 DrvScan(INT32 nAction, INT32 *pnMin)
{
	struct BurnArea ba;
	
	// Return minimum compatible version
   if (pnMin != NULL) 
   {
		*pnMin = 0x029737;
	}

	if (nAction & ACB_MEMORY_RAM) 
   {
		memset(&ba, 0, sizeof(ba));
		ba.Data	  = memory.RAM.Start;
		ba.nLen	  = memory.RAM.Size;
		ba.szName = "All Ram";
		BurnAcb(&ba);
	}
	
	if (nAction & ACB_DRIVER_DATA) {
		ZetScan(nAction);			// Scan Z80
      
      NamcoSoundScan(nAction, pnMin);
      BurnSampleScan(nAction, pnMin);
      
		// Scan critical driver variables
		SCAN_VAR(cpus.CPU[CPU1].FireIRQ);
		SCAN_VAR(cpus.CPU[CPU2].FireIRQ);
		SCAN_VAR(cpus.CPU[CPU3].FireIRQ);
		SCAN_VAR(cpus.CPU[CPU2].Halt);
		SCAN_VAR(cpus.CPU[CPU3].Halt);
		SCAN_VAR(machine.FlipScreen);
		SCAN_VAR(stars.ScrollX);
		SCAN_VAR(stars.ScrollY);
		SCAN_VAR(ioChip.CustomCommand);
		SCAN_VAR(ioChip.CPU1FireNMI);
		SCAN_VAR(ioChip.Mode);
		SCAN_VAR(ioChip.Credits);
		SCAN_VAR(ioChip.LeftCoinPerCredit);
		SCAN_VAR(ioChip.LeftCreditPerCoin);
		SCAN_VAR(ioChip.RightCoinPerCredit);
		SCAN_VAR(ioChip.RightCreditPerCoin);
		SCAN_VAR(ioChip.AuxCoinPerCredit);
		SCAN_VAR(ioChip.AuxCreditPerCoin);
		SCAN_VAR(input.PrevInValue);
		SCAN_VAR(stars.Control);
		SCAN_VAR(ioChip.Buffer);

		SCAN_VAR(namco54xx.Fetch);
		SCAN_VAR(namco54xx.FetchMode);
		SCAN_VAR(namco54xx.Config1);
		SCAN_VAR(namco54xx.Config2);
		SCAN_VAR(namco54xx.Config3);
		SCAN_VAR(gameVars.playfield);
		SCAN_VAR(gameVars.alphacolor);
		SCAN_VAR(gameVars.playenable);
		SCAN_VAR(gameVars.playcolor);
	}

	if (NAMCO_DIGDUG == machine.Game)
		earom_scan(nAction, pnMin); 

	return 0;
}


struct BurnDriver BurnDrvGalaga = 
{
   /* filename of zip without extension = */    "galaga", 
   /* filename of parent, no extension = */     NULL, 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              "galaga", 
	/* date = */                                 "1981",
   /* FullName = */                             "Galaga (Namco rev. B)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                          	NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                             	BDF_GAME_WORKING | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
   /* GetZipName func = */                   	NULL, 
   /* GetROMInfo func = */                      GalagaRomInfo, 
   /* GetROMName func = */                      GalagaRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
   /* Init func = */                         	GalagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvGalagao = 
{
	/* filename of zip without extension = */    "galagao", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              "galaga", 
	/* date = */                                 "1981",
   /* FullName = */                             "Galaga (Namco)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
   /* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      GalagaoRomInfo, 
   /* GetROMName func = */                      GalagaoRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
   /* Init func = */                            GalagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvGalagamw = 
{
	/* filename of zip without extension = */    "galagamw", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */           	"galaga", 
   /* date = */                                 "1981",
   /* FullName = */                             "Galaga (Midway set 1)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco (Midway License)", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                          	NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                             	BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
   /* GetZipName func = */                   	NULL, 
   /* GetROMInfo func = */                      GalagamwRomInfo, 
   /* GetROMName func = */                      GalagamwRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagamwDIPInfo,
   /* Init func = */                         	GalagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvGalagamk = 
{
	/* filename of zip without extension = */ 	"galagamk", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */           	"galaga", 
   /* date = */                                 "1981",
   /* FullName = */                             "Galaga (Midway set 2)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco (Midway License)", 
   /* System = */                               "Miscellaneous",
	/* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
	/* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
	/* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      GalagamkRomInfo, 
   /* GetROMName func = */                      GalagamkRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
	/* Init func = */                            GalagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvGalagamf = 
{
	/* filename of zip without extension = */    "galagamf", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              "galaga",
	/* date = */                                 "1981",
   /* FullName = */                          	"Galaga (Midway set 1 with fast shoot hack)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco (Midway License)", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                             	BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
   /* GetZipName func = */                   	NULL, 
   /* GetROMInfo func = */                      GalagamfRomInfo, 
   /* GetROMName func = */                      GalagamfRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
   /* Init func = */                            GalagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan,
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvGallag = 
{
	/* filename of zip without extension = */    "gallag", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              "galaga", 
   /* date = */                                 "1981",
   /* FullName = */                             "Gallag\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "bootleg", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_BOOTLEG | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
   /* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      GallagRomInfo, 
   /* GetROMName func = */                      GallagRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
   /* Init func = */                            GallagInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvNebulbee = 
{
	/* filename of zip without extension = */    "nebulbee", 
   /* filename of parent, no extension = */     "galaga", 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              "galaga", 
   /* date = */                                 "1981",
   /* FullName = */                             "Nebulous Bee\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "bootleg", 
   /* System = */                               "Miscellaneous",
	/* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_CLONE | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED | 
                                                BDF_BOOTLEG | 
                                                BDF_HISCORE_SUPPORTED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT, 
   /* Family = */                               0,
	/* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      NebulbeeRomInfo, 
   /* GetROMName func = */                      NebulbeeRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   GalagaSampleInfo, 
   /* GetSampleName func = */                   GalagaSampleName, 
   /* GetInputInfo func = */                    GalagaInputInfo, 
   /* GetDIPInfo func = */                      GalagaDIPInfo,
   /* Init func = */                            GallagInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          GalagaDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //576,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvDigdug = 
{
	/* filename of zip without extension = */    "digdug", 
   /* filename of parent, no extension = */     NULL, 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              NULL, 
   /* date = */                                 "1982",
   /* FullName = */                             "Dig Dug (rev 2)\0", 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_MAZE | GBF_ACTION, 
   /* Family = */                               0,
	/* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      digdugRomInfo, 
   /* GetROMName func = */                      digdugRomName, 
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   NULL, 
   /* GetSampleName func = */                   NULL, 
   /* GetInputInfo func = */                    DigdugInputInfo, 
   /* GetDIPInfo func = */                      DigdugDIPInfo,
   /* Init func = */                            DigdugInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DigDugDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //0x300,
   /* Width, Height, xAspect, yAspect = */      //224, 288, 3, 4
   /* Palette Entries count = */                DIGDUG_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

struct BurnDriver BurnDrvXevious = 
{
	/* filename of zip without extension = */    "xevious",
   /* filename of parent, no extension = */     NULL, 
   /* filename of board ROMs = */               NULL, 
   /* filename of samples ZIP = */              NULL, 
   /* date = */                                 "1982",
   /* FullName = */                             "Xevious (Namco)\0",
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         "Namco", 
   /* System = */                               "Miscellaneous",
   /* FullName = */                             NULL, 
   /* Comment = */                              NULL, 
   /* Manufacturer = */                         NULL, 
   /* System = */                               NULL,
   /* Flags = */                                BDF_GAME_WORKING | 
                                                BDF_ORIENTATION_VERTICAL | 
                                                BDF_ORIENTATION_FLIPPED, 
   /* No of Players = */                        2, 
   /* Hardware Type = */                        HARDWARE_MISC_PRE90S, 
   /* Genre = */                                GBF_VERSHOOT,
   /* Family = */                               0,
	/* GetZipName func = */                      NULL, 
   /* GetROMInfo func = */                      XeviousRomInfo, 
   /* GetROMName func = */                      XeviousRomName,
   /* GetHDDInfo func = */                      NULL, 
   /* GetHDDName func = */                      NULL, 
   /* GetSampleInfo func = */                   XeviousSampleInfo, 
   /* GetSampleName func = */                   XeviousSampleName, 
   /* GetInputInfo func = */                    XeviousInputInfo, 
   /* GetDIPInfo func = */                      XeviousDIPInfo,
   /* Init func = */                            XeviousInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          XeviousDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                //0x300,
   /* Width, Height, xAspect, yAspect = */   	//224, 288, 3, 4
   /* Palette Entries count = */                XEVIOUS_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

