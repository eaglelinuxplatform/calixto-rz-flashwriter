/*
 * Copyright (c) 2015-2016, Renesas Electronics Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file  emmc_mount.c
 * @brief MMC card mount operation.
 *
 */

/* ************************ HEADER (INCLUDE) SECTION *********************** */
#include "emmc_config.h"
#include "emmc_hal.h"
#include "emmc_std.h"
#include "emmc_registers.h"
#include "emmc_def.h"
#include "common.h"
#include "types.h"
#include "bit.h"
#include "cpudrv.h"

#define MMC_CMD1_TIMEOUT	3000
/* ***************** MACROS, CONSTANTS, COMPILATION FLAGS ****************** */

/* ********************** STRUCTURES, TYPE DEFINITIONS ********************* */

/* ********************** DECLARATION OF EXTERNAL DATA ********************* */

/* ************************** FUNCTION PROTOTYPES ************************** */
static EMMC_ERROR_CODE emmc_clock_ctrl(uint8_t mode);
static EMMC_ERROR_CODE emmc_card_init (void);
static EMMC_ERROR_CODE emmc_high_speed(void);
static EMMC_ERROR_CODE emmc_bus_width(uint32_t width);
//static EMMC_ERROR_CODE emmc_check_pattern(uint8_t *pat, uint32_t size);
static uint32_t emmc_set_timeout_register_value(uint32_t freq);
static void set_sd_clk(uint32_t clkDiv);
static uint32_t emmc_calc_tran_speed(uint32_t* freq);

/* ********************************* CODE ********************************** */

/** @brief eMMC mount operation.
 *
 * Sequence is the following.
 * 1) Bus initialization (emmc_card_init())
 * 2) Switching to high speed mode. (emmc_high_speed())
 * 3) Changing the data bus width. (emmc_bus_width())
 * 
 * - Pre-conditions:<BR>
 * eMMC driver is initialized. The power supply of MMC IF must be turning on. 
 * - Post-conditions:<BR>
 * MMC card state changes to transfer state.
 * 
 * @return eMMC error code.
 */
EMMC_ERROR_CODE emmc_mount(void)
{
    EMMC_ERROR_CODE result;

    /* state check */
    if ( (mmc_drv_obj.initialize != TRUE)
        || (mmc_drv_obj.card_power_enable != TRUE)
        || ( (GETR_32(SD_INFO2) & SD_INFO2_CBSY) != 0)
        )
    {
        emmc_write_error_info(EMMC_FUNCNO_MOUNT, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

    /* initialize card (IDLE state --> Transfer state) */
    result = emmc_card_init();
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        if (emmc_clock_ctrl(FALSE) != EMMC_SUCCESS)
        {
            /* nothing to do. */
        }
        return result;
    }

    /* Switching high speed mode */
    result = emmc_high_speed();
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_HIGH_SPEED);
        if (emmc_clock_ctrl(FALSE) != EMMC_SUCCESS)
        {
            /* nothing to do. */
        }
        return result;
    }

    /* Changing the data bus width */
    result = emmc_bus_width(8);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_BUS_WIDTH);
        if (emmc_clock_ctrl(FALSE) != EMMC_SUCCESS)
        {
            /* nothing to do. */
        }
        return result;
    }

    /* mount complete */
    mmc_drv_obj.mount = TRUE;

    return EMMC_SUCCESS;
}

/** @brief Bus initialization function
 *
 * - Pre-conditions:<BR>
 * eMMC driver is initialized. The power supply of MMC IF must be turning on. 
 * - Post-conditions:<BR>
 * MMC card state changes to transfer state.
 * 
 * @retval EMMC_SUCCESS successful.
 * @return eMMC error code.
 * @attention upper layer must be check pre-conditions.
 */
static EMMC_ERROR_CODE emmc_card_init (void)
{
    int32_t retry;
    uint32_t  freq = MMC_400KHZ;      /* 390KHz */
    EMMC_ERROR_CODE result;
	uint32_t	resultCalc;

    /* state check */
    if ( (mmc_drv_obj.initialize != TRUE)
        || (mmc_drv_obj.card_power_enable != TRUE)
        || ((GETR_32(SD_INFO2) & SD_INFO2_CBSY) != 0)
        )
    {
        emmc_write_error_info(EMMC_FUNCNO_CARD_INIT, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

    /* clock on (force change) */
    mmc_drv_obj.current_freq = 0;
    mmc_drv_obj.max_freq = MMC_20MHZ;     /* MMC_20MHZ = MMC_12MHZ = 12.187MHz */
    result = emmc_set_request_mmc_clock(&freq);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return EMMC_ERR;
    }

#ifdef REWRITE_TOOL
	StartTMU0usec(100);			/* wait 1ms */

	/* CMD0, arg=0x00000000 */
    result = emmc_send_idle_cmd (0x00000000);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }
#endif /* REWRITE_TOOL */

	StartTMU0usec(20);			/* wait 74clock 390kHz(189.74us)*/

   /* CMD1 */
    emmc_make_nontrans_cmd(CMD1_SEND_OP_COND, EMMC_HOST_OCR_VALUE);
    for (retry=MMC_CMD1_TIMEOUT; retry > 0; retry--)
    {
        result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
        if (result != EMMC_SUCCESS)
        {
            emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
            return result;
        }

    	if ((mmc_drv_obj.r3_ocr & EMMC_OCR_STATUS_BIT) != 0)
        {
            break;          /* card is ready. exit loop */
        }
		StartTMU0usec(100);			/* wait 1ms */
    }

    if (retry == 0)
    {
        emmc_write_error_info(EMMC_FUNCNO_CARD_INIT, EMMC_ERR_TIMEOUT);
        return EMMC_ERR_TIMEOUT;
    }

    switch (mmc_drv_obj.r3_ocr & EMMC_OCR_ACCESS_MODE_MASK)
    {
        case EMMC_OCR_ACCESS_MODE_SECT:
            mmc_drv_obj.access_mode = TRUE;     /* sector mode */
            break;
        default:
            /* unknown value */
            emmc_write_error_info(EMMC_FUNCNO_CARD_INIT, EMMC_ERR);
            return EMMC_ERR;
    }

    /* CMD2 */
    emmc_make_nontrans_cmd(CMD2_ALL_SEND_CID_MMC, 0x00000000);
    mmc_drv_obj.response = (uint32_t *)(&mmc_drv_obj.cid_data[0]);        /* use CID special buffer */
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }

    /* CMD3 */
    emmc_make_nontrans_cmd(CMD3_SET_RELATIVE_ADDR, EMMC_RCA<<16);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }

    /* CMD9 (CSD) */
    emmc_make_nontrans_cmd(CMD9_SEND_CSD, EMMC_RCA<<16);
    mmc_drv_obj.response = (uint32_t *)(&mmc_drv_obj.csd_data[0]);        /* use CSD special buffer */
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }

    /* card version check */
    if (EMMC_CSD_SPEC_VARS() < 4)
    {
        emmc_write_error_info(EMMC_FUNCNO_CARD_INIT, EMMC_ERR_ILLEGAL_CARD);
        return EMMC_ERR_ILLEGAL_CARD;
    }

    /* CMD7 (select card) */
    emmc_make_nontrans_cmd(CMD7_SELECT_CARD, EMMC_RCA<<16);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }

    mmc_drv_obj.selected = TRUE;

    /* card speed check */
    resultCalc = emmc_calc_tran_speed( &freq );      /* Card spec is calculated from TRAN_SPEED(CSD).  */
    if (resultCalc == 0)
    {
        emmc_write_error_info(EMMC_FUNCNO_CARD_INIT, EMMC_ERR_ILLEGAL_CARD);
        return EMMC_ERR_ILLEGAL_CARD;
    }
    mmc_drv_obj.max_freq = freq;        /* max frequency (card spec) */

	result = emmc_set_request_mmc_clock(&freq);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return EMMC_ERR;
    }

	/* set read/write timeout */
	mmc_drv_obj.data_timeout = emmc_set_timeout_register_value( freq );
	SETR_32( SD_OPTION,((GETR_32(SD_OPTION) & ~(SD_OPTION_TIMEOUT_CNT_MASK)) | mmc_drv_obj.data_timeout));
	
	/* SET_BLOCKLEN(512byte) */
    /* CMD16 */
    emmc_make_nontrans_cmd(CMD16_SET_BLOCKLEN, EMMC_BLOCK_LENGTH);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }

	/* Transfer Data Length */
	SETR_32( SD_SIZE, EMMC_BLOCK_LENGTH );

	/* CMD8 (EXT_CSD) */
    emmc_make_trans_cmd(CMD8_SEND_EXT_CSD, 0x00000000, (uint32_t *)(&mmc_drv_obj.ext_csd_data[0]), EMMC_MAX_EXT_CSD_LENGTH, HAL_MEMCARD_READ, HAL_MEMCARD_NOT_DMA);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        /* CMD12 is not send.
         * If BUS initialization is failed, user must be execute Bus initialization again.
         * Bus initialization is start CMD0(soft reset command).
         */
        emmc_write_error_info_func_no(EMMC_FUNCNO_CARD_INIT);
        return result;
    }


    return EMMC_SUCCESS;
}

/** @brief Switching to high-speed mode
 * 
 * - Pre-conditions:<BR>
 * Executing Bus initializatin by emmc_card_init().
 * EXT_CSD data must be stored in mmc_drv_obj.ext_csd_data[]. 
 * 
 * - Post-conditions:<BR>
 * Change the clock frequency to 26MHz or 52MHz.
 * 
 * @retval EMMC_SUCCESS successful or aleady switching.
 * @retval EMMC_ERR_STATE state error.
 * @retval EMMC_ERR unknown error.
 * @return emmc error code.
 */
static EMMC_ERROR_CODE emmc_high_speed(void)
{
    uint32_t  freq;           /**< High speed mode clock frequency */
    EMMC_ERROR_CODE result;
    uint8_t cardType;

    /* state check */
    if (mmc_drv_obj.selected != TRUE)
    {
        emmc_write_error_info(EMMC_FUNCNO_HIGH_SPEED, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

	/* max frequency */
	cardType = (uint8_t)mmc_drv_obj.ext_csd_data[EMMC_EXT_CSD_CARD_TYPE];
	if( (cardType & EMMC_EXT_CSD_CARD_TYPE_52MHZ ) != 0 )
    {
        freq = MMC_52MHZ;
    }
    else if( (cardType & EMMC_EXT_CSD_CARD_TYPE_26MHZ ) != 0 )
    {
        freq = MMC_26MHZ;
    }
	else
	{
        freq = MMC_20MHZ;
	}

	/* Hi-Speed-mode selction */
	if( ( MMC_52MHZ == freq ) ||
		( MMC_26MHZ == freq ) )
	{
	    /* CMD6 */
	    emmc_make_nontrans_cmd(CMD6_SWITCH, EMMC_SWITCH_HS_TIMING);
	    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
		if (result != EMMC_SUCCESS)
	    {
	        emmc_write_error_info_func_no(EMMC_FUNCNO_HIGH_SPEED);
	        return result;
	    }

		mmc_drv_obj.hs_timing =  TIMING_HIGH_SPEED;  /* High-Speed */
	}

	/* set mmc clock */
	mmc_drv_obj.max_freq = freq;
    result = emmc_set_request_mmc_clock(&freq);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_HIGH_SPEED);
        return EMMC_ERR;
    }

	/* set read/write timeout */
	mmc_drv_obj.data_timeout = emmc_set_timeout_register_value( freq );
	SETR_32( SD_OPTION,((GETR_32(SD_OPTION) & ~(SD_OPTION_TIMEOUT_CNT_MASK)) | mmc_drv_obj.data_timeout));

	/* CMD13 */
    emmc_make_nontrans_cmd(CMD13_SEND_STATUS, EMMC_RCA<<16);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        emmc_write_error_info_func_no(EMMC_FUNCNO_HIGH_SPEED);
        return result;
    }

    return EMMC_SUCCESS;
}

/** @brief Changing the data bus width
 *
 * if chinging the data bus width failed, card is reset by CMD0.
 * Please do Bus initialization over again.
 * 
 * - Pre-conditions:<BR>
 * Executing Bus initializatin by emmc_card_init().
 * 
 * - Post-conditions:<BR>
 * Change the data bus width to 8bit or 4bit.
 * mmc_drv_obj.ext_csd_data is updated.
 *
 * @param[in] width bus width (8 or 4)
 * @retval EMMC_SUCCESS successful.
 * @retval EMMC_ERR_PARAM parameter error
 * @retval EMMC_ERR_STATE state error.
 *
 */
static EMMC_ERROR_CODE emmc_bus_width(
    uint32_t width
    )
{
    EMMC_ERROR_CODE result = EMMC_ERR;
#ifdef EMMC_DEBUG
	int32_t lchCnt;
	char buf[16];
#endif /* EMMC_DEBUG */

    /* parameter check */
    if ( (width != 8) && (width != 4) && (width != 1) )
    {
#ifdef EMMC_DEBUG
		PutStr("err:width = ",0);
		Hex2Ascii((int32_t)width,buf,&lchCnt);
		PutStr(buf,1);
#endif /* EMMC_DEBUG */
        emmc_write_error_info(EMMC_FUNCNO_BUS_WIDTH, EMMC_ERR_PARAM);
        return EMMC_ERR_PARAM;
    }

    /* state check */
    if (mmc_drv_obj.selected != TRUE)
    {
        emmc_write_error_info(EMMC_FUNCNO_BUS_WIDTH, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

    mmc_drv_obj.bus_width = (HAL_MEMCARD_DATA_WIDTH)(width>>2);     /* 2 = 8bit, 1 = 4bit, 0 =1bit */

    /* CMD6 */
    emmc_make_nontrans_cmd(CMD6_SWITCH, ( EMMC_SWITCH_BUS_WIDTH_1 | ( mmc_drv_obj.bus_width << 8 ) ) );
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
	if (result != EMMC_SUCCESS)
    {
        /* occurred error */
        mmc_drv_obj.bus_width = HAL_MEMCARD_DATA_WIDTH_1_BIT;
        goto EXIT;
    }

#ifdef EMMC_DEBUG
	PutStr("SD_OPTION before= ",0);
	Hex2Ascii((int32_t)GETR_32(SD_OPTION),buf,&lchCnt);
	PutStr(buf,1);
#endif /* EMMC_DEBUG */

	switch(mmc_drv_obj.bus_width)
	{
	case HAL_MEMCARD_DATA_WIDTH_1_BIT:
		SETR_32( SD_OPTION,((GETR_32(SD_OPTION) & ~(BIT15|BIT13)) | BIT15 ));
		break;
	case HAL_MEMCARD_DATA_WIDTH_4_BIT:
		SETR_32( SD_OPTION,(GETR_32(SD_OPTION) & ~(BIT15|BIT13)));
		break;
	case HAL_MEMCARD_DATA_WIDTH_8_BIT:
		SETR_32( SD_OPTION,((GETR_32(SD_OPTION) & ~(BIT15|BIT13)) | BIT13 ));
		break;
	default:
        goto EXIT;
	}

#ifdef EMMC_DEBUG
	PutStr("SD_OPTION after = ",0);
	Hex2Ascii((int32_t)GETR_32(SD_OPTION),buf,&lchCnt);
	PutStr(buf,1);
#endif /* EMMC_DEBUG */

	/* CMD13 */
    emmc_make_nontrans_cmd(CMD13_SEND_STATUS, EMMC_RCA<<16);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
#ifdef EMMC_DEBUG
        PutStr("err:CMD13",1);
#endif /* EMMC_DEBUG */
        goto EXIT;
    }

   /* CMD8 (EXT_CSD) */
    emmc_make_trans_cmd(CMD8_SEND_EXT_CSD, 0x00000000, (uint32_t *)(&mmc_drv_obj.ext_csd_data[0]), EMMC_MAX_EXT_CSD_LENGTH, HAL_MEMCARD_READ, HAL_MEMCARD_NOT_DMA);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
#ifdef EMMC_DEBUG
        PutStr("err:CMD8",1);
#endif /* EMMC_DEBUG */
        goto EXIT;
    }

    return EMMC_SUCCESS;

EXIT:

    emmc_write_error_info(EMMC_FUNCNO_BUS_WIDTH, result);
    PutStr("bus_width error end",1);
    return result;
}

/** @brief select access partition
 *
 * This function write the EXT_CSD register(PARTITION_ACCESS: PARTITION_CONFIG[2:0]).
 * 
 * - Pre-conditions:<BR>
 * MMC card is mounted.
 * 
 * - Post-conditions:<BR>
 * selected partition can access.
 *
 * @param[in] id user selects partitions to access.
 * @retval EMMC_SUCCESS successful.
 * @retval EMMC_ERR_STATE state error.
 * @retval EMMC_ERR_PARAM parameter error.
 * @return emmc error code.
 */
EMMC_ERROR_CODE emmc_select_partition(
    EMMC_PARTITION_ID id
    )
{
	EMMC_ERROR_CODE result;
	uint32_t arg;
    uint32_t partition_config;
#ifdef EMMC_DEBUG
	char buf[16];
	int32_t chCnt;
#endif /* EMMC_DEBUG */

    /* state check */
    if (mmc_drv_obj.mount != TRUE)
    {
        emmc_write_error_info(EMMC_FUNCNO_NONE, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

    /* id = PARTITION_ACCESS(Bit[2:0]) */
    if ((id & ~PARTITION_ID_MASK) != 0)
    {
        emmc_write_error_info(EMMC_FUNCNO_NONE, EMMC_ERR_PARAM);
        return EMMC_ERR_PARAM;
    }

    /* EXT_CSD[179] value */
	partition_config = (uint32_t)mmc_drv_obj.ext_csd_data[EMMC_EXT_CSD_PARTITION_CONFIG];
	if ((partition_config & PARTITION_ID_MASK) == id) {
		result = EMMC_SUCCESS;
	} else {
	
		partition_config = (uint32_t)((partition_config & ~PARTITION_ID_MASK) | id);
		arg = EMMC_SWITCH_PARTITION_CONFIG | (partition_config<<8);

	result = emmc_set_ext_csd(arg);
	}

#ifdef EMMC_DEBUG
	PutStr("  EXT_CSD[179] = ",0);
	Hex2DecAscii((int32_t)mmc_drv_obj.ext_csd_data[EMMC_EXT_CSD_PARTITION_CONFIG],buf,&chCnt);
	PutStr(buf,1);
#endif /* EMMC_DEBUG */

    return result;
}

/** @brief set EXT CSD data
 *
 * - Pre-conditions:<BR>
 * MMC card is mounted.
 * 
 * - Post-conditions:<BR>
 * mmc_drv_obj.ext_csd_data[] is updated.
 * 
 * @param[in] arg argument of CMD6
 * @return emmc error code.
 */
EMMC_ERROR_CODE emmc_set_ext_csd(
    uint32_t arg
    )
{
    EMMC_ERROR_CODE result;
//    st_error_info backup;

    /* CMD6 */
    emmc_make_nontrans_cmd(CMD6_SWITCH, arg);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        return result;
    }

	/* CMD13 */
    emmc_make_nontrans_cmd(CMD13_SEND_STATUS, EMMC_RCA<<16);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        return result;
    }

	/* CMD8 (EXT_CSD) */
    emmc_make_trans_cmd(CMD8_SEND_EXT_CSD, 0x00000000, (uint32_t *)(&mmc_drv_obj.ext_csd_data[0]), EMMC_MAX_EXT_CSD_LENGTH, HAL_MEMCARD_READ, HAL_MEMCARD_NOT_DMA);
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        return result;
    }
    return EMMC_SUCCESS;
}

/** @brief set request MMC clock frequency.
 * 
 * Function returns EMMC_SUCCESS if clock is already running in the desired frequency.
 * EMMC_ERR is returned if the HW doesn't support requested clock frequency.
 * If matching frequence cannot be set the closest frequence below should be selected.
 * For example if 50MHz is requested, but HW supports only 48MHz then 48MHz should be returned in the freq parameter.
 * 
 * - Pre-conditions:<BR>
 * initialized eMMC driver with emmc_init().
 * Memory card and MMCSDIO host controller needs to be powered up beforehand.
 * 
 * - Post-conditions:<BR>
 * Desired clock frequency is set to memory card IF. 
 *
 * @param[in] freq frequency [Hz]
 * @retval EMMC_SUCCESS successful.
 * @retval EMMC_ERR_STATE state error.
 * @retval EMMC_ERR busy
 */
EMMC_ERROR_CODE emmc_set_request_mmc_clock(
    uint32_t *freq
    )
{
    /* parameter check */
    if (freq == NULL)
    {
        emmc_write_error_info(EMMC_FUNCNO_SET_CLOCK, EMMC_ERR_PARAM);
        return EMMC_ERR_PARAM;
    }

    /* state check */
    if ( (mmc_drv_obj.initialize != TRUE) || (mmc_drv_obj.card_power_enable != TRUE) )
    {
        emmc_write_error_info(EMMC_FUNCNO_SET_CLOCK, EMMC_ERR_STATE);
        return EMMC_ERR_STATE;
    }

    /* clock is already running in the desired frequency. */
    if ( (mmc_drv_obj.clock_enable == TRUE) && (mmc_drv_obj.current_freq == *freq) )
    {
        return EMMC_SUCCESS;
    }

    /* busy check */
    if ((GETR_32(SD_INFO2) & SD_INFO2_CBSY) != 0)
    {
        emmc_write_error_info(EMMC_FUNCNO_SET_CLOCK, EMMC_ERR_CARD_BUSY);
        return EMMC_ERR;
    }

	set_sd_clk(*freq);
    mmc_drv_obj.clock_enable = FALSE;

    return emmc_clock_ctrl(TRUE);       /* clock on */
}

/** @brief set sd clock.
 *
 * - Pre-conditions:<BR>
 * CSD data must be stored in mmc_drv_obj.csd_data[].
 * 
 * - Post-conditions:<BR>
 * set mmc clock.
 *
 * @param[in] clkDiv  request freq
 * @return None.
 */
static void set_sd_clk(uint32_t clkDiv)
{
	uint32_t dataL;
#ifdef EMMC_DEBUG
	char str[16];
	int32_t chCnt;
#endif /* EMMC_DEBUG */

	dataL = (GETR_32((volatile uint32_t  *)SD_CLK_CTRL) & (~SD_CLK_CTRL_CLKDIV_MASK) );

#ifdef EMMC_DEBUG
    PutStr("set_sd_clk freq:", 0);
	Hex2DecAscii(clkDiv, str, &chCnt);
    PutStr(str, 1);
#endif /* EMMC_DEBUG */

	switch(clkDiv){
		case   1:   dataL |= 0x000000FFU;  break;	/* 1/1   */
		case   2:	dataL |= 0x00000000U;  break;	/* 1/2   */
		case   4:	dataL |= 0x00000001U;  break;	/* 1/4   */
		case   8:	dataL |= 0x00000002U;  break;	/* 1/8   */
		case  16:	dataL |= 0x00000004U;  break;	/* 1/16  */
		case  32:	dataL |= 0x00000008U;  break;	/* 1/32  */
		case  64:	dataL |= 0x00000010U;  break;	/* 1/64  */
		case 128:	dataL |= 0x00000020U;  break;	/* 1/128 */
		case 256:	dataL |= 0x00000040U;  break;	/* 1/256 */
		case 512:	dataL |= 0x00000080U;  break;	/* 1/512 */
	}

	SETR_32((volatile uint32_t  *)SD_CLK_CTRL, dataL);
	mmc_drv_obj.current_freq = (uint32_t)clkDiv;
}


/** @brief Enable/Disable MMC clock 
 *
 * - Pre-conditions:<BR>
 * Before enabling the clock for the first time the desired clock frequency must be set with 
 * emmc_set_clock_freq().
 * Berore setting mmc_drv_obj.data_timeout with emmc_set_data_timeout().
 * 
 * - Post-conditions:<BR>
 * After this function is called, clock to memory card IF is on/off. 
 * 
 * @param[in] mode TRUE = clock on, FALSE = clock off
 * @retval EMMC_SUCCESS succeeded
 * @retval EMMC_ERR     Busy
 */
static EMMC_ERROR_CODE emmc_clock_ctrl(
    uint8_t mode
    )
{
    uint32_t  value;

    /* busy check */
    if ((GETR_32(SD_INFO2) & SD_INFO2_CBSY) != 0)
    {
        emmc_write_error_info(EMMC_FUNCNO_SET_CLOCK, EMMC_ERR_CARD_BUSY);
        return EMMC_ERR;
    }

    if (mode == TRUE)
    {
        /* clock ON */
    	value = ((GETR_32(SD_CLK_CTRL) | MMC_SD_CLK_START) & SD_CLK_WRITE_MASK);
        SETR_32(SD_CLK_CTRL, value);    /* on  */
        mmc_drv_obj.clock_enable = TRUE;
    }
    else
    {
        /* clock OFF */
    	value = ((GETR_32(SD_CLK_CTRL) & MMC_SD_CLK_STOP) & SD_CLK_WRITE_MASK);
        SETR_32(SD_CLK_CTRL, value);    /* off */
        mmc_drv_obj.clock_enable = FALSE;
    }

    return EMMC_SUCCESS;
}

/** @brief Calculate Card support frequency.
 * TRAN_SPEED defines the clock frequency when not in high speed mode.
 *
 * - Pre-conditions:<BR>
 * CSD data must be stored in mmc_drv_obj.csd_data[].
 * 
 * - Post-conditions:<BR>
 * None.
 * @return Frquency[Hz]
 */
static uint32_t emmc_calc_tran_speed( uint32_t* freq )
{
    static const uint32_t unit[8] = {10000, 100000, 1000000, 10000000, 0, 0, 0, 0};                   /**< frequency unit (1/10) */
    static const uint32_t mult[16] = {0, 10, 12, 13, 15, 20, 26, 30, 35, 40, 45, 52, 55, 60, 70, 80}; /**< multiple factor (x10) */
	uint32_t maxFreq;
	uint32_t result;
    uint32_t tran_speed = EMMC_CSD_TRAN_SPEED();

	/* tran_speed = 0x32
     * unit[tran_speed&0x7] = uint[0x2] = 1000000
     * mult[(tran_speed&0x78)>>3] = mult[0x30>>3] = mult[6] = 26
     * 1000000 * 26 = 26000000 (26MHz)
     */

	result= 1;
	maxFreq = unit[tran_speed&EMMC_TRANSPEED_FREQ_UNIT_MASK] * mult[(tran_speed&EMMC_TRANSPEED_MULT_MASK)>>EMMC_TRANSPEED_MULT_SHIFT];

	if( maxFreq == 0 )
	{
		result= 0;
	}
	else if( MMC_FREQ_52MHZ <= maxFreq )
	{
		*freq = MMC_52MHZ;
	}
	else if( MMC_FREQ_26MHZ <= maxFreq )
	{
		*freq = MMC_26MHZ;
	}
	else if( MMC_FREQ_20MHZ <= maxFreq )
	{
		*freq = MMC_20MHZ;
	}
	else
	{
		*freq = MMC_400KHZ;
	}

    return result;
}

/** @brief Calculate read/write timeout.
 *
 * - Pre-conditions:<BR>
 * CSD data must be stored in mmc_drv_obj.csd_data[].
 * 
 * - Post-conditions:<BR>
 * set mmc clock.
 *
 * @param[in] freq  Base clock Div
 * @return    SD_OPTION Timeout Counter
 */
static uint32_t emmc_set_timeout_register_value(
    uint32_t freq
    )
{
	uint32_t timeoutCnt=0;	/* SD_OPTION   - Timeout Counter  */

	switch(freq){
		case   1:	timeoutCnt = 0xE0U;  break;  /* SDCLK * 2^27 */
		case   2:	timeoutCnt = 0xE0U;  break;  /* SDCLK * 2^27 */
		case   4:	timeoutCnt = 0xD0U;  break;  /* SDCLK * 2^26 */
		case   8:	timeoutCnt = 0xC0U;  break;  /* SDCLK * 2^25 */
		case  16:	timeoutCnt = 0xB0U;  break;  /* SDCLK * 2^24 */
		case  32:	timeoutCnt = 0xA0U;  break;  /* SDCLK * 2^23 */
		case  64:	timeoutCnt = 0x90U;  break;  /* SDCLK * 2^22 */
		case 128:	timeoutCnt = 0x80U;  break;  /* SDCLK * 2^21 */
		case 256:	timeoutCnt = 0x70U;  break;  /* SDCLK * 2^20 */
		case 512:	timeoutCnt = 0x60U;  break;  /* SDCLK * 2^19 */
	}

	return timeoutCnt;
}
