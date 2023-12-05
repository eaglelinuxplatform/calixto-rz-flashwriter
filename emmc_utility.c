/*
 * Copyright (c) 2015-2016, Renesas Electronics Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** 
 * @file  emmc_utility.c
 * @brief MMC card utility program.
 *
 */

/* ************************ HEADER (INCLUDE) SECTION *********************** */

#include "emmc_config.h"
#include "emmc_hal.h"
#include "emmc_std.h"
#include "emmc_registers.h"
#include "emmc_def.h"
#include "common.h"
#include "bit.h"
/* ***************** MACROS, CONSTANTS, COMPILATION FLAGS ****************** */

static const uint32_t cmd_reg_hw[EMMC_CMD_MAX+1]= {
    0x00000000,     /* CMD0 */
    0x00000701,     /* CMD1 */
    0x00000002,     /* CMD2 */
    0x00000003,     /* CMD3 */
    0x00000004,     /* CMD4 */
    0x00000505,     /* CMD5 */
    0x00000406,     /* CMD6 */
    0x00000007,     /* CMD7 */
    0x00001C08,     /* CMD8 */
    0x00000009,     /* CMD9 */
    0x0000000A,     /* CMD10 */
    0x00000000,     /* reserved */
    0x0000000C,     /* CMD12 */
    0x0000000D,     /* CMD13 */
    0x00001C0E,     /* CMD14 */
    0x0000000F,     /* CMD15 */
    0x00000010,     /* CMD16 */
    0x00000011,     /* CMD17 */
    0x00007C12,     /* CMD18 */
    0x00000C13,     /* CMD19 */
    0x00000000,
    0x00001C15,     /* CMD21 */
    0x00000000,
    0x00000017,     /* CMD23 */
    0x00000018,     /* CMD24 */
    0x00006C19,     /* CMD25 */
    0x00000C1A,     /* CMD26 */
    0x0000001B,     /* CMD27 */
    0x0000001C,     /* CMD28 */
    0x0000001D,     /* CMD29 */
    0x0000001E,     /* CMD30 */
    0x00001C1F,     /* CMD31 */
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000423,     /* CMD35 */
    0x00000424,     /* CMD36 */
    0x00000000,
    0x00000026,     /* CMD38 */
    0x00000427,     /* CMD39 */
    0x00000428,     /* CMD40(send cmd) */
    0x00000000,
    0x0000002A,     /* CMD42 */
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000C31,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00007C35,
    0x00006C36,
    0x00000037,     /* CMD55 */
    0x00000038,     /* CMD56(Read) */
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
};
/* ********************** STRUCTURES, TYPE DEFINITIONS ********************* */

/* ********************** DECLARATION OF EXTERNAL DATA ********************* */

/* ************************** FUNCTION PROTOTYPES ************************** */

/* ********************************* CODE ********************************** */

/** @brief make non-transfer command data
 * 
 * Response data buffer is automatically selected.
 * 
 * - Pre-conditions:<BR>
 * Clock to memory card IF is enabled.
 * 
 * - Post-conditions:<BR>
 * After this function is called, command can be executed.
 * 
 * @param[in] cmd command information.
 * @param[in] arg command argument
 * @return None.
 */
void emmc_make_nontrans_cmd (
    HAL_MEMCARD_COMMAND cmd,
    uint32_t arg
    )
{
#ifdef EMMC_DEBUG
	int32_t lchCnt;
	char buf[16];

	PutStr("",1);
	PutStr("************************ CMD ",0);
	Hex2DecAscii((int32_t)cmd&0x3F,buf,&lchCnt);
	PutStr(buf,0);
	PutStr(" ************************",1);
#endif /* EMMC_DEBUG */

    /* command information */
    mmc_drv_obj.cmd_info.cmd = cmd;
    mmc_drv_obj.cmd_info.arg = arg;
    mmc_drv_obj.cmd_info.dir = HAL_MEMCARD_READ;
    mmc_drv_obj.cmd_info.hw = cmd_reg_hw[cmd&HAL_MEMCARD_COMMAND_INDEX_MASK];

    /* clear data transfer information */
    mmc_drv_obj.trans_size = 0;
    mmc_drv_obj.remain_size = 0;
    mmc_drv_obj.buff_address_virtual = NULL;
    mmc_drv_obj.buff_address_physical = NULL;

    /* response information */
    mmc_drv_obj.response_length = 6;

    switch (mmc_drv_obj.cmd_info.cmd & HAL_MEMCARD_RESPONSE_TYPE_MASK)
    {
        case HAL_MEMCARD_RESPONSE_NONE:
            mmc_drv_obj.response = (uint32_t *)mmc_drv_obj.response_data;
            mmc_drv_obj.response_length = 0;
            break;
        case HAL_MEMCARD_RESPONSE_R1:
            mmc_drv_obj.response = &mmc_drv_obj.r1_card_status;
            break;
        case HAL_MEMCARD_RESPONSE_R1b:
            mmc_drv_obj.cmd_info.hw |= BIT10;			/* bit10 = R1 busy bit */
            mmc_drv_obj.response = &mmc_drv_obj.r1_card_status;
            break;
        case HAL_MEMCARD_RESPONSE_R2:
            mmc_drv_obj.response = (uint32_t *)mmc_drv_obj.response_data;
            mmc_drv_obj.response_length = 17;
            break;
        case HAL_MEMCARD_RESPONSE_R3:
            mmc_drv_obj.response = &mmc_drv_obj.r3_ocr;
            break;
        case HAL_MEMCARD_RESPONSE_R4:
            mmc_drv_obj.response = &mmc_drv_obj.r4_resp;
            break;
        case HAL_MEMCARD_RESPONSE_R5:
            mmc_drv_obj.response = &mmc_drv_obj.r5_resp;
            break;
        default :
            mmc_drv_obj.response = (uint32_t *)mmc_drv_obj.response_data;
            break;
    }
}

/** @brief Making command information for data transfer command.
 *
 * - Pre-conditions:<BR>
 * None.
 * 
 * - Post-conditions:<BR>
 * After this function is called, command can be executed.
 *
 * @param[in] cmd command
 * @param[in] arg command argument
 * @param[in] buff_address_virtual Pointer to buffer where data is/will be stored. (virtual address) 
 *            Client is responsible of allocation and deallocation of the buffer.
 * @param[in] len transfer length in bytes
 * @param[in] dir direction
 * @param[in] transfer_mode Mode of data transfer, DMA or not DMA.
 * @return None.
 */
void emmc_make_trans_cmd (
    HAL_MEMCARD_COMMAND cmd,
    uint32_t arg,
    uint32_t *buff_address_virtual,       /* virtual address */
    uint32_t len,
    HAL_MEMCARD_OPERATION dir,
    HAL_MEMCARD_DATA_TRANSFER_MODE transfer_mode
    )
{
    emmc_make_nontrans_cmd(cmd, arg);   /* update common information */

    /* for data transfer command */
    mmc_drv_obj.cmd_info.dir = dir;
    mmc_drv_obj.buff_address_virtual = buff_address_virtual;
    mmc_drv_obj.buff_address_physical = buff_address_virtual;
    mmc_drv_obj.trans_size = len;
    mmc_drv_obj.remain_size = len;
    mmc_drv_obj.transfer_mode = transfer_mode;
}

/** @brief Send idle command.
 * Function execute CMD0.
 *
 * - Pre-conditions:<BR>
 * Clock to MMC I/F enabled.
 * 
 * - Post-conditions:<BR>
 * Card reset to idle or pre-idle state.
 * 
 * @param[in] arg CMD0 argument.
 * @return error code
 */
EMMC_ERROR_CODE emmc_send_idle_cmd (
    uint32_t arg
    )
{
    EMMC_ERROR_CODE result;
    uint32_t  freq;

    /* initialize state */
    mmc_drv_obj.mount = FALSE;
    mmc_drv_obj.selected = FALSE;
    mmc_drv_obj.during_transfer = FALSE;
    mmc_drv_obj.during_cmd_processing = FALSE;
    mmc_drv_obj.during_dma_transfer = FALSE;
    mmc_drv_obj.dma_error_flag = FALSE;
    mmc_drv_obj.force_terminate = FALSE;
    mmc_drv_obj.state_machine_blocking = FALSE;

    mmc_drv_obj.bus_width = HAL_MEMCARD_DATA_WIDTH_1_BIT;
    mmc_drv_obj.max_freq = MMC_20MHZ;                        /* 20MHz */
    mmc_drv_obj.current_state = EMMC_R1_STATE_IDLE;
    
    /* CMD0 (MMC clock is current frequency. if Data transfer mode, 20MHz or higher.) */
    emmc_make_nontrans_cmd(CMD0_GO_IDLE_STATE, arg);    /* CMD0 */
    result = emmc_exec_cmd(EMMC_R1_ERROR_MASK, mmc_drv_obj.response);
    if (result != EMMC_SUCCESS)
    {
        return result;
    }

    /* change MMC clock(400KHz) */
    freq = MMC_400KHZ;
    result = emmc_set_request_mmc_clock(&freq);
    if (result != EMMC_SUCCESS)
    {
        return result;
    }
    
    return EMMC_SUCCESS;
}

/** @brief get bit field data for 16bytes data(CSD register).
 *
 * - Pre-conditions:<BR>
 * .
 * - Post-conditions:<BR>
 * .
 *
 * @param[in] data  16bytes data.
 * @param[in] top bit number(top). 128>top
 * @param[in] bottom bit number(bottom). (0<=bottom<=top)
 * @return bit field.
 */
uint32_t emmc_bit_field (uint8_t *data, uint32_t top, uint32_t bottom)
{
    uint32_t  value;

	uint32_t  index_top = (uint32_t)(15-(top>>3));
	uint32_t  index_bottom = (uint32_t)(15-(bottom>>3));

    if (index_top == index_bottom)
    {
        value = data[index_top];
    }
    else if ((index_top+1) == index_bottom)
    {
        value = (uint32_t)((data[index_top]<<8) | data[index_bottom]);
    }
    else if ((index_top+2) == index_bottom)
    {
        value = (uint32_t)((data[index_top]<<16) | (data[index_top+1]<<8) | data[index_top+2]);
    }
    else
    {
        value = (uint32_t)((data[index_top]<<24) | (data[index_top+1]<<16) | (data[index_top+2]<<8) | data[index_top+3]);
    }

	value = ((value >> (bottom&0x07)) & ((1<<(top-bottom+1))-1));

    return value;
}

/** @brief set error information 
 *
 * eMMC driver's error information is 16bytes.
 * Format is the following, 
 * 
 * - Function No (2byte)
 * - Error code (2byte)
 * - Interrupt flag (4byte)
 * - Interrupt status1 (4byte)
 * - Interrupt status2 (4byte)
 * 
 * - Pre-conditions:<BR>
 * . 
 * 
 * - Post-conditions:<BR>
 * .
 * 
 * @param[in] func_no function number.
 * @param[in] error_code EMMC error code.
 * @return  None.
 */
void emmc_write_error_info(
    uint16_t func_no,
    EMMC_ERROR_CODE error_code
    )
{
	char str[16];
	int32_t chCnt;

    mmc_drv_obj.error_info.num = func_no;
    mmc_drv_obj.error_info.code = (uint16_t)error_code;

    PutStr("err:func_no=", 0);
	Hex2Ascii((int32_t)func_no, str, &chCnt);
	PutStr(str,0);
    PutStr(" code=", 0);
	Hex2Ascii((int32_t)error_code, str, &chCnt);
	PutStr(str,1);

    if (func_no != EMMC_FUNCNO_NONE) 
    {
        /* empty */
    }
}


/** @brief set error function number 
 *
 * Write function Number only.
 * eMMC driver's error information is 16bytes.
 * Format is the following, 
 * 
 * - Function No (2byte)
 * - Error code (2byte)
 * - Interrupt flag (4byte)
 * - Interrupt status1 (4byte)
 * - Interrupt status2 (4byte)
 * 
 * - Pre-conditions:<BR>
 * . 
 * 
 * - Post-conditions:<BR>
 * .
 * 
 * @param[in] func_no function number
 * @return  None.
 */
void emmc_write_error_info_func_no (
    uint16_t func_no
    )
{
	char str[16];
	int32_t chCnt;

	mmc_drv_obj.error_info.num = func_no;

    PutStr("err:func_no=", 0);
	Hex2Ascii((int32_t)func_no, str, &chCnt);
	PutStr(str,1);
}

/* ******************************** END ************************************ */

