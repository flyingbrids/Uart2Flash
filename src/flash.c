#include "xparameters.h"	/* SDK generated parameters */
#include "xqspips.h"		/* QSPI device driver */
#include "xqspips_hw.h"
#include "xil_printf.h"
#include "xdebug.h"
#include "xuartps_hw.h"
#include "xuartps.h"

#define UART_BASEADDR		XPAR_XUARTPS_0_BASEADDR

/*
 * The following constants define the commands which may be sent to the FLASH
 * device.
 */
#define WRITE_STATUS_CMD	0x01
#define WRITE_CMD		0x02
#define READ_CMD		0x03
#define WRITE_DISABLE_CMD	0x04
#define READ_STATUS_CMD		0x05
#define WRITE_ENABLE_CMD	0x06
#define FAST_READ_CMD		0x0B
#define DUAL_READ_CMD		0x3B
#define QUAD_READ_CMD		0x6B
#define BULK_ERASE_CMD		0xC7
#define	SEC_ERASE_CMD		0xD8
#define READ_ID			0x9F
#define DDR_ADDR0      0x0100000 + 0x2000000
#define DDR_ADDR1      DDR_ADDR0 + 0x3000000
/*
 * The following constants define the offsets within a FlashBuffer data
 * type for each kind of data.  Note that the read data offset is not the
 * same as the write data because the QSPI driver is designed to allow full
 * duplex transfers such that the number of bytes received is the number
 * sent and received.
 */
#define COMMAND_OFFSET		0 /* FLASH instruction */
#define ADDRESS_1_OFFSET	1 /* MSB byte of address to read or write */
#define ADDRESS_2_OFFSET	2 /* Middle byte of address to read or write */
#define ADDRESS_3_OFFSET	3 /* LSB byte of address to read or write */
#define DATA_OFFSET		4 /* Start of Data for Read/Write */
#define DUMMY_OFFSET		4 /* Dummy byte offset for fast, dual and quad
				   * reads
				   */
#define DUMMY_SIZE		1 /* Number of dummy bytes for fast, dual and
				   * quad reads
				   */
#define RD_ID_SIZE		4 /* Read ID command + 3 bytes ID response */
#define BULK_ERASE_SIZE		1 /* Bulk Erase command size */
#define SEC_ERASE_SIZE		4 /* Sector Erase command + Sector address */

/*
 * The following constants specify the extra bytes which are sent to the
 * FLASH on the QSPI interface, that are not data, but control information
 * which includes the command and address
 */
#define OVERHEAD_SIZE		4

/*
 * The following constants specify the page size, sector size, and number of
 * pages and sectors for the FLASH.  The page size specifies a max number of
 * bytes that can be written to the FLASH with a single transfer.
 */

#define SECTOR_SIZE		0x10000
#define PAGE_SIZE		256
#define NUM_SECTORS		0x100
#define BAUD_RATE       115200
#define FILE_SIZE       1614016 
//#define FILE_SIZE       167112
#define DUMMY_DATA_SIZE 0x8000
#define MAX_DATA        DUMMY_DATA_SIZE + FILE_SIZE      
#define XFER_CHUNK      1    
//#define UPLOAD_FILE
//#define LINEAR_READ
/************************** Function Prototypes ******************************/

void FlashErase(XQspiPs *QspiPtr, u32 Address, u32 ByteCount);

void FlashWrite(XQspiPs *QspiPtr, u32 Address, u32 ByteCount, u8 Command);

void FlashRead(XQspiPs *QspiPtr, u32 Address, u32 ByteCount, u8 Command);

int FlashReadID(void);

void FlashQuadEnable(XQspiPs *QspiPtr);

int QspiFlashPolledExample(XQspiPs *QspiInstancePtr, int offset);

XUartPs Uart_Ps;
static XQspiPs QspiInstance;

unsigned char *DDR_MEMB0  = (unsigned char* )DDR_ADDR0;
unsigned char *DDR_MEMB1  = (unsigned char* )DDR_ADDR1;
u8 ReadBuffer [DATA_OFFSET + DUMMY_SIZE];
u8 WriteBuffer[PAGE_SIZE + DATA_OFFSET];

void TeraTermFile_Receive ( u32 NoByteToRead)
{
     u32 FileByteCount=0;
     Xil_DCacheFlushRange((UINTPTR)DDR_MEMB0, NoByteToRead + DUMMY_DATA_SIZE);
     while (FileByteCount < NoByteToRead)
  	{
    	while (!XUartPs_IsReceiveData(UART_BASEADDR));
                DDR_MEMB0[FileByteCount + DUMMY_DATA_SIZE] = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
    			FileByteCount++;
  	}

}

void TeraTermFile_Transmit ( u32 NoByteToWrite)
{
        int SentCount = 0;        
        while (SentCount < NoByteToWrite) {
            #ifdef LINEAR_READ
                SentCount += XUartPs_Send(&Uart_Ps,(DDR_MEMB1+DUMMY_DATA_SIZE+SentCount), 1);
            #else
		        SentCount += XUartPs_Send(&Uart_Ps,(DDR_MEMB1+DUMMY_DATA_SIZE+DATA_OFFSET+SentCount), 1);
            #endif 
	    }

}

/************************** Main ******************************/
int main(void)
{
	int Status;	
    int SentCount;
    int Offset = 0;
    // UART Init 
    XUartPs_Config *Config;
    Config = XUartPs_LookupConfig(UART_BASEADDR);
	Status = XUartPs_CfgInitialize(&Uart_Ps, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	XUartPs_SetBaudRate(&Uart_Ps, BAUD_RATE);
    u32 CntrlRegister;
	CntrlRegister = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_CR_OFFSET);
	/* Enable TX and RX for the device */
	XUartPs_WriteReg(UART_BASEADDR, XUARTPS_CR_OFFSET,
			  ((CntrlRegister & ~XUARTPS_CR_EN_DIS_MASK) |
			   XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN));

    // Quad SPI flash Init
	XQspiPs_Config *QspiConfig;
	QspiConfig = XQspiPs_LookupConfig(XPAR_XQSPIPS_0_BASEADDR);
	if (QspiConfig == NULL) {
		return XST_FAILURE;
	}
	Status = XQspiPs_CfgInitialize(&QspiInstance, QspiConfig,QspiConfig->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/* Perform a self-test to check hardware build*/
	Status = XQspiPs_SelfTest(&QspiInstance);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
    	
	/* Set the prescaler for QSPI clock*/
	XQspiPs_SetClkPrescaler(&QspiInstance, XQSPIPS_CLK_PRESCALE_8);

    XQspiPs_SetOptions(&QspiInstance, XQSPIPS_MANUAL_START_OPTION |
			XQSPIPS_FORCE_SSELECT_OPTION |
			XQSPIPS_HOLD_B_DRIVE_OPTION);

	/* Assert the FLASH chip select.*/
	XQspiPs_SetSlaveSelect(&QspiInstance);
       
	FlashQuadEnable(&QspiInstance);

    FlashRead(&QspiInstance, Offset, MAX_DATA, READ_CMD);
   
    #ifdef UPLOAD_FILE
    TeraTermFile_Transmit (FILE_SIZE);
    #endif 
  
    XQspiPs_SetOptions(&QspiInstance, XQSPIPS_MANUAL_START_OPTION |
			XQSPIPS_FORCE_SSELECT_OPTION |
			XQSPIPS_HOLD_B_DRIVE_OPTION);
   
	/* Erase the flash.*/
	FlashErase(&QspiInstance, 0x0, NUM_SECTORS * SECTOR_SIZE);
    
    
    // loop ;
    //for (int t=0; t<XFER_CHUNK; t++) {   
        SentCount = 0;         
        u8 Wait[] = "Wait for file";
        while (SentCount < (sizeof(Wait) - 1)) {
		    SentCount += XUartPs_Send(&Uart_Ps,&Wait[SentCount], 1);
	    }
        TeraTermFile_Receive(FILE_SIZE);
        
        Status = QspiFlashPolledExample(&QspiInstance, Offset);
        //Offset = Offset + MAX_DATA;  
        if (Status != XST_SUCCESS) {
            u8 Fail[] = "Flash Failed";
            SentCount = 0;
    	    while (SentCount < (sizeof(Fail) - 1)) {
		        SentCount += XUartPs_Send(&Uart_Ps, &Fail[SentCount], 1);
	         }        
		    return XST_FAILURE;
	    }
   // }
	
    u8 Success[] = "Flash Success";
    SentCount = 0;
    while (SentCount < (sizeof(Success) - 1)) 
    {
		
		SentCount += XUartPs_Send(&Uart_Ps, &Success[SentCount], 1);
	}     
	return XST_SUCCESS;
}
 

int QspiFlashPolledExample(XQspiPs *QspiInstancePtr, int offset)
{
	int Page, PAGE_COUNT;
    if (MAX_DATA % PAGE_SIZE)
		PAGE_COUNT = MAX_DATA/PAGE_SIZE + 1;
	else
		PAGE_COUNT = MAX_DATA/PAGE_SIZE; 

	for (Page = 0; Page < PAGE_COUNT; Page++) {
		FlashWrite(QspiInstancePtr, (Page * PAGE_SIZE) + offset,
			   PAGE_SIZE, WRITE_CMD);
	}

	for (unsigned int Count = 0; Count < MAX_DATA; Count++) {
		if (*(DDR_MEMB0+Count) != *(DDR_MEMB1+Count + DATA_OFFSET)) {
			return XST_FAILURE;
		}
	}
	return XST_SUCCESS;
}

/************************** flash low level******************************/
void FlashWrite(XQspiPs *QspiPtr, u32 Address, u32 ByteCount, u8 Command)
{
	u8 WriteEnableCmd = { WRITE_ENABLE_CMD };
	u8 ReadStatusCmd[] = { READ_STATUS_CMD, 0 };  /* must send 2 bytes */
	u8 FlashStatus[2];

	/*
	 * Send the write enable command to the FLASH so that it can be
	 * written to, this needs to be sent as a separate transfer before
	 * the write
	 */
	XQspiPs_PolledTransfer(QspiPtr, &WriteEnableCmd, NULL,
				sizeof(WriteEnableCmd));


	/*
	 * Setup the write command with the specified address and data for the
	 * FLASH
	 */
	WriteBuffer[COMMAND_OFFSET]   = Command;
	WriteBuffer[ADDRESS_1_OFFSET] = (u8)((Address & 0xFF0000) >> 16);
	WriteBuffer[ADDRESS_2_OFFSET] = (u8)((Address & 0xFF00) >> 8);
	WriteBuffer[ADDRESS_3_OFFSET] = (u8)(Address & 0xFF);
    int Count;
	for (Count = 0; Count < PAGE_SIZE;Count++) {
		WriteBuffer[DATA_OFFSET + Count] = DDR_MEMB0[Count + Address];
	}
	/*
	 * Send the write command, address, and data to the FLASH to be
	 * written, no receive buffer is specified since there is nothing to
	 * receive
	 */
	XQspiPs_PolledTransfer(QspiPtr, WriteBuffer, NULL,
				ByteCount + OVERHEAD_SIZE);

	/*
	 * Wait for the write command to the FLASH to be completed, it takes
	 * some time for the data to be written
	 */
	while (1) {
		/*
		 * Poll the status register of the FLASH to determine when it
		 * completes, by sending a read status command and receiving the
		 * status byte
		 */
		XQspiPs_PolledTransfer(QspiPtr, ReadStatusCmd, FlashStatus,
					sizeof(ReadStatusCmd));

		/*
		 * If the status indicates the write is done, then stop waiting,
		 * if a value of 0xFF in the status byte is read from the
		 * device and this loop never exits, the device slave select is
		 * possibly incorrect such that the device status is not being
		 * read
		 */
		FlashStatus[1] |= FlashStatus[0];
		if ((FlashStatus[1] & 0x01) == 0) {
			break;
		}
	}
}

void FlashRead(XQspiPs *QspiPtr, u32 Address, u32 ByteCount, u8 Command)
{
	int Status; 
    #ifdef LINEAR_READ
	    Xil_DCacheFlushRange((UINTPTR)DDR_MEMB1, ByteCount);
        XQspiPs_SetOptions(QspiPtr, XQSPIPS_LQSPI_MODE_OPTION | XQSPIPS_HOLD_B_DRIVE_OPTION);
        Status = XQspiPs_LqspiRead(QspiPtr, DDR_MEMB1, Address, ByteCount);       
        if (Status == XST_FAILURE) {
            return;
        }
    #else     

    /*
	 * Setup the write command with the specified address and data for the
	 * FLASH
	 */
	WriteBuffer[COMMAND_OFFSET]   = Command;
	WriteBuffer[ADDRESS_1_OFFSET] = (u8)((Address & 0xFF0000) >> 16);
	WriteBuffer[ADDRESS_2_OFFSET] = (u8)((Address & 0xFF00) >> 8);
	WriteBuffer[ADDRESS_3_OFFSET] = (u8)(Address & 0xFF);

	if ((Command == FAST_READ_CMD) || (Command == DUAL_READ_CMD) ||
	    (Command == QUAD_READ_CMD)) {
		ByteCount += DUMMY_SIZE;
	}
    Xil_DCacheFlushRange((UINTPTR)DDR_MEMB1, ByteCount + OVERHEAD_SIZE);
	/*
	 * Send the read command to the FLASH to read the specified number
	 * of bytes from the FLASH, send the read command and address and
	 * receive the specified number of bytes of data in the data buffer
	 */
	XQspiPs_PolledTransfer(QspiPtr, WriteBuffer, DDR_MEMB1,
				ByteCount + OVERHEAD_SIZE);
    #endif
}

void FlashErase(XQspiPs *QspiPtr, u32 Address, u32 ByteCount)
{
	u8 WriteEnableCmd = { WRITE_ENABLE_CMD };
	u8 ReadStatusCmd[] = { READ_STATUS_CMD, 0 };  /* must send 2 bytes */
	u8 FlashStatus[2];
	int Sector;
	u32 NumSect;

	/*
	 * If erase size is same as the total size of the flash, use bulk erase
	 * command
	 */
	if (ByteCount == (NUM_SECTORS * SECTOR_SIZE)) {
		/*
		 * Send the write enable command to the FLASH so that it can be
		 * written to, this needs to be sent as a separate transfer
		 * before the erase
		 */
		XQspiPs_PolledTransfer(QspiPtr, &WriteEnableCmd, NULL,
				  sizeof(WriteEnableCmd));

		/* Setup the bulk erase command*/
		WriteBuffer[COMMAND_OFFSET]   = BULK_ERASE_CMD;

		/*
		 * Send the bulk erase command; no receive buffer is specified
		 * since there is nothing to receive
		 */
		XQspiPs_PolledTransfer(QspiPtr, WriteBuffer, NULL,
					BULK_ERASE_SIZE);

		/* Wait for the erase command to the FLASH to be completed*/
		while (1) {
			/*
			 * Poll the status register of the device to determine
			 * when it completes, by sending a read status command
			 * and receiving the status byte
			 */
			XQspiPs_PolledTransfer(QspiPtr, ReadStatusCmd,
						FlashStatus,
						sizeof(ReadStatusCmd));

			/*
			 * If the status indicates the write is done, then stop
			 * waiting; if a value of 0xFF in the status byte is
			 * read from the device and this loop never exits, the
			 * device slave select is possibly incorrect such that
			 * the device status is not being read
			 */
			FlashStatus[1] |= FlashStatus[0];
			if ((FlashStatus[1] & 0x01) == 0) {
				break;
			}
		}

		return;
	}

	/*
	 * Calculate no. of sectors to erase based on byte count
	 */
	if (ByteCount % SECTOR_SIZE)
		NumSect = ByteCount/SECTOR_SIZE + 1;
	else
		NumSect = ByteCount/SECTOR_SIZE;

	/*
	 * If the erase size is less than the total size of the flash, use
	 * sector erase command
	 */
	for (Sector = 0; Sector < NumSect; Sector++) {
		/*
		 * Send the write enable command to the SEEPOM so that it can be
		 * written to, this needs to be sent as a separate transfer
		 * before the write
		 */
		XQspiPs_PolledTransfer(QspiPtr, &WriteEnableCmd, NULL,
					sizeof(WriteEnableCmd));

		/*
		 * Setup the write command with the specified address and data
		 * for the FLASH
		 */
		WriteBuffer[COMMAND_OFFSET]   = SEC_ERASE_CMD;
		WriteBuffer[ADDRESS_1_OFFSET] = (u8)(Address >> 16);
		WriteBuffer[ADDRESS_2_OFFSET] = (u8)(Address >> 8);
		WriteBuffer[ADDRESS_3_OFFSET] = (u8)(Address & 0xFF);

		/*
		 * Send the sector erase command and address; no receive buffer
		 * is specified since there is nothing to receive
		 */
		XQspiPs_PolledTransfer(QspiPtr, WriteBuffer, NULL,
					SEC_ERASE_SIZE);

		/*
		 * Wait for the sector erse command to the
		 * FLASH to be completed
		 */
		while (1) {
			/*
			 * Poll the status register of the device to determine
			 * when it completes, by sending a read status command
			 * and receiving the status byte
			 */
			XQspiPs_PolledTransfer(QspiPtr, ReadStatusCmd,
						FlashStatus,
						sizeof(ReadStatusCmd));

			/*
			 * If the status indicates the write is done, then stop
			 * waiting, if a value of 0xFF in the status byte is
			 * read from the device and this loop never exits, the
			 * device slave select is possibly incorrect such that
			 * the device status is not being read
			 */
			FlashStatus[1] |= FlashStatus[0];
			if ((FlashStatus[1] & 0x01) == 0) {
				break;
			}
		}

		Address += SECTOR_SIZE;
	}
}


void FlashQuadEnable(XQspiPs *QspiPtr)
{
	u8 WriteEnableCmd = {WRITE_ENABLE_CMD};
	u8 ReadStatusCmd[] = {READ_STATUS_CMD, 0};
	u8 QuadEnableCmd[] = {WRITE_STATUS_CMD, 0};
	u8 FlashStatus[2];


	if (ReadBuffer[1] == 0x9D) {

		XQspiPs_PolledTransfer(QspiPtr, ReadStatusCmd,
					FlashStatus,
					sizeof(ReadStatusCmd));

		QuadEnableCmd[1] = FlashStatus[1] | 1 << 6;

		XQspiPs_PolledTransfer(QspiPtr, &WriteEnableCmd, NULL,
				  sizeof(WriteEnableCmd));

		XQspiPs_PolledTransfer(QspiPtr, QuadEnableCmd, NULL,
					sizeof(QuadEnableCmd));
		while (1) {
			/*
			 * Poll the status register of the FLASH to determine when
			 * Quad Mode is enabled and the device is ready, by sending
			 * a read status command and receiving the status byte
			 */
			XQspiPs_PolledTransfer(QspiPtr, ReadStatusCmd, FlashStatus,
					sizeof(ReadStatusCmd));
			/*
			 * If 6th bit is set & 0th bit is reset, then Quad is Enabled
			 * and device is ready.
			 */
			if ((FlashStatus[0] == 0x40) && (FlashStatus[1] == 0x40)) {
				break;
			}
		}
	}
}
