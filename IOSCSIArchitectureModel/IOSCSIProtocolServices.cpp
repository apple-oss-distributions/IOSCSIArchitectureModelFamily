/*
 * Copyright (c) 1998-2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */


//�����������������������������������������������������������������������������
//	Includes
//�����������������������������������������������������������������������������

// Libkern includes
#include <libkern/OSByteOrder.h>

// General IOKit includes
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>

// SCSI Architecture Model Family includes
#include <IOKit/scsi/IOSCSITargetDevice.h>
#include <IOKit/scsi/IOSCSIProtocolServices.h>

#include "SCSITaskDefinition.h"


//�����������������������������������������������������������������������������
//	Macros
//�����������������������������������������������������������������������������

#define DEBUG 												0
#define DEBUG_ASSERT_COMPONENT_NAME_STRING					"SCSIProtocolServices"

#if DEBUG
#define SCSI_PROTOCOL_SERVICES_DEBUGGING_LEVEL				0
#endif


#include "IOSCSIArchitectureModelFamilyDebugging.h"


#if ( SCSI_PROTOCOL_SERVICES_DEBUGGING_LEVEL >= 1 )
#define PANIC_NOW(x)		IOPanic x
#else
#define PANIC_NOW(x)
#endif

#if ( SCSI_PROTOCOL_SERVICES_DEBUGGING_LEVEL >= 2 )
#define ERROR_LOG(x)		IOLog x
#else
#define ERROR_LOG(x)
#endif

#if ( SCSI_PROTOCOL_SERVICES_DEBUGGING_LEVEL >= 3 )
#define STATUS_LOG(x)		IOLog x
#else
#define STATUS_LOG(x)
#endif


#define super IOSCSIProtocolInterface
OSDefineMetaClass ( IOSCSIProtocolServices, IOSCSIProtocolInterface );
OSDefineAbstractStructors ( IOSCSIProtocolServices, IOSCSIProtocolInterface );


//�����������������������������������������������������������������������������
//	Constants
//�����������������������������������������������������������������������������

// Used by power manager to figure out what states we support
// The default implementation supports two basic states: ON and OFF
// ON state means the device can be used on this transport layer
// OFF means the device cannot receive any I/O on this transport layer
static IOPMPowerState sPowerStates[kSCSIProtocolLayerNumDefaultStates] =
{
	{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, (IOPMDeviceUsable | IOPMMaxPerformance), IOPMPowerOn, IOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 }
};


#if 0
#pragma mark -
#pragma mark � Public Methods
#pragma mark -
#endif


//�����������������������������������������������������������������������������
//	� init - Initialization method.						 			   [PUBLIC]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::init ( OSDictionary * propTable )
{
	
	if ( super::init ( propTable ) == false )
	{
		return false;
	}
	
	return true;
	
}


//�����������������������������������������������������������������������������
//	� start - Called by IOKit to start our services.		  		   [PUBLIC]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::start ( IOService * provider )
{
	
	OSDictionary *  dict = NULL;	
	
	if ( !super::start ( provider ) )
	{
		return false;
	}
	
	// Setup to allow service requests
	fAllowServiceRequests = true;
	
	// Initialize the head pointer for the SCSI Task Queue.
	fSCSITaskQueueHead = NULL;
	
	// Allocate the mutex for accessing the SCSI Task Queue.
	fQueueLock = IOSimpleLockAlloc ( );
	require ( fQueueLock, PanicNow );
	
	// If the provider has a Protocol Characteristics dictionary, copy
	// it to the Protocol Services object.
	dict = OSDynamicCast ( OSDictionary, provider->getProperty ( kIOPropertyProtocolCharacteristicsKey ) );
	if ( dict != NULL )
	{
		
		OSDictionary *	protocolDict = NULL;
		
		protocolDict = OSDictionary::withDictionary ( dict );
		setProperty ( kIOPropertyProtocolCharacteristicsKey, protocolDict );
		protocolDict->release ( );
		
	}
	
	return true;
	
	
PanicNow:
	
	
	IOPanic ( "SCSIProtcolServices: Could not allocate simple lock for task queue\n" );
	
	return false;
	
}


//�����������������������������������������������������������������������������
//	� stop - Called by IOKit to stop our services.			  		   [PUBLIC]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::free ( void )
{
	
	if ( fQueueLock != NULL )
	{
		
		// Free the SCSI Task queue mutex.
		IOSimpleLockFree ( fQueueLock );
		fQueueLock = NULL;
		
	}
	
	super::free ( );
	
}


#if 0
#pragma mark -
#pragma mark � Power Management Methods
#pragma mark -
#endif


//�����������������������������������������������������������������������������
//	� GetInitialPowerState - Gets the initial power state for the device
//															 		[PROTECTED]
//�����������������������������������������������������������������������������

UInt32
IOSCSIProtocolServices::GetInitialPowerState ( void )
{
	
	STATUS_LOG ( ( "%s%s::%s called%s\n", "\033[33m", getName ( ), __FUNCTION__, "\033[0m" ) );
	return fCurrentPowerState;
	
}


//�����������������������������������������������������������������������������
//	� InitializePowerManagement - Called to initialize power management.
//															  		[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::InitializePowerManagement ( IOService * provider )
{
	
	fCurrentPowerState = kSCSIProtocolLayerPowerStateOn;
	
	// Call our super to initialize PM vars and to join the power
	// management tree
	super::InitializePowerManagement ( provider );
	
	// Register this piece with power management as the "policy maker"
	// i.e. the thing that controls power management for the protocol layer
	registerPowerDriver ( this, sPowerStates, kSCSIProtocolLayerNumDefaultStates );
	
	// make sure we default to on state
	changePowerStateTo ( kSCSIProtocolLayerPowerStateOn );
	
}


//�����������������������������������������������������������������������������
//	� HandlePowerChange - Handles power changes.			  		[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::HandlePowerChange ( void )
{
	
	IOReturn	status;
	
	STATUS_LOG ( ( "%s%s::%s called%s\n", "\033[33m",
						getName ( ), __FUNCTION__, "\033[0m" ) );
	
	STATUS_LOG ( ( "fProposedPowerState = %ld, fCurrentPowerState = %ld\n",
						fProposedPowerState, fCurrentPowerState ) );
	
	while ( fProposedPowerState != fCurrentPowerState )
	{
		
		STATUS_LOG ( ( "Looping because power states differ\n" ) );
		
		switch ( fProposedPowerState )
		{
			
			case kSCSIProtocolLayerPowerStateOff:
				status = HandlePowerOff ( );
				STATUS_LOG ( ( "HandlePowerOff returned status = %d\n", status ) );
				if ( status == kIOReturnSuccess )
				{
					fCurrentPowerState = kSCSIProtocolLayerPowerStateOff;
				}
				break;
			
			case kSCSIProtocolLayerPowerStateOn:
				status = HandlePowerOn ( );
				STATUS_LOG ( ( "HandlePowerOn returned status = %d\n", status ) );
				if ( status == kIOReturnSuccess )
				{
					fCurrentPowerState = kSCSIProtocolLayerPowerStateOn;
				}
				break;
			
			default:
				PANIC_NOW ( ( "HandlePowerChange: bad proposed power state\n" ) );
				break;
			
		}
		
	}
	
}


//�����������������������������������������������������������������������������
//	� HandleCheckPowerState - Handles checking the current power state.
//															  		[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::HandleCheckPowerState ( void )
{
	
	super::HandleCheckPowerState ( kSCSIProtocolLayerPowerStateOn );
	
}


//�����������������������������������������������������������������������������
//	� TicklePowerManager - Tickles the power manager with the desired state.
//															  		[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::TicklePowerManager ( void )
{
	
	super::TicklePowerManager ( kSCSIProtocolLayerPowerStateOn );
	
}


//�����������������������������������������������������������������������������
//	� HandlePowerOff - Default method that does nothing.	  		[PROTECTED]
//�����������������������������������������������������������������������������

IOReturn
IOSCSIProtocolServices::HandlePowerOff ( void )
{
	
	return kIOReturnSuccess;
	
}


//�����������������������������������������������������������������������������
//	� HandlePowerOn - Default method that does nothing.	  		 	[PROTECTED]
//�����������������������������������������������������������������������������

IOReturn
IOSCSIProtocolServices::HandlePowerOn ( void )
{
	
	return kIOReturnSuccess;
	
}


#if 0
#pragma mark -
#pragma mark � Status Notification Senders
#pragma mark -
#endif


//�����������������������������������������������������������������������������
//	� SendNotification_DeviceRemoved - 	Rejects all currently queued requests.
//														  		 	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::SendNotification_DeviceRemoved ( void )
{
	
	STATUS_LOG ( ( "%s: SendNotification_DeviceRemoved called\n", getName ( ) ) );
	
	// Set the flag to prevent execution of any other service requests
	fAllowServiceRequests = false;
	
	STATUS_LOG ( ( "%s: SendNotification_DeviceRemoved Reject queued tasks\n", getName ( ) ) );
	
	// Remove all tasks from the queue.
	RejectSCSITasksCurrentlyQueued ( );
	
}


//�����������������������������������������������������������������������������
//	� SendNotification_VerifyDeviceState - 	Messages the SCSI Application Layer
//											driver to verify the device's state
//														  		 	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::SendNotification_VerifyDeviceState ( void )
{
	
	STATUS_LOG ( ("%s: SendNotification_VerifyDeviceState called\n", getName ( ) ) );
	
	// Send message up to SCSI Application Layer.
	messageClients ( kSCSIProtocolNotification_VerifyDeviceState, 0 , 0 );
	
}


#if 0
#pragma mark -
#pragma mark � SCSI Task Field Accessors
#pragma mark -
#endif


// ---- Utility methods for accessing SCSITask attributes ----

//�����������������������������������������������������������������������������
//	� GetTaskAttribute - Gets the task attribute from the task. 	[PROTECTED]
//�����������������������������������������������������������������������������

SCSITaskAttribute
IOSCSIProtocolServices::GetTaskAttribute ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetTaskAttribute ( );
	
}


//�����������������������������������������������������������������������������
//	� SetTaskState - Sets the task state. 							[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetTaskState ( SCSITaskIdentifier	request,
									   SCSITaskState		newTaskState )
{
	
	SCSITask *	scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->SetTaskState ( newTaskState );
	
}


//�����������������������������������������������������������������������������
//	� GetTaskState - Gets the task state. 							[PROTECTED]
//�����������������������������������������������������������������������������

SCSITaskState
IOSCSIProtocolServices::GetTaskState ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetTaskState ( );
	
}


//�����������������������������������������������������������������������������
//	� GetLogicalUnitNumber - Gets the Logical Unit Number (LUN).	[PROTECTED]
//�����������������������������������������������������������������������������

UInt8
IOSCSIProtocolServices::GetLogicalUnitNumber ( SCSITaskIdentifier request )
{
	SCSITask *	scsiRequest;
	
    scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetLogicalUnitNumber();
}


//�����������������������������������������������������������������������������
//	� GetCommandDescriptorBlockSize - Gets the size of the CDB.		[PROTECTED]
//�����������������������������������������������������������������������������

UInt8
IOSCSIProtocolServices::GetCommandDescriptorBlockSize (
										SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	UInt8		size;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		size = scsiRequest->GetCommandDescriptorBlockSize ( );
	}
	
	else
	{
		size = scsiRequest->GetAutosenseCommandDescriptorBlockSize ( );
	}
	
	return size;
	
}


//�����������������������������������������������������������������������������
//	� GetCommandDescriptorBlock - Gets the CDB data.				[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::GetCommandDescriptorBlock (
							SCSITaskIdentifier 				request,
							SCSICommandDescriptorBlock *	cdbData )
{
	
	SCSITask *	scsiRequest;
	bool		result = false;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
   	 	result = scsiRequest->GetCommandDescriptorBlock ( cdbData );
	}
	
	else
	{
		result = scsiRequest->GetAutosenseCommandDescriptorBlock ( cdbData );
	}
	
	return result;
	
}


//�����������������������������������������������������������������������������
//	� GetDataTransferDirection - Gets the data transfer direction.	[PROTECTED]
//�����������������������������������������������������������������������������

UInt8
IOSCSIProtocolServices::GetDataTransferDirection (
								SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	UInt8		direction;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		direction = scsiRequest->GetDataTransferDirection ( );
	}
	
	else
	{
		direction = scsiRequest->GetAutosenseDataTransferDirection ( );
	}
	
	return direction;
	
}


//�����������������������������������������������������������������������������
//	� GetRequestedDataTransferCount - 	Gets the size of the requested data
//										transfer.					[PROTECTED]
//�����������������������������������������������������������������������������

UInt64
IOSCSIProtocolServices::GetRequestedDataTransferCount (
									SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	UInt64		amount;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		amount = scsiRequest->GetRequestedDataTransferCount ( );
	}
	
	else
	{
		amount = scsiRequest->GetAutosenseRequestedDataTransferCount ( );
	}
	
	return amount;
	
}


//�����������������������������������������������������������������������������
//	� SetRealizedDataTransferCount - 	Sets the amount of data actually
//										transferred.				[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetRealizedDataTransferCount (
								SCSITaskIdentifier	request,
								UInt64				newRealizedDataCount )
{
	
	SCSITask *	scsiRequest;
	bool		result;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		result = scsiRequest->SetRealizedDataTransferCount ( newRealizedDataCount );
	}
	
	else
	{
		result = scsiRequest->SetAutosenseRealizedDataCount ( newRealizedDataCount );
	}
	
	return result;
	
}


//�����������������������������������������������������������������������������
//	� GetRealizedDataTransferCount - 	Gets the amount of data actually
//										transferred.				[PROTECTED]
//�����������������������������������������������������������������������������

UInt64
IOSCSIProtocolServices::GetRealizedDataTransferCount (
									SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	UInt64		amount;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Check to see what the current execution mode is  
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		amount = scsiRequest->GetRealizedDataTransferCount ( );
	}
	
	else
	{
		amount = scsiRequest->GetAutosenseRealizedDataCount ( );
	}
	
	return amount;
	
}


//�����������������������������������������������������������������������������
//	� GetDataBuffer - Gets the data buffer associated with this request.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

IOMemoryDescriptor *
IOSCSIProtocolServices::GetDataBuffer ( SCSITaskIdentifier request )
{
	
	SCSITask *				scsiRequest;
	IOMemoryDescriptor *	buffer;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		buffer = scsiRequest->GetDataBuffer ( );
	}
	
	else
	{
		buffer = scsiRequest->GetAutosenseDataBuffer ( );
	}
	
	return buffer;
	
}


//�����������������������������������������������������������������������������
//	� GetDataBufferOffset - Gets the offset into the data buffer.	[PROTECTED]
//�����������������������������������������������������������������������������

UInt64
IOSCSIProtocolServices::GetDataBufferOffset ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetDataBufferOffset ( );
	
}


//�����������������������������������������������������������������������������
//	� GetTimeoutDuration - Gets the timeout duration for the request.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

UInt32
IOSCSIProtocolServices::GetTimeoutDuration ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetTimeoutDuration ( );

}


//�����������������������������������������������������������������������������
//	� SetAutoSenseData - Sets the auto sense data for this request.
//														[DEPRECATED][PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetAutoSenseData (
							SCSITaskIdentifier		request,
							SCSI_Sense_Data *		senseData )
{
	
	return SetAutoSenseData ( request, senseData, sizeof ( SCSI_Sense_Data ) );
	
}


//�����������������������������������������������������������������������������
//	� SetAutoSenseData - Sets the auto sense data for this request.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetAutoSenseData (
							SCSITaskIdentifier		request,
							SCSI_Sense_Data *		senseData,
							UInt8					senseDataSize )
{
	
	SCSITask *		scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->SetAutoSenseData ( senseData, senseDataSize );
	
}


//�����������������������������������������������������������������������������
//	� SetProtocolLayerReference - Sets the protocol layer refcon for this task.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetProtocolLayerReference (
								SCSITaskIdentifier	request,
								void *				newReferenceValue )
{
	
	SCSITask *		scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->SetProtocolLayerReference ( newReferenceValue );
	
}


//�����������������������������������������������������������������������������
//	� GetProtocolLayerReference - Gets the protocol layer refcon for this task.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

void *
IOSCSIProtocolServices::GetProtocolLayerReference ( SCSITaskIdentifier request )
{
	
	SCSITask *		scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetProtocolLayerReference ( );
	
}


//�����������������������������������������������������������������������������
//	� SetTaskExecutionMode - Sets the SCSITaskMode for this task.	[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::SetTaskExecutionMode (
							SCSITaskIdentifier	request,
							SCSITaskMode		newTaskMode )
{
	
	SCSITask *		scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->SetTaskExecutionMode ( newTaskMode );
	
}


//�����������������������������������������������������������������������������
//	� GetTaskExecutionMode - Gets the SCSITaskMode for this task.	[PROTECTED]
//�����������������������������������������������������������������������������

SCSITaskMode
IOSCSIProtocolServices::GetTaskExecutionMode ( SCSITaskIdentifier request )
{
	
	SCSITask *		scsiRequest;
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	return scsiRequest->GetTaskExecutionMode ( );
	
}


#if 0
#pragma mark -
#pragma mark � SCSI Task Queue Management
#pragma mark -
#endif

// Following are the commands used to manipulate the queue of pending SCSI Tasks.
// Currently the queuing is strictly first in, first out.  This needs to be changed
// to support the SCSI queueing model in the SCSI Architecture Model-2 specification.

//�����������������������������������������������������������������������������
//	� AddSCSITaskToQueue -	Add the SCSI Task to the queue. The Task's
//							Attribute determines where in the queue the Task
//							is placed.								[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::AddSCSITaskToQueue ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	STATUS_LOG ( ( "%s: AddSCSITaskToQueue called.\n", getName ( ) ) );
	
	IOSimpleLockLock ( fQueueLock );
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	// Make sure that the new request does not have a following task.
	scsiRequest->EnqueueFollowingSCSITask ( NULL );
	
	// Check to see if there are any tasks currently queued.
	if ( fSCSITaskQueueHead == NULL )
	{
		
		// There are no other tasks currently queued, so
		// save this one as the head.
		fSCSITaskQueueHead = scsiRequest;
		
	}
	
	else
	{
		
		// There is at least one task currently in the queue,
		// Add the current one to the end.
		SCSITask *	currentElement;
		
		currentElement = fSCSITaskQueueHead;
		while ( currentElement->GetFollowingSCSITask ( ) != NULL )
		{
			currentElement = currentElement->GetFollowingSCSITask ( );
		}
		
		currentElement->EnqueueFollowingSCSITask ( scsiRequest );
		
	}
	
	IOSimpleLockUnlock ( fQueueLock );
	
}


//�����������������������������������������������������������������������������
//	� AddSCSITaskToHeadOfQueue -	Add the SCSI Task to the head of the queue.
//									This is used when the task has been removed
//									from the head of the queue, but the
//									subclass indicates that it can not yet
//									process this task.				[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::AddSCSITaskToHeadOfQueue ( SCSITask * request )
{
	
	IOSimpleLockLock ( fQueueLock );
	
	// Make sure that the new request does not have a following task.
	request->EnqueueFollowingSCSITask ( fSCSITaskQueueHead );
	fSCSITaskQueueHead = request;
	
	IOSimpleLockUnlock ( fQueueLock );
	
}


//�����������������������������������������������������������������������������
//	� RetrieveNextSCSITaskFromQueue -	Remove the next SCSI Task from the
//										queue and return it.		[PROTECTED]
//�����������������������������������������������������������������������������

SCSITask *
IOSCSIProtocolServices::RetrieveNextSCSITaskFromQueue ( void )
{
	
	SCSITask *		selectedTask;
	
	IOSimpleLockLock ( fQueueLock );
	
	// Check to see if there are any tasks currently queued.
	if ( fSCSITaskQueueHead == NULL )
	{
		
		// There are currently no tasks queued, return NULL.
		selectedTask = NULL;
		
	}
	
	else
	{
		
		// There is at least one task currently in the queue,
		
		// Grab the head task
		selectedTask = fSCSITaskQueueHead;
		
		// Set the head pointer to the next task in the queue.  If there
		// is no more tasks, this head pointer will be set to NULL.
		fSCSITaskQueueHead = selectedTask->GetFollowingSCSITask ( );
		
		// Make sure that the new request does not have a following task.
		selectedTask->EnqueueFollowingSCSITask ( NULL );
		
	}
	
	IOSimpleLockUnlock ( fQueueLock );
	
	return selectedTask;
	
}


//�����������������������������������������������������������������������������
//	� AbortSCSITaskFromQueue -	Check to see if the SCSI Task resides and
//								abort it if it does. This currently does
//								nothing since the ABORT TASK and ABORT TASK SET
//								management functions are not supported.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::AbortSCSITaskFromQueue ( SCSITask *request )
{
	
	// If the indicated SCSI Task currently resides in the Queue, the SCSI Task
	// will be removed and no further processing shall occur on that Task.  This
	// method will then return true.
	
	// If the SCSI Task does not currently reside in the queue, this method will
	// return false.
	return false;
	
}


//�����������������������������������������������������������������������������
//	� SendSCSITasksFromQueue -	Removes tasks from the queue and sends them to
//								the protocol layer for processing.	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::SendSCSITasksFromQueue ( void )
{
	
	bool			cmdAccepted = false;
	SCSITask *		nextVictim;
	
	do 
	{
		
		SCSIServiceResponse 	serviceResponse;
		SCSITaskStatus			taskStatus;
		
		// get the next command from the request queue
		nextVictim = RetrieveNextSCSITaskFromQueue ( );
		if ( nextVictim != NULL )
		{
			
			cmdAccepted = SendSCSICommand ( nextVictim, &serviceResponse, &taskStatus );
			if ( cmdAccepted == false )
			{
				
				// The subclass can not process the command at this time,
				// add it to the queue and try again at CommandComplete time.
				AddSCSITaskToHeadOfQueue ( nextVictim );
				
			}
			
			else if ( serviceResponse != kSCSIServiceResponse_Request_In_Process )
			{
				
				// The command was sent and completed, send next Task based on its Attribute.
				nextVictim->SetServiceResponse ( serviceResponse );
				nextVictim->SetTaskStatus ( taskStatus );
				nextVictim->SetTaskState ( kSCSITaskState_ENDED );
				
			}
			
		}
		
	} while ( ( cmdAccepted == true ) && ( nextVictim != NULL ) );

}


//�����������������������������������������������������������������������������
//	� RejectSCSITasksCurrentlyQueued -	Rejects task currently queued.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::RejectSCSITasksCurrentlyQueued ( void )
{
	
	SCSITask *		nextVictim;
	
	STATUS_LOG ( ( "%s: RejectSCSITasksCurrentlyQueued called.\n", getName ( ) ) );
	
	do 
	{
		
		// get the next command from the request queue
		nextVictim = RetrieveNextSCSITaskFromQueue ( );
		if ( nextVictim != NULL )
		{
			RejectTask ( nextVictim );
		}
		
	} while ( nextVictim != NULL );
	
}


//�����������������������������������������������������������������������������
//	� ProcessCompletedTask - Processes completed tasks.				[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::ProcessCompletedTask (
							SCSITaskIdentifier		request, 
							SCSIServiceResponse		serviceResponse,
							SCSITaskStatus			taskStatus )
{
	
	SCSITask *	scsiRequest;
	
	STATUS_LOG ( ( "%s: ProcessCompletedTask called.\n", getName ( ) ) );
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	if ( scsiRequest->GetTaskExecutionMode ( ) == kSCSITaskMode_CommandExecution )
	{
		
		// The task is currently in Command Execution mode, update the service
		// response and  
		scsiRequest->SetServiceResponse ( serviceResponse );
		
		// Check to see if this task was successfully transported to the
		// device server.
		if ( serviceResponse == kSCSIServiceResponse_TASK_COMPLETE )
		{
			
			// Save that status into the Task object.
			scsiRequest->SetTaskStatus ( taskStatus );
			
			// See if the conditions for autosenses has been met: the task completed
			// with a CHECK_CONDITION.
			if ( ( serviceResponse == kSCSIServiceResponse_TASK_COMPLETE ) &&
				 ( taskStatus == kSCSITaskStatus_CHECK_CONDITION ) )
			{
				
				// Check if the client wants Autosense data
				if ( scsiRequest->IsAutosenseRequested ( ) == true )
				{
					
					// Check if the Protocol has already provided this data
					if ( scsiRequest->GetAutoSenseData ( NULL, 0 ) == false )
					{
						
						// Put the task into Autosense mode and
						// add to the head of the queue.
						scsiRequest->SetTaskExecutionMode ( kSCSITaskMode_Autosense );
						AddSCSITaskToHeadOfQueue ( scsiRequest );
						
						// The task can not be completed until the 
						// autosense is done, so exit and wait for that
						// completion.
						return;
						
					}
					
				}
				
			}
			
		}
		
	}
	
	else
	{
		
		// the task is in Autosense mode, check to see if the autosense
		// command completed successfully.
		if ( ( serviceResponse == kSCSIServiceResponse_TASK_COMPLETE ) &&
			 ( taskStatus == kSCSITaskStatus_GOOD ) )
		{
			
			scsiRequest->SetAutosenseIsValid ( true );
			
		}
		
	}
	
	scsiRequest->SetTaskState ( kSCSITaskState_ENDED );
	
	// The command is complete, release the retain for this command.
	release ( );	
	
	// The task has completed, execute the callback.
	scsiRequest->TaskCompletedNotification ( );
	
}


//�����������������������������������������������������������������������������
//	� RejectTask - Rejects a task.									[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::RejectTask ( SCSITaskIdentifier request )
{
	
	SCSITask *	scsiRequest;
	
	STATUS_LOG ( ( "%s: RejectTask called.\n", getName ( ) ) );
	
	scsiRequest = OSDynamicCast ( SCSITask, request );
	
	scsiRequest->SetTaskState ( kSCSITaskState_ENDED );
	scsiRequest->SetServiceResponse ( kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE );
	
	// Save that status into the Task object.
	scsiRequest->SetTaskStatus ( kSCSITaskStatus_No_Status );
	
	// The command is complete, release the retain for this command.
	release ( );
	scsiRequest->TaskCompletedNotification ( );
	
}


#if 0
#pragma mark -
#pragma mark � Provided Services to the SCSI Protocol Layer Subclasses
#pragma mark -
#endif


//�����������������������������������������������������������������������������
//	� CommandCompleted - Called by subclass to complete a command.	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::CommandCompleted ( 	SCSITaskIdentifier 	request, 
											SCSIServiceResponse serviceResponse,
											SCSITaskStatus		taskStatus )
{
	
	STATUS_LOG ( ( "%s: CommandCompleted called.\n", getName ( ) ) );
	
	// Check to see if service requests are allowed
	if ( fAllowServiceRequests == false )
	{
		
		// Service requests are not allowed, return the task back
		// with an error.
		RejectTask ( request );
		return;
		
	}
	
	ProcessCompletedTask ( request, serviceResponse, taskStatus );
	
	SendSCSITasksFromQueue ( );
	
}


//�����������������������������������������������������������������������������
//	� CreateSCSITargetDevice -	Creates the appropriate object to represent the
//								Target portion of a SCSI Device. This object is
//								responsible for managing the Target functions
//								of the SCSI Device including the Task Manager
//								and Logical Units.					[PROTECTED]
//�����������������������������������������������������������������������������

bool
IOSCSIProtocolServices::CreateSCSITargetDevice ( void )
{
	
	IOSCSITargetDevice *	newDevice = NULL;
	
	// Create the IOSCSIParallelInterfaceDevice object
	newDevice = OSTypeAlloc ( IOSCSITargetDevice );
	if ( newDevice == NULL )
	{
		
		// The device could not be created, let the caller know.
		return false;
		
	}
	
	// Attach the device
	if ( newDevice->init ( 0 ) == false )
	{
		goto ATTACH_FAILED_EXIT;
	}
	
	if ( newDevice->attach ( this ) == false )
	{
		goto ATTACH_FAILED_EXIT;
	}
	
	if ( newDevice->start ( this ) == false )
	{
		goto START_FAILED_EXIT;
	}
	
	newDevice->release ( );
	
	// The SCSI Target Device was successfully created.
	return true;
	
	
START_FAILED_EXIT:
	
	
	// Detach the target device
	newDevice->detach ( this );
	
	
ATTACH_FAILED_EXIT:	
	
	// The device can now be destroyed.
	newDevice->release ( );
	
	return false;
	
}


#if 0
#pragma mark -
#pragma mark � Provided Services to the SCSI Application Layer 
#pragma mark -
#endif


//�����������������������������������������������������������������������������
//	� ExecuteCommand -	The ExecuteCommand function will take a SCSI Task and
//						transport it across the physical wire(s) to the device.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

void
IOSCSIProtocolServices::ExecuteCommand ( SCSITaskIdentifier request )
{
	
	STATUS_LOG ( ( "%s::%s called.\n", getName ( ), __FUNCTION__ ) );
	
	// Make sure that the protocol driver does not go away 
	// if there are outstanding commands.
	retain ( );
	
	// Check to see if service requests are allowed
	if ( fAllowServiceRequests == false )
	{
		
		// Service requests are not allowed, return the task back
		// immediately with an error
		RejectTask ( request );
		return;
		
	}
	
	// Set the task state to ENABLED
	SetTaskState ( request, kSCSITaskState_ENABLED );
	
	// Set the execution mode to indicate standard command execution.
	SetTaskExecutionMode ( request, kSCSITaskMode_CommandExecution );
	
	// Add the new request to the queue
	AddSCSITaskToQueue ( request );
	
	SendSCSITasksFromQueue ( );
	
}


//�����������������������������������������������������������������������������
// � AbortTask -	The Task Management function to allow the SCSI Application
// 					Layer client to request that a specific task be aborted.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::AbortTask (
							UInt8						theLogicalUnit,
							SCSITaggedTaskIdentifier 	theTag )
{
	return HandleAbortTask ( theLogicalUnit, theTag );
}


//�����������������������������������������������������������������������������
// � AbortTaskSet -	The Task Management function to allow the SCSI Application
//					Layer client to request that a all tasks currently in the
//					task set be aborted.							[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::AbortTaskSet ( UInt8 theLogicalUnit )
{
	return HandleAbortTaskSet ( theLogicalUnit );
}


//�����������������������������������������������������������������������������
// � ClearACA -	The Task Management function to allow the SCSI Application
//				Layer client to request that an Auto-Contingent Allegiance
//				be cleared.											[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::ClearACA ( UInt8 theLogicalUnit )
{
	return HandleClearACA ( theLogicalUnit );
}


//�����������������������������������������������������������������������������
// � ClearTaskSet -	The Task Management function to allow the SCSI Application
//					Layer client to request that the task set be cleared.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::ClearTaskSet ( UInt8 theLogicalUnit )
{
	return HandleClearTaskSet ( theLogicalUnit );
}


//�����������������������������������������������������������������������������
// � LogicalUnitReset -	The Task Management function to allow the SCSI Application
//						Layer client to request that the Logical Unit be reset.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::LogicalUnitReset ( UInt8 theLogicalUnit )
{
	return HandleLogicalUnitReset ( theLogicalUnit );
}


//�����������������������������������������������������������������������������
// � TargetReset -	The Task Management function to allow the SCSI Application
//					Layer client to request that the Target Device be reset.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::TargetReset ( void )
{
	return HandleTargetReset ( );
}


#if 0
#pragma mark -
#endif


//�����������������������������������������������������������������������������
// � HandleAbortTask -	The Task Management function to abort the specified task.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleAbortTask ( 
							UInt8 						theLogicalUnit, 
							SCSITaggedTaskIdentifier 	theTag )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � HandleAbortTaskSet - 	The Task Management function to abort the
//							entire task set for the logical unit.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleAbortTaskSet ( 
							UInt8 						theLogicalUnit )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � HandleClearACA - 	The Task Management function to clear an
//						Auto-Contingent Allegiance for the logical unit.
//																	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleClearACA ( UInt8 theLogicalUnit )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � HandleClearTaskSet - 	The Task Management function to clear an
//							entire task set for the logical unit.	[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleClearTaskSet ( UInt8 theLogicalUnit )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � HandleLogicalUnitReset - 	The Task Management function to reset the
//								logical unit.						[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleLogicalUnitReset ( UInt8 theLogicalUnit )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � HandleTargetReset - 	The HandleTargetReset member routine requests that
//							the Protocol Services Driver perform the necessary
//							steps detailed in the specification that defines
//							the protocol the driver represents for the
//							TargetReset management function.		[PROTECTED]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::HandleTargetReset ( void )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


//�����������������������������������������������������������������������������
// � AbortCommand -	The AbortCommand method is replaced by the AbortTask
//					Management function and should no longer be called.
//														  			[PROTECTED]
// [OBSOLETE - DO NOT USE]
//�����������������������������������������������������������������������������

SCSIServiceResponse
IOSCSIProtocolServices::AbortCommand ( SCSITaskIdentifier request )
{
	return kSCSIServiceResponse_FUNCTION_REJECTED;
}


#if 0
#pragma mark -
#pragma mark � VTable Padding
#pragma mark -
#endif


OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 1 );	// HandleAbortTask
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 2 );	// HandleAbortTaskSet
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 3 );	// HandleClearACA
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 4 );	// HandleClearTaskSet
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 5 );	// HandleLogicalUnitReset
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 6 );	// HandleTargetReset
OSMetaClassDefineReservedUsed ( IOSCSIProtocolServices, 7 );	// CreateSCSITargetDevice

// Space reserved for future expansion.
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 8 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 9 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 10 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 11 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 12 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 13 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 14 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 15 );
OSMetaClassDefineReservedUnused ( IOSCSIProtocolServices, 16 );