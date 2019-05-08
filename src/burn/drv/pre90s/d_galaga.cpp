// Galaga & Dig-Dug driver for FB Alpha, based on the MAME driver by Nicola Salmoria & previous work by Martin Scragg, Mirko Buffoni, Aaron Giles
// Dig Dug added July 27, 2015
// Xevious added April 22, 2019

// notes: galaga freeplay mode doesn't display "freeplay" - need to investigate.

#include "tiles_generic.h"
#include "z80_intf.h"
#include "namco_snd.h"
#include "samples.h"
#include "earom.h"

enum
{
   PLAYER1 = 0,
   PLAYER2,
   NO_OF_PLAYERS
};

enum
{
   CPU1 = 0,
   CPU2,
   CPU3,
   NAMCO_BRD_CPU_COUNT
};

struct CPU_Control_Def
{
   UINT8 fireIRQ;
   UINT8 halt;
};

struct CPU_Def
{
   struct CPU_Control_Def CPU[NAMCO_BRD_CPU_COUNT];
};

static struct CPU_Def cpus = { 0 };

struct CPU_Memory_Map_Def
{
   UINT8    **byteArray;
   UINT32   startAddress;
   UINT32   endAddress;
   UINT32   type;
};

struct CPU_Config_Def
{
   UINT32   id;
   UINT8 __fastcall (*z80ProgRead)(UINT16 addr);
   void __fastcall (*z80ProgWrite)(UINT16 addr, UINT8 dta);
   void (*z80MemMap)(void);
};

struct Memory_Def
{
   struct
   {
      UINT8  *start;
      UINT32 size;
   } all;
   struct
   {
      UINT8 *start;
      UINT32 size;
      UINT8 *video;
      UINT8 *shared1;
      UINT8 *shared2;
      UINT8 *shared3;
   } RAM;
   struct
   {
      UINT8 *rom1;
      UINT8 *rom2;
      UINT8 *rom3;
   } Z80;
   struct
   {
      UINT8 *palette;
      UINT8 *charLookup;
      UINT8 *spriteLookup;
   } PROM;
};

static struct Memory_Def memory;

enum
{
   MEM_PGM = 0,
   MEM_RAM,
   MEM_ROM,
   MEM_DATA,
   MEM_DATA32,
   MEM_TYPES
};

struct Memory_Layout_Def
{
   union
   {
      UINT8    **uint8;
      UINT32   **uint32;
   } region;
   UINT32   size;
   UINT32   type;
};

struct ROM_Load_Def
{
   UINT8    **address;
   UINT32   offset;
   INT32    (*postProcessing)(void);
};

static UINT8 *tempRom = NULL;
static UINT8 *gameData; // digdug playfield data

struct Graphics_Def
{
   UINT8 *fgChars;
   UINT8 *sprites;
   UINT8 *bgTiles;
   UINT32 *palette;
};

static struct Graphics_Def graphics;

/* Weird video definitions...
 *  +---+
 *  |   |
 *  |   |
 *  |   | screen_height, x, tilemap_width
 *  |   |
 *  +---+
 *  screen_width, y, tilemap_height
 */
#define NAMCO_SCREEN_WIDTH    224
#define NAMCO_SCREEN_HEIGHT   288

#define NAMCO_TMAP_WIDTH      36
#define NAMCO_TMAP_HEIGHT     28


static const INT32 Colour2Bit[4] = 
{ 
   0x00, 0x47, 0x97, 0xde 
};

static const INT32 Colour3Bit[8] = 
{ 
   0x00, 0x21, 0x47, 0x68,
   0x97, 0xb8, 0xde, 0xff 
};

static const INT32 Colour4Bit[16] = 
{
   0x00, 0x0e, 0x1f, 0x2d,
   0x43, 0x51, 0x62, 0x70,
   0x8f, 0x9d, 0xae, 0xbc,
   0xd2, 0xe0, 0xf1, 0xff
};

#define NAMCO_BRD_INP_COUNT      3

struct InputSignalBits_Def
{
   UINT8 bit[8];
};

struct InputSignal_Def
{
   struct InputSignalBits_Def bits;
   UINT8 byte;
};

struct Port_Def
{
   struct InputSignal_Def previous;
   struct InputSignal_Def current;
};
   
struct Input_Def
{
   struct Port_Def ports[NAMCO_BRD_INP_COUNT];
   struct InputSignal_Def dip[2];
   UINT8 reset;
};

static struct Input_Def input;

/* check directions, according to the following 8-position rule */
/*         0          */
/*        7 1         */
/*       6 8 2        */
/*        5 3         */
/*         4          */
static const UINT8 namcoControls[16] = {
/* 0000, 0001, 0010, 0011, 0100, 0101, 0110, 0111, 1000, 1001, 1010, 1011, 1100, 1101, 1110, 1111  */
/* LDRU, LDRu, LDrU, LDru, LdRU, LdRu, LdrU, Ldru, lDRU, lDRu, lDrU, lDru, ldRU, ldRu, ldrU, ldru  */
   8,    8,    8,    5,    8,    8,    7,    6,    8,    3,    8,    4,    1,    2,    0,    8
};

struct Button_Def
{
   INT32 hold;
   INT32 held;
   INT32 last;
};

static struct Button_Def button[NO_OF_PLAYERS] = { 0 };

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
#define orient (xFlip | yFlip)

struct Namco_Sprite_Params
{
   INT32 sprite;
   INT32 colour;
   INT32 xStart;
   INT32 yStart;
   INT32 xStep;
   INT32 yStep;
   INT32 flags;
   INT32 paletteBits;
   INT32 paletteOffset;
};

//#define USE_NAMCO51

#ifndef USE_NAMCO51
#define IOCHIP_BUF_SIZE       16

struct IOChip_Def
{
   UINT8 customCommand;
   UINT8 CPU1FireNMI;
   UINT8 mode;
   UINT8 credits;
   UINT8 leftCoinPerCredit;
   UINT8 leftCreditPerCoin;
   UINT8 rightCoinPerCredit;
   UINT8 rightCreditPerCoin;
   UINT8 auxCoinPerCredit;
   UINT8 auxCreditPerCoin;
   UINT8 buffer[IOCHIP_BUF_SIZE];
   UINT8 coinsInserted;
   INT32 startEnable;
};

static struct IOChip_Def ioChip = { 0 };

#else 

struct Namco_51xx_Def
{
   UINT8 mode;
   UINT8 leftCoinPerCredit;
   UINT8 leftCreditPerCoins;
   UINT8 rightCoinPerCredit;
   UINT8 rightCreditPerCoins;
   UINT8 auxCoinPerCredit;
   UINT8 auxCreditPerCoins;
   UINT8 leftCoinsInserted;
   UINT8 rightCoinsInserted;
   UINT8 credits;
   UINT8 startEnable;
   UINT8 remapJoystick;
   UINT8 lastCoin;
   UINT8 counter;
   UINT8 coinCreditMode;
};

static struct Namco_51xx_Def namco51xx = { 0 };

#endif

struct Machine_Config_Def
{
   struct CPU_Config_Def      *cpus;
   struct CPU_Wr_Table        *wrAddrList;
   struct CPU_Rd_Table        *rdAddrList;
   struct Memory_Layout_Def   *memLayoutTable;
   UINT32                     memLayoutSize;
   struct ROM_Load_Def        *romLayoutTable;
   UINT32                     romLayoutSize;
   UINT32                     tempRomSize;
   INT32                      (*tilemapsConfig)(void);
   void                       (**drawLayerTable)(void);
   UINT32                     drawTableSize;
   UINT32                     (*getSpriteParams)(struct Namco_Sprite_Params *spriteParams, UINT32 offset);
   INT32                      (*reset)(void);
#ifndef USE_NAMCO51
   UINT32                     ioChipStartEnable;
#endif
   UINT32                     playerControlPort[NO_OF_PLAYERS];
};

enum GAMES_ON_MACHINE
{
   NAMCO_GALAGA = 0,
   NAMCO_DIGDUG,
   NAMCO_XEVIOUS,
   NAMCO_TOTAL_GAMES
};

struct MachineDef
{
   struct Machine_Config_Def *config;
   enum GAMES_ON_MACHINE game;
   UINT8 hasSamples;
   UINT8 flipScreen;
   UINT32 numOfDips;
};

static struct MachineDef machine = { 0 };

#define NAMCO54XX_CFG1_SIZE   4
#define NAMCO54XX_CFG2_SIZE   4
#define NAMCO54XX_CFG3_SIZE   5

enum
{
   NAMCO54_WR_CFG1 = 1,
   NAMCO54_WR_CFG2,
   NAMCO54_WR_CFG3
};

struct NAMCO54XX_Def
{
   INT32 fetch;
   INT32 fetchMode;
   UINT8 config1[NAMCO54XX_CFG1_SIZE];
   UINT8 config2[NAMCO54XX_CFG2_SIZE]; 
   UINT8 config3[NAMCO54XX_CFG3_SIZE];
};

static struct NAMCO54XX_Def namco54xx = { 0 };

struct InputMisc_Def
{
   UINT8 portNumber;
   UINT8 triggerValue;
   UINT8 triggerMask;
};

struct CoinAndCredit_Def
{
   struct InputMisc_Def leftCoin;
   struct InputMisc_Def rightCoin;
   struct InputMisc_Def auxCoin;
   struct InputMisc_Def start1;
   struct InputMisc_Def start2;
};

enum
{
   NAMCO_1BIT_PALETTE_BITS = 1,
   NAMCO_2BIT_PALETTE_BITS
};

static const INT32 planeOffsets1Bit[NAMCO_1BIT_PALETTE_BITS] = 
   { 0 };
static const INT32 planeOffsets2Bit[NAMCO_2BIT_PALETTE_BITS] = 
   { 0, 4 };

static const INT32 xOffsets8x8Tiles1Bit[8]      = { STEP8(7,-1) };
static const INT32 yOffsets8x8Tiles1Bit[8]      = { STEP8(0,8) };
static const INT32 xOffsets8x8Tiles2Bit[8]      = { 64, 65, 66, 67, 0, 1, 2, 3 };
static const INT32 yOffsets8x8Tiles2Bit[8]      = { 0, 8, 16, 24, 32, 40, 48, 56 };
static const INT32 xOffsets16x16Tiles2Bit[16]   = { 0,   1,   2,   3,   64,  65,  66,  67, 
                                                    128, 129, 130, 131, 192, 193, 194, 195 };
static const INT32 yOffsets16x16Tiles2Bit[16]   = { 0,   8,   16,  24,  32,  40,  48,  56, 
                                                    256, 264, 272, 280, 288, 296, 304, 312 };

typedef void (*DrawFunc_t)(void);

static INT32 namcoInitBoard(void);
static INT32 namcoMachineInit(void);
static void machineReset(void);
static INT32 DrvDoReset(void);

static INT32 namcoMemIndex(void);
static INT32 namcoLoadGameROMS(void);

static void namco54XXWrite(INT32 dta);
#ifndef USE_NAMCO51
static UINT8 updateCoinAndCredit(struct CoinAndCredit_Def *signals);
static UINT8 updateJoyAndButtons(UINT16 offset, UINT8 jp);
#else
static void namco51xxReset(void);
static UINT8 namco51xxRead(UINT16 offset);
static void namco51xxWrite(UINT16 offset, UINT8 dta);
#endif

static UINT8 namcoZ80ReadDip(UINT16 offset);
static UINT8 namcoZ80ReadIoCmd(UINT16 offset);

static UINT8 __fastcall namcoZ80ProgRead(UINT16 addr);

static void namcoZ80WriteSound(UINT16 offset, UINT8 dta);
static void namcoZ80WriteCPU1Irq(UINT16 offset, UINT8 dta);
static void namcoZ80WriteCPU2Irq(UINT16 offset, UINT8 dta);
static void namcoZ80WriteCPU3Irq(UINT16 offset, UINT8 dta);
static void namcoZ80WriteCPUReset(UINT16 offset, UINT8 dta);
static void namcoZ80WriteIoChip(UINT16 offset, UINT8 dta);
static void namcoZ80WriteIoCmd(UINT16 offset, UINT8 dta);
static void namcoZ80WriteFlipScreen(UINT16 offset, UINT8 dta);
static void __fastcall namcoZ80ProgWrite(UINT16 addr, UINT8 dta);

static tilemap_scan ( namco );
static void namcoRenderSprites(void);

static INT32 DrvExit(void);
static void DrvPreMakeInputs(void);
static void DrvMakeInputs(void);
static INT32 DrvFrame(void);

static INT32 DrvScan(INT32 nAction, INT32 *pnMin);

/* === Common === */

static INT32 namcoInitBoard(void)
{
	// Allocate and Blank all required memory
	memory.all.start = NULL;
	namcoMemIndex();
	
   memory.all.start = (UINT8 *)BurnMalloc(memory.all.size);
	if (NULL == memory.all.start) 
      return 1;
	memset(memory.all.start, 0, memory.all.size);
	
	namcoMemIndex();
   
   return namcoLoadGameROMS();
}

static INT32 namcoMachineInit(void)
{
   INT32 retVal = 0;
   
   for (INT32 cpuCount = CPU1; cpuCount < NAMCO_BRD_CPU_COUNT; cpuCount ++)
   {
      struct CPU_Config_Def *currentCPU = &machine.config->cpus[cpuCount];
      ZetInit(currentCPU->id);
      ZetOpen(currentCPU->id);
      ZetSetReadHandler(currentCPU->z80ProgRead);
      ZetSetWriteHandler(currentCPU->z80ProgWrite);
      currentCPU->z80MemMap();
      ZetClose();
	}
   
	NamcoSoundInit(18432000 / 6 / 32, 3, 0);
	NacmoSoundSetAllRoutes(0.90 * 10.0 / 16.0, BURN_SND_ROUTE_BOTH);
	BurnSampleInit(1);
	BurnSampleSetAllRoutesAllSamples(0.25, BURN_SND_ROUTE_BOTH);
	machine.hasSamples = BurnSampleGetStatus(0) != -1;
   
   GenericTilesInit();
   
   if (machine.config->tilemapsConfig)
   {
      retVal = machine.config->tilemapsConfig();
   }
   
   if (0 == retVal)
   {
      // Reset the driver
      machine.config->reset();
      
#ifndef USE_NAMCO51
      ioChip.startEnable = machine.config->ioChipStartEnable;
#endif
   }
   
   return retVal;
}

static void machineReset()
{
	cpus.CPU[CPU1].fireIRQ = 0;
	cpus.CPU[CPU2].fireIRQ = 0;
	cpus.CPU[CPU3].fireIRQ = 0;
	cpus.CPU[CPU2].halt = 0;
	cpus.CPU[CPU3].halt = 0;
   
	machine.flipScreen = 0;
	
#ifdef USE_NAMCO51
   namco51xxReset();
#else
	ioChip.customCommand = 0;
	ioChip.CPU1FireNMI = 0;
	ioChip.mode = 0;
	ioChip.credits = 0;
	ioChip.leftCoinPerCredit = 0;
	ioChip.leftCreditPerCoin = 0;
	ioChip.rightCoinPerCredit = 0;
	ioChip.rightCreditPerCoin = 0;
	ioChip.auxCoinPerCredit = 0;
	ioChip.auxCreditPerCoin = 0;
	for (INT32 i = 0; i < IOCHIP_BUF_SIZE; i ++) 
   {
		ioChip.buffer[i] = 0;
	}
#endif

}

static INT32 DrvDoReset(void)
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
   
	input.ports[0].previous.byte = 0;
	input.ports[1].previous.byte = 0;
	input.ports[2].previous.byte = 0;

   memset(&namco54xx, 0, sizeof(namco54xx));
   
	HiscoreReset();

	return 0;
}

static INT32 namcoMemIndex(void)
{
   struct Memory_Layout_Def *memoryLayout = machine.config->memLayoutTable;
	UINT8 *next = memory.all.start;

   for (UINT32 i = 0; i < machine.config->memLayoutSize; i ++)
   {
      if (next)
      {
         if ((MEM_RAM == memoryLayout[i].type) && (0 == memory.RAM.start))
         {
            memory.RAM.start = next;
         }
         switch (memoryLayout[i].type)
         {
            case MEM_DATA32:
               *(memoryLayout[i].region.uint32) = (UINT32 *)next;
            break;
            
            default:
               *(memoryLayout[i].region.uint8) = next;
            break;
         }
         next += memoryLayout[i].size;
         if ((MEM_RAM == memoryLayout[i].type) && (memory.RAM.size < (next - memory.RAM.start)))
         {
            memory.RAM.size = next - memory.RAM.start;
         }
      }
      else
      {
         memory.all.size += memoryLayout[i].size;
      }
   }
   
	return 0;
}

static INT32 namcoLoadGameROMS(void)
{
   struct ROM_Load_Def *romTable = machine.config->romLayoutTable;
   UINT32 tableSize = machine.config->romLayoutSize;
   UINT32 tempSize = machine.config->tempRomSize;
   INT32 retVal = 1;
   
   bprintf(PRINT_NORMAL, _T("ROM init started %04X\n"), tableSize);

   if (tempSize) tempRom = (UINT8 *)BurnMalloc(tempSize);
   
   if (NULL != tempRom)
   {
      memset(tempRom, 0, tempSize);
      
      retVal = 0;
      
      for (UINT32 idx = 0; ((idx < tableSize) && (0 == retVal)); idx ++)
      {
         bprintf(PRINT_NORMAL, _T("ROM init ... %d %x\n"), idx, *(romTable->address)+romTable->offset);
         
         retVal = BurnLoadRom(*(romTable->address) + romTable->offset, idx, 1);
         if ((0 == retVal) && (NULL != romTable->postProcessing)) 
            retVal = romTable->postProcessing();
         
         romTable ++;
      }
      
      BurnFree(tempRom);
   }
   
	return retVal;
}

static void namco54XXWrite(INT32 dta)
{
	if (namco54xx.fetch) 
   {
		switch (namco54xx.fetchMode) 
      {
			case NAMCO54_WR_CFG3:
				namco54xx.config3[NAMCO54XX_CFG3_SIZE - (namco54xx.fetch --)] = dta;
				break;
            
			case NAMCO54_WR_CFG2:
				namco54xx.config2[NAMCO54XX_CFG2_SIZE - (namco54xx.fetch --)] = dta;
				break;

			case NAMCO54_WR_CFG1:
				namco54xx.config1[NAMCO54XX_CFG1_SIZE - (namco54xx.fetch --)] = dta;
				break;
            
         default:
            if (NAMCO54XX_CFG1_SIZE <= namco54xx.fetch)
            {
               namco54xx.fetch = 1;
            }
            namco54xx.fetchMode = NAMCO54_WR_CFG1;
				namco54xx.config1[NAMCO54XX_CFG1_SIZE - (namco54xx.fetch --)] = dta;
            break;
		}
	} 
   else 
   {
		switch (dta & 0xf0) 
      {
			case 0x00:
				break;

			case 0x10:	// output sound on pins 4-7 only
				if (0 == memcmp(namco54xx.config1,"\x40\x00\x02\xdf",NAMCO54XX_CFG1_SIZE))
					// bosco
					// galaga
					// xevious
               BurnSamplePlay(0);
//				else if (memcmp(namco54xx.config1,"\x10\x00\x80\xff",4) == 0)
					// xevious
//					sample_start(0, 1, 0);
//				else if (memcmp(namco54xx.config1,"\x80\x80\x01\xff",4) == 0)
					// xevious
//					sample_start(0, 2, 0);
				break;

			case 0x20:	// output sound on pins 8-11 only
//				if (memcmp(namco54xx.config2,"\x40\x40\x01\xff",4) == 0)
					// xevious
//					sample_start(1, 3, 0);
//					BurnSamplePlay(1);
				/*else*/ if (0 == memcmp(namco54xx.config2,"\x30\x30\x03\xdf",NAMCO54XX_CFG2_SIZE))
					// bosco
					// galaga
               BurnSamplePlay(1);
//				else if (memcmp(namco54xx.config2,"\x60\x30\x03\x66",4) == 0)
					// polepos
//					sample_start( 0, 0, 0 );
				break;

			case 0x30:
				namco54xx.fetch = NAMCO54XX_CFG1_SIZE;
				namco54xx.fetchMode = NAMCO54_WR_CFG1;
				break;

			case 0x40:
				namco54xx.fetch = NAMCO54XX_CFG2_SIZE;
				namco54xx.fetchMode = NAMCO54_WR_CFG2;
				break;

			case 0x50:	// output sound on pins 17-20 only
//				if (memcmp(namco54xx.config3,"\x08\x04\x21\x00\xf1",5) == 0)
					// bosco
//					sample_start(2, 2, 0);
				break;

			case 0x60:
				namco54xx.fetch = NAMCO54XX_CFG3_SIZE;
				namco54xx.fetchMode = NAMCO54_WR_CFG3;
				break;

			case 0x70:
				// polepos
				/* 0x7n = Screech sound. n = pitch (if 0 then no sound) */
				/* followed by 0x60 command? */
				if (0 == ( dta & 0x0f )) 
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

#ifdef USE_NAMCO51
/* carbon copy of the latest emulation contained in MAME, 
 * except for loading the ROM
 */
static void namco51xxReset(void)
{
	namco51xx.mode = 0;

	namco51xx.leftCoinPerCredit = 1;
	namco51xx.leftCreditPerCoins = 1;
	namco51xx.rightCoinPerCredit = 1;
	namco51xx.rightCreditPerCoins = 1;
	namco51xx.auxCoinPerCredit = 1;
	namco51xx.auxCreditPerCoins = 1;

	namco51xx.credits = 0;
   namco51xx.leftCoinsInserted = 0;
   namco51xx.rightCoinsInserted = 0;

   namco51xx.startEnable = 0;

   namco51xx.remapJoystick = 0;
   
   namco51xx.counter = 0;
}

static UINT8 namco51xxRead(UINT16 offset)
{
   if (0 == namco51xx.mode) /* switch mode */
   {
      switch (namco51xx.counter % 3)
      {
         case 2: return 0;
         case 1: return input.ports[1].current.byte;
         case 0:
         default: return input.ports[0].current.byte;
      }
   }
   else /* credits mode */
   {
      switch (namco51xx.counter % 3)
      {
         case 1:
         {
            UINT8 portValue = input.ports[1].current.byte;
            UINT8 joy = portValue & 0x0f;
            if (namco51xx.remapJoystick) joy = namcoControls[joy];
            
            struct Button_Def *buttonSet = &button[1];
            
            UINT8 buttonsDown = ~(portValue & 0xf0);

            UINT8 toggle = buttonsDown ^ buttonSet->last;
            buttonSet->last = (buttonSet->last & 0x20) | (buttonsDown & 0x10);

            /* fire */
            joy |=  ((toggle & buttonsDown & 0x10)^0x10);
            joy |= (((         buttonsDown & 0x10)^0x10) << 1);

            return joy;
         }
         break;

         case 2:
         {
            UINT8 portValue = input.ports[2].current.byte;
            UINT8 joy = portValue & 0x0f;
            if (namco51xx.remapJoystick) joy = namcoControls[joy];
            
            struct Button_Def *buttonSet = &button[0];
            
            UINT8 buttonsDown = ~(portValue & 0xf0) << 1;

            UINT8 toggle = buttonsDown ^ buttonSet->last;
            buttonSet->last = (buttonSet->last & 0x10) | (buttonsDown & 0x20);

            /* fire */
            joy |= (((toggle & buttonsDown & 0x20)^0x20)>>1);
            joy |=  ((         buttonsDown & 0x20)^0x20);

            return joy;
         }
         break;
         
         case 0:
         default:
         {
            UINT8 in = input.ports[0].current.byte;
            UINT8 toggle = in ^ namco51xx.lastCoin;
            namco51xx.lastCoin = in;

            if (0 < namco51xx.leftCoinPerCredit) 
            {
               if (99 >= namco51xx.credits)
               {
                  if (in & toggle & 0x10)
                  {
                     namco51xx.leftCoinsInserted ++;
                     if (namco51xx.leftCoinsInserted >= namco51xx.leftCoinPerCredit) 
                     {
                        namco51xx.credits += namco51xx.leftCreditPerCoins;
                        namco51xx.leftCoinsInserted -= namco51xx.leftCoinPerCredit;
                     }
                  }
                  if (in & toggle & 0x20)
                  {
                     namco51xx.rightCoinsInserted ++;
                     if (namco51xx.rightCoinsInserted >= namco51xx.rightCoinPerCredit) 
                     {
                        namco51xx.credits += namco51xx.rightCreditPerCoins;
                        namco51xx.rightCoinsInserted -= namco51xx.rightCoinPerCredit;
                     }
                  }
                  if (in & toggle & 0x40)
                  {
                     namco51xx.credits ++;
                  }
               }
            } 
#ifndef FORCE_FREEPLAY
            else 
#endif
            {
               namco51xx.credits = 100;
            }
            
            if (namco51xx.startEnable)
            {
               in = input.ports[0].current.byte;
               if (toggle & in & 0x04)
               {
                  if (1 <= namco51xx.credits) 
                  {
                     namco51xx.credits --;
                     namco51xx.mode = 2;
                  }
               }
               
               else if (toggle & in & 0x08)
               {
                  if (2 <= namco51xx.credits)
                  {
                     namco51xx.credits -= 2;
                     namco51xx.mode = 2;
                  }
               }
            }
            
            if (~in & 0x80)
            {
               return 0xbb;
            }
            
            return (namco51xx.credits / 10) * 16 + namco51xx.credits % 10;
         }
         break;
      }
   }
   
   namco51xx.counter ++;
}

static void namco51xxWrite(UINT16 offset, UINT8 dta)
{
   dta &= 0x07;
   
   if (namco51xx.coinCreditMode)
   {
      switch (namco51xx.coinCreditMode --)
      {
         case 4: namco51xx.leftCoinPerCredit = dta; break;
         case 3: namco51xx.leftCreditPerCoins = dta; break;
         case 2: namco51xx.rightCoinPerCredit = dta; break;
         case 1: namco51xx.rightCreditPerCoins = dta; break;
      }
   }
   else
   {
      switch (dta)
      {
         case 0: // nop
         break;
         
         case 1:
            switch (machine.game)
            {
               case NAMCO_XEVIOUS:
               {
                  namco51xx.coinCreditMode = 6;
                  namco51xx.remapJoystick = 1;
               }
               break;
               
               default:
               {
                  namco51xx.coinCreditMode = 4;
               }
               break;
            }
            namco51xx.credits = 0;
         break;
         
         case 2:
            namco51xx.mode = 1;
            namco51xx.counter = 0;
         break;
         
         case 3:
            namco51xx.remapJoystick = 0;
         break;
         
         case 4:
            namco51xx.remapJoystick = 1;
         break;
         
         case 5:
            namco51xx.mode = 0;
            namco51xx.counter = 0;
         break;
         
         default:
            bprintf(PRINT_ERROR, _T("unknown 51XX command %02x\n"), dta);
         break;
      }
   }
}
#else
static UINT8 updateCoinAndCredit(struct CoinAndCredit_Def *signals)
{
   if (signals)
   {
      UINT8 portNo = signals->leftCoin.portNumber;
      UINT8 in = input.ports[portNo].current.byte & signals->leftCoin.triggerMask;

      if (0 < ioChip.leftCoinPerCredit) 
      {
         if (in != (input.ports[portNo].previous.byte & signals->leftCoin.triggerMask))
         {
            if ( (signals->leftCoin.triggerValue == in ) && 
                 (99 > ioChip.credits) ) 
            {
               ioChip.coinsInserted ++;
               if (ioChip.coinsInserted >= ioChip.leftCoinPerCredit) 
               {
                  ioChip.credits += ioChip.leftCreditPerCoin;
                  ioChip.coinsInserted = 0;
               }
            }
         }
      } 
#ifndef FORCE_FREEPLAY
      else 
#endif
      {
         ioChip.credits = 2;
      }
      
      if (ioChip.startEnable)
      {
         portNo = signals->start1.portNumber;
         in = input.ports[portNo].current.byte & signals->start1.triggerMask;
         if (in != (input.ports[portNo].previous.byte & signals->start1.triggerMask))
         {
            if ( signals->start1.triggerValue == in ) 
            {
               if (ioChip.credits >= 1) ioChip.credits --;
            }
         }
         
         portNo = signals->start2.portNumber;
         in = input.ports[portNo].current.byte & signals->start2.triggerMask;
         if (in != (input.ports[portNo].previous.byte & signals->start2.triggerMask))
         {
            if ( signals->start2.triggerValue == in ) 
            {
               if (ioChip.credits >= 2) ioChip.credits -= 2;
            }
         }
      }
   }
   
   return (ioChip.credits / 10) * 16 + ioChip.credits % 10;
}

/*
 * joy.5 | joy.4 | toggle | in.0 | last.0
 *   1   |   1   |    0   |   0  |  0     (released)
 *   0   |   0   |    1   |   1  |  0     (just pressed)
 *   0   |   1   |    0   |   1  |  1     (held)
 *   1   |   1   |    1   |   0  |  1     (just released)
 */ 
static UINT8 updateJoyAndButtons(UINT16 offset, UINT8 jp)
{
   UINT8 joy = jp & 0x0f;
   
   struct Button_Def *buttonSet = &button[offset & 1];
   
   UINT8 buttonsDown = ~((jp & 0xf0) >> 4);

   UINT8 toggle = buttonsDown ^ buttonSet->last;
   buttonSet->last = (buttonSet->last & 2) | (buttonsDown & 1);

   joy |= ((toggle & buttonsDown & 0x01)^1) << 4;
   joy |= ((buttonsDown & 0x01)^1) << 5;

   return joy;
}
#endif // USE_NAMCO51

static UINT8 namcoZ80ReadDip(UINT16 offset)
{
   UINT8 retVal = input.dip[1].bits.bit[offset] | input.dip[0].bits.bit[offset];
   
   return retVal;
}

static UINT8 namcoZ80ReadIoCmd(UINT16 offset)
{
   return ioChip.customCommand;
}

static UINT8 __fastcall namcoZ80ProgRead(UINT16 addr)
{
   struct CPU_Rd_Table *rdEntry = machine.config->rdAddrList;
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

static void namcoZ80WriteSound(UINT16 Offset, UINT8 dta)
{
   NamcoSoundWrite(Offset, dta);
}

static void namcoZ80WriteCPU1Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU1].fireIRQ = dta & 0x01;
   if (!cpus.CPU[CPU1].fireIRQ) 
   {
      INT32 nActive = ZetGetActive();
      ZetClose();
      ZetOpen(CPU1);
      ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
      ZetClose();
      ZetOpen(nActive);
   }

}

static void namcoZ80WriteCPU2Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU2].fireIRQ = dta & 0x01;
   if (!cpus.CPU[CPU2].fireIRQ) 
   {
      INT32 nActive = ZetGetActive();
      ZetClose();
      ZetOpen(CPU2);
      ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);
      ZetClose();
      ZetOpen(nActive);
   }

}

static void namcoZ80WriteCPU3Irq(UINT16 Offset, UINT8 dta)
{
   cpus.CPU[CPU3].fireIRQ = !(dta & 0x01);

}
		
static void namcoZ80WriteCPUReset(UINT16 Offset, UINT8 dta)
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
      cpus.CPU[CPU2].halt = 1;
      cpus.CPU[CPU3].halt = 1;
      return;
   } 
   else 
   {
      cpus.CPU[CPU2].halt = 0;
      cpus.CPU[CPU3].halt = 0;
   }
}
		
static void namcoZ80WriteIoChip(UINT16 offset, UINT8 dta)
{
#ifndef USE_NAMCO51
   ioChip.buffer[offset] = dta;
#endif

   namco54XXWrite(dta);
}

#ifndef USE_NAMCO51
static void namcoZ80WriteIoCmd(UINT16 offset, UINT8 dta)
{
   ioChip.customCommand = dta;
   ioChip.CPU1FireNMI = 1;
   
   switch (ioChip.customCommand) 
   {
      case 0x10: 
      {
         ioChip.CPU1FireNMI = 0;
      }
      break;
      
      case 0xa1: 
      {
         ioChip.mode = 1;
      }
      break;
      
      case 0xb1: 
      {
         ioChip.credits = 0;
      }
      break;
      
      case 0xc1:
      case 0xe1: 
      {
         ioChip.credits = 0;
         ioChip.mode = 0;
      }
      break;
      
      default:
      break;
   }
   
}
#endif

static void namcoZ80WriteFlipScreen(UINT16 offset, UINT8 dta)
{
   machine.flipScreen = dta & 0x01;
}

static void __fastcall namcoZ80ProgWrite(UINT16 addr, UINT8 dta)
{
   struct CPU_Wr_Table *wrEntry = machine.config->wrAddrList;
   
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

static tilemap_scan ( namco )
{
	if ((col - 2) & 0x20)
   {
		return (row + 2 + (((col - 2) & 0x1f) << 5));
	}

   return col - 2 + ((row + 2) << 5);
}

static void namcoRenderSprites(void)
{
   struct Namco_Sprite_Params spriteParams;
   
	for (INT32 offset = 0; offset < 0x80; offset += 2) 
   {
      if (machine.config->getSpriteParams(&spriteParams, offset))
      {
         INT32 spriteRows = ((spriteParams.flags & ySize) != 0);
         INT32 spriteCols = ((spriteParams.flags & xSize) != 0);
         
         for (INT32 y = 0; y <= spriteRows; y ++) 
         {
            for (INT32 x = 0; x <= spriteCols; x ++) 
            {
               INT32 code = spriteParams.sprite;
               if (spriteRows | spriteCols)
                  code += ((y * 2 + x) ^ (spriteParams.flags & orient));
               INT32 xPos = spriteParams.xStart + spriteParams.xStep * x;
               INT32 yPos = spriteParams.yStart + spriteParams.yStep * y;

               if ((xPos < -15) || (xPos >= nScreenWidth) ) continue;
               if ((yPos < -15) || (yPos >= nScreenHeight)) continue;

               switch (spriteParams.flags & orient)
               {
                  case 3:
                     Render16x16Tile_Mask_FlipXY_Clip(
                        pTransDraw, 
                        code, 
                        xPos, yPos, 
                        spriteParams.colour, 
                        spriteParams.paletteBits, 
                        0, 
                        spriteParams.paletteOffset, 
                        graphics.sprites
                     );
                     break;
                  case 2:
                     Render16x16Tile_Mask_FlipY_Clip(
                        pTransDraw, 
                        code, 
                        xPos, yPos, 
                        spriteParams.colour, 
                        spriteParams.paletteBits, 
                        0, 
                        spriteParams.paletteOffset, 
                        graphics.sprites
                     );
                     break;
                  case 1:
                     Render16x16Tile_Mask_FlipX_Clip(
                        pTransDraw, 
                        code, 
                        xPos, yPos, 
                        spriteParams.colour, 
                        spriteParams.paletteBits, 
                        0, 
                        spriteParams.paletteOffset, 
                        graphics.sprites
                     );
                     break;
                  case 0:
                  default:
                     Render16x16Tile_Mask_Clip(
                        pTransDraw, 
                        code, 
                        xPos, yPos, 
                        spriteParams.colour, 
                        spriteParams.paletteBits, 
                        0, 
                        spriteParams.paletteOffset, 
                        graphics.sprites
                     );
                     break;
               }
            }
         }
      }
	}
}

static INT32 DrvExit(void)
{
	GenericTilesExit();
   
   NamcoSoundExit();
   BurnSampleExit();
   
	ZetExit();

	earom_exit();

   machineReset();
   
	BurnFree(memory.all.start);
	
	machine.game = NAMCO_GALAGA; 

	return 0;
}

static void DrvPreMakeInputs(void) 
{
#ifndef USE_NAMCO51
	// silly bit of code to keep the joystick button pressed for only 1 frame
	// needed for proper pumping action in digdug & highscore name entry.
   struct InputSignal_Def *currentPlayerControls;
   struct InputSignal_Def *previousPlayerControls;
   
   for (INT32 player = PLAYER1; player < NO_OF_PLAYERS; player ++)
   {
      currentPlayerControls  = &input.ports[machine.config->playerControlPort[player]].current;
      previousPlayerControls = &input.ports[machine.config->playerControlPort[player]].previous;
      
      memcpy(&(previousPlayerControls->bits), &(currentPlayerControls->bits), sizeof(struct InputSignalBits_Def));

      previousPlayerControls->bits.bit[4] = 0;
      
      if (currentPlayerControls->bits.bit[4] && !button[player].held) 
      {
         button[player].hold = 1;
         button[player].held = 1;
      } else 
      {
         if (!currentPlayerControls->bits.bit[4]) 
         {
            button[player].held = 0;
         }
      }

      currentPlayerControls->bits.bit[4] = button[player].hold;
      
      if (button[player].hold) 
      {
         button[player].hold --;
      }
   }
#endif
}

static void DrvMakeInputs(void)
{
   struct InputSignal_Def *currentPort0 = &input.ports[0].current;
   struct InputSignal_Def *currentPort1 = &input.ports[1].current;
   struct InputSignal_Def *currentPort2 = &input.ports[2].current;
   
	// Reset Inputs
	currentPort0->byte = 0xff;
	currentPort1->byte = 0xff;
	currentPort2->byte = 0xff;

   switch (machine.game)
   {
      case NAMCO_XEVIOUS:
         // map blaster inputs from ports to dip switches
         input.dip[0].byte |= 0x11;
         if (currentPort1->bits.bit[4]) input.dip[0].byte &= 0xFE; 
         if (currentPort2->bits.bit[4]) input.dip[0].byte &= 0xEF;
         break;
         
      default:
         DrvPreMakeInputs();
         break;
   }
   
	// Compile Digital Inputs
	for (INT32 i = 0; i < 8; i ++) 
   {
		currentPort0->byte -= (currentPort0->bits.bit[i] & 1) << i;
		currentPort1->byte -= (currentPort1->bits.bit[i] & 1) << i;
		currentPort2->byte -= (currentPort2->bits.bit[i] & 1) << i;
      
      input.dip[0].bits.bit[i] = ((input.dip[0].byte >> i) & 1) << 0;
      input.dip[1].bits.bit[i] = ((input.dip[1].byte >> i) & 1) << 1;
 	}
}

static INT32 DrvDraw(void)
{
   BurnTransferClear();

   for (UINT32 drawLayer = 0; drawLayer < machine.config->drawTableSize; drawLayer ++)
   {
      machine.config->drawLayerTable[drawLayer]();
   }
   
   BurnTransferCopy(graphics.palette);

	return 0;
}


static INT32 DrvFrame(void)
{
	
	if (input.reset)
   {
      machine.config->reset();
   }

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
		if (i == (nInterleave-1) && cpus.CPU[CPU1].fireIRQ) 
      {
			ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
		}
#ifndef USE_NAMCO51
		if ( (9 == (i % 10)) && 
           ioChip.CPU1FireNMI ) 
#else
      if (9 == (i % 10)) 
#endif
      {
			ZetNmi();
		}
		ZetClose();
		
		if (!cpus.CPU[CPU2].halt) 
      {
			nCurrentCPU = CPU2;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (i == (nInterleave-1) && cpus.CPU[CPU2].fireIRQ) 
         {
				ZetSetIRQLine(0, CPU_IRQSTATUS_HOLD);
			}
			ZetClose();
		}
		
		if (!cpus.CPU[CPU3].halt) 
      {
			nCurrentCPU = CPU3;
			ZetOpen(nCurrentCPU);
			ZetRun(nCyclesTotal[nCurrentCPU] / nInterleave);
			if (((i == ((64 + 000) * nInterleave) / 272) ||
				 (i == ((64 + 128) * nInterleave) / 272)) && cpus.CPU[CPU3].fireIRQ) 
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
				if (machine.hasSamples)
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
			if (machine.hasSamples)
				BurnSampleRender(pSoundBuf, nSegmentLength);
		}
	}

	if (pBurnDraw)
		BurnDrvRedraw();

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
		ba.Data	  = memory.RAM.start;
		ba.nLen	  = memory.RAM.size;
		ba.szName = "All Ram";
		BurnAcb(&ba);
	}
	
	if (nAction & ACB_DRIVER_DATA) {
		ZetScan(nAction);			// Scan Z80
      
      NamcoSoundScan(nAction, pnMin);
      BurnSampleScan(nAction, pnMin);
      
		// Scan critical driver variables
		SCAN_VAR(cpus.CPU[CPU1].fireIRQ);
		SCAN_VAR(cpus.CPU[CPU2].fireIRQ);
		SCAN_VAR(cpus.CPU[CPU3].fireIRQ);
		SCAN_VAR(cpus.CPU[CPU2].halt);
		SCAN_VAR(cpus.CPU[CPU3].halt);
		SCAN_VAR(machine.flipScreen);
#ifndef USE_NAMCO51
		SCAN_VAR(ioChip.customCommand);
		SCAN_VAR(ioChip.CPU1FireNMI);
		SCAN_VAR(ioChip.mode);
		SCAN_VAR(ioChip.credits);
		SCAN_VAR(ioChip.leftCoinPerCredit);
		SCAN_VAR(ioChip.leftCreditPerCoin);
		SCAN_VAR(ioChip.rightCoinPerCredit);
		SCAN_VAR(ioChip.rightCreditPerCoin);
		SCAN_VAR(ioChip.auxCoinPerCredit);
		SCAN_VAR(ioChip.auxCreditPerCoin);
		SCAN_VAR(ioChip.buffer);
#endif
		SCAN_VAR(input.ports);

		SCAN_VAR(namco54xx.fetch);
		SCAN_VAR(namco54xx.fetchMode);
		SCAN_VAR(namco54xx.config1);
		SCAN_VAR(namco54xx.config2);
		SCAN_VAR(namco54xx.config3);
   }

	return 0;
}

/* === Galaga === */

#ifndef USE_NAMCO51
#define INP_GALAGA_COIN_TRIGGER_1   0x00
#define INP_GALAGA_COIN_MASK_1      0x10
#define INP_GALAGA_COIN_TRIGGER_2   0x00
#define INP_GALAGA_COIN_MASK_2      0x20
#define INP_GALAGA_START_TRIGGER_1  0x00
#define INP_GALAGA_START_MASK_1     0x04
#define INP_GALAGA_START_TRIGGER_2  0x00
#define INP_GALAGA_START_MASK_2     0x08

static struct CoinAndCredit_Def galagaCoinAndCreditParams = 
{
   .leftCoin = 
   {  
      .portNumber    = 0,
      .triggerValue  = INP_GALAGA_COIN_TRIGGER_1,
      .triggerMask   = INP_GALAGA_COIN_MASK_1
   },
   .rightCoin = 
   {
      .portNumber    = 0,
      .triggerValue  = INP_GALAGA_COIN_TRIGGER_2,
      .triggerMask   = INP_GALAGA_COIN_MASK_2
   },
   .auxCoin =
   {
      .portNumber    = 0, 
      .triggerValue  = 0,
      .triggerMask   = 0
   },
   .start1 =
   {
      .portNumber    = 0,
      .triggerValue  = INP_GALAGA_START_TRIGGER_1,
      .triggerMask   = INP_GALAGA_START_MASK_1
   },
   .start2 = 
   {
      .portNumber    = 0,
      .triggerValue  = INP_GALAGA_START_TRIGGER_2,
      .triggerMask   = INP_GALAGA_START_MASK_2
   },
};
#endif

static struct BurnInputInfo GalagaInputList[] =
{
	{"Coin 1"            , BIT_DIGITAL  , &input.ports[0].current.bits.bit[4], "p1 coin"   },
	{"Start 1"           , BIT_DIGITAL  , &input.ports[0].current.bits.bit[2], "p1 start"  },
	{"Coin 2"            , BIT_DIGITAL  , &input.ports[0].current.bits.bit[5], "p2 coin"   },
	{"Start 2"           , BIT_DIGITAL  , &input.ports[0].current.bits.bit[3], "p2 start"  },

	{"Left"              , BIT_DIGITAL  , &input.ports[1].current.bits.bit[3], "p1 left"   },
	{"Right"             , BIT_DIGITAL  , &input.ports[1].current.bits.bit[1], "p1 right"  },
	{"Fire 1"            , BIT_DIGITAL  , &input.ports[1].current.bits.bit[4], "p1 fire 1" },
	
	{"Left (Cocktail)"   , BIT_DIGITAL  , &input.ports[2].current.bits.bit[3], "p2 left"   },
	{"Right (Cocktail)"  , BIT_DIGITAL  , &input.ports[2].current.bits.bit[1], "p2 right"  },
	{"Fire 1 (Cocktail)" , BIT_DIGITAL  , &input.ports[2].current.bits.bit[4], "p2 fire 1" },

	{"Reset"             , BIT_DIGITAL  , &input.reset,                    "reset"     },
	{"Service"           , BIT_DIGITAL  , &input.ports[0].current.bits.bit[6], "service"   },
	{"Dip 1"             , BIT_DIPSWITCH, &input.dip[0].byte,              "dip"       },
	{"Dip 2"             , BIT_DIPSWITCH, &input.dip[1].byte,              "dip"       },
};

STDINPUTINFO(Galaga)

#define GALAGA_NUM_OF_DIPSWITCHES      2

static struct BurnDIPInfo GalagaDIPList[]=
{
   // offset
	{  0x0b,    0xf0,    0x01,    0x01,       NULL                     },

	// Default Values
   // nOffset, nID,     nMask,   nDefault,   NULL
	{  0x00,    0xff,    0x01,    0x01,       NULL                     },
	{  0x01,    0xff,    0xff,    0x97,       NULL                     },
	{  0x02,    0xff,    0xbf,    0xb7,       NULL                     },
	
	// Service Switch
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Service Mode"           },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x01,    0x01,       "Off"                    },
	{  0x00,    0x01,    0x01,    0x00,       "On"                     },

	// Dip 1	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Coinage"                },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x07,    0x04,       "4 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x02,       "3 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x06,       "2 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x07,       "1 Coin  1 Play"         },
	{  0x01,    0x01,    0x07,    0x01,       "2 Coins 3 Plays"        },
	{  0x01,    0x01,    0x07,    0x03,       "1 Coin  2 Plays"        },
	{  0x01,    0x01,    0x07,    0x05,       "1 Coin  3 Plays"        },
	{  0x01,    0x01,    0x07,    0x00,       "Freeplay"               },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Bonus Life"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x38,    0x20,       "20k  60k  60k"          },
   {  0x01,    0x01,    0x38,    0x18,       "20k  60k"               },
	{  0x01,    0x01,    0x38,    0x10,       "20k  70k  70k"          },
	{  0x01,    0x01,    0x38,    0x30,       "20k  80k  80k"          },
	{  0x01,    0x01,    0x38,    0x38,       "30k  80k"               },
	{  0x01,    0x01,    0x38,    0x08,       "30k 100k 100k"          },
	{  0x01,    0x01,    0x38,    0x28,       "30k 120k 120k"          },
	{  0x01,    0x01,    0x38,    0x00,       "None"                   },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Lives"                  },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0xc0,    0x00,       "2"                      },
	{  0x01,    0x01,    0xc0,    0x80,       "3"                      },
	{  0x01,    0x01,    0xc0,    0x40,       "4"                      },
	{  0x01,    0x01,    0xc0,    0xc0,       "5"                      },

	// Dip 2
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Difficulty"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x03,    0x03,       "Easy"                   },
	{  0x02,    0x01,    0x03,    0x00,       "Medium"                 },
	{  0x02,    0x01,    0x03,    0x01,       "Hard"                   },
	{  0x02,    0x01,    0x03,    0x02,       "Hardest"                },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Demo Sounds"            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x08,    0x08,       "Off"                    },
   {  0x02,    0x01,    0x08,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Freeze"                 },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x10,    0x10,       "Off"                    },
	{  0x02,    0x01,    0x10,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Rack Test"              },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x20,    0x20,       "Off"                    },
	{  0x02,    0x01,    0x20,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
   {  0,       0xfe,    0,       2,          "Cabinet"                },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x80,    0x80,       "Upright"                },
	{  0x02,    0x01,    0x80,    0x00,       "Cocktail"               },
	};

STDDIPINFO(Galaga)

static struct BurnDIPInfo GalagamwDIPList[]=
{
   // Offset
	{  0x0b,    0xf0,    0x01,    0x01,       NULL                     },

	// Default Values
   // nOffset, nID,     nMask,   nDefault,   NULL
	{  0x00,    0xff,    0x01,    0x01,       NULL                     },
	{  0x01,    0xff,    0xff,    0x97,       NULL                     },
	{  0x02,    0xff,    0xff,    0xf7,       NULL                     },
	
	// Service Switch
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Service Mode"           },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x01,    0x01,       "Off"                    },
	{  0x00,    0x01,    0x01,    0x00,       "On"                     },

	// Dip 1	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Coinage"                },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x07,    0x04,       "4 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x02,       "3 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x06,       "2 Coins 1 Play"         },
	{  0x01,    0x01,    0x07,    0x07,       "1 Coin  1 Play"         },
	{  0x01,    0x01,    0x07,    0x01,       "2 Coins 3 Plays"        },
	{  0x01,    0x01,    0x07,    0x03,       "1 Coin  2 Plays"        },
	{  0x01,    0x01,    0x07,    0x05,       "1 Coin  3 Plays"        },
   {  0x01,    0x01,    0x07,    0x00,       "Freeplay"               },	
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Bonus Life"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x38,    0x20,       "20k  60k  60k"          },
	{  0x01,    0x01,    0x38,    0x18,       "20k  60k"               },
	{  0x01,    0x01,    0x38,    0x10,       "20k  70k  70k"          },
	{  0x01,    0x01,    0x38,    0x30,       "20k  80k  80k"          },
   {  0x01,    0x01,    0x38,    0x38,       "30k  80k"               },
	{  0x01,    0x01,    0x38,    0x08,       "30k 100k 100k"          },
	{  0x01,    0x01,    0x38,    0x28,       "30k 120k 120k"          },
	{  0x01,    0x01,    0x38,    0x00,       "None"                   },	
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Lives"                  },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0xc0,    0x00,       "2"                      },
	{  0x01,    0x01,    0xc0,    0x80,       "3"                      },
	{  0x01,    0x01,    0xc0,    0x40,       "4"                      },
	{  0x01,    0x01,    0xc0,    0xc0,       "5"                      },

	// Dip 2
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "2 Credits Game"         },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x01,    0x00,       "1 Player"               },
	{  0x02,    0x01,    0x01,    0x01,       "2 Players"              },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Difficulty"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x06,    0x06,       "Easy"                   },
	{  0x02,    0x01,    0x06,    0x00,       "Medium"                 },
	{  0x02,    0x01,    0x06,    0x02,       "Hard"                   },
	{  0x02,    0x01,    0x06,    0x04,       "Hardest"                },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Demo Sounds"            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x08,    0x08,       "Off"                    },
	{  0x02,    0x01,    0x08,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Freeze"                 },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x10,    0x10,       "Off"                    },
	{  0x02,    0x01,    0x10,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Rack Test"              },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x20,    0x20,       "Off"                    },
	{  0x02,    0x01,    0x20,    0x00,       "On"                     },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Cabinet"                },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x02,    0x01,    0x80,    0x80,       "Upright"                },
	{  0x02,    0x01,    0x80,    0x00,       "Cocktail"               },
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

static INT32 galagaInit(void);
static INT32 gallagInit(void);
static INT32 galagaReset(void);

static void galagaMemoryMap1(void);
static void galagaMemoryMap2(void);
static void galagaMemoryMap3(void);
static tilemap_callback(galaga_fg);
static INT32 galagaCharDecode(void);
static INT32 galagaSpriteDecode(void);
static INT32 galagaTilemapConfig(void);

#ifndef USE_NAMCO51
static UINT8 galagaZ80ReadInputs(UINT16 offset);

static void galagaZ80Write7007(UINT16 offset, UINT8 dta);
#endif

static void galagaZ80WriteStars(UINT16 offset, UINT8 dta);

static void galagaCalcPalette(void);
static void galagaInitStars(void);
static void galagaRenderStars(void);
static void galagaStarScroll(void);
static void galagaRenderChars(void);

static UINT32 galagaGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset);

static INT32 galagaScan(INT32 nAction, INT32 *pnMin);

#define GALAGA_PALETTE_SIZE_CHARS         0x100
#define GALAGA_PALETTE_SIZE_SPRITES       0x100
#define GALAGA_PALETTE_SIZE_BGSTARS       0x040
#define GALAGA_PALETTE_SIZE (GALAGA_PALETTE_SIZE_CHARS + \
                             GALAGA_PALETTE_SIZE_SPRITES + \
                             GALAGA_PALETTE_SIZE_BGSTARS)

#define GALAGA_PALETTE_OFFSET_CHARS       0
#define GALAGA_PALETTE_OFFSET_SPRITE      (GALAGA_PALETTE_OFFSET_CHARS + GALAGA_PALETTE_SIZE_CHARS)
#define GALAGA_PALETTE_OFFSET_BGSTARS     (GALAGA_PALETTE_OFFSET_SPRITE + GALAGA_PALETTE_SIZE_SPRITES)

#define GALAGA_NUM_OF_CHAR                0x100
#define GALAGA_SIZE_OF_CHAR_IN_BYTES      0x80

#define GALAGA_NUM_OF_SPRITE              0x80
#define GALAGA_SIZE_OF_SPRITE_IN_BYTES    0x200

#define STARS_CTRL_NUM     6

struct Stars_Def
{
   UINT32 scrollX;
   UINT32 scrollY;
   UINT8 control[STARS_CTRL_NUM];
};

static struct Stars_Def stars = { 0 };

struct Star_Def 
{
	UINT16 x;
   UINT16 y;
	UINT8 colour;
   UINT8 set;
};

#define MAX_STARS 252
static struct Star_Def *starSeedTable;

static struct CPU_Config_Def galagaCPU[NAMCO_BRD_CPU_COUNT] =
{
   {  
      /* CPU ID = */          CPU1, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  galagaMemoryMap1
   },
   {  
      /* CPU ID = */          CPU2, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  galagaMemoryMap2
   },
   {  
      /* CPU ID = */          CPU3, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite, 
      /* Memory Mapping = */  galagaMemoryMap3
   },
};
   
static struct CPU_Rd_Table galagaReadTable[] =
{
   { 0x6800, 0x6807, namcoZ80ReadDip         },
#ifndef USE_NAMCO51
   { 0x7000, 0x7002, galagaZ80ReadInputs     },
#else
   { 0x7000, 0x7002, namco51xxRead           },
#endif
   { 0x7100, 0x7100, namcoZ80ReadIoCmd       },
   { 0x0000, 0x0000, NULL                    }, // End of Table marker
};

static struct CPU_Wr_Table galagaWriteTable[] =
{
   { 0x6800, 0x681f, namcoZ80WriteSound      },
   { 0x6820, 0x6820, namcoZ80WriteCPU1Irq    },
   { 0x6821, 0x6821, namcoZ80WriteCPU2Irq    },
   { 0x6822, 0x6822, namcoZ80WriteCPU3Irq    },
   { 0x6823, 0x6823, namcoZ80WriteCPUReset   },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
   { 0x7000, 0x700f, namcoZ80WriteIoChip     },
#ifndef USE_NAMCO51
   { 0x7007, 0x7007, galagaZ80Write7007      },
#else
   { 0x7000, 0x7007, namco51xxWrite          },
#endif
   { 0x7100, 0x7100, namcoZ80WriteIoCmd      },
   { 0xa000, 0xa005, galagaZ80WriteStars     },
   { 0xa007, 0xa007, namcoZ80WriteFlipScreen },
   { 0x0000, 0x0000, NULL                    }, // End of Table marker
};

static struct Memory_Layout_Def galagaMemTable[] = 
{
	{  &memory.Z80.rom1,           0x04000,                               MEM_PGM        },
	{  &memory.Z80.rom2,           0x04000,                               MEM_PGM        },
	{  &memory.Z80.rom3,           0x04000,                               MEM_PGM        },
	{  &memory.PROM.palette,       0x00020,                               MEM_ROM        },
	{  &memory.PROM.charLookup,    0x00100,                               MEM_ROM        },
	{  &memory.PROM.spriteLookup,  0x00100,                               MEM_ROM        },
	{  &NamcoSoundProm,            0x00200,                               MEM_ROM        },
   
	{  &memory.RAM.video,          0x00800,                               MEM_RAM        },
	{  &memory.RAM.shared1,        0x00400,                               MEM_RAM        },
	{  &memory.RAM.shared2,        0x00400,                               MEM_RAM        },
	{  &memory.RAM.shared3,        0x00400,                               MEM_RAM        },

	{  (UINT8 **)&starSeedTable,   sizeof(struct Star_Def) * MAX_STARS,   MEM_DATA       },
	{  &graphics.fgChars,          GALAGA_NUM_OF_CHAR * 8 * 8,            MEM_DATA       },
	{  &graphics.sprites,          GALAGA_NUM_OF_SPRITE * 16 * 16,        MEM_DATA       },
	{  (UINT8 **)&graphics.palette, GALAGA_PALETTE_SIZE * sizeof(UINT32),  MEM_DATA32     },
};

#define GALAGA_MEM_TBL_SIZE      (sizeof(galagaMemTable) / sizeof(struct Memory_Layout_Def))

static struct ROM_Load_Def galagaROMTable[] =
{
	{  &memory.Z80.rom1,             0x00000,    NULL                          },
	{  &memory.Z80.rom1,             0x01000,    NULL                          },
	{  &memory.Z80.rom1,             0x02000,    NULL                          },
	{  &memory.Z80.rom1,             0x03000,    NULL                          },
	{  &memory.Z80.rom2,             0x00000,    NULL                          },
	{  &memory.Z80.rom3,             0x00000,    NULL                          },
	{  &tempRom,                     0x00000,    galagaCharDecode              },
   {  &tempRom,                     0x00000,    NULL                          },
	{  &tempRom,                     0x01000,    galagaSpriteDecode            },
   {  &memory.PROM.palette,         0x00000,    NULL                          },
	{  &memory.PROM.charLookup,      0x00000,    NULL                          },
	{  &memory.PROM.spriteLookup,    0x00000,    NULL                          },
	{  &NamcoSoundProm,              0x00000,    namcoMachineInit              }

};

#define GALAGA_ROM_TBL_SIZE      (sizeof(galagaROMTable) / sizeof(struct ROM_Load_Def))

static struct ROM_Load_Def gallagROMTable[] =
{
	{  &memory.Z80.rom1,             0x00000,    NULL                          },
	{  &memory.Z80.rom1,             0x01000,    NULL                          },
	{  &memory.Z80.rom1,             0x02000,    NULL                          },
	{  &memory.Z80.rom1,             0x03000,    NULL                          },
	{  &memory.Z80.rom2,             0x00000,    NULL                          },
	{  &memory.Z80.rom3,             0x00000,    NULL                          },
	{  &tempRom,                     0x00000,    galagaCharDecode              },
	{  &tempRom,                     0x00000,    NULL                          },
	{  &tempRom,                     0x01000,    galagaSpriteDecode            },
	{  &memory.PROM.palette,         0x00000,    NULL                          },
	{  &memory.PROM.charLookup,      0x00000,    NULL                          },
	{  &memory.PROM.spriteLookup,    0x00000,    NULL                          },
	{  &NamcoSoundProm,              0x00000,    namcoMachineInit              },
};

#define GALLAG_ROM_TBL_SIZE      (sizeof(gallagROMTable) / sizeof(struct ROM_Load_Def))

static DrawFunc_t galagaDrawFuncs[] = 
{
   galagaCalcPalette,
   galagaRenderChars,
	galagaRenderStars,
	namcoRenderSprites,	
   galagaStarScroll,
};

#define GALAGA_DRAW_TBL_SIZE  (sizeof(galagaDrawFuncs) / sizeof(galagaDrawFuncs[0]))

static struct Machine_Config_Def galagaMachineConfig =
{
   .cpus                = galagaCPU,
   .wrAddrList          = galagaWriteTable,
   .rdAddrList          = galagaReadTable,
   .memLayoutTable      = galagaMemTable,
   .memLayoutSize       = GALAGA_MEM_TBL_SIZE,
   .romLayoutTable      = galagaROMTable,
   .romLayoutSize       = GALAGA_ROM_TBL_SIZE,
   .tempRomSize         = 0x2000,
   .tilemapsConfig      = galagaTilemapConfig,
   .drawLayerTable      = galagaDrawFuncs,
   .drawTableSize       = GALAGA_DRAW_TBL_SIZE,
   .getSpriteParams     = galagaGetSpriteParams,
   .reset               = galagaReset,
#ifndef USE_NAMCO51
   .ioChipStartEnable   = 1,
#endif
   .playerControlPort   = 
   {  
      /* Player 1 Port = */      1, 
      /* Player 2 Port = */      2
   }
};

static INT32 galagaInit(void)
{
   machine.game = NAMCO_GALAGA;
   machine.numOfDips = GALAGA_NUM_OF_DIPSWITCHES;
   
   machine.config = &galagaMachineConfig;
   
   return namcoInitBoard();
}

static struct Machine_Config_Def gallagMachineConfig =
{
   .cpus                = galagaCPU,
   .wrAddrList          = galagaWriteTable,
   .rdAddrList          = galagaReadTable,
   .memLayoutTable      = galagaMemTable,
   .memLayoutSize       = GALAGA_MEM_TBL_SIZE,
   .romLayoutTable      = gallagROMTable,
   .romLayoutSize       = GALLAG_ROM_TBL_SIZE,
   .tempRomSize         = 0x2000,
   .tilemapsConfig      = galagaTilemapConfig,
   .drawLayerTable      = galagaDrawFuncs,
   .drawTableSize       = GALAGA_DRAW_TBL_SIZE,
   .getSpriteParams     = galagaGetSpriteParams,
   .reset               = galagaReset,
#ifndef USE_NAMCO51
   .ioChipStartEnable   = 1,
#endif
   .playerControlPort   = 
   {  
      /* Player 1 Port = */      1, 
      /* Player 2 Port = */      2
   }
};

static INT32 gallagInit(void)
{
   machine.game = NAMCO_GALAGA;
   machine.numOfDips = GALAGA_NUM_OF_DIPSWITCHES;
   
   machine.config = &gallagMachineConfig;
   
   return namcoInitBoard();
}

static INT32 galagaReset(void)
{
   for (INT32 i = 0; i < STARS_CTRL_NUM; i ++)
   {
		stars.control[i] = 0;
	}
	stars.scrollX = 0;
	stars.scrollY = 0;
	
   return DrvDoReset();
}

static void galagaMemoryMap1(void)
{
   ZetMapMemory(memory.Z80.rom1,    0x0000, 0x3fff, MAP_ROM);
   ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static void galagaMemoryMap2(void)
{
   ZetMapMemory(memory.Z80.rom2,    0x0000, 0x3fff, MAP_ROM);
   ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static void galagaMemoryMap3(void)
{
   ZetMapMemory(memory.Z80.rom3,    0x0000, 0x3fff, MAP_ROM);
   ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static tilemap_callback ( galaga_fg )
{
   INT32 code   = memory.RAM.video[offs + 0x000] & 0x7f;
   INT32 colour = memory.RAM.video[offs + 0x400] & 0x3f;

	TILE_SET_INFO(0, code, colour, 0);
}

static INT32 galagaCharDecode(void)
{
	GfxDecode(
      GALAGA_NUM_OF_CHAR, 
      NAMCO_2BIT_PALETTE_BITS, 
      8, 8, 
      (INT32*)planeOffsets2Bit, 
      (INT32*)xOffsets8x8Tiles2Bit, 
      (INT32*)yOffsets8x8Tiles2Bit, 
      GALAGA_SIZE_OF_CHAR_IN_BYTES, 
      tempRom, 
      graphics.fgChars
   );

   return 0;
}

static INT32 galagaSpriteDecode(void)
{
	GfxDecode(
      GALAGA_NUM_OF_SPRITE, 
      NAMCO_2BIT_PALETTE_BITS, 
      16, 16, 
      (INT32*)planeOffsets2Bit, 
      (INT32*)xOffsets16x16Tiles2Bit, 
      (INT32*)yOffsets16x16Tiles2Bit, 
      GALAGA_SIZE_OF_SPRITE_IN_BYTES, 
      tempRom, 
      graphics.sprites
   );
   
   return 0;
}

static INT32 galagaTilemapConfig(void)
{
   GenericTilemapInit(
      0, //TILEMAP_FG,
      namco_map_scan, 
      galaga_fg_map_callback, 
      8, 8, 
      NAMCO_TMAP_WIDTH, NAMCO_TMAP_HEIGHT
   );

   GenericTilemapSetGfx(
      0, //TILEMAP_FG,
      graphics.fgChars, 
      NAMCO_2BIT_PALETTE_BITS, 
      8, 8, 
      (GALAGA_NUM_OF_CHAR * 8 * 8), 
      0x0, 
      (GALAGA_PALETTE_SIZE_CHARS - 1)
   );

	GenericTilemapSetTransparent(0, 0);
   
	GenericTilemapSetOffsets(TMAP_GLOBAL, 0, 0);
   
   return 0;
}

#ifndef USE_NAMCO51
static UINT8 galagaZ80ReadInputs(UINT16 offset)
{
   UINT8 retVal = 0xff;
   
   if ( (0x71 == ioChip.customCommand) ||
        (0xb1 == ioChip.customCommand) )
   {
      switch (offset)
      {
         case 0:
         {
            if (ioChip.mode) 
            {
               retVal = input.ports[0].current.byte;
            } 
            else 
            {
               retVal = updateCoinAndCredit(&galagaCoinAndCreditParams);
            }
            input.ports[0].previous.byte = input.ports[0].current.byte;
         }
         break;
            
         case 1:
         case 2:
         {
            retVal = updateJoyAndButtons(offset, input.ports[offset].current.byte);
         }   
         break;
            
         default:
            break;
      }
   }
   
   return retVal;
}

static void galagaZ80Write7007(UINT16 offset, UINT8 dta)
{
   if (0xe1 == ioChip.customCommand) 
   {
      ioChip.leftCoinPerCredit = ioChip.buffer[1];
      ioChip.leftCreditPerCoin = ioChip.buffer[2];
   }
   
   return;
}
#endif

static void galagaZ80WriteStars(UINT16 offset, UINT8 dta)
{
   stars.control[offset] = dta & 0x01;
}

#define GALAGA_3BIT_PALETTE_SIZE    32
#define GALAGA_2BIT_PALETTE_SIZE    64

static void galagaCalcPalette(void)
{
	UINT32 palette3Bit[GALAGA_3BIT_PALETTE_SIZE];
	
	for (INT32 i = 0; i < GALAGA_3BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.palette[i] >> 5) & 0x06];
      
		palette3Bit[i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_CHARS; i ++) 
   {
		graphics.palette[GALAGA_PALETTE_OFFSET_CHARS + i] = 
         palette3Bit[((memory.PROM.charLookup[i]) & 0x0f) + 0x10];
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_SPRITES; i ++) 
   {
		graphics.palette[GALAGA_PALETTE_OFFSET_SPRITE + i] = 
         palette3Bit[memory.PROM.spriteLookup[i] & 0x0f];
	}
	
	UINT32 palette2Bit[GALAGA_2BIT_PALETTE_SIZE];

	for (INT32 i = 0; i < GALAGA_2BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour2Bit[(i >> 0) & 0x03];
      INT32 g = Colour2Bit[(i >> 2) & 0x03];
      INT32 b = Colour2Bit[(i >> 4) & 0x03];
      
		palette2Bit[i] = BurnHighCol(r, g, b, 0);
	}
	
	for (INT32 i = 0; i < GALAGA_PALETTE_SIZE_BGSTARS; i ++) 
   {
		graphics.palette[GALAGA_PALETTE_OFFSET_BGSTARS + i] = 
         palette2Bit[i];
	}

	galagaInitStars();
}

static void galagaInitStars(void)
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

				starSeedTable[idx].x = cnt % 256;
				starSeedTable[idx].y = cnt / 256;
				starSeedTable[idx].colour = clr;
				starSeedTable[idx].set = sf;
				++ idx;
			}

			// update the LFSR
			if (i & 1) i = (i >> 1) ^ feed;
			else       i = (i >> 1);
		}
	}
}

static void galagaRenderStars(void)
{
	if (1 == stars.control[5]) 
   {
		INT32 setA = stars.control[3];
		INT32 setB = stars.control[4] | 0x02;

		for (INT32 starCounter = 0; starCounter < MAX_STARS; starCounter ++) 
      {
			if ( (setA == starSeedTable[starCounter].set) || 
              (setB == starSeedTable[starCounter].set) ) 
         {
				INT32 x = (                      starSeedTable[starCounter].x + stars.scrollX) % 256 + 16;
				INT32 y = ((nScreenHeight / 2) + starSeedTable[starCounter].y + stars.scrollY) % 256;

				if ( (x >= 0) && (x < nScreenWidth)  && 
                 (y >= 0) && (y < nScreenHeight) ) 
            {
					pTransDraw[(y * nScreenWidth) + x] = starSeedTable[starCounter].colour + GALAGA_PALETTE_OFFSET_BGSTARS;
				}
			}

		}
	}
}

static void galagaStarScroll(void)
{
   static const INT32 speeds[8] = { -1, -2, -3, 0, 3, 2, 1, 0 };

   stars.scrollX += speeds[stars.control[0] + (stars.control[1] * 2) + (stars.control[2] * 4)];
}

static void galagaRenderChars(void)
{
	GenericTilemapSetScrollX(0, 0);
	GenericTilemapSetScrollY(0, 0);
   GenericTilemapSetEnable(0, 1);
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_TRANSPARENT);
}

static UINT32 galagaGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset)
{
	UINT8 *spriteRam1 = memory.RAM.shared1 + 0x380;
	UINT8 *spriteRam2 = memory.RAM.shared2 + 0x380;
	UINT8 *spriteRam3 = memory.RAM.shared3 + 0x380;

   spriteParams->sprite =    spriteRam1[offset + 0] & 0x7f;
   spriteParams->colour =    spriteRam1[offset + 1] & 0x3f;
   
   spriteParams->xStart =    spriteRam2[offset + 1] - 40 + (0x100 * (spriteRam3[offset + 1] & 0x03));
   spriteParams->yStart =    NAMCO_SCREEN_WIDTH - spriteRam2[offset + 0] + 1;
   spriteParams->xStep =     16;
   spriteParams->yStep =     16;
   
   spriteParams->flags =     spriteRam3[offset + 0] & 0x0f;
   
   if (spriteParams->flags & ySize)
   {
      if (spriteParams->flags & yFlip)
      {
         spriteParams->yStep = -16;
      }
      else
      {
         spriteParams->yStart -= 16;
      }
   }
   
   if (spriteParams->flags & xSize)
   {
      if (spriteParams->flags & xFlip)
      {
         spriteParams->xStart += 16;
         spriteParams->xStep  = -16;
      }
   }
   
   spriteParams->paletteBits   = NAMCO_2BIT_PALETTE_BITS;
   spriteParams->paletteOffset = GALAGA_PALETTE_OFFSET_SPRITE;
   
   return 1;
}

static INT32 galagaScan(INT32 nAction, INT32 *pnMin)
{
   if (nAction & ACB_DRIVER_DATA) {
		SCAN_VAR(stars.scrollX);
		SCAN_VAR(stars.scrollY);
		SCAN_VAR(stars.control);
   }
   
   return DrvScan(nAction, pnMin);
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
   /* Init func = */                         	galagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
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
   /* Init func = */                            galagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
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
   /* Init func = */                         	galagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
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
	/* Init func = */                            galagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
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
   /* Init func = */                            galagaInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan,
   /* Recalc Palette = */                       NULL, 
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
   /* Init func = */                            gallagInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
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
   /* Init func = */                            gallagInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        galagaScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                GALAGA_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

/* === Dig Dug === */

#ifndef USE_NAMCO51
#define INP_DIGDUG_COIN_TRIGGER     0x00
#define INP_DIGDUG_COIN_MASK        0x01
#define INP_DIGDUG_START_TRIGGER_1  0x00
#define INP_DIGDUG_START_MASK_1     0x10
#define INP_DIGDUG_START_TRIGGER_2  0x00
#define INP_DIGDUG_START_MASK_2     0x20

static struct CoinAndCredit_Def digdugCoinAndCreditParams = 
{
   /*.leftCoin = 
   {
      .portNumber    = 0,
      .triggerValue  = INP_DIGDUG_COIN_TRIGGER,
      .triggerMask   = INP_DIGDUG_COIN_MASK
   },*/
   {  0, INP_DIGDUG_COIN_TRIGGER,      INP_DIGDUG_COIN_MASK       },
   {  0, INP_DIGDUG_COIN_TRIGGER,      INP_DIGDUG_COIN_MASK       },
   {  0, 0,                            0                          },
   /*.start1 =
   {
      .portNumber    = 0,
      .triggerValue  = INP_DIGDUG_START_TRIGGER_1,
      .triggerMask   = INP_DIGDUG_START_MASK_1,
   },*/
   {  0, INP_DIGDUG_START_TRIGGER_1,   INP_DIGDUG_START_MASK_1,   },
   /*.start2 = 
   {
      .portNumber    = 0,
      .triggerValue  = INP_DIGDUG_START_TRIGGER_2,
      .triggerMask   = INP_DIGDUG_START_MASK_2,
   }*/
   {  0, INP_DIGDUG_START_TRIGGER_2,   INP_DIGDUG_START_MASK_2,   },
};
#endif

static struct BurnInputInfo DigdugInputList[] =
{
	{"P1 Coin"              , BIT_DIGITAL  , &input.ports[0].current.bits.bit[0], "p1 coin"   },
	{"P1 Start"             , BIT_DIGITAL  , &input.ports[0].current.bits.bit[4], "p1 start"  },
	{"P2 Coin"              , BIT_DIGITAL  , &input.ports[0].current.bits.bit[1], "p2 coin"   },
	{"P2 Start"             , BIT_DIGITAL  , &input.ports[0].current.bits.bit[5], "p2 start"  },

	{"P1 Up"                , BIT_DIGITAL  , &input.ports[1].current.bits.bit[0], "p1 up"     },
	{"P1 Down"              , BIT_DIGITAL  , &input.ports[1].current.bits.bit[2], "p1 down"   },
	{"P1 Left"              , BIT_DIGITAL  , &input.ports[1].current.bits.bit[3], "p1 left"   },
	{"P1 Right"             , BIT_DIGITAL  , &input.ports[1].current.bits.bit[1], "p1 right"  },
	{"P1 Fire 1"            , BIT_DIGITAL  , &input.ports[1].current.bits.bit[4], "p1 fire 1" },
	
	{"P2 Up"                , BIT_DIGITAL  , &input.ports[2].current.bits.bit[0], "p2 up"     },
	{"P2 Down"              , BIT_DIGITAL  , &input.ports[2].current.bits.bit[2], "p2 down"   },
	{"P2 Left (Cocktail)"   , BIT_DIGITAL  , &input.ports[2].current.bits.bit[3], "p2 left"   },
	{"P2 Right (Cocktail)"  , BIT_DIGITAL  , &input.ports[2].current.bits.bit[1], "p2 right"  },
	{"P2 Fire 1 (Cocktail)" , BIT_DIGITAL  , &input.ports[2].current.bits.bit[4], "p2 fire 1" },

	{"Service"              , BIT_DIGITAL  , &input.ports[0].current.bits.bit[7], "service"   },
	{"Reset"                , BIT_DIGITAL  , &input.reset,                  "reset"     },
	{"Dip 1"                , BIT_DIPSWITCH, &input.dip[0].byte,            "dip"       },
	{"Dip 2"                , BIT_DIPSWITCH, &input.dip[1].byte,            "dip"       },
};

STDINPUTINFO(Digdug)

#define DIGDUG_NUM_OF_DIPSWITCHES      2

static struct BurnDIPInfo DigdugDIPList[]=
{
 	{  0x10,    0xf0,    0xff,    0xa1,       NULL		               },
   
  // nOffset, nID,     nMask,   nDefault,   NULL
	{  0x00,    0xff,    0xff,    0xa1,       NULL		               },
	{  0x01,    0xff,    0xff,    0x24,       NULL		               },

	// Dip 1	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
   {  0,       0xfe,    0,       8,          "Coin B"		            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x07,    0x07,       "3 Coins 1 Credits"		},
	{  0x00,    0x01,    0x07,    0x03,       "2 Coins 1 Credits"		},
	{  0x00,    0x01,    0x07,    0x05,       "2 Coins 3 Credits"		},
	{  0x00,    0x01,    0x07,    0x01,       "1 Coin  1 Credits"		},
	{  0x00,    0x01,    0x07,    0x06,       "1 Coin  2 Credits"		},
	{  0x00,    0x01,    0x07,    0x02,       "1 Coin  3 Credits"		},
	{  0x00,    0x01,    0x07,    0x04,       "1 Coin  6 Credits"		},
	{  0x00,    0x01,    0x07,    0x00,       "1 Coin  7 Credits"		},

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Bonus Life (1,2,3) / (5)"		      },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x38,    0x20,       "10K & Every 40K / 20K & Every 60K"	},
	{  0x00,    0x01,    0x38,    0x10,       "10K & Every 50K / 30K & Every 80K"	},
	{  0x00,    0x01,    0x38,    0x30,       "20K & Every 60K / 20K & 50K Only"	},
	{  0x00,    0x01,    0x38,    0x08,       "20K & Every 70K / 20K & 60K Only"	},
	{  0x00,    0x01,    0x38,    0x28,       "10K & 40K Only  / 30K & 70K Only"	},
	{  0x00,    0x01,    0x38,    0x18,       "20K & 60K Only  / 20K Only"  		},
	{  0x00,    0x01,    0x38,    0x38,       "10K Only        / 30K Only"        },
	{  0x00,    0x01,    0x38,    0x00,       "None            / None"		      },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Lives"		            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0xc0,    0x00,       "1"		               },
	{  0x00,    0x01,    0xc0,    0x40,       "2"		               },
	{  0x00,    0x01,    0xc0,    0x80,       "3"		               },
	{  0x00,    0x01,    0xc0,    0xc0,       "5"		               },

   // DIP 2
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Coin A"		            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0xc0,    0x40,       "2 Coins 1 Credits"		},
	{  0x01,    0x01,    0xc0,    0x00,       "1 Coin  1 Credits"		},
	{  0x01,    0x01,    0xc0,    0xc0,       "2 Coins 3 Credits"		},
	{  0x01,    0x01,    0xc0,    0x80,       "1 Coin  2 Credits"		},

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Freeze"		            },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x20,    0x20,       "Off"		               },
	{  0x01,    0x01,    0x20,    0x00,       "On"		               },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Demo Sounds"		      },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x10,    0x10,       "Off"		               },
	{  0x01,    0x01,    0x10,    0x00,       "On"		               },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Allow Continue"		   },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x08,    0x08,       "No"		               },
	{  0x01,    0x01,    0x08,    0x00,       "Yes"		               },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Cabinet"		         },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x04,    0x04,       "Upright"		         },
	{  0x01,    0x01,    0x04,    0x00,       "Cocktail"		         },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Difficulty"		      },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x03,    0x00,       "Easy"		            },
	{  0x01,    0x01,    0x03,    0x02,       "Medium"		            },
	{  0x01,    0x01,    0x03,    0x01,       "Hard"		            },
	{  0x01,    0x01,    0x03,    0x03,       "Hardest"		         },
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

static struct DigDug_PlayField_Params
{
// Dig Dug playfield stuff
   INT32 playField;
   INT32 alphaColor;
   INT32 playEnable;
   INT32 playColor;
} playFieldParams;

#define DIGDUG_NUM_OF_CHAR_PALETTE_BITS   1
#define DIGDUG_NUM_OF_SPRITE_PALETTE_BITS 2
#define DIGDUG_NUM_OF_BGTILE_PALETTE_BITS 2

#define DIGDUG_PALETTE_SIZE_BGTILES       0x100
#define DIGDUG_PALETTE_SIZE_SPRITES       0x100
#define DIGDUG_PALETTE_SIZE_CHARS         0x20
#define DIGDUG_PALETTE_SIZE (DIGDUG_PALETTE_SIZE_CHARS + \
                             DIGDUG_PALETTE_SIZE_SPRITES + \
                             DIGDUG_PALETTE_SIZE_BGTILES)
                             
#define DIGDUG_PALETTE_OFFSET_BGTILES     0x0
#define DIGDUG_PALETTE_OFFSET_SPRITE      (DIGDUG_PALETTE_OFFSET_BGTILES + \
                                           DIGDUG_PALETTE_SIZE_BGTILES)
#define DIGDUG_PALETTE_OFFSET_CHARS       (DIGDUG_PALETTE_OFFSET_SPRITE + \
                                           DIGDUG_PALETTE_SIZE_SPRITES)

#define DIGDUG_NUM_OF_CHAR                0x80
#define DIGDUG_SIZE_OF_CHAR_IN_BYTES      0x40

#define DIGDUG_NUM_OF_SPRITE              0x100
#define DIGDUG_SIZE_OF_SPRITE_IN_BYTES    0x200

#define DIGDUG_NUM_OF_BGTILE              0x100
#define DIGDUG_SIZE_OF_BGTILE_IN_BYTES    0x80

static INT32 digdugInit(void);
static INT32 digdugReset(void);

static void digdugMemoryMap1(void);
static void digdugMemoryMap2(void);
static void digdugMemoryMap3(void);

static INT32 digdugCharDecode(void);
static INT32 digdugBGTilesDecode(void);
static INT32 digdugSpriteDecode(void);
static tilemap_callback(digdug_bg);
static tilemap_callback(digdug_fg);
static INT32 digdugTilemapConfig(void);

#ifndef USE_NAMCO51
static UINT8 digdugZ80ReadInputs(UINT16 offset);

static void digdugZ80WriteIoChip(UINT16 offset, UINT8 dta);
#endif

static void digdug_pf_latch_w(UINT16 offset, UINT8 dta);
static void digdugZ80Writeb840(UINT16 offset, UINT8 dta);

static void digdugCalcPalette(void);
static void digdugRenderTiles(void);
static UINT32 digdugGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset);

static INT32 digdugScan(INT32 nAction, INT32 *pnMin);

static struct CPU_Config_Def digdugCPU[NAMCO_BRD_CPU_COUNT] =
{
   {  
      /* CPU ID = */          CPU1, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  digdugMemoryMap1
   },
   {  
      /* CPU ID = */          CPU2, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  digdugMemoryMap2
   },
   {  
      /* CPU ID = */          CPU3, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite, 
      /* Memory Mapping = */  digdugMemoryMap3
   },
};
   
static struct CPU_Rd_Table digdugReadTable[] =
{
	{ 0x6800, 0x6807, namcoZ80ReadDip         }, 
#ifndef USE_NAMCO51
   { 0x7000, 0x700f, digdugZ80ReadInputs     },
#else
   { 0x7000, 0x7002, namco51xxRead           }, 
#endif
	{ 0x7100, 0x7100, namcoZ80ReadIoCmd       },
   // EAROM Read
	{ 0xb800, 0xb83f, earom_read              },
	{ 0x0000, 0x0000, NULL                    },
};

static struct CPU_Wr_Table digdugWriteTable[] =
{
   // EAROM Write
	{ 0xb800, 0xb83f, earom_write             },
	{ 0x6800, 0x681f, namcoZ80WriteSound      },
   { 0xb840, 0xb840, digdugZ80Writeb840      },
   { 0x6820, 0x6820, namcoZ80WriteCPU1Irq    },
	{ 0x6821, 0x6821, namcoZ80WriteCPU2Irq    },
	{ 0x6822, 0x6822, namcoZ80WriteCPU3Irq    },
	{ 0x6823, 0x6823, namcoZ80WriteCPUReset   },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
#ifndef USE_NAMCO51
	{ 0x7000, 0x700f, namcoZ80WriteIoChip     },
   { 0x7008, 0x7008, digdugZ80WriteIoChip    },
#else
   { 0x7000, 0x7008, namco51xxWrite          },
#endif
	{ 0x7100, 0x7100, namcoZ80WriteIoCmd      },
	{ 0xa000, 0xa006, digdug_pf_latch_w       },
	{ 0xa007, 0xa007, namcoZ80WriteFlipScreen },
   { 0x0000, 0x0000, NULL                    },

};

static struct Memory_Layout_Def digdugMemTable[] = 
{
	{  &memory.Z80.rom1,             0x04000,                               MEM_PGM        },
	{  &memory.Z80.rom2,             0x04000,                               MEM_PGM        },
	{  &memory.Z80.rom3,             0x04000,                               MEM_PGM        },
	{  &memory.PROM.palette,         0x00020,                               MEM_ROM        },
	{  &memory.PROM.charLookup,      0x00100,                               MEM_ROM        },
	{  &memory.PROM.spriteLookup,    0x00100,                               MEM_ROM        },
	{  &NamcoSoundProm,              0x00200,                               MEM_ROM        },
   
	{  &memory.RAM.video,            0x00800,                               MEM_RAM        },
	{  &memory.RAM.shared1,          0x00400,                               MEM_RAM        },
	{  &memory.RAM.shared2,          0x00400,                               MEM_RAM        },
	{  &memory.RAM.shared3,          0x00400,                               MEM_RAM        },

	{  (UINT8 **)&gameData,          0x1000,                                MEM_DATA       },
	{  &graphics.fgChars,            DIGDUG_NUM_OF_CHAR * 8 * 8,            MEM_DATA       },
   {  &graphics.bgTiles,            DIGDUG_NUM_OF_BGTILE * 8 * 8,          MEM_DATA       },
	{  &graphics.sprites,            DIGDUG_NUM_OF_SPRITE * 16 * 16,        MEM_DATA       },
	{  (UINT8 **)&graphics.palette,  DIGDUG_PALETTE_SIZE * sizeof(UINT32),  MEM_DATA32     },
};

#define DIGDUG_MEM_TBL_SIZE      (sizeof(digdugMemTable) / sizeof(struct Memory_Layout_Def))

static struct ROM_Load_Def digdugROMTable[] =
{
   {  &memory.Z80.rom1,             0x00000, NULL                 },
   {  &memory.Z80.rom1,             0x01000, NULL                 },
	{  &memory.Z80.rom1,             0x02000, NULL                 },
	{  &memory.Z80.rom1,             0x03000, NULL                 },
   {  &memory.Z80.rom2,             0x00000, NULL                 },
	{  &memory.Z80.rom2,             0x01000, NULL                 },
   {  &memory.Z80.rom3,             0x00000, NULL                 },
   {  &tempRom,                     0x00000, digdugCharDecode     },
   {  &tempRom,                     0x00000, NULL                 },
	{  &tempRom,                     0x01000, NULL                 },
	{  &tempRom,                     0x02000, NULL                 },
	{  &tempRom,                     0x03000, digdugSpriteDecode   },
   {  &tempRom,                     0x00000, digdugBGTilesDecode  },
	{  &gameData,                    0x00000, NULL                 },
   {  &memory.PROM.palette,         0x00000, NULL                 },
	{  &memory.PROM.spriteLookup,    0x00000, NULL                 },
	{  &memory.PROM.charLookup,      0x00000, NULL                 },
	{  &NamcoSoundProm,              0x00000, NULL                 },
   {  &NamcoSoundProm,              0x00100, namcoMachineInit     },
};

#define DIGDUG_ROM_TBL_SIZE      (sizeof(digdugROMTable) / sizeof(struct ROM_Load_Def))

typedef void (*DrawFunc_t)(void);

static DrawFunc_t digdugDrawFuncs[] = 
{
   digdugCalcPalette,
	digdugRenderTiles,
	namcoRenderSprites,	
};

#define DIGDUG_DRAW_TBL_SIZE  (sizeof(digdugDrawFuncs) / sizeof(digdugDrawFuncs[0]))

static struct Machine_Config_Def digdugMachineConfig =
{
   .cpus                = digdugCPU,
   .wrAddrList          = digdugWriteTable,
   .rdAddrList          = digdugReadTable,
   .memLayoutTable      = digdugMemTable,
   .memLayoutSize       = DIGDUG_MEM_TBL_SIZE,
   .romLayoutTable      = digdugROMTable,
   .romLayoutSize       = DIGDUG_ROM_TBL_SIZE,
   .tempRomSize         = 0x4000,
   .tilemapsConfig      = digdugTilemapConfig,
   .drawLayerTable      = digdugDrawFuncs,
   .drawTableSize       = DIGDUG_DRAW_TBL_SIZE,
   .getSpriteParams     = digdugGetSpriteParams,
   .reset               = digdugReset,
#ifndef USE_NAMCO51
   .ioChipStartEnable   = 1,
#endif
   .playerControlPort   = 
   {  
      /* Player 1 Port = */      1, 
      /* Player 2 Port = */      2
   }
};

static INT32 digdugInit(void)
{
   machine.game = NAMCO_DIGDUG;
   machine.numOfDips = DIGDUG_NUM_OF_DIPSWITCHES;
   
   machine.config = &digdugMachineConfig;
   
   INT32 retVal = namcoInitBoard();

   if (0 == retVal)
      earom_init();
   
   return retVal;
}

static INT32 digdugReset(void)
{
   playFieldParams.playField = 0;
	playFieldParams.alphaColor = 0;
	playFieldParams.playEnable = 0;
	playFieldParams.playColor = 0;

#ifndef USE_NAMCO51
   ioChip.startEnable = 0;
   ioChip.coinsInserted = 0;
#endif

	earom_reset();

   return DrvDoReset();
}

static void digdugMemoryMap1(void)
{
	ZetMapMemory(memory.Z80.rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static void digdugMemoryMap2(void)
{
	ZetMapMemory(memory.Z80.rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static void digdugMemoryMap3(void)
{
	ZetMapMemory(memory.Z80.rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(memory.RAM.video,   0x8000, 0x87ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared1, 0x8800, 0x8bff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x93ff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0x9800, 0x9bff, MAP_RAM);
}

static INT32 digdugCharDecode(void)
{
   GfxDecode(
      DIGDUG_NUM_OF_CHAR, 
      DIGDUG_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      (INT32*)planeOffsets1Bit, 
      (INT32*)xOffsets8x8Tiles1Bit, 
      (INT32*)yOffsets8x8Tiles1Bit, 
      DIGDUG_SIZE_OF_CHAR_IN_BYTES, 
      tempRom, 
      graphics.fgChars
   );
   
   return 0;
}

static INT32 digdugBGTilesDecode(void)
{
   GfxDecode(
      DIGDUG_NUM_OF_BGTILE, 
      DIGDUG_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      (INT32*)planeOffsets2Bit, 
      (INT32*)xOffsets8x8Tiles2Bit, 
      (INT32*)yOffsets8x8Tiles2Bit, 
      DIGDUG_SIZE_OF_BGTILE_IN_BYTES, 
      tempRom, 
      graphics.bgTiles
   );
   
   return 0;
};

static INT32 digdugSpriteDecode(void)
{
   GfxDecode(
      DIGDUG_NUM_OF_SPRITE, 
      DIGDUG_NUM_OF_SPRITE_PALETTE_BITS, 
      16, 16, 
      (INT32*)planeOffsets2Bit, 
      (INT32*)xOffsets16x16Tiles2Bit, 
      (INT32*)yOffsets16x16Tiles2Bit, 
      DIGDUG_SIZE_OF_SPRITE_IN_BYTES, 
      tempRom, 
      graphics.sprites
   );
   
   return 0;
}

static tilemap_callback ( digdug_fg )
{
   INT32 code = memory.RAM.video[offs];
   INT32 colour = ((code >> 4) & 0x0e) | ((code >> 3) & 2);
   code &= 0x7f;

   TILE_SET_INFO(1, code, colour, 0);
}

static tilemap_callback ( digdug_bg )
{
   UINT8 *pf = gameData + (playFieldParams.playField << 10);
   INT8 pfval = pf[offs & 0xfff];
   INT32 pfColour = (pfval >> 4) + (playFieldParams.playColor << 4);
   
   TILE_SET_INFO(0, pfval, pfColour, 0);
}

static INT32 digdugTilemapConfig(void)
{
   GenericTilemapInit(
      0, 
      namco_map_scan, digdug_bg_map_callback, 
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
      namco_map_scan, digdug_fg_map_callback, 
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
   
   return 0;
}

#ifndef USE_NAMCO51
static UINT8 digdugZ80ReadInputs(UINT16 offset)
{
   UINT8 retVal = 0xff;

   switch (ioChip.customCommand) 
   {
      case 0xd2: 
      {
         if ( (0 == offset) || (1 == offset) )
            retVal = input.dip[offset].byte;
         break;
      }
      
      case 0x71:
      case 0xb1: 
      {
         if (0xb1 == ioChip.customCommand)
         {
            if (offset <= 2) // status
               retVal = 0;
            else
               retVal = 0xff;
         }
         
         if (0 == offset) 
         {
            if (ioChip.mode) 
            {
               retVal = input.ports[0].current.byte;
            } 
            else 
            {
               retVal = updateCoinAndCredit(&digdugCoinAndCreditParams);
            }
            input.ports[0].previous.byte = input.ports[0].current.byte;
         }
         
         if ( (1 == offset) || (2 == offset) ) 
         {
            INT32 jp = input.ports[offset].current.byte;

            if (0 == ioChip.mode)
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

            retVal = updateJoyAndButtons(offset, jp);
         }
      }
   }
   
   return retVal;
}

static void digdugZ80WriteIoChip(UINT16 offset, UINT8 dta)
{
   if (0xc1 == ioChip.customCommand) 
   {
      ioChip.leftCoinPerCredit = ioChip.buffer[2] & 0x0f;
      ioChip.leftCreditPerCoin = ioChip.buffer[3] & 0x0f;
   }
}
#endif

static void digdug_pf_latch_w(UINT16 offset, UINT8 dta)
{
	switch (offset)
	{
		case 0:
			playFieldParams.playField = (playFieldParams.playField & ~1) | (dta & 1);
			break;

		case 1:
			playFieldParams.playField = (playFieldParams.playField & ~2) | ((dta << 1) & 2);
			break;

		case 2:
			playFieldParams.alphaColor = dta & 1;
			break;

		case 3:
			playFieldParams.playEnable = dta & 1;
			break;

		case 4:
			playFieldParams.playColor = (playFieldParams.playColor & ~1) | (dta & 1);
			break;

		case 5:
			playFieldParams.playColor = (playFieldParams.playColor & ~2) | ((dta << 1) & 2);
			break;
	}
}

static void digdugZ80Writeb840(UINT16 offset, UINT8 dta)
{
   earom_ctrl_write(0xb840, dta);
}

#define DIGDUG_3BIT_PALETTE_SIZE    32

static void digdugCalcPalette(void)
{
   UINT32 palette[DIGDUG_3BIT_PALETTE_SIZE];
   
   for (INT32 i = 0; i < DIGDUG_3BIT_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour3Bit[(memory.PROM.palette[i] >> 0) & 0x07];
      INT32 g = Colour3Bit[(memory.PROM.palette[i] >> 3) & 0x07];
      INT32 b = Colour3Bit[(memory.PROM.palette[i] >> 5) & 0x06];
      
      palette[i] = BurnHighCol(r, g, b, 0);
   }

   /* bg_select */
   for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_BGTILES; i ++) 
   {
      graphics.palette[DIGDUG_PALETTE_OFFSET_BGTILES + i] = 
         palette[memory.PROM.charLookup[i] & 0x0f];
   }

   /* sprites */
   for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_SPRITES; i ++) 
   {
      graphics.palette[DIGDUG_PALETTE_OFFSET_SPRITE + i] = 
         palette[(memory.PROM.spriteLookup[i] & 0x0f) + 0x10];
   }

   /* characters - direct mapping */
   for (INT32 i = 0; i < DIGDUG_PALETTE_SIZE_CHARS; i += 2)
   {
      graphics.palette[DIGDUG_PALETTE_OFFSET_CHARS + i + 0] = palette[0];
      graphics.palette[DIGDUG_PALETTE_OFFSET_CHARS + i + 1] = palette[i/2];
   }
}

static void digdugRenderTiles(void)
{
   GenericTilemapSetScrollX(0, 0);
   GenericTilemapSetScrollY(0, 0);
   GenericTilemapSetOffsets(0, 0, 0);
   GenericTilemapSetEnable(0, (0 == playFieldParams.playEnable));
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_DRAWOPAQUE);
   GenericTilemapSetEnable(1, 1);
   GenericTilemapDraw(1, pTransDraw, 0 | TMAP_TRANSPARENT);
}

static UINT32 digdugGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset)
{
   UINT8 *spriteRam1 = memory.RAM.shared1 + 0x380;
   UINT8 *spriteRam2 = memory.RAM.shared2 + 0x380;
   UINT8 *spriteRam3 = memory.RAM.shared3 + 0x380;
   
   INT32 sprite = spriteRam1[offset + 0];
   if (sprite & 0x80) spriteParams->sprite = (sprite & 0xc0) | ((sprite & ~0xc0) << 2);
   else               spriteParams->sprite = sprite;
   spriteParams->colour = spriteRam1[offset + 1] & 0x3f;

   spriteParams->xStart = spriteRam2[offset + 1] - 40 + 1;
   if (8 > spriteParams->xStart) spriteParams->xStart += 0x100;
   spriteParams->yStart = NAMCO_SCREEN_WIDTH - spriteRam2[offset + 0] + 1;
   spriteParams->xStep = 16;
   spriteParams->yStep = 16;

   spriteParams->flags = spriteRam3[offset + 0] & 0x03;
   spriteParams->flags |= ((sprite & 0x80) >> 4) | ((sprite & 0x80) >> 5);

   if (spriteParams->flags & ySize)
   {
      spriteParams->yStart -= 16;
   }
   
   if (spriteParams->flags & xSize)
   {
      if (spriteParams->flags & xFlip)
      {
         spriteParams->xStart += 16;
         spriteParams->xStep  = -16;
      }
   }
   
   spriteParams->paletteBits = DIGDUG_NUM_OF_SPRITE_PALETTE_BITS;
   spriteParams->paletteOffset = DIGDUG_PALETTE_OFFSET_SPRITE;
   
   return 1;
}

static INT32 digdugScan(INT32 nAction, INT32 *pnMin)
{
   if (nAction & ACB_DRIVER_DATA) {
      SCAN_VAR(playFieldParams.playField);
      SCAN_VAR(playFieldParams.alphaColor);
      SCAN_VAR(playFieldParams.playEnable);
      SCAN_VAR(playFieldParams.playColor);

      earom_scan(nAction, pnMin); 
   }
   
   return DrvScan(nAction, pnMin);
}

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
   /* Init func = */                            digdugInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        digdugScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                DIGDUG_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

/* === XEVIOUS === */

#ifndef USE_NAMCO51
#define INP_XEVIOUS_COIN_TRIGGER_1  0x00
#define INP_XEVIOUS_COIN_MASK_1     0x10
#define INP_XEVIOUS_COIN_TRIGGER_2  0x00
#define INP_XEVIOUS_COIN_MASK_2     0x20
#define INP_XEVIOUS_START_TRIGGER_1 0x00
#define INP_XEVIOUS_START_MASK_1    0x04
#define INP_XEVIOUS_START_TRIGGER_2 0x00
#define INP_XEVIOUS_START_MASK_2    0x08

static struct CoinAndCredit_Def xeviousCoinAndCreditParams = 
{
   /*                   .portNumber,   .triggerValue,                .triggerMask*/
   /*.leftCoin = */  {  0,             INP_XEVIOUS_COIN_TRIGGER_1,   INP_XEVIOUS_COIN_MASK_1    },
   /*.rightCoin =*/  {  0,             INP_XEVIOUS_COIN_TRIGGER_2,   INP_XEVIOUS_COIN_MASK_2    },
   /*.auxCoin = */   {  0,             0,                            0                          },
   /*.start1 = */    {  0,             INP_XEVIOUS_START_TRIGGER_1,  INP_XEVIOUS_START_MASK_1,  },
   /*.start2 = */    {  0,             INP_XEVIOUS_START_TRIGGER_2,  INP_XEVIOUS_START_MASK_2,  },
};
#endif

static struct BurnInputInfo XeviousInputList[] =
{
	{"Dip 1"             , BIT_DIPSWITCH,  &input.dip[0].byte,            "dip"       },
	{"Dip 2"             , BIT_DIPSWITCH,  &input.dip[1].byte,            "dip"       },

	{"Reset"             , BIT_DIGITAL,    &input.reset,                  "reset"     },

	{"Up"                , BIT_DIGITAL,    &input.ports[1].current.bits.bit[0], "p1 up"     },
	{"Right"             , BIT_DIGITAL,    &input.ports[1].current.bits.bit[1], "p1 right"  },
	{"Down"              , BIT_DIGITAL,    &input.ports[1].current.bits.bit[2], "p1 down"   },
	{"Left"              , BIT_DIGITAL,    &input.ports[1].current.bits.bit[3], "p1 left"   },
	{"P1 Button 1"       , BIT_DIGITAL,    &input.ports[1].current.bits.bit[5], "p1 fire 1" },
   // hack! CUF - must remap this input to DIP1.0
	{"P1 Button 2"       , BIT_DIGITAL,    &input.ports[1].current.bits.bit[4], "p1 fire 2" },
	
	{"Up (Cocktail)"     , BIT_DIGITAL,    &input.ports[2].current.bits.bit[0], "p2 up"     },
	{"Right (Cocktail)"  , BIT_DIGITAL,    &input.ports[2].current.bits.bit[1], "p2 right"  },
	{"Down (Cocktail)"   , BIT_DIGITAL,    &input.ports[2].current.bits.bit[2], "p2 down"   },
	{"Left (Cocktail)"   , BIT_DIGITAL,    &input.ports[2].current.bits.bit[3], "p2 left"   },
	{"Fire 1 (Cocktail)" , BIT_DIGITAL,    &input.ports[2].current.bits.bit[5], "p2 fire 1" },
   // hack! CUF - must remap this input to DIP1.4
	{"Fire 2 (Cocktail)" , BIT_DIGITAL,    &input.ports[2].current.bits.bit[4], "p2 fire 2" },

	{"Start 1"           , BIT_DIGITAL,    &input.ports[0].current.bits.bit[2], "p1 start"  },
	{"Start 2"           , BIT_DIGITAL,    &input.ports[0].current.bits.bit[3], "p2 start"  },
	{"Coin 1"            , BIT_DIGITAL,    &input.ports[0].current.bits.bit[4], "p1 coin"   },
	{"Coin 2"            , BIT_DIGITAL,    &input.ports[0].current.bits.bit[5], "p2 coin"   },
	{"Service"           , BIT_DIGITAL,    &input.ports[0].current.bits.bit[7], "service"   },

};

STDINPUTINFO(Xevious)

#define XEVIOUS_NUM_OF_DIPSWITCHES     2

static struct BurnDIPInfo XeviousDIPList[]=
{
	// Default Values
   // nOffset, nID,     nMask,   nDefault,   NULL
	{  0x00,    0xff,    0xff,    0xFF,       NULL                     },
	{  0x01,    0xff,    0xff,    0xFF,       NULL                     },
	
	// Dip 1	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       0,          "Button 2 (Not a DIP)"   },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x01,    0x01,       "Released"               },
	{  0x00,    0x01,    0x01,    0x00,       "Held"                   },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Flags Award Bonus Life" },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x02,    0x02,       "Yes"                    },
	{  0x00,    0x01,    0x02,    0x00,       "No"                     },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Coin B"                 },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x0C,    0x04,       "2 Coins 1 Play"         },
	{  0x00,    0x01,    0x0C,    0x0C,       "1 Coin  1 Play"         },
	{  0x00,    0x01,    0x0C,    0x00,       "2 Coins 3 Plays"        },
	{  0x00,    0x01,    0x0C,    0x08,       "1 Coin  2 Plays"        },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       0,          "Button 2 (Cocktail) (Not a DIP)" },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x10,    0x10,       "Released"               },
	{  0x00,    0x01,    0x10,    0x00,       "Held"                   },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Difficulty"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x60,    0x40,       "Easy"                   },
	{  0x00,    0x01,    0x60,    0x60,       "Normal"                 },
	{  0x00,    0x01,    0x60,    0x20,       "Hard"                   },
	{  0x00,    0x01,    0x60,    0x00,       "Hardest"                },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Freeze"                 },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x00,    0x01,    0x80,    0x80,       "Off"                    },
	{  0x00,    0x01,    0x80,    0x00,       "On"                     },
	
	// Dip 2	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Coin A"                 },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x03,    0x01,       "2 Coins 1 Play"         },
	{  0x01,    0x01,    0x03,    0x03,       "1 Coin  1 Play"         },
	{  0x01,    0x01,    0x03,    0x00,       "2 Coins 3 Plays"        },
	{  0x01,    0x01,    0x03,    0x02,       "1 Coin  2 Plays"        },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       8,          "Bonus Life"             },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x1C,    0x18,       "10k  40k  40k"          },
	{  0x01,    0x01,    0x1C,    0x14,       "10k  50k  50k"          },
	{  0x01,    0x01,    0x1C,    0x10,       "20k  50k  50k"          },
	{  0x01,    0x01,    0x1C,    0x1C,       "20k  60k  60k"          },
	{  0x01,    0x01,    0x1C,    0x0C,       "20k  70k  70k"          },
	{  0x01,    0x01,    0x1C,    0x08,       "20k  80k  80k"          },
	{  0x01,    0x01,    0x1C,    0x04,       "20k  60k"               },
	{  0x01,    0x01,    0x1C,    0x00,       "None"                   },
	
   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       4,          "Lives"                  },
   // nInput,  nFlags,  nMask,   nSetting,   szText
	{  0x01,    0x01,    0x60,    0x40,       "1"                      },
	{  0x01,    0x01,    0x60,    0x20,       "2"                      },
	{  0x01,    0x01,    0x60,    0x60,       "3"                      },
	{  0x01,    0x01,    0x60,    0x00,       "5"                      },

   // x,       DIP_GRP, x,       OptionCnt,  szTitle
	{  0,       0xfe,    0,       2,          "Cabinet"                },
   // nInput,  nFlags,  nMask,   nSetting,   szText
   {  0x01,    0x01,    0x80,    0x80,       "Upright"                },
	{  0x01,    0x01,    0x80,    0x00,       "Cocktail"               },
	
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
#define XEVIOUS_PALETTE_MEM_SIZE_IN_BYTES    (XEVIOUS_PALETTE_SIZE * \
                                              sizeof(UINT32))
                             
#define XEVIOUS_NUM_OF_CHAR                  0x200
#define XEVIOUS_SIZE_OF_CHAR_IN_BYTES        (8 * 8)
#define XEVIOUS_CHAR_MEM_SIZE_IN_BYTES       (XEVIOUS_NUM_OF_CHAR * \
                                              XEVIOUS_SIZE_OF_CHAR_IN_BYTES)

#define XEVIOUS_NUM_OF_SPRITE1               0x080
#define XEVIOUS_NUM_OF_SPRITE2               0x080
#define XEVIOUS_NUM_OF_SPRITE3               0x040
#define XEVIOUS_NUM_OF_SPRITE                (XEVIOUS_NUM_OF_SPRITE1 + \
                                              XEVIOUS_NUM_OF_SPRITE2 + \
                                              XEVIOUS_NUM_OF_SPRITE3)
#define XEVIOUS_SIZE_OF_SPRITE_IN_BYTES      0x200
#define XEVIOUS_SPRITE_MEM_SIZE_IN_BYTES     (XEVIOUS_NUM_OF_SPRITE * \
                                              XEVIOUS_SIZE_OF_SPRITE_IN_BYTES)

#define XEVIOUS_NUM_OF_BGTILE                0x200
#define XEVIOUS_SIZE_OF_BGTILE_IN_BYTES      (8 * 8)
#define XEVIOUS_TILES_MEM_SIZE_IN_BYTES      (XEVIOUS_NUM_OF_BGTILE * \
                                              XEVIOUS_SIZE_OF_BGTILE_IN_BYTES)

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

static INT32 xeviousInit(void);
static void xeviousMemoryMap1(void);
static void xeviousMemoryMap2(void);
static void xeviousMemoryMap3(void);
static INT32 xeviousCharDecode(void);
static INT32 xeviousTilesDecode(void);
static INT32 xeviousSpriteDecode(void);
static tilemap_scan(xevious);
static tilemap_callback(xevious_bg);
static tilemap_callback(xevious_fg);
static INT32 xeviousTilemapConfig(void);

static UINT8 xeviousPlayFieldRead(UINT16 offset);
static UINT8 xeviousWorkRAMRead(UINT16 offset);
static UINT8 xeviousSharedRAM1Read(UINT16 offset);
static UINT8 xeviousSharedRAM2Read(UINT16 offset);
static UINT8 xeviousSharedRAM3Read(UINT16 offset);

#ifndef USE_NAMCO51
static UINT8 xeviousZ80ReadInputs(UINT16 offset);

static void xeviousZ80WriteIoChip(UINT16 offset, UINT8 dta);
#endif

static void xevious_bs_wr(UINT16 offset, UINT8 dta);
static void xevious_vh_latch_w(UINT16 offset, UINT8 dta);
static void xeviousBGColorRAMWrite(UINT16 offset, UINT8 dta);
static void xeviousBGCharRAMWrite(UINT16 offset, UINT8 dta);
static void xeviousFGColorRAMWrite(UINT16 offset, UINT8 dta);
static void xeviousFGCharRAMWrite(UINT16 offset, UINT8 dta);
static void xeviousWorkRAMWrite(UINT16 offset, UINT8 dta);
static void xeviousSharedRAM1Write(UINT16 offset, UINT8 dta);
static void xeviousSharedRAM2Write(UINT16 offset, UINT8 dta);
static void xeviousSharedRAM3Write(UINT16 offset, UINT8 dta);

static void xeviousCalcPalette(void);
static void xeviousRenderTiles0(void);
static void xeviousRenderTiles1(void);
static UINT32 xeviousGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset);

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

static struct CPU_Config_Def xeviousCPU[NAMCO_BRD_CPU_COUNT] =
{
   {  
      /* CPU ID = */          CPU1, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  xeviousMemoryMap1
   },
   {  
      /* CPU ID = */          CPU2, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite,
      /* Memory Mapping = */  xeviousMemoryMap2
   },
   {  
      /* CPU ID = */          CPU3, 
      /* CPU Read Func = */   namcoZ80ProgRead, 
      /* CPU Write Func = */  namcoZ80ProgWrite, 
      /* Memory Mapping = */  xeviousMemoryMap3
   },
};
   
static struct CPU_Rd_Table xeviousZ80ReadTable[] =
{
	{ 0x6800, 0x6807, namcoZ80ReadDip            }, 
#ifndef USE_NAMCO51
   { 0x7000, 0x700f, xeviousZ80ReadInputs       },
#else
   { 0x7000, 0x7002, namco51xxRead              },
#endif
	{ 0x7100, 0x7100, namcoZ80ReadIoCmd          },
   { 0x7800, 0x7fff, xeviousWorkRAMRead         },
   { 0x8000, 0x8fff, xeviousSharedRAM1Read      },
   { 0x9000, 0x9fff, xeviousSharedRAM2Read      },
   { 0xa000, 0xafff, xeviousSharedRAM3Read      },
   { 0xf000, 0xffff, xeviousPlayFieldRead       },
   { 0x0000, 0x0000, NULL                       },
};

static struct CPU_Wr_Table xeviousZ80WriteTable[] = 
{
	{ 0x6800, 0x681f, namcoZ80WriteSound         },
   { 0x6820, 0x6820, namcoZ80WriteCPU1Irq       },
	{ 0x6821, 0x6821, namcoZ80WriteCPU2Irq       },
	{ 0x6822, 0x6822, namcoZ80WriteCPU3Irq       },
	{ 0x6823, 0x6823, namcoZ80WriteCPUReset      },
//	{ 0x6830, 0x6830, WatchDogWriteNotImplemented }, 
#ifndef USE_NAMCO51
	{ 0x7000, 0x700f, xeviousZ80WriteIoChip      },
#else
	{ 0x7000, 0x700f, namco51xxWrite             },
#endif
	{ 0x7100, 0x7100, namcoZ80WriteIoCmd         },
   { 0x7800, 0x7fff, xeviousWorkRAMWrite        },
   { 0x8000, 0x8fff, xeviousSharedRAM1Write     },
   { 0x9000, 0x9fff, xeviousSharedRAM2Write     },
   { 0xa000, 0xafff, xeviousSharedRAM3Write     },
   { 0xb000, 0xb7ff, xeviousFGColorRAMWrite     },
   { 0xb800, 0xbfff, xeviousBGColorRAMWrite     },
   { 0xc000, 0xc7ff, xeviousFGCharRAMWrite      },
   { 0xc800, 0xcfff, xeviousBGCharRAMWrite      },
   { 0xd000, 0xd07f, xevious_vh_latch_w         },
   { 0xf000, 0xffff, xevious_bs_wr              },
   { 0x0000, 0x0000, NULL                       },
};

static struct Memory_Layout_Def xeviousMemTable[] = 
{
	{  &memory.Z80.rom1,           0x04000,                           MEM_PGM  },
	{  &memory.Z80.rom2,           0x04000,                           MEM_PGM  },
	{  &memory.Z80.rom3,           0x04000,                           MEM_PGM  },
	{  &memory.PROM.palette,       0x00300,                           MEM_ROM  },
	{  &memory.PROM.charLookup,    0x00400,                           MEM_ROM  },
	{  &memory.PROM.spriteLookup,  0x00400,                           MEM_ROM  },
	{  &NamcoSoundProm,            0x00200,                           MEM_ROM  },
   
	{  &xeviousRAM.workram,        0x00800,                           MEM_RAM  },
	{  &memory.RAM.shared1,        0x01000,                           MEM_RAM  },
	{  &memory.RAM.shared2,        0x01000,                           MEM_RAM  },
	{  &memory.RAM.shared3,        0x01000,                           MEM_RAM  },
	{  &memory.RAM.video,          0x02000,                           MEM_RAM  },

	{  &graphics.bgTiles,          XEVIOUS_TILES_MEM_SIZE_IN_BYTES,   MEM_DATA },
   {  &xeviousROM.rom2a,          0x01000,                           MEM_DATA },
   {  &xeviousROM.rom2b,          0x02000,                           MEM_DATA },
   {  &xeviousROM.rom2c,          0x01000,                           MEM_DATA },
   {  &graphics.fgChars,          XEVIOUS_CHAR_MEM_SIZE_IN_BYTES,    MEM_DATA },
	{  &graphics.sprites,          XEVIOUS_SPRITE_MEM_SIZE_IN_BYTES,  MEM_DATA },
	{  (UINT8 **)&graphics.palette, XEVIOUS_PALETTE_MEM_SIZE_IN_BYTES,MEM_DATA32},
};
	
#define XEVIOUS_MEM_TBL_SIZE      (sizeof(xeviousMemTable) / sizeof(struct Memory_Layout_Def))

static struct ROM_Load_Def xeviousROMTable[] =
{
	{  &memory.Z80.rom1,             0x00000, NULL                 },
	{  &memory.Z80.rom1,             0x01000, NULL                 },
	{  &memory.Z80.rom1,             0x02000, NULL                 },
	{  &memory.Z80.rom1,             0x03000, NULL                 },
   {  &memory.Z80.rom2,             0x00000, NULL                 },
	{  &memory.Z80.rom2,             0x01000, NULL                 },
   {  &memory.Z80.rom3,             0x00000, NULL                 },
	
	{  &tempRom,                     0x00000, xeviousCharDecode    },

	{  &tempRom,                     0x00000, NULL                 },
	{  &tempRom,                     0x01000, xeviousTilesDecode   },

	{  &tempRom,                     0x00000, NULL                 },
	{  &tempRom,                     0x02000, NULL                 },
	{  &tempRom,                     0x04000, NULL                 },
	{  &tempRom,                     0x06000, xeviousSpriteDecode  },

	{  &xeviousROM.rom2a,            0x00000, NULL                 },
	{  &xeviousROM.rom2b,            0x00000, NULL                 },
	{  &xeviousROM.rom2c,            0x00000, NULL                 },
   
	{  &memory.PROM.palette,         0x00000, NULL                 },
	{  &memory.PROM.palette,         0x00100, NULL                 },
	{  &memory.PROM.palette,         0x00200, NULL                 },
	{  &memory.PROM.charLookup,      0x00000, NULL                 },
	{  &memory.PROM.charLookup,      0x00200, NULL                 },
	{  &memory.PROM.spriteLookup,    0x00000, NULL                 },
	{  &memory.PROM.spriteLookup,    0x00200, NULL                 },
	{  &NamcoSoundProm,              0x00000, NULL                 },
	{  &NamcoSoundProm,              0x00100, namcoMachineInit     }
};
	
#define XEVIOUS_ROM_TBL_SIZE      (sizeof(xeviousROMTable) / sizeof(struct ROM_Load_Def))

static DrawFunc_t xeviousDrawFuncs[] = 
{
	xeviousCalcPalette,
   xeviousRenderTiles0,
   namcoRenderSprites,
   xeviousRenderTiles1,
};

#define XEVIOUS_DRAW_TBL_SIZE  (sizeof(xeviousDrawFuncs) / sizeof(xeviousDrawFuncs[0]))

static struct Machine_Config_Def xeviousMachineConfig =
{
   .cpus                = xeviousCPU,
   .wrAddrList          = xeviousZ80WriteTable,
   .rdAddrList          = xeviousZ80ReadTable,
   .memLayoutTable      = xeviousMemTable,
   .memLayoutSize       = XEVIOUS_MEM_TBL_SIZE,
   .romLayoutTable      = xeviousROMTable,
   .romLayoutSize       = XEVIOUS_ROM_TBL_SIZE,
   .tempRomSize         = 0x8000,
   .tilemapsConfig      = xeviousTilemapConfig,
   .drawLayerTable      = xeviousDrawFuncs,
   .drawTableSize       = XEVIOUS_DRAW_TBL_SIZE,
   .getSpriteParams     = xeviousGetSpriteParams,
   .reset               = DrvDoReset,
#ifndef USE_NAMCO51
   .ioChipStartEnable   = 1,
#endif
   .playerControlPort   = 
   {  
      /* Player 1 Port = */      1, 
      /* Player 2 Port = */      2
   }
};

static INT32 xeviousInit(void)
{
   machine.game = NAMCO_XEVIOUS;
   machine.numOfDips = XEVIOUS_NUM_OF_DIPSWITCHES;
   
   machine.config = &xeviousMachineConfig;
   
   return namcoInitBoard();
}

static void xeviousMemoryMap1(void)
{
	ZetMapMemory(memory.Z80.rom1,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
   ZetMapMemory(memory.RAM.shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.video,   0xb000, 0xcfff, MAP_RAM);
}

static void xeviousMemoryMap2(void)
{
	ZetMapMemory(memory.Z80.rom2,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.video,   0xb000, 0xcfff, MAP_RAM);
}

static void xeviousMemoryMap3(void)
{
	ZetMapMemory(memory.Z80.rom3,    0x0000, 0x3fff, MAP_ROM);
	ZetMapMemory(xeviousRAM.workram, 0x7800, 0x7fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared1, 0x8000, 0x8fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared2, 0x9000, 0x9fff, MAP_RAM);
	ZetMapMemory(memory.RAM.shared3, 0xa000, 0xafff, MAP_RAM);
	ZetMapMemory(memory.RAM.video,   0xb000, 0xcfff, MAP_RAM);
}

static INT32 xeviousCharDecode(void)
{
	// Load and decode the chars
   /* foreground characters: */
   /* 512 characters */
   /* 1 bit per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	GfxDecode(
      XEVIOUS_NUM_OF_CHAR, 
      XEVIOUS_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      xeviousOffsets.fgChars, 
      XeviousCharXOffsets, 
      XeviousCharYOffsets, 
      XEVIOUS_SIZE_OF_CHAR_IN_BYTES, 
      tempRom, 
      graphics.fgChars
   );

	memset(tempRom, 0, machine.config->tempRomSize);

   return 0;
}

static INT32 xeviousTilesDecode(void)
{
   /* background tiles */
   /* 512 characters */
   /* 2 bits per pixel */
   /* 8 x 8 characters */
   /* every char takes 8 consecutive bytes */
	GfxDecode(
      XEVIOUS_NUM_OF_BGTILE, 
      XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      xeviousOffsets.bgChars, 
      XeviousCharXOffsets, 
      XeviousCharYOffsets, 
      XEVIOUS_SIZE_OF_BGTILE_IN_BYTES, 
      tempRom, 
      graphics.bgTiles
   );

	memset(tempRom, 0, machine.config->tempRomSize);
   
   return 0;
}

static INT32 xeviousSpriteDecode(void)
{
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
      (INT32*)xOffsets16x16Tiles2Bit, 
      (INT32*)yOffsets16x16Tiles2Bit, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      tempRom + (0x0000), 
      graphics.sprites
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
      (INT32*)xOffsets16x16Tiles2Bit, 
      (INT32*)yOffsets16x16Tiles2Bit, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      tempRom + (0x2000), 
      graphics.sprites + (XEVIOUS_NUM_OF_SPRITE1 * (16 * 16))
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
      (INT32*)xOffsets16x16Tiles2Bit, 
      (INT32*)yOffsets16x16Tiles2Bit, 
      XEVIOUS_SIZE_OF_SPRITE_IN_BYTES, 
      tempRom + (0x6000), 
      graphics.sprites + ((XEVIOUS_NUM_OF_SPRITE1 + XEVIOUS_NUM_OF_SPRITE2) * (16 * 16))
   );

   return 0;
}

static tilemap_scan ( xevious )
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

static INT32 xeviousTilemapConfig(void)
{
   xeviousRAM.fg_colorram = memory.RAM.video;            // 0xb000 - 0xb7ff
   xeviousRAM.bg_colorram = memory.RAM.video + 0x0800;   // 0xb800 - 0xbfff
   xeviousRAM.fg_videoram = memory.RAM.video + 0x1000;   // 0xc000 - 0xc7ff
   xeviousRAM.bg_videoram = memory.RAM.video + 0x1800;   // 0xc800 - 0xcfff
   
   GenericTilemapInit(
      0, 
      xevious_map_scan, xevious_bg_map_callback, 
      8, 8, 
      XEVIOUS_NO_OF_COLS, XEVIOUS_NO_OF_ROWS
   );
	GenericTilemapSetGfx(
      0, 
      graphics.bgTiles, 
      XEVIOUS_NUM_OF_BGTILE_PALETTE_BITS, 
      8, 8, 
      XEVIOUS_TILES_MEM_SIZE_IN_BYTES, 
      XEVIOUS_PALETTE_OFFSET_BGTILES, 
      0x7f //XEVIOUS_PALETTE_SIZE_BGTILES - 1
   );

   GenericTilemapInit(
      1, 
      xevious_map_scan, xevious_fg_map_callback, 
      8, 8, 
      XEVIOUS_NO_OF_COLS, XEVIOUS_NO_OF_ROWS
   );
	GenericTilemapSetGfx(
      1, 
      graphics.fgChars, 
      XEVIOUS_NUM_OF_CHAR_PALETTE_BITS, 
      8, 8, 
      XEVIOUS_CHAR_MEM_SIZE_IN_BYTES, 
      XEVIOUS_PALETTE_OFFSET_CHARS, 
      0x3f // XEVIOUS_PALETTE_SIZE_CHARS - 1
   );
	GenericTilemapSetTransparent(1, 0);
	
   GenericTilemapSetOffsets(TMAP_GLOBAL, 0, 0);

   return 0;
}

static UINT8 xeviousPlayFieldRead(UINT16 offset)
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
   if (offset & 1)
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

static UINT8 xeviousWorkRAMRead(UINT16 offset)
{
   return xeviousRAM.workram[offset];
}

static UINT8 xeviousSharedRAM1Read(UINT16 offset)
{
   return memory.RAM.shared1[offset & 0x07ff];
}

static UINT8 xeviousSharedRAM2Read(UINT16 offset)
{
   return memory.RAM.shared2[offset & 0x07ff];
}

static UINT8 xeviousSharedRAM3Read(UINT16 offset)
{
   return memory.RAM.shared3[offset & 0x07ff];
}

#ifndef USE_NAMCO51
static UINT8 xeviousZ80ReadInputs(UINT16 offset)
{
   UINT8 retVal = 0xff;
   
   switch (ioChip.customCommand & 0x0f) 
   {
      case 0x01: 
      {
         if (0 == offset) 
         {
            if (ioChip.mode) 
            {
               retVal = input.ports[0].current.byte;
            } 
            else 
            {
               retVal = updateCoinAndCredit(&xeviousCoinAndCreditParams);
            }
            input.ports[0].previous.byte = input.ports[0].current.byte;
         }
         
         if ( (1 == offset) || (2 == offset) ) 
         {
            INT32 jp = input.ports[offset].current.byte;

            if (0 == ioChip.mode)
            {
               jp = namcoControls[jp & 0x0f] | (jp & 0xf0);
            }

            retVal = jp;
         }
         break;
      }
      
      case 0x04:
      {
         if (3 == offset)
         {
            if ((0x80 == ioChip.buffer[0]) || (0x10 == ioChip.buffer[0]))
               retVal = 0x05;
            else
               retVal = 0x95;
         }
         else
            retVal = 0;
         break;
      }
      
      default:
      {
         break;
      }
   }
   
   return retVal;
}

static void xeviousZ80WriteIoChip(UINT16 offset, UINT8 dta)
{
   ioChip.buffer[offset & 0x0f] = dta;
   
   switch (ioChip.customCommand & 0x0f)
   {
      case 0x01:
      {
         if (0 == offset)
         {
            switch (dta & 0x0f)
            {
               case 0x00:
                  // nop
                  break;
               case 0x01:
                  ioChip.credits = 0;
                  ioChip.mode = 0;
                  ioChip.startEnable = 1;
                  break;
               case 0x02:
                  ioChip.startEnable = 1;
                  break;
               case 0x03:
                  ioChip.mode = 1;
                  break;
               case 0x04:
                  ioChip.mode = 0;
                  break;
               case 0x05:
                  ioChip.startEnable = 0;
                  ioChip.mode = 1;
                  break;
            }
         }
         if (7 == offset)
         {
            ioChip.auxCoinPerCredit = ioChip.buffer[1] & 0x0f;
            ioChip.auxCreditPerCoin = ioChip.buffer[2] & 0x0f;
            ioChip.leftCoinPerCredit = ioChip.buffer[3] & 0x0f;
            ioChip.leftCreditPerCoin = ioChip.buffer[4] & 0x0f;
            ioChip.rightCoinPerCredit = ioChip.buffer[5] & 0x0f;
            ioChip.rightCreditPerCoin = ioChip.buffer[6] & 0x0f;
         }
         break;
      }
      
      case 0x04:
         break;
         
      case 0x08:
      {
         if (6 == offset)
         {
            // it is not known how the parameters control the explosion. 
				// We just use samples. 
				if (memcmp(ioChip.buffer,"\x40\x40\x40\x01\xff\x00\x20",7) == 0)
				{
					BurnSamplePlay(0); //sample_start (0, 0, 0);
            }
				else if (memcmp(ioChip.buffer,"\x30\x40\x00\x02\xdf\x00\x10",7) == 0)
				{
					BurnSamplePlay(1); //sample_start (0, 1, 0);
				}
				else if (memcmp(ioChip.buffer,"\x30\x10\x00\x80\xff\x00\x10",7) == 0)
				{
					BurnSamplePlay(2); //sample_start (0, 2, 0);
				}
				else if (memcmp(ioChip.buffer,"\x30\x80\x80\x01\xff\x00\x10",7) == 0)
				{
					BurnSamplePlay(3); //sample_start (0, 3, 0);
				}
         }
         break;
      }
   }
   
}
#endif

static void xevious_bs_wr(UINT16 offset, UINT8 dta)
{
   xeviousRAM.bs[offset & 0x01] = dta;
}

static void xevious_vh_latch_w(UINT16 offset, UINT8 dta)
{
   UINT16 dta16 = dta + ((offset & 1) << 8);
   UINT16 reg = (offset & 0xf0) >> 4;
   
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
         machine.flipScreen = dta & 1;
         break;
      }
      default:
      {
         break;
      }
   }
   
}

static void xeviousBGColorRAMWrite(UINT16 offset, UINT8 dta)
{
   *(xeviousRAM.bg_colorram + (offset & 0x7ff)) = dta;
}

static void xeviousBGCharRAMWrite(UINT16 offset, UINT8 dta)
{
   *(xeviousRAM.bg_videoram + (offset & 0x7ff)) = dta;
}

static void xeviousFGColorRAMWrite(UINT16 offset, UINT8 dta)
{
   *(xeviousRAM.fg_colorram + (offset & 0x7ff)) = dta;
}

static void xeviousFGCharRAMWrite(UINT16 offset, UINT8 dta)
{
   *(xeviousRAM.fg_videoram + (offset & 0x7ff)) = dta;
}

static void xeviousWorkRAMWrite(UINT16 offset, UINT8 dta)
{
   xeviousRAM.workram[offset & 0x7ff] = dta;
}

static void xeviousSharedRAM1Write(UINT16 offset, UINT8 dta)
{
   memory.RAM.shared1[offset & 0x07ff] = dta;
}

static void xeviousSharedRAM2Write(UINT16 offset, UINT8 dta)
{
   memory.RAM.shared2[offset & 0x07ff] = dta;
}

static void xeviousSharedRAM3Write(UINT16 offset, UINT8 dta)
{
   memory.RAM.shared3[offset & 0x07ff] = dta;
}

#define XEVIOUS_BASE_PALETTE_SIZE   128

static void xeviousCalcPalette(void)
{
	UINT32 palette[XEVIOUS_BASE_PALETTE_SIZE + 1];
   UINT32 code = 0;
	
	for (INT32 i = 0; i < XEVIOUS_BASE_PALETTE_SIZE; i ++) 
   {
      INT32 r = Colour4Bit[(memory.PROM.palette[0x0000 + i]) & 0x0f];
      INT32 g = Colour4Bit[(memory.PROM.palette[0x0100 + i]) & 0x0f];
      INT32 b = Colour4Bit[(memory.PROM.palette[0x0200 + i]) & 0x0f];
      
		palette[i] = BurnHighCol(r, g, b, 0);
	}
   
   palette[XEVIOUS_BASE_PALETTE_SIZE] = BurnHighCol(0, 0, 0, 0); // Transparency Colour for Sprites

	/* bg_select */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_BGTILES; i ++) 
   {
      code = ( (memory.PROM.charLookup[                               i] & 0x0f)       | 
              ((memory.PROM.charLookup[XEVIOUS_PALETTE_SIZE_BGTILES + i] & 0x0f) << 4) );
		graphics.palette[XEVIOUS_PALETTE_OFFSET_BGTILES + i] = palette[code];
	}

	/* sprites */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_SPRITES; i ++) 
   {
      code = ( (memory.PROM.spriteLookup[i                               ] & 0x0f)       |
              ((memory.PROM.spriteLookup[XEVIOUS_PALETTE_SIZE_SPRITES + i] & 0x0f) << 4) );
      if (code & 0x80)
         graphics.palette[XEVIOUS_PALETTE_OFFSET_SPRITE + i] = palette[code & 0x7f];
      else
         graphics.palette[XEVIOUS_PALETTE_OFFSET_SPRITE + i] = palette[XEVIOUS_BASE_PALETTE_SIZE];
	}

	/* characters - direct mapping */
	for (INT32 i = 0; i < XEVIOUS_PALETTE_SIZE_CHARS; i += 2)
	{
		graphics.palette[XEVIOUS_PALETTE_OFFSET_CHARS + i + 0] = palette[XEVIOUS_BASE_PALETTE_SIZE];
		graphics.palette[XEVIOUS_PALETTE_OFFSET_CHARS + i + 1] = palette[i / 2];
	}

}

static void xeviousRenderTiles0(void)
{
   GenericTilemapSetEnable(0, 1);
   GenericTilemapDraw(0, pTransDraw, 0 | TMAP_DRAWOPAQUE);
}

static void xeviousRenderTiles1(void)
{
   GenericTilemapSetEnable(1, 1);
   GenericTilemapDraw(1, pTransDraw, 0 | TMAP_TRANSPARENT);
}

static UINT32 xeviousGetSpriteParams(struct Namco_Sprite_Params *spriteParams, UINT32 offset)
{
	UINT8 *spriteRam2 = memory.RAM.shared1 + 0x780;
	UINT8 *spriteRam3 = memory.RAM.shared2 + 0x780;
	UINT8 *spriteRam1 = memory.RAM.shared3 + 0x780;
  
   if (0 == (spriteRam1[offset + 1] & 0x40))
   {
      INT32 sprite =      spriteRam1[offset + 0];
      
      if (spriteRam3[offset + 0] & 0x80)
      {
         sprite &= 0x3f;
         sprite += 0x100;
      }
      spriteParams->sprite = sprite;
      spriteParams->colour = spriteRam1[offset + 1] & 0x7f;

      spriteParams->xStart = ((spriteRam2[offset + 1] - 40) + (spriteRam3[offset + 1] & 1 ) * 0x100);
      spriteParams->yStart = NAMCO_SCREEN_WIDTH - (spriteRam2[offset + 0] - 1);
      spriteParams->xStep = 16;
      spriteParams->yStep = 16;
      
      spriteParams->flags = ((spriteRam3[offset + 0] & 0x03) << 2) | 
                            ((spriteRam3[offset + 0] & 0x0c) >> 2);
      
      if (spriteParams->flags & ySize)
      {
         spriteParams->yStart -= 16;
      }
      
      spriteParams->paletteBits = XEVIOUS_NUM_OF_SPRITE_PALETTE_BITS;
      spriteParams->paletteOffset = XEVIOUS_PALETTE_OFFSET_SPRITE;

      return 1;
   }
   
   return 0;
}

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
   /* Init func = */                            xeviousInit, 
   /* Exit func = */                            DrvExit, 
   /* Frame func = */                           DrvFrame, 
   /* Redraw func = */                          DrvDraw, 
   /* Areascan func = */                        DrvScan, 
   /* Recalc Palette = */                       NULL, 
   /* Palette Entries count = */                XEVIOUS_PALETTE_SIZE,
   /* Width, Height = */   	                  NAMCO_SCREEN_WIDTH, NAMCO_SCREEN_HEIGHT,
   /* xAspect, yAspect = */   	               3, 4
};

