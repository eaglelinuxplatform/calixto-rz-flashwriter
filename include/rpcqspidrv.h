/*
 * Copyright (c) 2015-2018, Renesas Electronics Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define SPI_IOADDRESS_TOP		0x20000000		//RPC memory space 0x20000000-0x2FFFFFFF = 256Bytes
#define RPC_CLK_40M			0x01
#define RPC_CLK_80M			0x02
#define RPC_CLK_160M			0x03


void InitRPC_QspiFlashQuadExtMode(void);
void InitRPC_QspiFlash4FastReadExtMode(void);
void InitRPC_QspiFlash(uint32_t rpcclk);
void ReadConfigRegQspiFlash(uint32_t *cnfigReg);
void WriteRegisterQspiFlash(uint32_t statusReg, uint32_t configReg);
void WriteRegisterQspiFlash_Byte2(uint32_t statusReg, uint32_t configReg);
void SectorEraseQspiFlash(uint32_t sector_addr);
void SectorErase4QspiFlash(uint32_t sector_addr);
void WriteData4ppWithBufferQspiFlash(uint32_t addr, uint32_t source_addr);
void WriteData4ppQspiFlash(uint32_t addr, uint32_t writeData);
void WriteData4ppQspiFlash_CsCont(uint32_t addr, uint32_t *writeData,uint32_t cnt);
void WriteData4qppQspiFlash(uint32_t addr, uint32_t writeData);
uint32_t SingleFastReadQspiFlashData4Byte(uint32_t addr, uint32_t *readData);	//for QSPIx1ch
uint32_t SingleFastReadQspiFlashData1Byte(uint32_t addr, uint32_t *readData);	//for QSPIx1ch
uint32_t ReadAnyRegisterQspiFlash(uint32_t addr, unsigned char *readData);	// Add24bit,Data8bit
void WriteAnyRegisterQspiFlash(uint32_t addr, unsigned char writeData);		// Add24bit,Data8bit
void SetRPC_ClockMode(uint32_t mode);
void WaitRpcTxEnd(void);

void InitRPC_QspiFlashFastReadExtMode(void);
void WriteDataPpWithBufferQspiFlash(uint32_t addr, uint32_t source_addr);

void ParameterSectorErase3QspiFlash(uint32_t sector_addr);
void ParameterSectorErase4QspiFlash(uint32_t sector_addr);
void ResetRPC(void);
//void SetResetRPC(void);
//void ClearResetRPC(void);
void SetRPC_SSL_Delay(void);

void PowerOnRPC(void);
uint32_t ReadQspiFlashID(uint32_t *readData);		//for QSPIx1ch
uint32_t ReadStatusQspiFlash(uint32_t *readData);	//for QSPIx1ch
void WriteCommandQspiFlash(uint32_t command);		//for QSPIx1ch
void WriteDataWithBufferQspiFlash(uint32_t addr, uint32_t source_addr);	//for QSPIx1ch
void SectorEraseQspiFlash(uint32_t sector_addr);	//for QSPIx1ch
void InitRPC_ExtMode_QuadIORead(void);			//for QSPIx1ch
void EnableQuadModeQspiFlashS25fs128s(void);
