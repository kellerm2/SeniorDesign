/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  ******************************************************************************
  */
 /* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

#define SD_CS_PORT GPIOC
#define SD_CS_PIN  GPIO_PIN_1
#define SELECT()   HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET)
#define DESELECT() HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET)

static volatile DSTATUS Stat = STA_NOINIT;
static uint8_t CardType; // Tracks if card is SD or SDHC

static uint8_t SPI_TransmitReceive(uint8_t data) {
    uint8_t received = 0;
    HAL_SPI_TransmitReceive(&hspi2, &data, &received, 1, 10);
    return received;
}

static uint8_t Send_CMD(uint8_t cmd, uint32_t arg) {
    uint8_t res, n;

    if (cmd & 0x80) { // Handle App Commands (ACMD)
        cmd &= 0x7F;
        res = Send_CMD(55, 0);
        if (res > 1) return res;
    }

    DESELECT();
    SPI_TransmitReceive(0xFF);
    SELECT();
    SPI_TransmitReceive(0xFF);

    SPI_TransmitReceive(0x40 | cmd);
    SPI_TransmitReceive((uint8_t)(arg >> 24));
    SPI_TransmitReceive((uint8_t)(arg >> 16));
    SPI_TransmitReceive((uint8_t)(arg >> 8));
    SPI_TransmitReceive((uint8_t)arg);

    n = 0x01;
    if (cmd == 0) n = 0x95;
    if (cmd == 8) n = 0x87;
    SPI_TransmitReceive(n);

    n = 10;
    do { res = SPI_TransmitReceive(0xFF); } while ((res & 0x80) && --n);
    return res;
}
/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    uint8_t n, ty, cmd, ocr[4];
    uint16_t tmr;

    Stat = STA_NOINIT;
    DESELECT();
    for (n = 10; n; n--) SPI_TransmitReceive(0xFF);

    ty = 0;
    if (Send_CMD(0, 0) == 1) {
        if (Send_CMD(8, 0x1AA) == 1) {
            for (n = 0; n < 4; n++) ocr[n] = SPI_TransmitReceive(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
                for (tmr = 1000; tmr; tmr--) {
                    if (Send_CMD(0x80 | 41, 1UL << 30) == 0) break;
                    HAL_Delay(1);
                }
                if (tmr && Send_CMD(58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = SPI_TransmitReceive(0xFF);
                    ty = (ocr[0] & 0x40) ? 6 : 2;
                }
            }
        } else {
            if (Send_CMD(0x80 | 41, 0) <= 1) {
                ty = 2; cmd = 0x80 | 41;
            } else {
                ty = 1; cmd = 1;
            }
            for (tmr = 1000; tmr; tmr--) {
                if (Send_CMD(cmd, 0) == 0) break;
                HAL_Delay(1);
            }
            if (!tmr || Send_CMD(16, 512) != 0) ty = 0;
        }
    }
    CardType = ty;
    DESELECT();

    if (ty) Stat &= ~STA_NOINIT;
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (!(CardType & 4)) sector *= 512;

    while (count--) {
        if (Send_CMD(17, sector) == 0) {
            uint16_t timeout = 40000;
            while (SPI_TransmitReceive(0xFF) != 0xFE && --timeout);
            if (timeout > 0) {
                for (int i = 0; i < 512; i++) *buff++ = SPI_TransmitReceive(0xFF);
                SPI_TransmitReceive(0xFF);
                SPI_TransmitReceive(0xFF);
            } else {
                DESELECT();
                return RES_ERROR;
            }
        } else {
            DESELECT();
            return RES_ERROR;
        }
        sector += (CardType & 4) ? 1 : 512;
    }
    DESELECT();
    SPI_TransmitReceive(0xFF);
    return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (!(CardType & 4)) sector *= 512;

    while (count--) {
        if (Send_CMD(24, sector) == 0) {
            SPI_TransmitReceive(0xFE);
            for (int i = 0; i < 512; i++) SPI_TransmitReceive(*buff++);
            SPI_TransmitReceive(0xFF);
            SPI_TransmitReceive(0xFF);

            if ((SPI_TransmitReceive(0xFF) & 0x1F) == 0x05) {
                uint16_t timeout = 40000;
                while (SPI_TransmitReceive(0xFF) == 0 && --timeout);
                if (timeout == 0) { DESELECT(); return RES_ERROR; }
            } else {
                DESELECT(); return RES_ERROR;
            }
        } else {
            DESELECT(); return RES_ERROR;
        }
        sector += (CardType & 4) ? 1 : 512;
    }
    DESELECT();
    SPI_TransmitReceive(0xFF);
    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    DRESULT res = RES_ERROR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (cmd == CTRL_SYNC) {
        SELECT();
        while (SPI_TransmitReceive(0xFF) == 0);
        DESELECT();
        res = RES_OK;
    }
    return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

