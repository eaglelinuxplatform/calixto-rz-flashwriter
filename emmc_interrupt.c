/*
 * Copyright (c) 2015-2016, Renesas Electronics Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** 
 * @file  emmc_interrupt.c
 * @brief interrupt service for MMC boot driver.
 *
 */

#include "emmc_config.h"
#include "emmc_hal.h"
#include "emmc_std.h"
#include "emmc_registers.h"
#include "emmc_def.h"
#include "common.h"
#include "bit.h"
#include "types.h"
/* ***************** MACROS, CONSTANTS, COMPILATION FLAGS ****************** */

/* ********************** STRUCTURES, TYPE DEFINITIONS ********************* */

/* ********************** DECLARATION OF EXTERNAL DATA ********************* */

/* ************************** FUNCTION PROTOTYPES ************************** */
static EMMC_ERROR_CODE emmc_trans_sector (uint32_t *buff_address_virtual);
static void InterruptRegLog(void);


/* ********************************* CODE ********************************** */



/** @brief emmc driver interrupt service routine.
 *
 * - Pre-conditions:<BR>
 * Must be block emmc driver state machine.
 * - Post-conditions:<BR>
 * unblocking emmc driver state machine.
 * 
 * @retval INT_SUCCESS
 */
uint32_t emmc_interrupt(void)
{
    EMMC_ERROR_CODE result;
#ifdef EMMC_DEBUG
	char str[16];
	int32_t chCnt;
#endif /* EMMC_DEBUG */

	/* SD_INFO */
	mmc_drv_obj.error_info.info1 = GETR_32(SD_INFO1);
	mmc_drv_obj.error_info.info2 = GETR_32(SD_INFO2);

	/* SD_INFO EVENT */
    mmc_drv_obj.int_event1 = mmc_drv_obj.error_info.info1 & GETR_32(SD_INFO1_MASK);
    mmc_drv_obj.int_event2 = mmc_drv_obj.error_info.info2 & GETR_32(SD_INFO2_MASK);

	/* ERR_STS */
	mmc_drv_obj.error_info.status1 = GETR_32(SD_ERR_STS1);
	mmc_drv_obj.error_info.status2 = GETR_32(SD_ERR_STS2);

	/* DM_CM_INFO */
	mmc_drv_obj.error_info.dm_info1 = GETR_32(DM_CM_INFO1);
	mmc_drv_obj.error_info.dm_info2 = GETR_32(DM_CM_INFO2);

	/* DM_CM_INFO EVENT */
    mmc_drv_obj.dm_event1 = mmc_drv_obj.error_info.dm_info1 & GETR_32(DM_CM_INFO1_MASK);
    mmc_drv_obj.dm_event2 = mmc_drv_obj.error_info.dm_info2 & GETR_32(DM_CM_INFO2_MASK);

	InterruptRegLog();

/* ERR SD_INFO2 */
	if( (SD_INFO2_ALL_ERR & mmc_drv_obj.int_event2) != 0 )
    {
        SETR_32(SD_INFO1_MASK, 0x00000000U);   /* interrupt disable */
        SETR_32(SD_INFO2_MASK, SD_INFO2_CLEAR );   /* interrupt disable */
        SETR_32(SD_INFO1, 0x00000000U);        /* interrupt clear */
        SETR_32(SD_INFO2, SD_INFO2_CLEAR );    /* interrupt clear */
        mmc_drv_obj.state_machine_blocking = FALSE;
    }

/* PIO Transfer */
	/* BWE/BRE */
	else if((( SD_INFO2_BWE | SD_INFO2_BRE ) & mmc_drv_obj.int_event2 ) != 0 )
    {
    	/* BWE */
    	if( SD_INFO2_BWE & mmc_drv_obj.int_event2 )
    	{
        	SETR_32(SD_INFO2, (GETR_32(SD_INFO2) & ~SD_INFO2_BWE) );    /* interrupt clear */
    	}
    	/* BRE */
    	else
    	{
        	SETR_32(SD_INFO2, (GETR_32(SD_INFO2) & ~SD_INFO2_BRE) );    /* interrupt clear */
    	}

#ifdef EMMC_DEBUG
	    PutStr("remain_size before= 0x", 0);
		Hex2Ascii(mmc_drv_obj.remain_size, str, &chCnt);
	    PutStr(str, 1);
#endif /* EMMC_DEBUG */

    	result = emmc_trans_sector((uint32_t *)mmc_drv_obj.buff_address_virtual);                 /* sector R/W */
        mmc_drv_obj.buff_address_virtual += EMMC_BLOCK_LENGTH;
    	mmc_drv_obj.remain_size -= EMMC_BLOCK_LENGTH;

#ifdef EMMC_DEBUG
	    PutStr("remain_size after= 0x", 0);
		Hex2Ascii(mmc_drv_obj.remain_size, str, &chCnt);
	    PutStr(str, 1);
#endif /* EMMC_DEBUG */

        if(result != EMMC_SUCCESS)
        {
#ifdef EMMC_DEBUG
            PutStr("err:Transfer",1);
#endif /* EMMC_DEBUG */
            /* data transfer error */
            emmc_write_error_info(EMMC_FUNCNO_NONE, result);

            /* Panic */
            SETR_32(SD_INFO1_MASK, 0x00000000U);   /* interrupt disable */
            SETR_32(SD_INFO2_MASK, SD_INFO2_CLEAR );   /* interrupt disable */
            SETR_32(SD_INFO1, 0x00000000U);    /* interrupt clear */
            SETR_32(SD_INFO2, SD_INFO2_CLEAR );    /* interrupt clear */
            mmc_drv_obj.force_terminate = TRUE;
        }
    	else
    	{
#ifdef EMMC_DEBUG
		    PutStr("Transfer End", 0);
#endif /* EMMC_DEBUG */
			mmc_drv_obj.during_transfer = FALSE;
        }
        mmc_drv_obj.state_machine_blocking = FALSE;
    }

/* DMA_TRANSFER */
	/* DM_CM_INFO1: DMA-ch0 transfer complete or error occured */
	else if( (BIT16 & mmc_drv_obj.dm_event1) != 0 )
	{  
		SETR_32(DM_CM_INFO1, 0x00000000U);
		SETR_32(DM_CM_INFO2, 0x00000000U);
		SETR_32(SD_INFO2, (GETR_32(SD_INFO2) & ~SD_INFO2_BWE) );    /* interrupt clear */
		/* DM_CM_INFO2:  DMA-ch0 error occured */
		if( ( BIT16 & mmc_drv_obj.dm_event2 ) != 0 )
		{
#ifdef EMMC_DEBUG
			PutStr("DMA-ch0 error",1);
#endif /* EMMC_DEBUG */
			mmc_drv_obj.dma_error_flag = TRUE;
		}
		else
		{
#ifdef EMMC_DEBUG
			PutStr("DMA-ch0 end",1);
#endif /* EMMC_DEBUG */
			mmc_drv_obj.during_dma_transfer = FALSE;
			mmc_drv_obj.during_transfer = FALSE;
		}
        mmc_drv_obj.state_machine_blocking = FALSE;		/* wait next interrupt */
	}
	/* DM_CM_INFO1: DMA-ch1 transfer complete or error occured */
	else if( (BIT17 & mmc_drv_obj.dm_event1) != 0 )
	{
		SETR_32(DM_CM_INFO1, 0x00000000U);
		SETR_32(DM_CM_INFO2, 0x00000000U);
		SETR_32(SD_INFO2, (GETR_32(SD_INFO2) & ~SD_INFO2_BRE) );    /* interrupt clear */
		/* DM_CM_INFO2: DMA-ch1 error occured */
		if( ( BIT17 & mmc_drv_obj.dm_event2 ) != 0 )
		{
#ifdef EMMC_DEBUG
			PutStr("DMA-ch1 error",1);
#endif /* EMMC_DEBUG */
			mmc_drv_obj.dma_error_flag = TRUE;
		}
		else
		{
#ifdef EMMC_DEBUG
			PutStr("DMA-ch1 end",1);
#endif /* EMMC_DEBUG */
			mmc_drv_obj.during_dma_transfer = FALSE;
			mmc_drv_obj.during_transfer = FALSE;
		}
        mmc_drv_obj.state_machine_blocking = FALSE;		/* wait next interrupt */
	}

	/* Response end  */
    else if( (SD_INFO1_INFO0 & mmc_drv_obj.int_event1) != 0)
	{
#ifdef EMMC_DEBUG
		PutStr("Response end",1);
#endif /* EMMC_DEBUG */
		SETR_32(SD_INFO1, (GETR_32(SD_INFO1) & ~SD_INFO1_INFO0) );    /* interrupt clear */
        mmc_drv_obj.state_machine_blocking = FALSE;
	}
	/* Access end  */
    else if( (SD_INFO1_INFO2 & mmc_drv_obj.int_event1) != 0)
	{
#ifdef EMMC_DEBUG
		PutStr("Access end",1);
#endif /* EMMC_DEBUG */
		SETR_32(SD_INFO1, (GETR_32(SD_INFO1) & ~SD_INFO1_INFO2) );    /* interrupt clear */
        mmc_drv_obj.state_machine_blocking = FALSE;
	}
    else
    {
    	/* nothing to do. */
    }

#ifdef EMMC_DEBUG
	PutStr("", 1);
#endif /* EMMC_DEBUG */
    return (uint32_t)0;
}

/** @brief Data transfer function with PIO (Single sector).
 *
 * - Pre-conditions:<BR>
 * Called from interrupt service.
 * - Post-conditions:<BR>
 * .
 * 
 * @param[in,out] buff_address_virtual Dest/Src buffer address(virtual).
 * @retval EMMC_SUCCESS successful.
 * @retval EMMC_ERR_PARAM parameter error.
 * @retval EMMC_ERR_STATE state error.
 */
static EMMC_ERROR_CODE emmc_trans_sector (
    uint32_t *buff_address_virtual
    )
{
	uint32_t length,i;
	uint64_t *bufPtrLL;
#ifdef EMMC_DEBUG
	char str[16];
	int32_t chCnt;
#endif /* EMMC_DEBUG */

    if (buff_address_virtual == NULL)
    {
#ifdef EMMC_DEBUG
		PutStr("address err",1);
#endif /* EMMC_DEBUG */
        return EMMC_ERR_PARAM;
    }

    if ((mmc_drv_obj.during_transfer != TRUE) || (mmc_drv_obj.remain_size == 0) )
    {
#ifdef EMMC_DEBUG
		PutStr("status err",1);
#endif /* EMMC_DEBUG */
        return EMMC_ERR_STATE;
    }

	bufPtrLL = (uint64_t*)buff_address_virtual;
	length = mmc_drv_obj.remain_size;

#ifdef EMMC_DEBUG
	PutStr("remain_size = 0x",0);
	Hex2Ascii(mmc_drv_obj.remain_size, str, &chCnt);
    PutStr(str, 1);
	PutStr("length      = 0x",0);
	Hex2Ascii(length, str, &chCnt);
    PutStr(str, 1);
#endif /* EMMC_DEBUG */

	/* data transefer */
	for (i=0; i<(length>>3);i++)
    {
		/* Write */
	    if (mmc_drv_obj.cmd_info.dir == HAL_MEMCARD_WRITE)
	    {
	        SETR_64(SD_BUF0, *bufPtrLL);     /* buffer --> FIFO */
		}
    	/* Read */
		else
		{
            *bufPtrLL = GETR_64(SD_BUF0);    /* FIFO --> buffer */
        }
        bufPtrLL++;
	}

#ifdef EMMC_DEBUG
	PutStr("i = 0x",0);
	Hex2Ascii(i, str, &chCnt);
    PutStr(str, 1);
#endif /* EMMC_DEBUG */

    return EMMC_SUCCESS;
}

/** @brief debug log interrupt register
 *
 * - Pre-conditions:<BR>
 * after emmc_interrupt.
 * - Post-conditions:<BR>
 * .
 * 
 * @return None.
 */
static void InterruptRegLog(void)
{
#ifdef EMMC_DEBUG
	char str[16];
	int32_t chCnt;

	/* SD_INFO */
    PutStr("info  SD_INFO1    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.info1, str, &chCnt);
    PutStr(str, 1);
	PutStr("info  SD_INFO2    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.info2, str, &chCnt);
    PutStr(str, 1);
	/* SD_INFO EVENT */
    PutStr("event SD_INFO1    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.int_event1, str, &chCnt);
    PutStr(str, 1);
	PutStr("event SD_INFO2    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.int_event2, str, &chCnt);
    PutStr(str, 1);
	/* ERR_STS */
    PutStr("error ERR_STS1    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.status1, str, &chCnt);
    PutStr(str, 1);
    PutStr("error ERR_STS2    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.status2, str, &chCnt);
    PutStr(str, 1);
	/* DM_CM_INFO */
    PutStr("info  DM_INFO1    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.dm_info1, str, &chCnt);
    PutStr(str, 1);
    PutStr("info  DM_INFO2    = 0x", 0);
	Hex2Ascii(mmc_drv_obj.error_info.dm_info2, str, &chCnt);
    PutStr(str, 1);
	/* DM_CM_INFO EVENT */
    PutStr("event DM_CM_INFO1 = 0x", 0);
	Hex2Ascii(mmc_drv_obj.dm_event1, str, &chCnt);
    PutStr(str, 1);
    PutStr("event DM_CM_INFO2 = 0x", 0);
	Hex2Ascii(mmc_drv_obj.dm_event2, str, &chCnt);
    PutStr(str, 1);
#endif /* EMMC_DEBUG */
}

/* ******************************** END ************************************ */

