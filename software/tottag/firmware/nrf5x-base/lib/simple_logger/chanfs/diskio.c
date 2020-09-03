// SPI SD-Card Control Module for FatFS

/* For allowing SPI to run, you must define the following pins in your board file:
 *      SD_CARD_ENABLE
 *      SD_CARD_DETECT
 *      SD_CARD_SPI_CS
 *      SD_CARD_SPI_MISO
 *      SD_CARD_SPI_MOSI
 *      SD_CARD_SPI_SCLK
 *      SD_CARD_SPI_INSTANCE
 */

#include "app_timer.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "boards.h"

static char diskio_timer_inited = 0;
APP_TIMER_DEF(diskio_timer);

#define FCLK_SLOW() SD_CARD_SPI_INSTANCE->FREQUENCY = SPI_FREQUENCY_FREQUENCY_K250
#define FCLK_FAST() SD_CARD_SPI_INSTANCE->FREQUENCY = SPI_FREQUENCY_FREQUENCY_M4

#define CS_DESELECT() nrf_gpio_pin_set(SD_CARD_SPI_CS)
#define CS_SELECT() nrf_gpio_pin_clear(SD_CARD_SPI_CS)

// If SD_CARD_DETECT is available, use it; otherwise, assume that it's always detected
#ifdef SD_CARD_DETECT
#define	MMC_CD		    !nrf_gpio_pin_read(SD_CARD_DETECT)
#define SD_PIN_INIT() 	nrf_gpio_cfg_output(SD_CARD_SPI_CS);\
						nrf_gpio_cfg_output(SD_CARD_ENABLE);\
						nrf_gpio_cfg_input(SD_CARD_DETECT, NRF_GPIO_PIN_NOPULL);\
						nrf_gpio_cfg_input(SD_CARD_SPI_MISO, NRF_GPIO_PIN_PULLUP);
#else
#define MMC_CD          1
#define SD_PIN_INIT() 	nrf_gpio_cfg_output(SD_CARD_SPI_CS);\
						nrf_gpio_cfg_output(SD_CARD_ENABLE);\
						nrf_gpio_cfg_input(SD_CARD_SPI_MISO, NRF_GPIO_PIN_PULLUP);
#endif
#define	MMC_WP		    0

#define SD_POWER_ON()	nrf_gpio_pin_set(SD_CARD_ENABLE)
#define SD_POWER_OFF()	nrf_gpio_pin_clear(SD_CARD_ENABLE)

#define	SPI_CONFIG() {\
 	SD_CARD_SPI_INSTANCE->PSELSCK    = SD_CARD_SPI_SCLK;\
 	SD_CARD_SPI_INSTANCE->PSELMOSI   = SD_CARD_SPI_MOSI;\
 	SD_CARD_SPI_INSTANCE->PSELMISO   = SD_CARD_SPI_MISO;\
 	SD_CARD_SPI_INSTANCE->CONFIG     = (uint32_t)(SPI_CONFIG_CPHA_Leading << SPI_CONFIG_CPHA_Pos) |\
 							(SPI_CONFIG_CPOL_ActiveHigh << SPI_CONFIG_CPOL_Pos) |\
 							(SPI_CONFIG_ORDER_MsbFirst << SPI_CONFIG_ORDER_Pos);\
 	SD_CARD_SPI_INSTANCE->ENABLE = (SPI_ENABLE_ENABLE_Enabled << SPI_ENABLE_ENABLE_Pos);\
 	SD_CARD_SPI_INSTANCE->EVENTS_READY = 0;\
}


/*--------------------------------------------------------------------------
   Module Private Functions
---------------------------------------------------------------------------*/

#include "diskio.h"

#define BOOL    BYTE
#define TRUE    (1)
#define FALSE   (0)

// MMC/SD commands
#define CMD0	(0)         // GO_IDLE_STATE
#define CMD1	(1)         // SEND_OP_COND (MMC)
#define	ACMD41	(0x80+41)   // SEND_OP_COND (SDC)
#define CMD8	(8)         // SEND_IF_COND
#define CMD9	(9)         // SEND_CSD
#define CMD10	(10)        // SEND_CID
#define CMD12	(12)        // STOP_TRANSMISSION
#define ACMD13	(0x80+13)   // SD_STATUS (SDC)
#define CMD16	(16)        // SET_BLOCKLEN
#define CMD17	(17)        // READ_SINGLE_BLOCK
#define CMD18	(18)        // READ_MULTIPLE_BLOCK
#define CMD23	(23)        // SET_BLOCK_COUNT (MMC)
#define	ACMD23	(0x80+23)   // SET_WR_BLK_ERASE_COUNT (SDC)
#define CMD24	(24)        // WRITE_BLOCK
#define CMD25	(25)        // WRITE_MULTIPLE_BLOCK
#define CMD32	(32)        // ERASE_ER_BLK_START
#define CMD33	(33)        // ERASE_ER_BLK_END
#define CMD38	(38)        // ERASE
#define CMD55	(55)        // APP_CMD
#define CMD58	(58)        // READ_OCR

static volatile DSTATUS Stat = STA_NOINIT;  // Physical drive status
static volatile UINT Timers[2] = { 0 };
static BYTE CardType;


/*-----------------------------------------------------------------------*/
/* Timer controls                                                        */
/*-----------------------------------------------------------------------*/

static void disk_timerproc(void *p_context)
{
   // Decrease timer timeouts every millisecond
   if (Timers[0])
      --Timers[0];
   if (Timers[1])
      --Timers[1];

   // Update the SD card metadata
   if (MMC_WP)      // Write protected
      Stat |= STA_PROTECT;
   else             // Write enabled
      Stat &= ~STA_PROTECT;
   if (MMC_CD)      // Card is in socket
      Stat &= ~STA_NODISK;
   else             // Socket empty
      Stat |= (STA_NODISK | STA_NOINIT);

   // Stop the timer if no more timeouts
   if (!Timers[0] && !Timers[1])
      APP_ERROR_CHECK(app_timer_stop(diskio_timer));
}

static void timer_init(void)
{
   // Create application timer
   diskio_timer_inited = TRUE;
   APP_ERROR_CHECK(app_timer_create(&diskio_timer, APP_TIMER_MODE_REPEATED, disk_timerproc));
}

static void timer_start(BYTE timer_index, UINT timeout_ms)
{
   // Start the Disk IO timer with the specified timeout
   bool start_timer = !Timers[0] && !Timers[1];
   Timers[timer_index] = timeout_ms;
   if (start_timer)
      APP_ERROR_CHECK(app_timer_start(diskio_timer, APP_TIMER_TICKS(1), NULL));
}


/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

static void init_spi(void)
{
   // Initialize SPI
   SD_PIN_INIT();
   SD_POWER_ON();
   SPI_CONFIG();
   CS_DESELECT();

   // Wait 10 ms
   timer_start(0, 10);
   while (Timers[0]);
}

static BYTE xchg_spi(BYTE dat)
{
   SD_CARD_SPI_INSTANCE->TXD = dat;
   while (!SD_CARD_SPI_INSTANCE->EVENTS_READY);
   BYTE data = (BYTE)SD_CARD_SPI_INSTANCE->RXD;
   SD_CARD_SPI_INSTANCE->EVENTS_READY = 0;
   return data;
}


/*-----------------------------------------------------------------------*/
/* SD Card controls                                                      */
/*-----------------------------------------------------------------------*/

static BOOL wait_ready(UINT timeout_ms)
{
   BYTE d = 0x00;
   timer_start(1, timeout_ms);
   while ((d != 0xFF) && Timers[1])
      d = xchg_spi(0xFF);
   return (d == 0xFF);
}

static void deselect_sd(void)
{
   CS_DESELECT();
   xchg_spi(0xFF);
}

static BOOL select_sd(void)
{
   // Wait 500 ms for the card to become ready
   CS_SELECT();
   xchg_spi(0xFF);
   if (wait_ready(500))
      return TRUE;

   // Deselect the card in the case of a timeout
   deselect_sd();
   return FALSE;
}

static BOOL rcvr_datablock(BYTE *buff, UINT btr)
{
   // Wait 200 ms for a Data Token
   BYTE token = 0xFF;
   timer_start(0, 200);
   while ((token == 0xFF) && Timers[0])
      token = xchg_spi(0xFF);

   // Handle a timeout
   if (token != 0xFE)
      return FALSE;

   // Read the sector
   for (UINT i = 0; i < btr; ++i)
      buff[i] = xchg_spi(0xFF);

   // Ignore the checksum
   xchg_spi(0xFF);
   xchg_spi(0xFF);
   return TRUE;
}

static BOOL xmit_datablock(const BYTE *buff, BYTE token)
{
   // Wait for the card to become ready
   BYTE resp;
   if (!wait_ready(500))
      return FALSE;

   // Send xmit command
   xchg_spi(token);

   // Transmit data if command was not STOP_TRAN
   if (token != 0xFD)
   {
      // Write the sector
      for (UINT i = 0; i < 512; ++i)
         xchg_spi(buff[i]);

      // Ignore the checksum
      xchg_spi(0xFF);
      xchg_spi(0xFF);

      // Check if packet was accepted
      resp = xchg_spi(0xFF);
      if ((resp & 0x1F) != 0x05)
         return FALSE;
   }
   return TRUE;
}

static BYTE send_cmd(BYTE cmd, DWORD arg)
{
   // Send CMD55 first if command is ACMD
   if (cmd & 0x80)
   {
      cmd &= 0x7F;
      BYTE res = send_cmd(CMD55, 0);
      if (res > 1)
         return res;
   }

   // Select the card and wait for it to become ready, unless this command is to stop transmission
   if (cmd != CMD12)
   {
      deselect_sd();
      if (!select_sd())
         return 0xFF;
   }

   // Send the command packet (Start + CMD, 4 byte argument, dummy CRC)
   xchg_spi(0x40 | cmd);
   xchg_spi((BYTE)(arg >> 24));
   xchg_spi((BYTE)(arg >> 16));
   xchg_spi((BYTE)(arg >> 8));
   xchg_spi((BYTE)arg);
   xchg_spi((cmd == CMD0) ? 0x95 : ((cmd == CMD8) ? 0x87 : 0xFF));

   // Wait for response (discard extra byte if CMD12)
   BYTE max_bytes = 10, res = 0x80;
   if (cmd == CMD12)
      xchg_spi(0xFF);
   while ((res & 0x80) && --max_bytes)
      res = xchg_spi(0xFF);
   return res;
}


/*-----------------------------------------------------------------------*/
/* Disk Drive (IO) functions                                             */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE drv)
{
   // Initialize the Disk IO timer
   if (!diskio_timer_inited)
      timer_init();
   disk_timerproc(NULL);

   // Initialize SPI, only supporting a single drive
   init_spi();
   if (drv)
      return STA_NOINIT;

   // Initialize the SD Card
   if (Stat & STA_NODISK)
      return Stat;

   // Send 80 dummy clocks
   FCLK_SLOW();
   for (BYTE n = 10; n; --n)
      xchg_spi(0xFF);

   BYTE type = 0, cmd = 0, ocr[4] = { 0 };
   if (send_cmd(CMD0, 0) == 1)      // Put the card SPI/Idle state
   {
      timer_start(0, 1000);
      if (send_cmd(CMD8, 0x1AA) == 1)       // SDv2?
      {
         // Get 32 bit return value of R7 resp
         for (BYTE n = 0; n < 4; ++n)
            ocr[n] = xchg_spi(0xFF);
         if (ocr[2] == 0x01 && ocr[3] == 0xAA)      // Does the card support VCC of 2.7-3.6V?
         {
            // Wait for end of initialization with ACMD41(HCS)
            while (Timers[0] && send_cmd(ACMD41, 1UL << 30));

            // Check CCS bit in the OCR
            if (Timers[0] && send_cmd(CMD58, 0) == 0)
            {
               for (BYTE n = 0; n < 4; ++n)
                  ocr[n] = xchg_spi(0xFF);
               type = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;     // Card id SDv2
            }
         }
      }
      else              // Not SDv2 card
      {
         if (send_cmd(ACMD41, 0) <= 1)      // SDv1 or MMC?
         {
            type = CT_SD1;                  // SDv1 (ACMD41(0))
            cmd = ACMD41;
         }
         else
         {
            type = CT_MMC;                  // MMCv3 (CMD1(0))
            cmd = CMD1;
         }

         // Wait for end of initialization
         while (Timers[0] && send_cmd(cmd, 0));

         // Set block length to 512
         if (!Timers[0] || send_cmd(CMD16, 512) != 0)
            type = 0;
      }
   }

   // Set the SD Card type
   CardType = type;
   deselect_sd();

   if (type)
   {
      FCLK_FAST();
      Stat &= ~STA_NOINIT;
   }
   else
      Stat = STA_NOINIT;
   return Stat;
}

DSTATUS disk_status(BYTE drv)
{
   // Only supports a single drive
   if (drv)
      return STA_NOINIT;
   return Stat;
}

DRESULT disk_read(BYTE drv,	BYTE *buff,	DWORD sector, UINT num_sectors)
{
   // Enable SD card and check parameters
   SPI_CONFIG();
   if (drv || !num_sectors)
      return RES_PARERR;

   // Check SD card status
   if (Stat & STA_NOINIT)
      return RES_NOTRDY;

   // LBA to BA conversion (for byte addressing cards)
   if (!(CardType & CT_BLOCK))
      sector *= 512;

   // Carry out single- or multi-sector read
   if (num_sectors == 1)
   {
      // Single-sector read
      if ((send_cmd(CMD17, sector) == 0) && rcvr_datablock(buff, 512))
         num_sectors = 0;
   }
   else
   {
      // Multi-sector read
      if (send_cmd(CMD18, sector) == 0)
      {
         // Read as many blocks as requested
         do
         {
            if (!rcvr_datablock(buff, 512))
               break;
            buff += 512;
         } while (--num_sectors);

         // Stop transmission
         send_cmd(CMD12, 0);
      }
   }

   // Disable SD card
   deselect_sd();
   return num_sectors ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, DWORD sector, UINT num_sectors)
{
   // Enable SD card and check parameters
   SPI_CONFIG();
   if (drv || !num_sectors)
      return RES_PARERR;

   // Check SD card status
   if (Stat & STA_NOINIT)
      return RES_NOTRDY;
   if (Stat & STA_PROTECT)
      return RES_WRPRT;

   // LBA to BA conversion (for byte addressing cards)
   if (!(CardType & CT_BLOCK))
      sector *= 512;

   // Carry out single- or multi-sector write
   if (num_sectors == 1)
   {
      // Single-sector write
      if ((send_cmd(CMD24, sector) == 0) && xmit_datablock(buff, 0xFE))
         num_sectors = 0;
   }
   else
   {
      // Predefine number of sectors for multi-sector write
      if (CardType & CT_SDC)
         send_cmd(ACMD23, num_sectors);

      // Write as many blocks as requested
      if (send_cmd(CMD25, sector) == 0)
      {
         do
         {
            if (!xmit_datablock(buff, 0xFC))
               break;
            buff += 512;
         } while (--num_sectors);

         // Stop transmission
         if (!xmit_datablock(0, 0xFD))
            num_sectors = 1;
      }
   }

   // Disable SD card
   deselect_sd();
   return num_sectors ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buff)
{
   // Enable SD card and check parameters
   SPI_CONFIG();
   if (drv)
      return RES_PARERR;

   // Check SD card status
   DRESULT res = RES_ERROR;
   if (Stat & STA_NOINIT)
      return RES_NOTRDY;

   // Process IOCTL based on the issued command
   switch (cmd)
   {
      case CTRL_SYNC:           // End of internal write process of the drive
      {
         if (select_sd())
            res = RES_OK;
         break;
      }
      case GET_SECTOR_COUNT:    // Get drive capacity in units of sectors
      {
         BYTE csd[16] = { 0 };
         if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
         {
            if ((csd[0] >> 6) == 1)     // SDC ver 2.00
            {
               DWORD csize = csd[9] + ((WORD) csd[8] << 8) + ((DWORD) (csd[7] & 63) << 16) + 1;
               *(DWORD*)buff = csize << 10;
            }
            else                        // SDC ver 1.XX or MMC ver 3
            {
               BYTE n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
               DWORD csize = (csd[8] >> 6) + ((WORD) csd[7] << 2) + ((WORD) (csd[6] & 3) << 10) + 1;
               *(DWORD*)buff = csize << (n - 9);
            }
            res = RES_OK;
         }
         break;
      }
      case GET_BLOCK_SIZE:      // Get erase block size in units of sectors
      {
         BYTE csd[16] = { 0 };
         if (CardType & CT_SD2)         // SDC ver 2.00
         {
            // Read SD status
            if (send_cmd(ACMD13, 0) == 0)
            {
               // Read partial block
               xchg_spi(0xFF);
               if (rcvr_datablock(csd, 16))
               {
                  // Purge trailing data
                  for (BYTE n = 64 - 16; n; --n)
                     xchg_spi(0xFF);
               	  *(DWORD*)buff = 16UL << (csd[10] >> 4);
               	  res = RES_OK;
               }
            }
         }
         else                           // SDC ver 1.XX or MMC
         {
            if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
            {
               // Read CSD
               if (CardType & CT_SD1)   // SDC ver 1.XX
                  *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
               else                     // MMC
                  *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
               res = RES_OK;
            }
         }
         break;
      }
      case CTRL_TRIM:           // Erase a block of sectors
      {
         // Check if the card is SDC
         if (!(CardType & CT_SDC))
            break;

         // Get CSD
         BYTE csd[16] = { 0 };
         if (disk_ioctl(drv, MMC_GET_CSD, csd))
            break;

         // Check if sector erase can be applied to the card
         if (!(csd[0] >> 6) && !(csd[10] & 0x40))
            break;

         // Load sector block
         DWORD *dp = buff;
         DWORD st = dp[0];
         DWORD ed = dp[1];
         if (!(CardType & CT_BLOCK))
         {
            st *= 512;
            ed *= 512;
         }

         // Erase sector block
         if ((send_cmd(CMD32, st) == 0) && (send_cmd(CMD33, ed) == 0) && (send_cmd(CMD38, 0) == 0) && wait_ready(30000))
            res = RES_OK;
         break;
      }
      default:
         res = RES_PARERR;
   }

   // Disable SD card
   deselect_sd();
   return res;
}

void disk_enable(void) { SD_POWER_ON(); }
void disk_disable(void) { SD_POWER_OFF(); }
void disk_restart(void)
{
   SD_POWER_OFF();
   nrf_delay_ms(10);
   SD_POWER_ON();
}
