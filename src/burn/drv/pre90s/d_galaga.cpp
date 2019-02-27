// Galaga & Dig-Dug driver for FB Alpha, based on the MAME driver by Nicola Salmoria & previous work by Martin Scragg, Mirko Buffoni, Aaron Giles
// Dig Dug added July 27, 2015

// notes: galaga freeplay mode doesn't display "freeplay" - need to investigate.

#include "tiles_generic.h"
#include "z80_intf.h"
#include "namco_snd.h"
#include "samples.h"
#include "earom.h"

#define NAMCO_BRD_CPU_COUNT      3
#define NAMCO_BRD_INP_COUNT      3

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

struct Input_Def
{
   struct PortBits_Def PortBits[NAMCO_BRD_INP_COUNT];
   UINT8 Dip[3];
   UINT8 Ports[NAMCO_BRD_INP_COUNT];
   UINT8 PrevInValue;
   UINT8 Reset;
};

static struct Input_Def input;

struct Graphics_Def
{
   UINT8 *Chars;
   UINT8 *Sprites;
   UINT8 *Chars2;
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

struct CPU_Control_Def
{
   UINT8 FireIRQ;
   UINT8 Halt;
};

struct CPU_Def
{
   struct CPU_Control_Def CPU1;
   struct CPU_Control_Def CPU2;
   struct CPU_Control_Def CPU3;
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
   UINT8 CPU1FireIRQ;
   UINT8 Mode;
   UINT8 Credits;
   UINT8 CoinPerCredit;
   UINT8 CreditPerCoin;
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
   NAMCO_XEVIOUS
};

//static UINT8 DrvFlipScreen;

struct MachineDef
{
   INT32 Game;
   UINT8 bHasSamples;
   UINT8 FlipScreen;
};

static struct MachineDef machine = { 0 };

struct Button_Def
{
   INT32 Hold[NAMCO_BRD_INP_COUNT];
   INT32 Held[NAMCO_BRD_INP_COUNT];
   INT32 Last;
};

static struct Button_Def button = { 0 };

// Dig Dug playfield stuff
static INT32 playfield, alphacolor, playenable, playcolor;

#define INP_GALAGA_COIN_TRIGGER     0x70
#define INP_GALAGA_COIN_MASK        0x70
#define INP_GALAGA_START_1          0x04
#define INP_GALAGA_START_2          0x08

#define INP_DIGDUG_COIN_TRIGGER     0x01
#define INP_DIGDUG_COIN_MASK        0x01
#define INP_DIGDUG_START_1          0x10
#define INP_DIGDUG_START_2          0x20

static INT32 GalagaInit(void);
static INT32 GallagInit(void);
static INT32 GalagaMemIndex(void);
static void GalagaMachineInit(void);
static UINT8 __fastcall GalagaZ80ProgRead(UINT16 addr);
static void __fastcall GalagaZ80ProgWrite(UINT16 addr, UINT8 dta);
static INT32 GalagaDraw(void);
static void GalagaCalcPalette(void);
static void GalagaInitStars(void);
static void GalagaRenderStars(void);
static void GalagaRenderTilemap(void);
static void GalagaRenderSprites(void);

static INT32 DigdugInit(void);
static INT32 DigDugMemIndex(void);
static void DigDugMachineInit(void);
static UINT8 __fastcall DigDugZ80ProgRead(UINT16 addr);
void __fastcall digdug_pf_latch_w(UINT16 offset, UINT8 data);
static void __fastcall DigDugZ80ProgWrite(UINT16 addr, UINT8 dta);
static INT32 DigDugDraw(void);
static void DigDugCalcPalette(void);
static void DigDugRenderTiles(void);
static void DigDugRenderSprites(void);

static INT32 XeviousInit(void);
static INT32 XeviousMemIndex(void);
static void XeviousMachineInit(void);
static UINT8 __fastcall XeviousZ80ProgRead(UINT16 addr);
static void __fastcall XeviousZ80ProgWrite(UINT16 addr, UINT8 dta);
static INT32 XeviousDraw(void);
static void XeviousCalcPalette(void);
static void XeviousRenderTiles(void);
static void XeviousRenderSprites(void);

static void machineReset(void);
static INT32 DrvDoReset(void);
static void Namco54XXWrite(INT32 Data);
static UINT8 __fastcall NamcoZ80ProgRead(UINT16 addr);
static UINT8 updateCoinAndCredit(UINT8 trigger, UINT8 mask, UINT8 start1, UINT8 start2);
static UINT8 updateJoyAndButtons(UINT16 Offset, UINT8 jp);
static void __fastcall NamcoZ80ProgWrite(UINT16 addr, UINT8 dta);
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

static struct BurnDIPInfo GalagaDIPList[]=
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

static INT32 CharPlaneOffsets[2]   = { 0, 4 };
static INT32 CharXOffsets[8]       = { 64, 65, 66, 67, 0, 1, 2, 3 };
static INT32 CharYOffsets[8]       = { 0, 8, 16, 24, 32, 40, 48, 56 };
static INT32 SpritePlaneOffsets[2] = { 0, 4 };
static INT32 SpriteXOffsets[16]    = { 0, 1, 2, 3, 64, 65, 66, 67, 128, 129, 130, 131, 192, 193, 194, 195 };
static INT32 SpriteYOffsets[16]    = { 0, 8, 16, 24, 32, 40, 48, 56, 256, 264, 272, 280, 288, 296, 304, 312 };

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
	GfxDecode(0x100, 2, 8, 8, CharPlaneOffsets, CharXOffsets, CharYOffsets, 0x80, DrvTempRom, graphics.Chars);
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         7, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         8, 1)) return 1;
	GfxDecode(0x80, 2, 16, 16, SpritePlaneOffsets, SpriteXOffsets, SpriteYOffsets, 0x200, DrvTempRom, graphics.Sprites);

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
	GfxDecode(0x100, 2, 8, 8, CharPlaneOffsets, CharXOffsets, CharYOffsets, 0x80, DrvTempRom, graphics.Chars);
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         8, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         9, 1)) return 1;
	GfxDecode(0x80, 2, 16, 16, SpritePlaneOffsets, SpriteXOffsets, SpriteYOffsets, 0x200, DrvTempRom, graphics.Sprites);

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

	graphics.Chars2            = Next; Next += 0x00180 * 8 * 8;
	PlayFieldData              = Next; Next += 0x01000;
	graphics.Chars             = Next; Next += 0x01100 * 8 * 8;
	graphics.Sprites           = Next; Next += 0x01100 * 16 * 16;
	graphics.Palette           = (UINT32*)Next; Next += 0x300 * sizeof(UINT32);

	memory.All.Size            = Next - memory.All.Start;

	return 0;
}

static void GalagaMachineInit()
{
	ZetInit(0);
	ZetOpen(0);
	ZetSetReadHandler(GalagaZ80ProgRead);
	ZetSetWriteHandler(GalagaZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(1);
	ZetOpen(1);
	ZetSetReadHandler(GalagaZ80ProgRead);
	ZetSetWriteHandler(GalagaZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(2);
	ZetOpen(2);
	ZetSetReadHandler(GalagaZ80ProgRead);
	ZetSetWriteHandler(GalagaZ80ProgWrite);
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

	GenericTilesInit();

	earom_init();

	// Reset the driver
	DrvDoReset();
}

static UINT8 __fastcall GalagaZ80ProgRead(UINT16 addr)
{
	switch (addr) 
   {
		case 0x7000:
      {
			if ( (0x71 == ioChip.CustomCommand) ||
              (0xb1 == ioChip.CustomCommand) )
         {
            if (ioChip.Mode) 
            {
               return input.Ports[0];
            } 
            else 
            {
               return updateCoinAndCredit(
                  INP_GALAGA_COIN_TRIGGER, 
                  INP_GALAGA_COIN_MASK,
                  INP_GALAGA_START_1, 
                  INP_GALAGA_START_2
               );
            }
			}
			
			return 0xff;
		}
      
		case 0x7001:
		case 0x7002:
      {
         UINT32 Offset = addr - 0x7000;
         
			if ( (0x71 == ioChip.CustomCommand) ||
              (0xb1 == ioChip.CustomCommand) )
         {
            UINT8 jp = input.Ports[Offset];

            return updateJoyAndButtons(Offset, jp);
			}
			
			return 0xff;
		}
		
	}
	
	return NamcoZ80ProgRead(addr);
}

static void __fastcall GalagaZ80ProgWrite(UINT16 addr, UINT8 dta)
{
	switch (addr) 
   {
		case 0x7007:
			if (0xe1 == ioChip.CustomCommand) 
         {
            ioChip.CoinPerCredit = ioChip.Buffer[1];
            ioChip.CreditPerCoin = ioChip.Buffer[2];
			}
			
			return;
	
		default: 
      {
		}
	}
   
   return NamcoZ80ProgWrite(addr, dta);
}

static INT32 GalagaDraw()
{
	BurnTransferClear();
	GalagaCalcPalette();
	GalagaRenderTilemap();
	GalagaRenderStars();
	GalagaRenderSprites();	
	BurnTransferCopy(graphics.Palette);
	return 0;
}

static void GalagaCalcPalette()
{
	UINT32 Palette[96];
	
	for (INT32 i = 0; i < 32; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.Palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.Palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.Palette[i] >> 5) & 0x06];
      
		Palette[i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < 64; i ++) 
   {
      INT32 r = Colour2Bit[(i >> 0) & 0x03];
      INT32 g = Colour2Bit[(i >> 2) & 0x03];
      INT32 b = Colour2Bit[(i >> 4) & 0x03];
      
		Palette[32 + i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < 256; i ++) 
   {
		graphics.Palette[i]         = Palette[((memory.PROM.CharLookup[i]) & 0x0f) + 0x10];
	}
	
	for (INT32 i = 0; i < 256; i ++) 
   {
		graphics.Palette[0x100 + i] = Palette[  memory.PROM.SpriteLookup[i] & 0x0f];
	}
	
	for (INT32 i = 0; i < 64; i ++) 
   {
		graphics.Palette[0x200 + i] = Palette[32 + i];
	}

	GalagaInitStars();
}

struct Star_Def 
{
	UINT16 x;
   UINT16 y;
	UINT8 Colour;
   UINT8 Set;
};

#define MAX_STARS 252
static struct Star_Def StarSeedTable[MAX_STARS];

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
					pTransDraw[(y * nScreenWidth) + x] = StarSeedTable[StarCounter].Colour + 512;
				}
			}

		}
	}
}

static void GalagaRenderTilemap()
{
	INT32 TileIndex;

	for (INT32 mx = 0; mx < 28; mx ++) 
   {
		for (INT32 my = 0; my < 36; my ++) 
      {
			INT32 Row = mx + 2;
			INT32 Col = my - 2;
			if (Col & 0x20) {
				TileIndex = Row + ((Col & 0x1f) << 5);
			} else {
				TileIndex = Col + (Row << 5);
			}
			
			INT32 Code   = memory.RAM.Video[TileIndex + 0x000] & 0x7f;
			INT32 Colour = memory.RAM.Video[TileIndex + 0x400] & 0x3f;

			INT32 y = 8 * mx;
			INT32 x = 8 * my;
			
			if (machine.FlipScreen) {
				x = 280 - x;
				y = 216 - y;
			}
			
			if (x > 8 && x < 280 && y > 8 && y < 216) 
         {
				if (machine.FlipScreen) {
					Render8x8Tile_FlipXY(pTransDraw, Code, x, y, Colour, 2, 0, graphics.Chars);
				} else {
					Render8x8Tile(pTransDraw, Code, x, y, Colour, 2, 0, graphics.Chars);
				}
			} else {
				if (machine.FlipScreen) {
					Render8x8Tile_FlipXY_Clip(pTransDraw, Code, x, y, Colour, 2, 0, graphics.Chars);
				} else {
					Render8x8Tile_Clip(pTransDraw, Code, x, y, Colour, 2, 0, graphics.Chars);
				}
			}
		}
	}
}

static void GalagaRenderSprites()
{
	UINT8 *SpriteRam1 = memory.RAM.Shared1 + 0x380;
	UINT8 *SpriteRam2 = memory.RAM.Shared2 + 0x380;
	UINT8 *SpriteRam3 = memory.RAM.Shared3 + 0x380;

	for (INT32 Offset = 0; Offset < 0x80; Offset += 2) {
		static const INT32 GfxOffset[2][2] = {
			{ 0, 1 },
			{ 2, 3 }
		};
		INT32 Sprite =    SpriteRam1[Offset + 0] & 0x7f;
		INT32 Colour =    SpriteRam1[Offset + 1] & 0x3f;
		INT32 sx =        SpriteRam2[Offset + 1] - 40 + (0x100 * (SpriteRam3[Offset + 1] & 0x03));
		INT32 sy = 256 -  SpriteRam2[Offset + 0] + 1;
		INT32 xFlip =    (SpriteRam3[Offset + 0] & 0x01);
		INT32 yFlip =    (SpriteRam3[Offset + 0] & 0x02) >> 1;
		INT32 xSize =    (SpriteRam3[Offset + 0] & 0x04) >> 2;
		INT32 ySize =    (SpriteRam3[Offset + 0] & 0x08) >> 3;
      INT32 Orient =    SpriteRam3[Offset + 0] & 0x03;
		sy -= 16 * ySize;
		sy = (sy & 0xff) - 32;

		if (machine.FlipScreen) 
      {
			xFlip = !xFlip;
			yFlip = !yFlip;
         Orient = 3 - Orient;
		}

		for (INT32 y = 0; y <= ySize; y ++) 
      {
			for (INT32 x = 0; x <= xSize; x ++) 
         {
				INT32 Code = Sprite + GfxOffset[y ^ (ySize * yFlip)][x ^ (xSize * xFlip)];
				INT32 xPos = sx + 16 * x;
				INT32 yPos = sy + 16 * y;

            if ((xPos < -15) || (xPos >= nScreenWidth) ) continue;
				if ((yPos < -15) || (yPos >= nScreenHeight)) continue;

				if ((xPos > 16) && (xPos < 272) && 
                (yPos > 16) && (yPos < 208) ) 
            {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
               }
				} else {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 256, graphics.Sprites);
                     break;
               }
				}
			}
		}
	}
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

	{"Reset"                , BIT_DIGITAL  , &input.Reset,                  "reset"     },
	{"Service"              , BIT_DIGITAL  , &input.PortBits[0].Current[7], "service"   },
	{"Dip 1"                , BIT_DIPSWITCH, &input.Dip[0],                 "dip"       },
	{"Dip 2"                , BIT_DIPSWITCH, &input.Dip[1],                 "dip"       },
};

STDINPUTINFO(Digdug)


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

static INT32 DigdugCharPlaneOffsets[2] = { 0 };
static INT32 DigdugCharXOffsets[8] = { STEP8(7,-1) };
static INT32 DigdugCharYOffsets[8] = { STEP8(0,8) };

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

	DrvTempRom = (UINT8 *)BurnMalloc(0x10000);

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

	memset(DrvTempRom, 0, 0x10000);
	// Load and decode the chars 8x8 (in digdug)
	if (0 != BurnLoadRom(DrvTempRom,                   7, 1)) return 1;
	GfxDecode(0x80, 1, 8, 8, DigdugCharPlaneOffsets, DigdugCharXOffsets, DigdugCharYOffsets, 0x40, DrvTempRom, graphics.Chars2);
	
	// Load and decode the sprites
	memset(DrvTempRom, 0, 0x10000);
	if (0 != BurnLoadRom(DrvTempRom + 0x00000,         8, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,         9, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x02000,         10, 1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x03000,         11, 1)) return 1;
	GfxDecode(0x80 + 0x80, 2, 16, 16, SpritePlaneOffsets, SpriteXOffsets, SpriteYOffsets, 0x200, DrvTempRom, graphics.Sprites);

	memset(DrvTempRom, 0, 0x10000);
	// Load and decode the chars 2bpp
	if (0 != BurnLoadRom(DrvTempRom,                   12, 1)) return 1;
	GfxDecode(0x100, 2, 8, 8, CharPlaneOffsets, CharXOffsets, CharYOffsets, 0x80, DrvTempRom, graphics.Chars);

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

	graphics.Chars2            = Next; Next += 0x00180 * 8 * 8;
	PlayFieldData              = Next; Next += 0x01000;
	graphics.Chars             = Next; Next += 0x01100 * 8 * 8;
	graphics.Sprites           = Next; Next += 0x01100 * 16 * 16;
	graphics.Palette           = (UINT32*)Next; Next += 0x300 * sizeof(UINT32);

	memory.All.Size            = Next - memory.All.Start;

	return 0;
}

static void DigDugMachineInit()
{
	ZetInit(0);
	ZetOpen(0);
	ZetSetReadHandler(DigDugZ80ProgRead);
	ZetSetWriteHandler(DigDugZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(1);
	ZetOpen(1);
	ZetSetReadHandler(DigDugZ80ProgRead);
	ZetSetWriteHandler(DigDugZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0x9800, 0x9bff, MAP_RAM);
	ZetClose();
	
	ZetInit(2);
	ZetOpen(2);
	ZetSetReadHandler(DigDugZ80ProgRead);
	ZetSetWriteHandler(DigDugZ80ProgWrite);
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

	GenericTilesInit();

	earom_init();

	// Reset the driver
	DrvDoReset();
}

static UINT8 __fastcall DigDugZ80ProgRead(UINT16 addr)
{
   // EAROM Read
	if ( (addr >= 0xb800) && (addr <= 0xb83f) )
   {
		return earom_read(addr - 0xb800);
	}

	switch (addr) 
   {
		case 0x7000:
		case 0x7001:
		case 0x7002:
		case 0x7003:
		case 0x7004:
		case 0x7005:
		case 0x7006:
		case 0x7007:
		case 0x7008:
		case 0x7009:
		case 0x700a:
		case 0x700b:
		case 0x700c:
		case 0x700d:
		case 0x700e:
		case 0x700f: 
      {
			INT32 Offset = addr - 0x7000;
			
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
                     return updateCoinAndCredit(
                        INP_DIGDUG_COIN_TRIGGER,
                        INP_DIGDUG_COIN_MASK,
                        INP_DIGDUG_START_1,
                        INP_DIGDUG_START_2
                     );
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
							/*         4          */
							if ((jp & 0x01) == 0)		/* up */
								jp = (jp & ~0x0f) | 0x00;
							else if ((jp & 0x02) == 0)	/* right */
								jp = (jp & ~0x0f) | 0x02;
							else if ((jp & 0x04) == 0)	/* down */
								jp = (jp & ~0x0f) | 0x04;
							else if ((jp & 0x08) == 0) /* left */
								jp = (jp & ~0x0f) | 0x06;
							else
								jp = (jp & ~0x0f) | 0x08;
						}

						return updateJoyAndButtons(Offset, jp);
					}
				}
			}
			
			return 0xff;
		}
		
	}
	
	return NamcoZ80ProgRead(addr);
}

void __fastcall digdug_pf_latch_w(UINT16 offset, UINT8 data)
{
	switch (offset)
	{
		case 0:
			playfield = (playfield & ~1) | (data & 1);
			break;

		case 1:
			playfield = (playfield & ~2) | ((data << 1) & 2);
			break;

		case 2:
			alphacolor = data & 1;
			break;

		case 3:
			playenable = data & 1;
			break;

		case 4:
			playcolor = (playcolor & ~1) | (data & 1);
			break;

		case 5:
			playcolor = (playcolor & ~2) | ((data << 1) & 2);
			break;
	}
}

static void __fastcall DigDugZ80ProgWrite(UINT16 addr, UINT8 dta)
{
   // EAROM Write
	if ( (addr >= 0xb800) && (addr <= 0xb83f) ) 
   {
		earom_write(addr - 0xb800, dta);
		return;
	}

	switch (addr) 
   {
		case 0xb840:
         earom_ctrl_write(0xb840, dta);
			return;

		case 0x7008:
			if (0xc1 == ioChip.CustomCommand) 
         {
            ioChip.CoinPerCredit = ioChip.Buffer[2] & 0x0f;
            ioChip.CreditPerCoin = ioChip.Buffer[3] & 0x0f;
         }
         break;
	
      case 0xa000:
      case 0xa001:
      case 0xa002:
      case 0xa003:
      case 0xa004:
      case 0xa005:
		case 0xa006: 
      {
			digdug_pf_latch_w(addr - 0xa000, dta);
			break;
		}

		default: 
      {
         break;
		}
	}
   
   return NamcoZ80ProgWrite(addr, dta);
}

static INT32 DigDugDraw()
{
	BurnTransferClear();
	DigDugCalcPalette();
	DigDugRenderTiles();
	DigDugRenderSprites();
	BurnTransferCopy(graphics.Palette);
	return 0;
}

static void DigDugCalcPalette()
{
	UINT32 Palette[96];
	
	for (INT32 i = 0; i < 32; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.Palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.Palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.Palette[i] >> 5) & 0x06];
      
		Palette[i] = BurnHighCol(r, g, b, 0);
	}

	/* characters - direct mapping */
	for (INT32 i = 0; i < 16; i ++)
	{
		graphics.Palette[i*2+0] = Palette[0];
		graphics.Palette[i*2+1] = Palette[i];
	}

	/* sprites */
	for (INT32 i = 0; i < 0x100; i ++) 
   {
		graphics.Palette[0x200 + i] = Palette[(memory.PROM.SpriteLookup[i] & 0x0f) + 0x10];
	}

	/* bg_select */
	for (INT32 i = 0; i < 0x100; i ++) 
   {
		graphics.Palette[0x100 + i] = Palette[memory.PROM.CharLookup[i] & 0x0f];
	}
}

static void DigDugRenderTiles()
{
	INT32 TileIndex;
	UINT8 *pf = PlayFieldData + (playfield << 10);
	UINT8 pfval;
	UINT32 pfcolor = playcolor << 4;

	if (playenable != 0)
		pf = NULL;

	for (INT32 mx = 0; mx < 28; mx ++) 
   {
		for (INT32 my = 0; my < 36; my ++) 
      {
			INT32 Row = mx + 2;
			INT32 Col = my - 2;
			if (Col & 0x20) 
         {
				TileIndex = Row + ((Col & 0x1f) << 5);
			} else {
				TileIndex = Col + (Row << 5);
			}

			INT32 Code = memory.RAM.Video[TileIndex];
			INT32 Colour = ((Code >> 4) & 0x0e) | ((Code >> 3) & 2);
			Code &= 0x7f;

			INT32 y = 8 * mx;
			INT32 x = 8 * my;
			
			if (machine.FlipScreen) 
         {
				x = 280 - x;
				y = 216 - y;
			}

			if (pf) 
         {
				// Draw playfield / background
				pfval = pf[TileIndex & 0xfff];
				INT32 pfColour = (pfval >> 4) + pfcolor;
				if (x > 8 && x < 280 && y > 8 && y < 216) 
            {
					if (machine.FlipScreen) {
						Render8x8Tile_FlipXY(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					} else {
						Render8x8Tile(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					}
				} else {
					if (machine.FlipScreen) {
						Render8x8Tile_FlipXY_Clip(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					} else {
						Render8x8Tile_Clip(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					}
				}
			}

			if (x >= 0 && x <= 288 && y >= 0 && y <= 224) 
         {
				if (machine.FlipScreen) {
					Render8x8Tile_Mask_FlipXY(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				} else {
					Render8x8Tile_Mask(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				}
			} else {
				if (machine.FlipScreen) {
					Render8x8Tile_Mask_FlipXY_Clip(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				} else {
					Render8x8Tile_Mask_Clip(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				}
			}
		}
	}
}

static void DigDugRenderSprites()
{
	UINT8 *SpriteRam1 = memory.RAM.Shared1 + 0x380;
	UINT8 *SpriteRam2 = memory.RAM.Shared2 + 0x380;
	UINT8 *SpriteRam3 = memory.RAM.Shared3 + 0x380;
	
	for (INT32 Offset = 0; Offset < 0x80; Offset += 2) 
   {
		static const INT32 GfxOffset[2][2] = {
			{ 0, 1 },
			{ 2, 3 }
		};
		INT32 Sprite =   SpriteRam1[Offset + 0];
		INT32 Colour =   SpriteRam1[Offset + 1] & 0x3f;
		INT32 sx =       SpriteRam2[Offset + 1] - 40 + 1;
		INT32 sy = 256 - SpriteRam2[Offset + 0] + 1;
		INT32 xFlip =   (SpriteRam3[Offset + 0] & 0x01);
		INT32 yFlip =   (SpriteRam3[Offset + 0] & 0x02) >> 1;
      UINT32 Orient =  SpriteRam3[Offset + 0] & 0x03;
		
      INT32 sSize = (Sprite & 0x80) >> 7;

		sy -= 16 * sSize;
		sy = (sy & 0xff) - 32;

		if (sSize)
			Sprite = (Sprite & 0xc0) | ((Sprite & ~0xc0) << 2);

		if (machine.FlipScreen) 
      {
			xFlip = !xFlip;
			yFlip = !yFlip;
         Orient = 3 - Orient;
		}

		for (INT32 y = 0; y <= sSize; y ++) 
      {
			for (INT32 x = 0; x <= sSize; x ++) 
         {
				INT32 Code = Sprite + GfxOffset[y ^ (sSize * yFlip)][x ^ (sSize * xFlip)];
				INT32 xPos = (sx + 16 * x);
				INT32 yPos =  sy + 16 * y;

				if (xPos < 8) 
               xPos += 0x100; // that's a wrap!

				if ((xPos < -15) || (xPos >= nScreenWidth))  continue;
				if ((yPos < -15) || (yPos >= nScreenHeight)) continue;

				if ( ((xPos > 0) && (xPos < nScreenWidth-16)) && 
                 ((yPos > 0) && (yPos < nScreenHeight-16)) ) 
            {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
               }
				} else 
            {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
               }
				}
			}
		}
	}
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

static struct BurnDIPInfo XeviousDIPList[]=
{
	// Default Values
   // nInput, nFlags, nMask, nSettings, szInfo
	{0x0c, 0xff, 0xff, 0xFF, NULL                     },
	{0x0d, 0xff, 0xff, 0xFF, NULL                     },
	
	// Dip 1
	{0   , 0x01, 0   , 2   , "Button 2"               },
	{0x0c, 0x01, 0x01, 0x01, "Released"               },
	{0x0c, 0x01, 0x01, 0x00, "Held"                   },

	{0   , 0x02, 0   , 2   , "Flags Award Bonus Life" },
	{0x0c, 0x01, 0x02, 0x02, "Yes"                    },
	{0x0c, 0x01, 0x02, 0x00, "No"                     },

	{0   , 0x0C, 0   , 4   , "Coin B"                 },
	{0x0c, 0x02, 0x0C, 0x04, "2 Coins 1 Play"         },
	{0x0c, 0x02, 0x0C, 0x0C, "1 Coin  1 Play"         },
	{0x0c, 0x02, 0x0C, 0x00, "2 Coins 3 Plays"        },
	{0x0c, 0x02, 0x0C, 0x08, "1 Coin  2 Plays"        },

	{0   , 0x10, 0   , 2   , "Button 2 (Cocktail)"    },
	{0x0c, 0x01, 0x10, 0x10, "Released"               },
	{0x0c, 0x01, 0x10, 0x00, "Held"                   },

	{0   , 0x60, 0   , 4   , "Difficulty"             },
	{0x0c, 0x02, 0x60, 0x40, "Easy"                   },
	{0x0c, 0x02, 0x60, 0x60, "Normal"                 },
	{0x0c, 0x02, 0x60, 0x20, "Hard"                   },
	{0x0c, 0x02, 0x60, 0x00, "Hardest"                },
	
	{0   , 0x80, 0   , 2   , "Freeze"                 },
	{0x0c, 0x01, 0x80, 0x80, "Off"                    },
	{0x0c, 0x01, 0x80, 0x00, "On"                     },
	
	// Dip 2	
	{0   , 0x03, 0   , 4   , "Coin A"                 },
	{0x0d, 0x02, 0x03, 0x01, "2 Coins 1 Play"         },
	{0x0d, 0x02, 0x03, 0x03, "1 Coin  1 Play"         },
	{0x0d, 0x02, 0x03, 0x00, "2 Coins 3 Plays"        },
	{0x0d, 0x02, 0x03, 0x02, "1 Coin  2 Plays"        },
	
	{0   , 0x1C, 0   , 8   , "Bonus Life"             },
	{0x0d, 0x03, 0x1C, 0x18, "10k  40k  40k"          },
	{0x0d, 0x03, 0x1C, 0x14, "10k  50k  50k"          },
	{0x0d, 0x03, 0x1C, 0x10, "20k  50k  50k"          },
	{0x0d, 0x03, 0x1C, 0x1C, "20k  60k  60k"          },
	{0x0d, 0x03, 0x1C, 0x0C, "20k  70k  70k"          },
	{0x0d, 0x03, 0x1C, 0x08, "20k  80k  80k"          },
	{0x0d, 0x03, 0x1C, 0x04, "20k  60k"               },
	{0x0d, 0x03, 0x1C, 0x00, "None"                   },
	
	{0   , 0x60, 0   , 4   , "Lives"                  },
	{0x0d, 0x02, 0x60, 0x40, "1"                      },
	{0x0d, 0x02, 0x60, 0x20, "2"                      },
	{0x0d, 0x02, 0x60, 0x60, "3"                      },
	{0x0d, 0x02, 0x60, 0x00, "5"                      },

	{0   , 0x80, 0   , 2   , "Cabinet"                },
	{0x0d, 0x01, 0x80, 0x80, "Upright"                },
	{0x0d, 0x01, 0x80, 0x00, "Cocktail"               },
	

/*	{0   , 0xfe, 0   , 2   , "Demo Sounds"            },
	{0x0c, 0x01, 0x08, 0x08, "Off"                    },
	{0x0c, 0x01, 0x08, 0x00, "On"                     },
	
	{0   , 0xfe, 0   , 2   , "Rack Test"              },
	{0x0c, 0x01, 0x20, 0x20, "Off"                    },
	{0x0c, 0x01, 0x20, 0x00, "On"                     },
*/	
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
	{ "explo1.wav", SAMPLE_NOLOOP },	/* ground target explosion */
	{ "explo2.wav", SAMPLE_NOLOOP },	/* Solvalou explosion */
	{ "explo3.wav", SAMPLE_NOLOOP },	/* credit */
	{ "explo4.wav", SAMPLE_NOLOOP },	/* Garu Zakato explosion */
#endif
   { "",           0 }
};

STD_SAMPLE_PICK(Xevious)
STD_SAMPLE_FN(Xevious)

#define XEVIOUS_FG_COLOR_CODES      64
#define XEVIOUS_FG_COLOR_PLANES     1
#define XEVIOUS_FG_COLOR_GRAN       1 << (XEVIOUS_FG_COLOR_PLANES - 1)
#define XEVIOUS_BG_COLOR_CODES      128
#define XEVIOUS_BG_COLOR_PLANES     2
#define XEVIOUS_BG_COLOR_GRAN       1 << (XEVIOUS_BG_COLOR_PLANES - 1)
#define XEVIOUS_SPRITE_COLOR_CODES  64
#define XEVIOUS_SPRITE_COLOR_PLANES 3
#define XEVIOUS_SPRITE_COLOR_GRAN   1 << (XEVIOUS_SPRITE_COLOR_PLANES - 1)

/* foreground characters */
static INT32 XeviousFGCharPlaneOffsets[XEVIOUS_FG_COLOR_PLANES] = { 0 }; 

/* background tiles */
   /* 512 characters */
   /* 2 bits per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
static INT32 XeviousBGCharPlaneOffsets[XEVIOUS_BG_COLOR_PLANES] = { 
   0, 
   512 * 8 * 8 
};

static INT32 XeviousCharXOffsets[8] = 	{ 0, 1, 2, 3, 4, 5, 6, 7 };
static INT32 XeviousCharYOffsets[8] = 	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 };

/* sprite set #1 */
   /* 128 sprites */
   /* 3 bits per pixel */
   /* 16 x 16 sprites */
   /* every sprite takes 128 (64?) consecutive bytes */
static INT32 XeviousSprite1PlaneOffsets[XEVIOUS_SPRITE_COLOR_PLANES] = { 
   0x0000, //0, 
   0x0004, //4, 
   0x2000 // 128*16*16 
};

/* sprite set #2 */
static INT32 XeviousSprite2PlaneOffsets[XEVIOUS_SPRITE_COLOR_PLANES] = { 
   0x2004, // 128*16*16+4, 
   0x4000, // 256*16*16, 
   0x4004, // 256*16*16+4 
};

/* sprite set #3 */
static INT32 XeviousSprite3PlaneOffsets[XEVIOUS_SPRITE_COLOR_PLANES] = { 
   0x6000, // (128+128), 
   0x6004, // 0, 
   0       //4 
};

static INT32 XeviousInit()
{
	bprintf(PRINT_NORMAL, _T("Xevious: Init\n"));

	// Allocate and Blank all required memory
	memory.All.Start = NULL;
	XeviousMemIndex();
	
   memory.All.Start = (UINT8 *)BurnMalloc(memory.All.Size);
	if (NULL == memory.All.Start) 
      return 1;
	
   memset(memory.All.Start, 0, memory.All.Size);
	XeviousMemIndex();

	bprintf(PRINT_NORMAL, _T("Xevious Memory Allocated\n"));

	DrvTempRom = (UINT8 *)BurnMalloc(0x08000);
   if (NULL == DrvTempRom) 
      return 1;
   
	// Load Z80 #1 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x00000,  0,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x01000,  1,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x02000,  2,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom1 + 0x03000,  3,  1)) return 1;
	
	bprintf(PRINT_NORMAL, _T("Xevious: Z80-1 ROM loaded\n"));

	// Load Z80 #2 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x00000,  4,  1)) return 1;
	if (0 != BurnLoadRom(memory.Z80.Rom2 + 0x01000,  5,  1)) return 1;
	
	bprintf(PRINT_NORMAL, _T("Xevious: Z80-2 ROM loaded\n"));

	// Load Z80 #3 Program Roms
	if (0 != BurnLoadRom(memory.Z80.Rom3 + 0x00000,  6,  1)) return 1;
	
	bprintf(PRINT_NORMAL, _T("Xevious: Z80-3 ROM loaded\n"));

	// Load and decode the chars
   /* foreground characters: */
   /* 512 characters */
   /* 1 bit per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	if (0 != BurnLoadRom(DrvTempRom,                      7,  1)) return 1;
	GfxDecode(0x200, 1, 8, 8, XeviousFGCharPlaneOffsets, XeviousCharXOffsets, XeviousCharYOffsets, 8*8, DrvTempRom, graphics.Chars);

	bprintf(PRINT_NORMAL, _T("Xevious: 1bit Chars decoded\n"));
	
   /* background tiles */
   /* 512 characters */
   /* 2 bits per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	memset(DrvTempRom, 0, 0x02000);
	if (0 != BurnLoadRom(DrvTempRom,                      8,  1)) return 1;
	if (0 != BurnLoadRom(DrvTempRom + 0x01000,            9,  1)) return 1;
	GfxDecode(0x200, 2, 8, 8, XeviousBGCharPlaneOffsets, XeviousCharXOffsets, XeviousCharYOffsets, 8*8, DrvTempRom, graphics.Chars2);

	bprintf(PRINT_NORMAL, _T("Xevious: 2bit chars decoded\n"));

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
	GfxDecode(0x80, 3, 16, 16, XeviousSprite1PlaneOffsets, SpriteXOffsets, SpriteYOffsets, 16*16, DrvTempRom, graphics.Sprites);

   /* sprite set #2 */
   /* 128 sprites */
   /* 3 bits per pixel */
   /* 16 x 16 sprites */
   /* every sprite takes 128 (64?) consecutive bytes */
	GfxDecode(0x80, 3, 16, 16, XeviousSprite2PlaneOffsets, SpriteXOffsets, SpriteYOffsets, 16*16, DrvTempRom + 128*16*16*3, graphics.Sprites + 128*16*16);

   /* sprite set #3 */
   /* 64 sprites */
   /* 3 bits per pixel (one is always 0) */
   /* 16 x 16 sprites */
   /* every sprite takes 64 consecutive bytes */
	GfxDecode(0x40, 3, 16, 16, XeviousSprite3PlaneOffsets, SpriteXOffsets, SpriteYOffsets, 16*16, DrvTempRom + 128*16*16*3 + 128*16*16*3, graphics.Sprites + 128*16*16 + 128*16*16);

	bprintf(PRINT_NORMAL, _T("Xevious: Sprites decoded\n"));

   // Load PlayFieldData

	if (0 != BurnLoadRom(PlayFieldData + 0x00000,  14,  1)) return 1;
	if (0 != BurnLoadRom(PlayFieldData + 0x01000,  15,  1)) return 1;
	if (0 != BurnLoadRom(PlayFieldData + 0x03000,  16,  1)) return 1;
   
	bprintf(PRINT_NORMAL, _T("Xevious: Playfield loaded\n"));

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
	
	bprintf(PRINT_NORMAL, _T("Xevious: PROM loaded\n"));

   if (NULL != DrvTempRom)
   {
      BurnFree(DrvTempRom);
	
      bprintf(PRINT_NORMAL, _T("Xevious: temp mem freed\n"));
   }
   
   machine.Game = NAMCO_XEVIOUS;
   
	XeviousMachineInit();

	bprintf(PRINT_NORMAL, _T("Xevious: Machine Init completed\n"));

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

	memory.RAM.Video = Next;            Next += 0x02000;
	memory.RAM.Shared1 = Next;          Next += 0x01000;
	memory.RAM.Shared2 = Next;          Next += 0x00800;
	memory.RAM.Shared3 = Next;          Next += 0x00800;

	memory.RAM.Size = Next - memory.RAM.Start;

	graphics.Chars2 = Next;             Next += 0x00200 * 8 * 8;
	PlayFieldData = Next;               Next += 0x04000;
	graphics.Chars = Next;              Next += 0x00200 * 8 * 8;
	graphics.Sprites = Next;            Next += 0x00240 * 16 * 16;
	graphics.Palette = (UINT32*)Next;   Next += 0x300 * sizeof(UINT32);

	memory.All.Size = Next - memory.All.Start;

   return 0;
}

static void XeviousMachineInit()
{
	ZetInit(0);
	ZetOpen(0);
	ZetSetReadHandler(XeviousZ80ProgRead);
	ZetSetWriteHandler(XeviousZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Shared1, 0x7800, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x97ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xa7ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	ZetInit(1);
	ZetOpen(1);
	ZetSetReadHandler(XeviousZ80ProgRead);
	ZetSetWriteHandler(XeviousZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Shared1, 0x7800, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x97ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xa7ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	ZetInit(2);
	ZetOpen(2);
	ZetSetReadHandler(XeviousZ80ProgRead);
	ZetSetWriteHandler(XeviousZ80ProgWrite);
	ZetMapMemory(memory.Z80.Rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.Shared1, 0x7800, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared2, 0x9000, 0x97ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Shared3, 0xa000, 0xa7ff, MAP_RAM);
	ZetMapMemory(memory.RAM.Video,   0xb000, 0xcfff, MAP_RAM);
	ZetClose();
	
	NamcoSoundInit(18432000 / 6 / 32, 3, 0);
	NacmoSoundSetAllRoutes(0.90 * 10.0 / 16.0, BURN_SND_ROUTE_BOTH);
	BurnSampleInit(1);
	BurnSampleSetAllRoutesAllSamples(0.25, BURN_SND_ROUTE_BOTH);
	machine.bHasSamples = BurnSampleGetStatus(0) != -1;

	GenericTilesInit();

	// Reset the driver
	DrvDoReset();
}

static UINT8 __fastcall XeviousZ80ProgRead(UINT16 addr)
{
	switch (addr) 
   {
		case 0x7000:
		case 0x7001:
		case 0x7002:
		case 0x7003:
		case 0x7004:
		case 0x7005:
		case 0x7006:
		case 0x7007:
		case 0x7008:
		case 0x7009:
		case 0x700a:
		case 0x700b:
		case 0x700c:
		case 0x700d:
		case 0x700e:
		case 0x700f: 
      {
			INT32 Offset = addr - 0x7000;
			
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
                     return updateCoinAndCredit(
                        INP_DIGDUG_COIN_TRIGGER,
                        INP_DIGDUG_COIN_MASK,
                        INP_DIGDUG_START_1,
                        INP_DIGDUG_START_2
                     );
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
							/*         4          */
							if ((jp & 0x01) == 0)		/* up */
								jp = (jp & ~0x0f) | 0x00;
							else if ((jp & 0x02) == 0)	/* right */
								jp = (jp & ~0x0f) | 0x02;
							else if ((jp & 0x04) == 0)	/* down */
								jp = (jp & ~0x0f) | 0x04;
							else if ((jp & 0x08) == 0) /* left */
								jp = (jp & ~0x0f) | 0x06;
							else
								jp = (jp & ~0x0f) | 0x08;
						}

						return updateJoyAndButtons(Offset, jp);
					}
				}
			}
			
			return 0xff;
		}
		
	}
	
	return NamcoZ80ProgRead(addr);
}

static void __fastcall XeviousZ80ProgWrite(UINT16 addr, UINT8 dta)
{
	switch (addr) 
   {
		case 0x7008:
			if (0xc1 == ioChip.CustomCommand) 
         {
            ioChip.CoinPerCredit = ioChip.Buffer[2] & 0x0f;
            ioChip.CreditPerCoin = ioChip.Buffer[3] & 0x0f;
         }
         break;
	
      case 0xa000:
      case 0xa001:
      case 0xa002:
      case 0xa003:
      case 0xa004:
      case 0xa005:
		case 0xa006: 
      {
			break;
		}

		default: 
      {
         break;
		}
	}
   
   return NamcoZ80ProgWrite(addr, dta);
}

static INT32 XeviousDraw()
{
	BurnTransferClear();
	XeviousCalcPalette();
	XeviousRenderTiles();
	XeviousRenderSprites();
	BurnTransferCopy(graphics.Palette);
	return 0;
}

static void XeviousCalcPalette()
{
	UINT32 Palette[129];
   UINT32 color = 0;
	
	for (INT32 i = 0; i < 128; i ++) 
   {
      INT32 r = Colour4Bit[(memory.PROM.Palette[0x0000 + i]) & 0x0f];
      INT32 g = Colour4Bit[(memory.PROM.Palette[0x0100 + i]) & 0x0f];
      INT32 b = Colour4Bit[(memory.PROM.Palette[0x0200 + i]) & 0x0f];
      
		Palette[i] = BurnHighCol(r, g, b, 0);
	}
   
   Palette[128] = BurnHighCol(0, 0, 0, 0); // Transparency Colour for Sprites

	/* bg_select */
	for (INT32 i = 0; i < XEVIOUS_BG_COLOR_CODES * XEVIOUS_BG_COLOR_GRAN; i ++) 
   {
      c = memory.PROM.CharLookup[                         i] & 0x0f      | 
          memory.PROM.CharLookup[XEVIOUS_BG_COLOR_CODES + i] & 0x0f << 4;
		graphics.Palette[0x100 + i] = Palette[c];
	}

	/* sprites */
	for (INT32 i = 0; i < XEVIOUS_SPRITE_COLOR_CODES * XEVIOUS_SPRITE_COLOR_GRAN; i ++) 
   {
      c = memory.PROM.SpriteLookup[i                             ] & 0x0f      |
          memory.PROM.SpriteLookup[XEVIOUS_SPRITE_COLOR_CODES + i] & 0x0f << 4;
      if (c & 0x80)
         graphics.Palette[0x200 + i] = Palette[c & 0x7f]
      else
         graphics.Palette[0x200 + i] = Palette[0x80];
	}

	/* characters - direct mapping */
	for (INT32 i = 0; i < XEVIOUS_FG_COLOR_CODES * XEVIOUS_FG_COLOR_GRAN; i += 2)
	{
		graphics.Palette[i+0] = Palette[0x80];
		graphics.Palette[i+1] = Palette[i / 2];
	}

}

static void XeviousRenderTiles()
{
	INT32 TileIndex;
	UINT8 *pf = PlayFieldData + (playfield << 10);
	UINT8 pfval;
	UINT32 pfcolor = playcolor << 4;

	if (playenable != 0)
		pf = NULL;

	for (INT32 mx = 0; mx < 28; mx ++) 
   {
		for (INT32 my = 0; my < 36; my ++) 
      {
			INT32 Row = mx + 2;
			INT32 Col = my - 2;
			if (Col & 0x20) 
         {
				TileIndex = Row + ((Col & 0x1f) << 5);
			} else {
				TileIndex = Col + (Row << 5);
			}

			INT32 Code = memory.RAM.Video[TileIndex];
			INT32 Colour = ((Code >> 4) & 0x0e) | ((Code >> 3) & 2);
			Code &= 0x7f;

			INT32 y = 8 * mx;
			INT32 x = 8 * my;
			
			if (machine.FlipScreen) 
         {
				x = 280 - x;
				y = 216 - y;
			}

			if (pf) 
         {
				// Draw playfield / background
				pfval = pf[TileIndex & 0xfff];
				INT32 pfColour = (pfval >> 4) + pfcolor;
				if (x > 8 && x < 280 && y > 8 && y < 216) 
            {
					if (machine.FlipScreen) {
						Render8x8Tile_FlipXY(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					} else {
						Render8x8Tile(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					}
				} else {
					if (machine.FlipScreen) {
						Render8x8Tile_FlipXY_Clip(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					} else {
						Render8x8Tile_Clip(pTransDraw, pfval, x, y, pfColour, 2, 0x100, graphics.Chars);
					}
				}
			}

			if (x >= 0 && x <= 288 && y >= 0 && y <= 224) 
         {
				if (machine.FlipScreen) {
					Render8x8Tile_Mask_FlipXY(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				} else {
					Render8x8Tile_Mask(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				}
			} else {
				if (machine.FlipScreen) {
					Render8x8Tile_Mask_FlipXY_Clip(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				} else {
					Render8x8Tile_Mask_Clip(pTransDraw, Code, x, y, Colour, 1, 0, 0, graphics.Chars2);
				}
			}
		}
	}
}

static void XeviousRenderSprites()
{
	UINT8 *SpriteRam1 = memory.RAM.Shared1 + 0x380;
	UINT8 *SpriteRam2 = memory.RAM.Shared2 + 0x380;
	UINT8 *SpriteRam3 = memory.RAM.Shared3 + 0x380;
	
	for (INT32 Offset = 0; Offset < 0x80; Offset += 2) 
   {
		static const INT32 GfxOffset[2][2] = {
			{ 0, 1 },
			{ 2, 3 }
		};
		INT32 Sprite =   SpriteRam1[Offset + 0];
		INT32 Colour =   SpriteRam1[Offset + 1] & 0x3f;
		INT32 sx =       SpriteRam2[Offset + 1] - 40 + 1;
		INT32 sy = 256 - SpriteRam2[Offset + 0] + 1;
		INT32 xFlip =   (SpriteRam3[Offset + 0] & 0x01);
		INT32 yFlip =   (SpriteRam3[Offset + 0] & 0x02) >> 1;
      UINT32 Orient =  SpriteRam3[Offset + 0] & 0x03;
		
      INT32 sSize = (Sprite & 0x80) >> 7;

		sy -= 16 * sSize;
		sy = (sy & 0xff) - 32;

		if (sSize)
			Sprite = (Sprite & 0xc0) | ((Sprite & ~0xc0) << 2);

		if (machine.FlipScreen) 
      {
			xFlip = !xFlip;
			yFlip = !yFlip;
         Orient = 3 - Orient;
		}

		for (INT32 y = 0; y <= sSize; y ++) 
      {
			for (INT32 x = 0; x <= sSize; x ++) 
         {
				INT32 Code = Sprite + GfxOffset[y ^ (sSize * yFlip)][x ^ (sSize * xFlip)];
				INT32 xPos = (sx + 16 * x);
				INT32 yPos =  sy + 16 * y;

				if (xPos < 8) 
               xPos += 0x100; // that's a wrap!

				if ((xPos < -15) || (xPos >= nScreenWidth))  continue;
				if ((yPos < -15) || (yPos >= nScreenHeight)) continue;

				if ( ((xPos > 0) && (xPos < nScreenWidth-16)) && 
                 ((yPos > 0) && (yPos < nScreenHeight-16)) ) 
            {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
               }
				} else 
            {
               switch (Orient)
               {
                  case 3:
							Render16x16Tile_Mask_FlipXY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 2:
							Render16x16Tile_Mask_FlipY_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 1:
							Render16x16Tile_Mask_FlipX_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
                  case 0:
                  default:
							Render16x16Tile_Mask_Clip(pTransDraw, Code, xPos, yPos, Colour, 2, 0, 0x200, graphics.Sprites);
                     break;
               }
				}
			}
		}
	}
}


/* === Common === */

static void machineReset()
{
	cpus.CPU1.FireIRQ = 0;
	cpus.CPU2.FireIRQ = 0;
	cpus.CPU3.FireIRQ = 0;
	cpus.CPU2.Halt = 0;
	cpus.CPU3.Halt = 0;
   
	machine.FlipScreen = 0;
	
   for (INT32 i = 0; i < STARS_CTRL_NUM; i++) {
		stars.Control[i] = 0;
	}
	stars.ScrollX = 0;
	stars.ScrollY = 0;
	
	ioChip.CustomCommand = 0;
	ioChip.CPU1FireIRQ = 0;
	ioChip.Mode = 0;
	ioChip.Credits = 0;
	ioChip.CoinPerCredit = 0;
	ioChip.CreditPerCoin = 0;
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
   
	playfield = 0;
	alphacolor = 0;
	playenable = 0;
	playcolor = 0;

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

static UINT8 __fastcall NamcoZ80ProgRead(UINT16 addr)
{
	switch (addr) 
   {
		case 0x6800:
		case 0x6801:
		case 0x6802:
		case 0x6803:
		case 0x6804:
		case 0x6805:
		case 0x6806:
		case 0x6807: 
      {
			INT32 Offset = addr - 0x6800;
			INT32 Bit0 = (input.Dip[2] >> Offset) & 0x01;
			INT32 Bit1 = (input.Dip[1] >> Offset) & 0x01;

			return (Bit1 << 1) | Bit0;
		}
		
		case 0x7100: 
      {
			return ioChip.CustomCommand;
		}
      
		case 0xa000:
		case 0xa001:
		case 0xa002:
		case 0xa003:
		case 0xa004:
		case 0xa005:
		case 0xa006: break; // (ignore) spurious reads when playfield latch written to

		default: {
			bprintf(PRINT_NORMAL, _T("Z80 #%i Read %04x\n"), ZetGetActive(), addr);
		}
	}
	
	return 0;
}

static UINT8 updateCoinAndCredit(UINT8 trigger, UINT8 mask, UINT8 start1, UINT8 start2)
{
   static UINT8 CoinInserted;
   
   UINT8 In = input.Ports[0];
   if (In != input.PrevInValue) 
   {
      if (0 < ioChip.CoinPerCredit) 
      {
         if ( (trigger != (In & mask) ) && 
              (99 > ioChip.Credits) ) 
         {
            CoinInserted ++;
            if (CoinInserted >= ioChip.CoinPerCredit) 
            {
               ioChip.Credits += ioChip.CreditPerCoin;
               CoinInserted = 0;
            }
         }
      } 
      else 
      {
         ioChip.Credits = 2;
      }
      
      if ( 0 == (In & start1) ) 
      {
         if (ioChip.Credits >= 1) ioChip.Credits --;
      }
      
      if ( 0 == (In & start2) ) 
      {
         if (ioChip.Credits >= 2) ioChip.Credits -= 2;
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

static void __fastcall NamcoZ80ProgWrite(UINT16 addr, UINT8 dta)
{
	if ( (addr >= 0x6800) && (addr <= 0x681f) ) 
   {
		NamcoSoundWrite(addr - 0x6800, dta);
		return;
	}

//	bprintf(PRINT_NORMAL, _T("54XX z80 #%i Write %X, %X nbs %X\n"), ZetGetActive(), a, d, nBurnSoundLen);

	switch (addr) 
   {
		case 0x6820: 
      {
			cpus.CPU1.FireIRQ = dta & 0x01;
			if (!cpus.CPU1.FireIRQ) 
         {
				INT32 nActive = ZetGetActive();
				ZetClose();
				ZetOpen(0);
				ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
				ZetClose();
				ZetOpen(nActive);
			}
			return;
		}
		
		case 0x6821: 
      {
			cpus.CPU2.FireIRQ = dta & 0x01;
			if (!cpus.CPU2.FireIRQ) 
         {
				INT32 nActive = ZetGetActive();
				ZetClose();
				ZetOpen(1);
				ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
				ZetClose();
				ZetOpen(nActive);
			}
			return;
		}
		
		case 0x6822: 
      {
			cpus.CPU3.FireIRQ = !(dta & 0x01);
			return;
		}
		
		case 0x6823: 
      {
			if (!(dta & 0x01)) 
         {
				INT32 nActive = ZetGetActive();
				ZetClose();
				ZetOpen(1);
				ZetReset();
				ZetClose();
				ZetOpen(2);
				ZetReset();
				ZetClose();
				ZetOpen(nActive);
				cpus.CPU2.Halt = 1;
				cpus.CPU3.Halt = 1;
				return;
			} 
         else 
         {
				cpus.CPU2.Halt = 0;
				cpus.CPU3.Halt = 0;
			}
		}
		
		case 0x6830: 
      {
			// watchdog write
			return;
		}
		
		case 0x7000:
		case 0x7001:
		case 0x7002:
		case 0x7003:
		case 0x7004:
		case 0x7005:
		case 0x7006:
		case 0x7007:
		case 0x7008:
		case 0x7009:
		case 0x700a:
		case 0x700b:
		case 0x700c:
		case 0x700d:
		case 0x700e:
		case 0x700f: 
      {
			INT32 Offset = addr - 0x7000;
			ioChip.Buffer[Offset] = dta;
			Namco54XXWrite(dta);
			
			return;
		}
	
		case 0x7100: 
      {
			ioChip.CustomCommand = dta;
			ioChip.CPU1FireIRQ = 1;
			
			switch (ioChip.CustomCommand) 
         {
				case 0x10: 
            {
					ioChip.CPU1FireIRQ = 0;
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
			
			return;
		}
		
		case 0xa000:
		case 0xa001:
		case 0xa002:
		case 0xa003:
		case 0xa004:
		case 0xa005:
      {
         stars.Control[addr - 0xa000] = dta & 0x01;
			return;
		}
		
		case 0xa007: 
      {
			machine.FlipScreen = dta & 0x01;
			return;
		}
		
		default: 
      {
			//bprintf(PRINT_NORMAL, _T("Z80 #%i Write %04x, %02x\n"), ZetGetActive(), a, d);
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
	memcpy(&input.PortBits[1].Last[0], &input.PortBits[1].Current[0], sizeof(input.PortBits[1].Current));
	memcpy(&input.PortBits[2].Last[0], &input.PortBits[2].Current[0], sizeof(input.PortBits[2].Current));

	{
		input.PortBits[1].Last[4] = 0;
		input.PortBits[2].Last[4] = 0;
		for (INT32 i = 0; i < 2; i++) {
			if(((!i) ? input.PortBits[1].Current[4] : input.PortBits[2].Current[4]) && !button.Held[i]) 
         {
				button.Hold[i] = 2; // number of frames to be held + 1.
				button.Held[i] = 1;
			} else {
				if (((!i) ? !input.PortBits[1].Current[4] : !input.PortBits[2].Current[4])) {
					button.Held[i] = 0;
				}
			}

			if(button.Hold[i]) 
         {
				button.Hold[i] --;
				((!i) ? input.PortBits[1].Current[4] : input.PortBits[2].Current[4]) = ((button.Hold[i]) ? 1 : 0);
			} else {
				(!i) ? input.PortBits[1].Current[4] : input.PortBits[2].Current[4] = 0;
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
		
		nCurrentCPU = 0;
		ZetOpen(nCurrentCPU);
		ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
		if (i == (nInterleave-1) && cpus.CPU1.FireIRQ) 
      {
			ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
		}
		if ( (9 == (i % 10)) && 
           ioChip.CPU1FireIRQ ) 
      {
			ZetNmi();
		}
		ZetClose();
		
		if (!cpus.CPU2.Halt) 
      {
			nCurrentCPU = 1;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (i == (nInterleave-1) && cpus.CPU2.FireIRQ) 
         {
				ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
			}
			ZetClose();
		}
		
		if (!cpus.CPU3.Halt) 
      {
			nCurrentCPU = 2;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (((i == ((64 + 000) * nInterleave) / 272) ||
				 (i == ((64 + 128) * nInterleave) / 272)) && cpus.CPU3.FireIRQ) 
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
		SCAN_VAR(cpus.CPU1.FireIRQ);
		SCAN_VAR(cpus.CPU2.FireIRQ);
		SCAN_VAR(cpus.CPU3.FireIRQ);
		SCAN_VAR(cpus.CPU2.Halt);
		SCAN_VAR(cpus.CPU3.Halt);
		SCAN_VAR(machine.FlipScreen);
		SCAN_VAR(stars.ScrollX);
		SCAN_VAR(stars.ScrollY);
		SCAN_VAR(ioChip.CustomCommand);
		SCAN_VAR(ioChip.CPU1FireIRQ);
		SCAN_VAR(ioChip.Mode);
		SCAN_VAR(ioChip.Credits);
		SCAN_VAR(ioChip.CoinPerCredit);
		SCAN_VAR(ioChip.CreditPerCoin);
		SCAN_VAR(input.PrevInValue);
		SCAN_VAR(stars.Control);
		SCAN_VAR(ioChip.Buffer);

		SCAN_VAR(namco54xx.Fetch);
		SCAN_VAR(namco54xx.FetchMode);
		SCAN_VAR(namco54xx.Config1);
		SCAN_VAR(namco54xx.Config2);
		SCAN_VAR(namco54xx.Config3);
		SCAN_VAR(playfield);
		SCAN_VAR(alphacolor);
		SCAN_VAR(playenable);
		SCAN_VAR(playcolor);
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
   /* Palette Entries count = */                576,
   /* Width, Height, xAspect, yAspect = */   	224, 288, 3, 4
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
   /* Palette Entries count = */                576,
   /* Width, Height, xAspect, yAspect = */     	224, 288, 3, 4
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
   /* Palette Entries count = */                576,
   /* Width, Height, xAspect, yAspect = */	   224, 288, 3, 4
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
   /* Palette Entries count = */                576,
	/* Width, Height, xAspect, yAspect = */   	224, 288, 3, 4
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
   /* Palette Entries count = */                576,
   /* Width, Height, xAspect, yAspect = */      224, 288, 3, 4
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
   /* Palette Entries count = */                576,
	/* Width, Height, xAspect, yAspect = */      224, 288, 3, 4
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
   /* Palette Entries count = */                576,
   /* Width, Height, xAspect, yAspect = */	   224, 288, 3, 4
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
   /* Palette Entries count = */                0x300,
   /* Width, Height, xAspect, yAspect = */      224, 288, 3, 4
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
   /* Palette Entries count = */                0x300,
   /* Width, Height, xAspect, yAspect = */      224, 288, 3, 4
};

