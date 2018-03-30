/*
             LUFA Library
     Copyright (C) Dean Camera, 2010.
              
  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com
*/

/*
  Copyright 2010  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this 
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in 
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting 
  documentation, and that the name of the author not be used in 
  advertising or publicity pertaining to distribution of the 
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the Mass Storage demo. This file contains the main tasks of the demo and
 *  is responsible for the initial application hardware configuration.
 */

#define  INCLUDE_FROM_MASSSTORAGE_C
#include "MassStorage.h"

/** Structure to hold the latest Command Block Wrapper issued by the host, containing a SCSI command to execute. */
CommandBlockWrapper_t  CommandBlock;

/** Structure to hold the latest Command Status Wrapper to return to the host, containing the status of the last issued command. */
CommandStatusWrapper_t CommandStatus = { .Signature = CSW_SIGNATURE };

/** Flag to asynchronously abort any in-progress data transfers upon the reception of a mass storage reset command. */
volatile bool          IsMassStoreReset = false;


/** Main program entry point. This routine configures the hardware required by the application, then
 *  enters a loop to run the application tasks in sequence.
 */
int main(void)
{
	SetupHardware();
	
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);

	for (;;)
	{
		MassStorage_Task();
		USB_USBTask();
	}
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	/* Hardware Initialization */
	LEDs_Init();
	SPI_Init(SPI_SPEED_FCPU_DIV_2 | SPI_SCK_LEAD_FALLING | SPI_SAMPLE_TRAILING | SPI_MODE_MASTER);
	Dataflash_Init();
	USB_Init();

	/* Clear Dataflash sector protections, if enabled */
	DataflashManager_ResetDataflashProtections();
}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs. */
void EVENT_USB_Device_Connect(void)
{
	/* Indicate USB enumerating */
	LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
	
	/* Reset the MSReset flag upon connection */
	IsMassStoreReset = false;
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
 *  the status LEDs and stops the Mass Storage management task.
 */
void EVENT_USB_Device_Disconnect(void)
{
	/* Indicate USB not ready */
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
 *  of the USB device after enumeration - the device endpoints are configured and the Mass Storage management task started.
 */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	/* Indicate USB connected and ready */
	LEDs_SetAllLEDs(LEDMASK_USB_READY);

	/* Setup Mass Storage In and Out Endpoints */
	if (!(Endpoint_ConfigureEndpoint(MASS_STORAGE_IN_EPNUM, EP_TYPE_BULK,
		                             ENDPOINT_DIR_IN, MASS_STORAGE_IO_EPSIZE,
	                                 ENDPOINT_BANK_SINGLE)))
	{
		LEDs_SetAllLEDs(LEDMASK_USB_ERROR);
	}
	
	if (!(Endpoint_ConfigureEndpoint(MASS_STORAGE_OUT_EPNUM, EP_TYPE_BULK,
		                             ENDPOINT_DIR_OUT, MASS_STORAGE_IO_EPSIZE,
	                                 ENDPOINT_BANK_SINGLE)))
	{
		LEDs_SetAllLEDs(LEDMASK_USB_ERROR);
	}							   
}

/** Event handler for the USB_UnhandledControlPacket event. This is used to catch standard and class specific
 *  control requests that are not handled internally by the USB library (including the Mass Storage class-specific
 *  requests) so that they can be handled appropriately for the application.
 */
void EVENT_USB_Device_UnhandledControlRequest(void)
{
	/* Process UFI specific control requests */
	switch (USB_ControlRequest.bRequest)
	{
		case REQ_MassStorageReset:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				/* Indicate that the current transfer should be aborted */
				IsMassStoreReset = true;

				Endpoint_ClearStatusStage();
			}

			break;
		case REQ_GetMaxLUN:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				/* Indicate to the host the number of supported LUNs (virtual disks) on the device */
				Endpoint_Write_Byte(TOTAL_LUNS - 1);
				
				Endpoint_ClearIN();
				
				Endpoint_ClearStatusStage();
			}
			
			break;
	}
}

/** Task to manage the Mass Storage interface, reading in Command Block Wrappers from the host, processing the SCSI commands they
 *  contain, and returning Command Status Wrappers back to the host to indicate the success or failure of the last issued command.
 */
void MassStorage_Task(void)
{
	/* Device must be connected and configured for the task to run */
	if (USB_DeviceState != DEVICE_STATE_Configured)
	  return;
	  
	/* Select the Data Out Endpoint */
	Endpoint_SelectEndpoint(MASS_STORAGE_OUT_EPNUM);
	
	/* Check to see if a command from the host has been issued */
	if (Endpoint_IsReadWriteAllowed())
	{
		/* Indicate busy */
		LEDs_SetAllLEDs(LEDMASK_USB_BUSY);

		/* Process sent command block from the host */
		if (ReadInCommandBlock())
		{
			/* Check direction of command, select Data IN endpoint if data is from the device */
			if (CommandBlock.Flags & COMMAND_DIRECTION_DATA_IN)
			  Endpoint_SelectEndpoint(MASS_STORAGE_IN_EPNUM);

			/* Decode the received SCSI command, set returned status code */
			CommandStatus.Status = SCSI_DecodeSCSICommand() ? Command_Pass : Command_Fail;		

			/* Load in the CBW tag into the CSW to link them together */
			CommandStatus.Tag = CommandBlock.Tag;

			/* Load in the data residue counter into the CSW */
			CommandStatus.DataTransferResidue = CommandBlock.DataTransferLength;
			
			/* Stall the selected data pipe if command failed (if data is still to be transferred) */
			if ((CommandStatus.Status == Command_Fail) && (CommandStatus.DataTransferResidue))
			  Endpoint_StallTransaction();

			/* Return command status block to the host */
			ReturnCommandStatus();

			/* Indicate ready */
			LEDs_SetAllLEDs(LEDMASK_USB_READY);
		}
		else
		{
			/* Indicate error reading in the command block from the host */
			LEDs_SetAllLEDs(LEDMASK_USB_ERROR);
		}
	}

	/* Check if a Mass Storage Reset occurred */
	if (IsMassStoreReset)
	{
		/* Reset the data endpoint banks */
		Endpoint_ResetFIFO(MASS_STORAGE_OUT_EPNUM);
		Endpoint_ResetFIFO(MASS_STORAGE_IN_EPNUM);
		
		Endpoint_SelectEndpoint(MASS_STORAGE_OUT_EPNUM);
		Endpoint_ClearStall();
		Endpoint_ResetDataToggle();
		Endpoint_SelectEndpoint(MASS_STORAGE_IN_EPNUM);
		Endpoint_ClearStall();
		Endpoint_ResetDataToggle();

		/* Clear the abort transfer flag */
		IsMassStoreReset = false;
	}
}

/** Function to read in a command block from the host, via the bulk data OUT endpoint. This function reads in the next command block
 *  if one has been issued, and performs validation to ensure that the block command is valid.
 *
 *  \return Boolean true if a valid command block has been read in from the endpoint, false otherwise
 */
static bool ReadInCommandBlock(void)
{
	/* Select the Data Out endpoint */
	Endpoint_SelectEndpoint(MASS_STORAGE_OUT_EPNUM);

	/* Read in command block header */
	Endpoint_Read_Stream_LE(&CommandBlock, (sizeof(CommandBlock) - sizeof(CommandBlock.SCSICommandData)),
	                        StreamCallback_AbortOnMassStoreReset);

	/* Check if the current command is being aborted by the host */
	if (IsMassStoreReset)
	  return false;

	/* Verify the command block - abort if invalid */
	if ((CommandBlock.Signature         != CBW_SIGNATURE) ||
	    (CommandBlock.LUN               >= TOTAL_LUNS)    ||
		(CommandBlock.Flags              & 0x1F)          ||
		(CommandBlock.SCSICommandLength == 0)             ||
		(CommandBlock.SCSICommandLength >  MAX_SCSI_COMMAND_LENGTH))
	{
		/* Stall both data pipes until reset by host */
		Endpoint_StallTransaction();
		Endpoint_SelectEndpoint(MASS_STORAGE_IN_EPNUM);
		Endpoint_StallTransaction();
		
		return false;
	}

	/* Read in command block command data */
	Endpoint_Read_Stream_LE(&CommandBlock.SCSICommandData,
	                        CommandBlock.SCSICommandLength,
	                        StreamCallback_AbortOnMassStoreReset);
	  
	/* Check if the current command is being aborted by the host */
	if (IsMassStoreReset)
	  return false;

	/* Finalize the stream transfer to send the last packet */
	Endpoint_ClearOUT();
	
	return true;
}

/** Returns the filled Command Status Wrapper back to the host via the bulk data IN endpoint, waiting for the host to clear any
 *  stalled data endpoints as needed.
 */
static void ReturnCommandStatus(void)
{
	/* Select the Data Out endpoint */
	Endpoint_SelectEndpoint(MASS_STORAGE_OUT_EPNUM);

	/* While data pipe is stalled, wait until the host issues a control request to clear the stall */
	while (Endpoint_IsStalled())
	{
		/* Check if the current command is being aborted by the host */
		if (IsMassStoreReset)
		  return;
	}

	/* Select the Data In endpoint */
	Endpoint_SelectEndpoint(MASS_STORAGE_IN_EPNUM);

	/* While data pipe is stalled, wait until the host issues a control request to clear the stall */
	while (Endpoint_IsStalled())
	{
		/* Check if the current command is being aborted by the host */
		if (IsMassStoreReset)
		  return;
	}
	
	/* Write the CSW to the endpoint */
	Endpoint_Write_Stream_LE(&CommandStatus, sizeof(CommandStatus),
	                          StreamCallback_AbortOnMassStoreReset);
	
	/* Check if the current command is being aborted by the host */
	if (IsMassStoreReset)
	  return;

	/* Finalize the stream transfer to send the last packet */
	Endpoint_ClearIN();
}

/** Stream callback function for the Endpoint stream read and write functions. This callback will abort the current stream transfer
 *  if a Mass Storage Reset request has been issued to the control endpoint.
 */
uint8_t StreamCallback_AbortOnMassStoreReset(void)
{	
	/* Abort if a Mass Storage reset command was received */
	if (IsMassStoreReset)
	  return STREAMCALLBACK_Abort;
	
	/* Continue with the current stream operation */
	return STREAMCALLBACK_Continue;
}
