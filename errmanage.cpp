/*******************************************************
** $Workfile: ErrManag.cpp $
** Name: Gestor de errores
** Copyright: Ikerlan, Koop. E. 2004
** $Author: ccruces $
** $Revision: 1.2 $
**
** Analysis reference: Gestor de errores
**
** Compiler: Microsoft eMbedded Visual C++ SP2
** Target: Geode GX1 300 MHz (Pentium II-like x86)
**
** $Log: ErrManag.cpp,v $
** Revision 1.2  2011/01/25 18:05:51  ccruces
** Primera versi�n con la estructura requerida por Alstom
**
** Revision 1.1  2010/09/16 16:53:32  ccruces
** Control: Deshabilitado watchdog.
** Cambios varios en Faultable.h y .cpp as� como en ErrManag.cpp
** En DBManag.cpp a�adido l�nea que fuerza a false el modo simulaci�n.
** En el proyecto, realizados cambios para que genere adecuadamente versi�n debug y release.
** En galileo_functions.cpp eliminados dos printfs
** En ecatNotification.cpp: linea 2444: Los codigos de error para los diferentes esclavos tienen una base com�n y luego se suma su identificador con el @ de autoincremento. Este autoincremento es negativo pero el compilador nos tomaba el 65535 (-1) como unsigned y por ello sumaba 65535 al c�digo de error.
** En Management.cpp: corregidos variables multibyte, a�adido printf cuando nos dicen de cerrar, Reserva est�tica del array empleado para leer el xml.
** en Error_ecat.h corregidos c�digos de errores que no respetaban los huecos asignados.
** En Developer.cpp: los errores reportados a Galileo han de ser c�digos negativos (a�adido - en el paso de par�metros de process_errors)
**
** Ahora est� funcionando tanto en debug como en release y para que funcione, adem�s de estos cambios se ha modificado el fichero FCfgErro.xml (compactflash/APCtrEco/Configcn/CfgGest) haciendo que por cada error solo se empile una �nica otra variable (antes ten�a como una docena por cada error) y esto ha liberado mucho de carga al programa y ha permitido funcionar OK. La clave estaba en que los datos recibidos del bus Ethercat provocan errores porque los m�dulos no tienen un HW real detras, el numero de errores contabilizado ha sido de 65 (con el hmi).
**
*/

/*! @file    ErrManag.cpp						*/
/*! @brief   Gestor de errores					*/
/*! @version 1.0								*/
/*! @author  Oskar Berreteaga					*/
/*! @date    2005-01-19							*/

/******************************************* Include */
#include "..\StdAfx.h"
#include "globdefs.h"
#include "globtype.h"

#include "..\..\XMLDRIVER\stdafx.h"
#include "..\..\XMLDRIVER\XMLDriver.h"

#include "TimeBased.h" // <- NCALib

#include "Paths.h"
#include "PilaError.h"
#include "Msgqueue.h"
#include "Logger.h"
#include "FaultTable.h"
#include "IOMonitor.h"
#include "PersistentVars\PerSistentVariable.h"
#include "Utils\MessageQueues.h"
#include "Control.h"

#include "ErrManag.h"

/******************************************* Defines */
#define PRINT_DBG_INFO					(0)						///< Control global de la visualizaci�n de mensajes de depuraci�n
#define	ZONE_INIT						(1 && PRINT_DBG_INFO)	///< Controla la visualizaci�n de mensajes en la inicializaci�n
#define	ZONE_RUN						(0 && PRINT_DBG_INFO)	///< Controla la visualizaci�n de mensajes en la ejecuci�n
#define	ZONE_EXIT						(1 && PRINT_DBG_INFO)	///< Controla la visualizaci�n de mensajes en la finalizaci�n
#define ZONE_WARNING					(1)
#define ZONE_ERROR						(1)

#define NUMBER_OF_PILE_VARS				(NUM_VARS)				///< Defined in FaultTable.h

// To perform message queues reader/writter check
#define DO_MESSAGE_QUEUE_VERIFICATION	(0)						///< Set it to 0 in final release in order not to do extra work

// For "reset colectivo" orders from OPC or HMI (and others)
#define MAX_NUMBER_OF_RESETS_FIRST_TIME	(1)  /* old: 2 */		///< Number of errors reset inmediatly, when receiving a request
#define NUMBER_OF_ERRORS_TO_READ		(5)						///< Default number of errors to read by request
#define MAX_NUMBER_OF_ERRORS_TO_READ	(200)					///< Number of requested errors to read
#define LAUNCH_WARNING_MQ_MESSAGES		(10)					///< Number of messages in a MQ to launch a warning message
#define THREAD_IDLE_NUM_CYCLES			(1)	 /* old: 5 */		///< Number of cycles to wait before trying to reset next error

// These are the time (in milliseconds) defintions that ErrManag threads
// wait for an event on the message queue they listen. That is, when no event
// is in the queues, the threads wake up after this time, anyway (very useful
// for, for example, run the 'nca_quit')
//
#define TIME_FOR_DISPLAY_ERROR_THREAD	(500UL)					///< time for 'erDisplayErrorsThread' (0'5 seconds)
#define COMMANAG_LISTEN_TIMEOUT			(3000UL)				///< time for 'erComManCommunicationThread' (3 seconds) -shared value-
#define FILEMANAG_LISTEN_TIMEOUT		(15000UL)				///< time for 'erErrFileManagerThread' (30 seconds)
#define EVENTMANAG_LISTEN_TIMEOUT		(3000UL)				///< time for 'erEventFileManagerThread' (3 seconds)

#if ENABLE_TIMING_COUNT
#define ERCOMMANAG_THREAD_DWHITS		(1000)					///< For 'erComManCommunicationThread', probably more than 1s makes no sense
#define ERFILEMAN_THREAD_DWHITS			(1000)					///< For 'erErrFileManagerThread', probably more than 1s makes no sense
#define EVFILEMAN_THREAD_DWHITS			(1000)					///< For 'erEventFileManagerThread', probably more than 1s makes no sense
#define DISPLAYERR_THREAD_DWHITS		(30000)					///< For 'erDisplayErrorsThread', less than 1s never happen!
#endif

// Tag definition related to 'FCfgErro.xml' contents
//
#define FCFGERROR_MAIN_NODE_WSTRING		L"config_errores/error"
#define FCFGERROR_AL_NAME_WSTRING		L"name"
#define FCFGERROR_AL_DESC_WSTRING		L"descripcion"
#define FCFGERROR_AL_DISABLE_REPORTING	L"disable_reporting"
#define FCFGERROR_AL_ERRORREAC_WSTRING	L"reaccion_error"
#define FCFGERROR_AL_RESETLEV_WSTRING	L"nivel_reset"
#define FCFGERROR_AL_TRIGDELAY_WSTRING	L"retardo_activacion"
#define FCFGERROR_AL_RESETDELAY_WSTRING	L"retardo_reset"
#define FCFGERROR_AL_AUTORESETS_WSTRING	L"num_resetauto"
#define FCFGERROR_AL_STACK_WSTRING		L"pila"
#define FCFGERROR_AL_XTRAINFO_WSTRING	L"extra_info"

/******************************************* Typedefs */
typedef enum TTypeOfFile
{
	FIC_ERROR = 0,	///< Indicador de ficheros de errores
	FIC_EVENTO		///< Indicador de ficheros de eventos
};

/******************************************* App Globals */
CError			Error;				///< Objeto de la clase CError utilizado para registrar errores de SW, warnings y errores de sistema
VARPILA			pilaVars[NUM_VARS];				///< Estructura que contiene las variables de pila

INT32			iNumMs = 0;						///< Contador de milisegundos (con precisi�n de 100 ms)
BOOL			bWriteErrorFile = FALSE;		///< Indica si se ha de escribir en un fichero de error (de SW, de sistema o warning)

extern	BOOL	g_tb_bFinished;					///< Se�alizado por la base de tiempos para terminar la aplicaci�n

/******************************************* Globals */

// Otros
static	HANDLE					er_htComManCommunication = INVALID_HANDLE_VALUE;	///< Handle del thread de comunicaci�n con el Gestor de Comunicaciones
static	HANDLE					er_htDisplayErrors = INVALID_HANDLE_VALUE;			///< Handle del thread que visualiza los c�digos de error por el display de 7 segmentos

static	BOOL					m_bErrorManagerFinishOrder = FALSE;					///< Si TRUE indica que el G. de Errores debe finalizar

static	HANDLE					hMQEventManRead = INVALID_HANDLE_VALUE;				///< Handle de la cola de mensajes de registro de eventos en fichero XML
static	HANDLE					hMQReadFromComM = INVALID_HANDLE_VALUE;				///< Handle de la cola de mensajes provenientes del Gestor de Comunicaciones
static	HANDLE					hMQWriteToComM = INVALID_HANDLE_VALUE;				///< Handle de la cola de mensajes con destino Gestor de Comunicaciones

static	IOMonitor				*cDB = NULL;										///< Acceso a la base de datos

static	HANDLE					hDrv = INVALID_HANDLE_VALUE;						///< Handle del driver del display de 7 segmentos
static	BOOL					bResetColectivo = FALSE;							///< Si TRUE, se est� llevando a cabo un reset colectivo de errores
static	BOOL					bResetColectivoHMI = FALSE;

static	DWORD					dwNumErroresRC = 0UL;								///< N�mero de errores que falta por resetear cuando se lleva a cabo un reset colectivo
static	THandle*				thFaultListRC = NULL;								///< Array de identificadores de error que deben de resetearse cuando se lleva a cabo un reset colectivo
static	UINT32					uiFaultListRCSize = 0;
static	DWORD					dwNumErrReseteadosRC = 0UL;							///< N�mero de errores que ya se han reseteado cuando se lleva a cabo un reset colectivo

static	THandle*				thCommFaultList = NULL;
static	UINT32					uiCommFaultListSize = 0;
static	THandle*				thErrFaultList = NULL;
static	UINT32					uiErrFaultListSize = 0;

static	INT32					iNumCicles = 0;										///< N�mero de veces que se ha entrado en el ciclo encargado de llevar a cabo un reset colectivo

static	IUsrCommand*			m_ptrUsrCommand = NULL;								///< The UsrCommand implementation!

#if ENABLE_TIMING_COUNT
// For 'erComManCommunicationThread'
static DWORD					dwMax = 0UL;
static DWORD					dwMin = ERCOMMANAG_THREAD_DWHITS;
static DWORD					dwDiff = 0UL;
static DWORD					dwHits[ERCOMMANAG_THREAD_DWHITS + 1];
static DWORD					dwTicksBegin = 0UL;
// For 'erErrFileManagerThread'
static DWORD					dwMax3 = 0UL;
static DWORD					dwMin3 = ERFILEMAN_THREAD_DWHITS;
static DWORD					dwDiff3 = 0UL;
static DWORD					dwHits3[ERFILEMAN_THREAD_DWHITS + 1];
static DWORD					dwTicksBegin3 = 0UL;
// For 'erEventFileManagerThread'
static DWORD					dwMax4 = 0UL;
static DWORD					dwMin4 = EVFILEMAN_THREAD_DWHITS;
static DWORD					dwDiff4 = 0UL;
static DWORD					dwHits4[EVFILEMAN_THREAD_DWHITS + 1];
static DWORD					dwTicksBegin4 = 0UL;
// For 'erDisplayErrorsThread'
static DWORD					dwMax5 = 0UL;
static DWORD					dwMin5 = DISPLAYERR_THREAD_DWHITS;
static DWORD					dwDiff5 = 0UL;
static DWORD					dwHits5[DISPLAYERR_THREAD_DWHITS + 1];
static DWORD					dwTicksBegin5 = 0UL;
#endif

#define THREADCOUNT 4
static HANDLE thread[THREADCOUNT];
static INT32 threadCount = 0;

/**************************************** Prototypes */
static	DWORD WINAPI	erComManCommunicationThread(LPVOID lpvParam);
static	DWORD WINAPI	erDisplayErrorsThread(LPVOID lpvParam);

static	BOOL			erLeerXMLConfGestorErrores();
static	BOOL			erLeerXMLConfGestorEventos();
static	BOOL			erLeerXMLConfErrores();
static	BOOL			erLeerXMLConfPilaErrores();

static	DWORD			erComManCommunication();
static	DWORD			erStoreErrorPileVars();

static	BOOL			erHasControlEnded();
static	INT32			erSetupErrManagerThreads(Control *ctrl);
static	INT				erCleanupErrManagerThread();
static	INT32			erVisualizarErroresDisplay(const CHAR *cbCadenaError);

static BOOL		erAbrirMsgQueuesComManCommunication();
static INT32	erProcessRequest(const CONSULTA_ERRMAN*, RESPUESTA_DN*);
static VOID		erCheckMemoryLeaks();

// ----------------------------------
// The TimeBased for the ErrorManager
// ----------------------------------

class TErrorManagerTimeBased : public acl::core::TimeBased
{
public:
	
	TErrorManagerTimeBased(CFaultTable *ftable) : acl::core::TimeBased(L"ErrorManager",
										 ERRM_PRIORITY,
										 ERRMANAGER_RATE) 
	{ 
		ASSERT (ftable);
		FaultTable = ftable;
	};

	virtual INT32 fTimeBasedLoop()
	{
		INT32			iFRet = 0;

		SYSTEMTIME		stLocalTime = {0};
		INT32			iSecond = -1;
		INT32			i = 0;

		DEBUGMSG(ZONE_RUN, (_T("=> TErrorManagerTimeBased::fTimeBasedLoop\r\n")));

		// Inicializamos o actualizamos la precisi�n de 100ms

		if (iNumMs < 0)
		{
			GetLocalTime(&stLocalTime);
			if (iSecond < 0)
			{
				iSecond = (INT32)stLocalTime.wSecond;
			}
			else
			{
				if ((INT32)stLocalTime.wSecond != iSecond)
				{
					iNumMs = 0;
				}
			}
		}
		else
		{
			if (iNumMs == 9)
			{
				iNumMs = 0;
			}
			else
			{
				iNumMs++;
			}
		}

		// Se almacenam los valores de las variables de pila

		erStoreErrorPileVars();
		
		// Se refresca el estado de los errores registrados en el sistema

		if ((i = FaultTable->TimerCall()) != 0)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("TErrorManagerTimeBased::fTimeBasedLoop: CFaultTable::TimerCall() fails (returns %d)!\r\n"), i));
			Error.Set(errorFAULTTABLE_TIMERCALL_FAILS_NOT_REFRESHING_ERROR_STATES);
			iFRet = -1;
		}

		DEBUGMSG(ZONE_RUN, (_T("<= TErrorManagerTimeBased::fTimeBasedLoop\r\n")));
		return iFRet;
	};
private:
	CFaultTable *FaultTable; ///< puntero a la faultable
};

// The global var...

static TErrorManagerTimeBased* m_pErrorManagerTimeBased = NULL;

/************************** Funciones de inicializaci�n *********************************/

//  ==========================================================
//   
///
/// Pone en marcha el gestor de errores
/// 
/// \return INT32
/// \retval 0 si todo ha ido bien
/// \retval -1 si algo ha fallado
///
INT32 erSetupErrManager (Control *ctrl)
{
	
	INT32		iRet = 0;
	DWORD		dwRet = 0UL;
	BOOL		bRet = FALSE;

	INT32		iNumEvents = 0;
	DWORD		dwThreadID = 0UL;
	ULONG		ulMaxNumHist = 0UL;
	SYSTEMTIME	stMyTime = {0};
	WORD		wYear = (WORD)0;

	static BOOL bInitialized = FALSE;
	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);

	DEBUGMSG(ZONE_INIT, (_T("=> erSetupErrManager\r\n")));

	try
	{
		// Checking just one initialization
		if (bInitialized == TRUE)
		{
			DEBUGMSG(ZONE_WARNING, (_T("erSetupErrManager: module already initialized! Nothing done\r\n")));
			return(0);
		}

		// Obtiene acceso a la base de datos
		cDB = IOMonitor::GetIOMonitor();
		if (cDB == NULL)
		{
			ERRORMSG(ZONE_ERROR, (_T("erSetupErrManager: IOMonitor::GetIOMonitor() fails!\r\n")));
			return(-1);
		}

		// Inicializaci�n: Registramos las variables de pila leyendolas del XML
		//bRet = erLeerXMLConfPilaErrores();
		//if (bRet == FALSE)
		//{
		//	ERRORMSG(ZONE_ERROR, (L"[ERROR] erSetupErrManager: erLeerXMLConfPilaErrores() fails! Launching exception...\r\n"));
		//}

		//Registramos los errores leyendolos del XML
		bRet = erLeerXMLConfErrores();
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: erLeerXMLConfErrores() fails! Launching exception...\r\n"));
			throw 4UL;
		}

		// Right here we already have all errors loaded in memory. So, it's time
		// to ask the FaultTable to review its active list (the list should contain
		// nothing or errors code related to SwError -in the way '-XXXX'-)

		// TO_BE_DONE!!!

		// Intentamos leer los par�metros de control de tama�o y n�mero de
		// ficheros del fichero de configuraci�n del Gestor de Errores
		bRet = erLeerXMLConfGestorErrores();
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: erLeerXMLConfGestorErrores() fails! Launching exception...\r\n"));
			throw 4UL;
		}

		// Intentamos leer los par�metros de control de tama�o y n�mero de
		// ficheros del fichero de configuraci�n del Gestor de Eventos
		bRet = erLeerXMLConfGestorEventos();
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: erLeerXMLConfGestorEventos() fails! Launching exception...\r\n"));
			throw 4UL;
		}

		// Here we have that, all errors have been loaded into the FaultTable, and also, all
		// errors that were activated before last power down have been restored. So, errors from
		// now on will continue with the last unique alarm counter (needed for SCADA) whenever
		// they are triggered.

#if ENABLE_TIMING_COUNT
		memset((void *)dwHits, 0, sizeof(dwHits));
		memset((void *)dwHits3, 0, sizeof(dwHits3));
		memset((void *)dwHits4, 0, sizeof(dwHits4));
		memset((void *)dwHits5, 0, sizeof(dwHits5));
#endif

		// Crea todos los thread relacionados con el gestor de errores (directamente despiertos)
		iRet = erSetupErrManagerThreads(ctrl);
		if (iRet != 0)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: erSetupErrManagerThread() fails (returns %d)! Launching exception...\r\n", iRet));
			throw 4UL;
		}

		//// Damos tiempo a que entre la tarea del G. de Errores para que est� preparada
		//Sleep(5UL);	// <- magic number here!
	}
	catch (ULONG ulExcepCode)
	{
		ERRORMSG(ZONE_ERROR, (_T("Trying to handle exception in erSetupErrManager\r\n")));
		iRet = -1;

		switch (ulExcepCode)
		{
			case 5UL:
				iRet = erCleanupErrManagerThread();
				if (iRet != 0)
				{
					ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: erCleanupErrManagerThread() fails. Error %d\r\n", iRet));
				}
				iRet = -5;
				break;
			case 4UL:
				iRet--;
			case 3UL:
				iRet--;
			case 2UL:
				bRet = CloseMsgQueue(hMQEventManRead);
				if (bRet == FALSE)
				{
					ERRORMSG(ZONE_ERROR, (_T("erSetupErrManager: CloseMsgQueue() fails. GetLastError %lu\r\n"), GetLastError()));
				}
				iRet--;
			case 1UL:
				//nada en especial
				break;
			default:
				ERRORMSG(ZONE_ERROR, (L"erSetupErrManager: Unhandled exception code %lu\r\n", ulExcepCode));
				throw;
				break;
		}
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erSetupErrManager\r\n"));
		throw;
	}

	bInitialized = TRUE;
	DEBUGMSG(ZONE_INIT, (_T("<= erSetupErrManager\r\n")));
	return(iRet);
}

//  ==========================================================
//   
/// See ErrManag.h for more information
///
INT32 erSetUsrCommand(IUsrCommand* ptrUsrCommand)
{
	INT32 iFRet = 0;

	if (m_ptrUsrCommand != NULL)
	{
		ERRORMSG(ZONE_WARNING, (TEXT("erSetUsrCommand: IUsrCommand* already set! Nothing done\r\n")));
		iFRet = -1;
	}
	else
	{
		if (ptrUsrCommand == NULL)
		{
			ERRORMSG(ZONE_WARNING, (TEXT("erSetUsrCommand: parameter is NULL! Nothing done\r\n")));
			iFRet = -1;
		}
		else
		{
			DEBUGMSG(ZONE_WARNING, (TEXT("erSetUsrCommand: received the IUsrComand object\r\n")));
			m_ptrUsrCommand = ptrUsrCommand;
		}
	}

	return iFRet;
}

//  ==========================================================
//
///
/// Crea el objeto (hereda de TimeBased) del ErrorManager
/// y otros threads relacionados con la gesti�n de errores
///
/// \return INT32 iFRet
/// \retval 0 si todo ha ido bien
/// \retval <0 si algo ha fallado
///
static INT32 erSetupErrManagerThreads( Control *ctrl)
{
	INT32	iFRet = 0;
	DWORD	dwThreadID = 0UL;
	BOOL	bRet = FALSE;

	DEBUGMSG(ZONE_INIT, (_T("=> erSetupErrManagerThreads\r\n")));

	try
	{
		// 1. ErrorManager (TimeBased)

		if (m_pErrorManagerTimeBased == NULL)
		{
			m_pErrorManagerTimeBased = new TErrorManagerTimeBased(CFaultTable::getFaultTable());
			if (m_pErrorManagerTimeBased == NULL)
			{
				ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: can't create ErrorManager-TimeBased object!\r\n"));
				throw 1UL;
			}
		}
		else
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: ErrorManager-TimeBased already initialized!\r\n"));
			throw 2UL;
		}

		// 4. Crea el thread del G. de comunicaciones (CommManager)
		er_htComManCommunication = CreateThread(NULL,							// CE Security
												0UL,							// Default Size
												erComManCommunicationThread,	// Thread
												NULL,							// No Parameters
												0UL,							// Not SUSPENDED
												&dwThreadID);					// Thread Id: not used
		if (er_htComManCommunication == NULL)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: CreateThread() fails (GetLastError() returns %u)!\r\n", GetLastError()));
			throw 6UL;
		}
		thread[threadCount++] = er_htComManCommunication;

		// 4.1. Ajusta la prioridad del thread

		bRet = CeSetThreadPriority(er_htComManCommunication, COMM_PRIORITY);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: CeSetThreadPriority() fails (GetLastError() returns %u)!\r\n", GetLastError()));
			throw 7UL;
		}

		DEBUGMSG(ZONE_WARNING, (L"ErrComManager thread ID: 0x%08lX, priority %d\r\n", dwThreadID, CeGetThreadPriority(er_htComManCommunication)));

	}
	catch (ULONG ulExcepCode)
	{
		ERRORMSG(ZONE_ERROR, (L"Trying to handle exception in erSetupErrManagerThreads\r\n"));

		switch (ulExcepCode) // note: no break anywhere! (which is correct)
		{
			case 8UL: // Nothing created, nothing to do

			case 7UL: // Destruimos el thread 'er_htComManCommunication'

				bRet = TerminateThread(er_htComManCommunication, 0UL);
				if (bRet == FALSE)
				{
					ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: TerminateThread() fails (GetLastError() returns %u)!\r\n", GetLastError()));
				}
				bRet = CloseHandle(er_htComManCommunication);
				if (bRet == FALSE)
				{
					ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: CloseHandle() fails (GetLastError() returns %u)!\r\n", GetLastError()));
				}

			case 6UL: // Destruimos el thread 'er_htEventFileManager'

			case 5UL: // Nothing created, nothing to do

			case 4UL: // Destruimos el thread 'er_htErrFileManager'

			case 3UL:  // Nothing created, nothing to do

			case 2UL: // Destruimos el ErrorManager (TimeBased)

				if (m_pErrorManagerTimeBased != NULL)
				{
					delete m_pErrorManagerTimeBased;
					m_pErrorManagerTimeBased = NULL;
				}

			case 1UL: // Nothing created, nothing to do
				break;

			default:
				ERRORMSG(ZONE_ERROR, (L"erSetupErrManagerThreads: Unhandled exception code %lu\r\n", ulExcepCode));
				throw;
				break;
		}

		iFRet = (-1 * ulExcepCode);
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erSetupErrManagerThreads\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erSetupErrManagerThreads\r\n")));
	return iFRet;
}

//  ==========================================================
//   
///
/// Pone en marcha el gestor de display de errores
/// 
/// \return INT
/// \retval 0 si todo ha ido bien
/// \retval -1 si algo ha fallado
///
INT32 erSetupErrDisplayManager()
{
	INT			iRet = 0;
	DWORD		dwThreadID = 0UL;

	DEBUGMSG(ZONE_INIT, (_T("=> erSetupErrDisplayManager\r\n")));

	try
	{
		// Crea el thread que visualiza errores por el display (note: priority not set!)
		er_htDisplayErrors = CreateThread(NULL,						// CE Security
										  0UL,						// Default Size
										  erDisplayErrorsThread,	// Thread
										  NULL,						// No Parameters
										  0UL,						// Not SUSPENDED
										  &dwThreadID);				// Thread Id: not used
		if (er_htDisplayErrors == NULL)
		{
			ERRORMSG(ZONE_ERROR, (L"erSetupErrDisplayManager: CreateThread() fails (GetLastError() returns %u)!\r\n", GetLastError()));
			throw 1UL;
		}
		
		thread[threadCount++] = er_htDisplayErrors;

		DEBUGMSG(ZONE_WARNING, (L"DisplayErrors thread ID: 0x%08lX, priority %d\r\n", dwThreadID, CeGetThreadPriority(er_htDisplayErrors)));
	}
	catch (ULONG ulExcepCode)
	{
		ERRORMSG(ZONE_ERROR, (_T("Trying to handle exception in erSetupErrDisplayManager\r\n")));

		switch (ulExcepCode)
		{
			case 1UL:
				//nada en especial
				break;
			default:
				ERRORMSG(ZONE_ERROR, (L"erSetupErrDisplayManager: Unhandled exception code %lu\r\n", ulExcepCode));
				throw;
				break;
		}

		iRet = -1;
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erSetupErrDisplayManager\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erSetupErrDisplayManager\r\n")));
	return(iRet);
}

/************************** Funciones de finalizaci�n ***********************************/

//  ==========================================================
//   
///
/// Detiene el gestor de errores
/// 
/// \return INT
/// \retval 0 si todo ha ido bien
/// \retval -1 si algo ha fallado
///
INT32 erCleanupErrManager()
{
	INT iRet = 0;

	DEBUGMSG(ZONE_EXIT, (_T("=> erCleanupErrManager\r\n")));

	try
	{
		// Detiene el thread del Gestor de Errores
		m_bErrorManagerFinishOrder = TRUE;
		// Damos tiempo a que terminen todos los threads del Gestor de Errores
		DWORD dwTTS = MAX(TIME_FOR_DISPLAY_ERROR_THREAD, MAX(COMMANAG_LISTEN_TIMEOUT, MAX(FILEMANAG_LISTEN_TIMEOUT, EVENTMANAG_LISTEN_TIMEOUT)));
		DEBUGMSG(1, (_T("erCleanupErrManager: MAX waiting %d ms...\r\n"), dwTTS));

		if ( threadCount < THREADCOUNT )
		{
			WaitForMultipleObjects (threadCount, thread, TRUE, dwTTS);
		}
		else
		{
			WaitForMultipleObjects (THREADCOUNT, thread, TRUE, dwTTS);
		}

#if ENABLE_TIMING_COUNT
		DWORD dwTotal = 0UL;

		// ---
		// 1: erComManCommunicationThread
		// ---

		if (dwTicksBegin != 0UL)
		{
			// This says: "This means that I've been deleted and my loop didn't finished!
			// Am I the guilty?". At least it would give us more information...
			DEBUGMSG(ZONE_WARNING, (L"ErrComManager: last execution didn't finish (%d)! GUILTY?\r\n", dwTicksBegin));
		}

		// Let's print...
		dwTotal = 0UL;
		UINT32 index;
		for (index = 0; index < dim(dwHits); index++)
		{
			if (dwHits[index] != 0UL) dwTotal += dwHits[index];
		}
		if (dwTotal != 0UL)
		{
			DEBUGMSG(ZONE_WARNING, (L"ErrComManager: dwMin = %lu, dwMax = %lu, total = %lu\r\n", dwMin, dwMax, dwTotal));
			for (index = 0; index < dim(dwHits); index++)
			{
				if (dwHits[index] != 0UL)
				{
					DEBUGMSG(ZONE_WARNING, (L"ErrComManager: dwHits[%d] = %lu (%.2f)\r\n", index, dwHits[index], ((FLOAT)dwHits[index] / dwTotal) * 100.0f));
				}
			}
		}
		else
		{
			DEBUGMSG(ZONE_WARNING, (L"ErrComManager: not executed!\r\n"));
		}

		// ---
		// 3: erErrFileManagerThread
		// ---

		if (dwTicksBegin3 != 0UL)
		{
			// This says: "This means that I've been deleted and my loop didn't finished!
			// Am I the guilty?". At least it would give us more information...
			DEBUGMSG(ZONE_WARNING, (L"ErrorFileManager: last execution didn't finish (%d)! GUILTY?\r\n", dwTicksBegin3));
		}

		// Let's print...
		dwTotal = 0;
		for (index = 0; index < dim(dwHits3); index++)
		{
			if (dwHits3[index] != 0UL) dwTotal += dwHits3[index];
		}
		if (dwTotal != 0UL)
		{
			DEBUGMSG(ZONE_WARNING, (L"ErrorFileManager: dwMin = %lu, dwMax = %lu, total = %lu\r\n", dwMin3, dwMax3, dwTotal));
			for (index = 0; index < dim(dwHits3); index++)
			{
				if (dwHits3[index] != 0UL)
				{
					DEBUGMSG(ZONE_WARNING, (L"ErrorFileManager: dwHits[%d] = %lu (%.2f)\r\n", index, dwHits3[index], ((FLOAT)dwHits3[index] / dwTotal) * 100.0f));
				}
			}
		}
		else
		{
			DEBUGMSG(ZONE_WARNING, (L"ErrorFileManager: not executed!\r\n"));
		}

		// ---
		// 4: erEventFileManagerThread
		// ---

		if (dwTicksBegin4 != 0UL)
		{
			// This says: "This means that I've been deleted and my loop didn't finished!
			// Am I the guilty?". At least it would give us more information...
			DEBUGMSG(ZONE_WARNING, (L"EventFileManager: last execution didn't finish (%d)! GUILTY?\r\n", dwTicksBegin4));
		}

		// Let's print...
		dwTotal = 0;
		for (index = 0; index < dim(dwHits4); index++)
		{
			if (dwHits4[index] != 0UL) dwTotal += dwHits4[index];
		}
		if (dwTotal != 0UL)
		{
			DEBUGMSG(ZONE_WARNING, (L"EventFileManager: dwMin = %lu, dwMax = %lu, total = %lu\r\n", dwMin4, dwMax4, dwTotal));
			for (index = 0; index < dim(dwHits4); index++)
			{
				if (dwHits4[index] != 0UL)
				{
					DEBUGMSG(ZONE_WARNING, (L"EventFileManager: dwHits[%d] = %lu (%.2f)\r\n", index, dwHits4[index], ((FLOAT)dwHits4[index] / dwTotal) * 100.0f));
				}
			}
		}
		else
		{
			DEBUGMSG(ZONE_WARNING, (L"EventFileManager: not executed!\r\n"));
		}

		// ---
		// 5: erDisplayErrorsThread
		// ---

		if (dwTicksBegin5 != 0UL)
		{
			// This says: "This means that I've been deleted and my loop didn't finished!
			// Am I the guilty?". At least it would give us more information...
			DEBUGMSG(ZONE_WARNING, (L"DisplayErrors: last execution didn't finish (%d)! GUILTY?\r\n", dwTicksBegin5));
		}

		// Let's print...
		dwTotal = 0;
		for (index = 0; index < dim(dwHits5); index++)
		{
			if (dwHits5[index] != 0UL) dwTotal += dwHits5[index];
		}
		if (dwTotal != 0UL)
		{
			DEBUGMSG(ZONE_WARNING, (L"DisplayErrors: dwMin = %lu, dwMax = %lu, total = %lu\r\n", dwMin5, dwMax5, dwTotal));
			for (index = 0; index < dim(dwHits5); index++)
			{
				if (dwHits5[index] != 0UL)
				{
					DEBUGMSG(ZONE_WARNING, (L"DisplayErrors: dwHits[%d] = %lu (%.2f)\r\n", index, dwHits5[index], ((FLOAT)dwHits5[index] / dwTotal) * 100.0f));
				}
			}
		}
		else
		{
			DEBUGMSG(ZONE_WARNING, (L"DisplayErrors: not executed!\r\n"));
		}
#endif

		// Destruye el thread del Gestor de Errores
		iRet = erCleanupErrManagerThread();
		if (iRet != 0)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManager: erCleanupErrManagerThread() fails (returns %d)!\r\n", iRet));
			// No hay raz�n para lanzar una excepci�n;
		}

	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (_T("Rethrowing exception in erCleanupErrManager\r\n")));
		throw;
	}

	DEBUGMSG(ZONE_EXIT, (_T("<= erCleanupErrManager\r\n")));
	return(iRet);
}

//  ==========================================================
//   
///
/// Destruye el thread del G. de Errores y los eventos asociados
/// 
/// \return INT
/// \retval 0 si todo ha ido bien
/// \retval -1 si algo ha fallado
///
static INT erCleanupErrManagerThread()
{
	INT iRet = 0;
	BOOL bRet = FALSE;

	DEBUGMSG(ZONE_EXIT, (_T("=> erCleanupErrManagerThread\r\n")));

	try
	{
		// Destruimos el thread er_htComManCommunication
		bRet = TerminateThread(er_htComManCommunication, 0UL);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManagerThread: TerminateThread() fails (GetLastError() returns %u)!\r\n", GetLastError()));
		}
		bRet = CloseHandle(er_htComManCommunication);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManagerThread: CloseHandle() fails (GetLastError() returns %u)!\r\n", GetLastError()));
		}
			
		
		// Y el ErrorManager (TimeBased)
		if (m_pErrorManagerTimeBased != NULL)
		{
			delete m_pErrorManagerTimeBased;
			m_pErrorManagerTimeBased = NULL;
		}

		//Cerramos los handles de las colas de mensajes
		bRet = CloseHandle(hMQEventManRead);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManagerThread: CloseHandle() fails (GetLastError() returns %u)!\r\n", GetLastError()));
		}
		bRet = CloseHandle(hMQReadFromComM);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManagerThread: CloseHandle() fails (GetLastError() returns %u)!\r\n", GetLastError()));
		}
		bRet = CloseHandle(hMQWriteToComM);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrManagerThread: CloseHandle() fails (GetLastError() returns %u)!\r\n", GetLastError()));
		}		
	}
	catch (ULONG ulExcepCode)
	{
		ERRORMSG(ZONE_ERROR, (_T("erCleanupErrManagerThread: Unhandled exception code %lu\r\n"), ulExcepCode));
		iRet = -1;
		throw;
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (_T("Rethrowing exception in erCleanupErrManagerThread\r\n")));
		throw;
	}

	DEBUGMSG(ZONE_EXIT, (_T("<= erCleanupErrManagerThread\r\n")));
	return(iRet);
}

//  ==========================================================
//   
///
/// Finaliza el gestor de display de errores
/// 
/// \return INT
/// \retval 0 si todo ha ido bien
/// \retval -1 si algo ha fallado
///
INT32 erCleanupErrDisplayManager()
{
	BOOL	bRet = FALSE;
	INT		iRet = -1;

	DEBUGMSG(ZONE_INIT, (_T("=> erCleanupErrDisplayManager\r\n")));

	try
	{
		//Finaliza el thread que muestra errores por el display de 7 segmentos
		bRet = TerminateThread(er_htDisplayErrors, 0UL);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrDisplayManager: TerminateThread() fails. GetLastError %u\r\n", GetLastError()));
		}
		bRet = CloseHandle(er_htDisplayErrors);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_WARNING, (L"erCleanupErrDisplayManager: CloseHandle() fails. GetLastError %u\r\n", GetLastError()));
		}

		//Cierra el handle del driver del display de 7 segmentos
		bRet = CloseHandle(hDrv);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erCleanupErrDisplayManager: CloseHandle failed. GetLastError() returns %d\r\n", GetLastError()));
			Error.SetSysErr(syserrorD7SDRIVER_CLOSEHANDLE_FAILS);
			return (-1);
		}
		iRet = 0;
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erCleanupErrDisplayManager\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erCleanupErrDisplayManager\r\n")));
	return(iRet);
}

/*************** Funciones de lectura de ficheros de configuraci�n ********************/

//  ==========================================================
//   
///
/// Lee del fichero 'FCfgSist.xml' el nivel de traza de error
/// que se quiere configurar para la app. de Control
///
/// \return BOOL
/// \retval TRUE	si todo ha ido bien
/// \retval FALSE	si ha fallado algo
///
BOOL erConfigurarTrazaErrores()
{
	BOOL			bReturn = FALSE;
	BOOL			bRet = FALSE;

	TCHAR			tcbTmpResult[NODE_VALUE_LENGTH];
	BOOL			bTraceLevelCorrect = FALSE;
	TCHAR			*tcbStop = NULL;
	DWORD			dwTraza = 0UL;
	DWORD			dwMaxFileNo = 0UL;
	TCHAR			*fileName = NULL;

	NodeDetails		*myNode = new NodeDetails();
	NodeDetails		*currentNode = new NodeDetails();
	CXMLdriverApp	*xml_tmp = new CXMLdriverApp();

	try
	{
		ASSERT((myNode != NULL) && (currentNode != NULL) && (xml_tmp != NULL));

		memset(tcbTmpResult, 0, sizeof(tcbTmpResult));
		fileName = _wcsdup(F_FCFGSIST);
		if (fileName != NULL)
		{
			bRet = xml_tmp->loadXMLFile(fileName);
			if (bRet == TRUE)
			{
				bRet = xml_tmp->findNode(TEXT("config_sistema/Control"), *myNode);
				if (bRet == TRUE)
				{
					bRet = xml_tmp->nextNode(*currentNode);
					if (bRet == TRUE)
					{
						bRet = xml_tmp->getParameter(TEXT("nivel_traza"), tcbTmpResult);
						if (bRet == TRUE)
						{
							if ((tcbTmpResult != NULL) && (wcscmp(tcbTmpResult, TEXT("")) != 0))
							{
								dwTraza = wcstoul(tcbTmpResult, &tcbStop, 10);

								if ((dwTraza == 0) && (wcscmp(tcbTmpResult, TEXT("0")) != 0))
								{
									bTraceLevelCorrect = FALSE;
								}
								//Vamos a comprobar que el valor leido es coherente
								else if ((dwTraza >= 0) || (dwTraza <= 40))
								{
									bTraceLevelCorrect = TRUE;
								}
							}
							if (bTraceLevelCorrect == FALSE)
							{
								ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: El valor del atributo 'nivel_traza' del nodo 'config_sistema/Control' no es v�lido\r\n")));
								Error.Warning(warningINCORRECT_VALUE_OF_ERROR_TRACE_CONFIG_DATA);
							}
						}
						//Leemos el n�mero m�ximo de ficheros que se van a guardar
						bRet = xml_tmp->getParameter(TEXT("max_file_number"), tcbTmpResult);
						if (bRet == TRUE)
						{
							if ((tcbTmpResult != NULL) && (wcscmp(tcbTmpResult, TEXT("")) != 0))
							{
								dwMaxFileNo = wcstoul(tcbTmpResult, &tcbStop, 10);
								SetMaxTraceFileNumber(dwMaxFileNo);
								// NOTA: 
								// si no se lee el n�mero m�ximo de ficheros de traza, 
								// se mantiene la configuraci�n por defecto. No es un error grave 
							}
							else
							{
								ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: El valor del atributo 'max_file_number' del nodo 'config_sistema/Control' no es v�lido\r\n")));
								Error.Warning(warningINCORRECT_VALUE_OF_ERROR_TRACE_CONFIG_DATA);
							}	
						}
						else
						{
							ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: No se ha podido acceder a uno de los atributos del nodo 'config_sistema/Control' del fichero 'FCfgSist.xml'\r\n")));
							Error.Warning(warningCAN_NOT_ACCESS_ERROR_TRACE_NODE);
						}
					}
					else
					{
						ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: No se ha podido acceder al nodo 'config_sistema/Control' del fichero 'FCfgSist.xml'\r\n")));
						Error.Warning(warningCAN_NOT_ACCESS_ERROR_TRACE_NODE);
					}
				}
				else
				{
					ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: No se encuentra el nodo 'config_sistema/Control' en el fichero 'FCfgSist.xml'\r\n")));
					Error.Warning(warningMISSING_ERROR_TRACE_CONFIG_DATA);
				}
			}
			else
			{
				//No lo consideramos un error, porque se va a configurar un valor por defecto
				ERRORMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: Error al acceder al fichero 'FCfgSist.xml'\r\n")));
				Error.Warning(warningIMPOSSIBLE_ACCESS_TO_FCFGSIST_CONFIG_XML);
			}
			
			free(fileName);
			fileName = NULL;

			if (bTraceLevelCorrect == FALSE)
			{
				DEBUGMSG(ZONE_WARNING, (TEXT("erConfigurarTrazaErrores: No se ha podido leer correctamente el valor de la traza\r\n")));
				DEBUGMSG(ZONE_WARNING, (TEXT("\tSe ha configurado con el valor por defecto (30)\r\n")));
				//No se ha leido bien el valor => se pone el valor por defecto
				dwTraza = 30UL;
			}

			SetTraceLevel(dwTraza);
			bReturn = TRUE;
		}
		else
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erConfigurarTrazaErrores: La funci�n _wcsdup() ha devuelto error. Puede que no haya memoria disponible.\r\n")));
			Error.Set(errorWCSDUP_FAILS_IT_MIGHT_NOT_HAVE_ENOUGHT_FREE_MEMORY);
			bReturn = FALSE;
		}
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erConfigurarTrazaErrores\r\n"));
		throw;
	}

	delete(myNode);
	myNode = NULL;
	delete(currentNode);
	currentNode = NULL;
	delete(xml_tmp);
	xml_tmp = NULL;

	return bReturn;
}

//  ==========================================================
///
/// Funci�n encargada de leer los datos de configuraci�n de
/// las alarmas del fichero de configuraci�n XML
/// 
/// \return BOOL
/// \retval TRUE si todo ha ido bien
/// \retval FALSE si algo ha fallado
///
static BOOL erLeerXMLConfErrores()
{
	BOOL			bResult = TRUE;	// <- function returned value

	NodeDetails		*myNode = NULL;
	NodeDetails		*currentNode = NULL;
	CXMLdriverApp	*xml_tmp = NULL;

	TCHAR			*fileName = NULL;
	BOOL			bRet = FALSE;
	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);

	DEBUGMSG(ZONE_INIT, (_T("=> erLeerXMLConfErrores\r\n")));

	try
	{
		// 0. Allocating resources

		myNode = new NodeDetails();
		if (myNode == NULL)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: Can't allocate 1st-NodeDetails object!\r\n")));
			return FALSE;
		}

		currentNode = new NodeDetails();
		if (currentNode == NULL)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: Can't allocate 2nd-NodeDetails object!\r\n")));
			delete(myNode);
			return FALSE;
		}

		xml_tmp = new CXMLdriverApp();
		if (xml_tmp == NULL)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: Can't allocate CXMLdriverApp object!\r\n")));
			delete (myNode);
			delete (currentNode);
			return FALSE;
		}

		// 1. Accessing 'FCfgErro.xml' file

		fileName = _wcsdup(F_FCFGERRO);
		if (fileName == NULL)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: _wcsdup() fails (not enough memory?)!\r\n")));
			bResult = FALSE;
		}
		else
		{
			bRet = xml_tmp->loadXMLFile(fileName);
			if (bRet != TRUE)
			{
				ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: CXMLdriverApp::loadXMLFile('%s') fails!\r\n"), fileName));
				bResult = FALSE;
			}
			else
			{
				bRet = xml_tmp->findNode(FCFGERROR_MAIN_NODE_WSTRING, *myNode);
				if (bRet != TRUE)
				{
					ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: node '%s' not found in '%s'!\r\n"), FCFGERROR_MAIN_NODE_WSTRING, fileName));
					bResult = FALSE;
				}
				else
				{
					THandle	tHandle = UNDEFINED_HANDLE;
					INT32	i = 0;

					TCHAR	tcbErrorName[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorReaccion[NODE_VALUE_LENGTH];
					TCHAR	tcbDisableReporting[NODE_VALUE_LENGTH];
					BOOL	bDisablesReporting = FALSE;
					TCHAR	tcbErrorNivelReset[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorRetardoAct[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorRetardoReset[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorNumResetAut[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorPila[NODE_VALUE_LENGTH];
					TCHAR	description[NODE_VALUE_LENGTH];
					TCHAR	tcbErrorExtraInfo[NODE_VALUE_LENGTH];

					// These are the fields in the XML, and how this FTable will treat them
					// (hopefully, this is a better implementation that the one in the 'classic'
					// FaultTable component):
					//
					// <error name=... descripcion=...>	<- just 'name' is mandatory, 'descripcion' is not (empty string will be used as the default description, or maybe something like "no description")
					//  <disable_reporting>...</>		<- not mandatory; if not declared or declared with different value than 'YES'|'NO', 'NO' will be considered
					//  <reaccion_error>...</>			<- not mandatory; if not declared or declared with different value than 'faultWARNING2'|'faultWARNING1'|'faultALARM1'|'faultALARM2'|'faultALARM3'|'faultAUTOYAW'|'faultPTH'|'faultHYD'|'faultYAW'|'faultGRID'|'faultMOTORIZING'|'faultSOFTSTOP'|'faultCUT'|'faultEMERGENCYSTOP'|'faultBRAKEBAD'|'faultSECLOOPOFF', 'faultWARNING1' will be considered
					//  <estado_aero>...</>				<- not mandatory; ???
					//  <nivel_reset>...</>				<- not mandatory; if not declared or declared with different value than 'faultALL'|'faultREMOTE'|'faultBASE'|'faultNACELLE'|'faultLOCAL'|'faultSYSTEM', 'faultALL' will be considered
					//  <retardo_activacion>...</>		<- not mandatory; if not declared or declared with different value than [0..999999999), 0 will be considered
					//  <retardo_reset>...</>			<- not mandatory; if not declared or declared with different value than [0..999999999), 0 will be considered
					//  <num_reset_auto>...</>			<- not mandatory; if not declared or declared with different value than [-1,0..999999999), -1 will be considered
					//  <pila>...</>					<- not mandatory; if not declared or declared...

					while (xml_tmp->nextNode(*currentNode))
					{
						memset(tcbErrorName, 0, sizeof(tcbErrorName));
						memset(tcbErrorReaccion, 0, sizeof(tcbErrorReaccion));
						memset(tcbDisableReporting, 0, sizeof(tcbDisableReporting));
						bDisablesReporting = FALSE;
						memset(tcbErrorNivelReset, 0, sizeof(tcbErrorNivelReset));
						memset(tcbErrorRetardoAct, 0, sizeof(tcbErrorRetardoAct));
						memset(tcbErrorRetardoReset, 0, sizeof(tcbErrorRetardoReset));
						memset(tcbErrorNumResetAut, 0, sizeof(tcbErrorNumResetAut));
						memset(tcbErrorPila, 0, sizeof(tcbErrorPila));
						memset(description, 0, sizeof(description));
						memset(tcbErrorExtraInfo, 0, sizeof(tcbErrorExtraInfo));

						// 1.1. Name of the alarm (we finish if it doesn't exist!)

						if (xml_tmp->getParameter(FCFGERROR_AL_NAME_WSTRING, tcbErrorName) == FALSE)
						{
							ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: tag 'error' attribute '%s' not found in '%s'!\r\n"), FCFGERROR_AL_NAME_WSTRING, fileName));
							bResult = FALSE;
							break;
						}
						else
						{
							// Checking the 1st alarm: it has to be the 'SwError'!!!
							// Historically, 2nd will be 'Warning' and 3rd 'SysError' but these
							// have no special check... why ??? Also, 'SecLoopOff' is required
							// by FaultTable, and no check is done here. The reason to force
							// only 'SwError' check is that previous alarms (those occured
							// before having the FaultTable initialized) all are SwErrors, and
							// they have to be managed in the correct way.

							if (i == 0)
							{
								if (wcscmp(tcbErrorName, TEXT("SwError")) != 0)
								{
									ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: First alarm is not 'SwError' (this is mandatory) in '%s'!\r\n"), fileName));
									bResult = FALSE;
									break;
								}
							}
						}

						// 1.2. We read the alarm's information

						// <error descripcion=...> 
						if (xml_tmp->getParameter(FCFGERROR_AL_DESC_WSTRING, description) == FALSE)
						{
							ERRORMSG(ZONE_ERROR, (TEXT("[WARNING] erLeerXMLConfErrores: %s attribute '%s' not found in '%s'!\r\n"), FCFGERROR_AL_DESC_WSTRING, tcbErrorName));
						}

						// <disable_reporting>

						if (xml_tmp->getParameter(FCFGERROR_AL_DISABLE_REPORTING, tcbDisableReporting) == TRUE)
						{
							// If parameter was not found (previous call returns FALSE), the default
							// value is already set (that is, something like having read 'NO' from the
							// tag related to disabling error reporting	when the alarm occurs)

							// We check the parameter value (we're only interested in 'YES')...

							if (!wcscmp(L"YES", tcbDisableReporting))
							{
								DEBUGMSG(ZONE_WARNING, (TEXT("erLeerXMLConfErrores: alarm '%s' disables error reporting while active\r\n"), tcbErrorName));
								bDisablesReporting = TRUE;
							}
						}

						// <reaccion_error>

						if (xml_tmp->getParameter(FCFGERROR_AL_ERRORREAC_WSTRING, tcbErrorReaccion) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_ERRORREAC_WSTRING));
							wsprintf(tcbErrorReaccion, L"faultWARNING1");
						}

						// <estado_aero> We don't read this tag!

						// <nivel_reset>

						if (xml_tmp->getParameter(FCFGERROR_AL_RESETLEV_WSTRING, tcbErrorNivelReset) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_RESETLEV_WSTRING));
							wsprintf(tcbErrorNivelReset, L"faultALL");
						}

						// <retardo_activacion>

						if (xml_tmp->getParameter(FCFGERROR_AL_TRIGDELAY_WSTRING, tcbErrorRetardoAct) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_TRIGDELAY_WSTRING));
							wsprintf(tcbErrorRetardoAct, L"0");
						}

						// <retardo_reset>

						if (xml_tmp->getParameter(FCFGERROR_AL_RESETDELAY_WSTRING, tcbErrorRetardoReset) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_RESETDELAY_WSTRING));
							wsprintf(tcbErrorRetardoReset, L"0");
						}

						// <num_resetauto>

						if (xml_tmp->getParameter(FCFGERROR_AL_AUTORESETS_WSTRING, tcbErrorNumResetAut) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_AUTORESETS_WSTRING));
							wcscpy(tcbErrorNumResetAut, L"-1");
						}

						// <pila>

						if (xml_tmp->getParameter(FCFGERROR_AL_STACK_WSTRING, tcbErrorPila) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_STACK_WSTRING));
							wcscpy(tcbErrorPila, L"");
						}

						// <extra_info>

						if (xml_tmp->getParameter(FCFGERROR_AL_XTRAINFO_WSTRING, tcbErrorExtraInfo) == FALSE)
						{
							DEBUGMSG(0, (TEXT("erLeerXMLConfErrores: alarm '%s' in '%s' without '%s'. Using default value...\r\n"), tcbErrorName, fileName, FCFGERROR_AL_XTRAINFO_WSTRING));
							wcscpy(tcbErrorExtraInfo, L"");
						}
						
						// 1.3.- We add it to the FaultTable (if no error encountered)

						// Alarm registration (this is the only SW code that adds
						// alarms to the FaultTable!)

						tHandle = FaultTable->Add(tcbErrorName,
												 tcbErrorReaccion,
												 tcbErrorNivelReset,
												 tcbErrorRetardoAct,
												 tcbErrorRetardoReset,
												 tcbErrorNumResetAut,
												 tcbErrorPila,
												 description,
												 (tcbErrorExtraInfo[0] != 0) ? tcbErrorExtraInfo : NULL,
												 bDisablesReporting);

						if (tHandle <= UNDEFINED_HANDLE)
						{
							ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: FaultTable::Add('%s') fails!\r\n"), tcbErrorName));
							bResult = FALSE;
							break;
						}

						// 1.4.- Let's process next alarm

						i++;

					} // <- while ends!

					// Note: all alarm have been loaded from XML to FaultTable at this moment
					// (but maybe all errors mean 0 errors)

					if (i == 0) // <- no alarm loaded!
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: no alarm defined in '%s'!\r\n"), fileName));
						bResult = FALSE;
					}

					// We try to read/init some 'historical' alarm handles. These calls to FaultTable
					// and Error could be considered as 'ok FaultTable and Error, all errors have
					// been loaded. So, do whatever you need before starting running normally', je, je.

					if (bResult == TRUE)
					{
						bRet = Error.GetFaultHandles();
						if (bRet == FALSE)
						{
							ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfErrores: Error.GetFaultHandles() fails!\r\n")));
							bResult = FALSE;
						}
					}
				}
			}

			free(fileName);
		}

		// 2. Freeing resources

		delete(myNode);
		delete(currentNode);
		delete(xml_tmp);
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erLeerXMLConfErrores\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erLeerXMLConfErrores\r\n")));
	return bResult;
}

//  ==========================================================
//   
///
/// Funci�n encargada de leer los datos de configuraci�n del gestor 
///	de errores del fichero de configuraci�n del G. de Errores
/// 
/// \return BOOL
/// \retval TRUE si todo ha ido bien
/// \retval FALSE si algo ha fallado
///
static BOOL erLeerXMLConfGestorErrores()
{
	BOOL			bResult;
	NodeDetails		*myNode;
	NodeDetails		*currentNode;
	CXMLdriverApp	*xml_tmp;
	TCHAR			tcbMaxNumFich[NODE_VALUE_LENGTH];
	TCHAR			tcbMaxNumErrFich[NODE_VALUE_LENGTH];
	INT32			i;
	TCHAR			*tcCadena;
	ULONG			ulMaxNumFic;
	ULONG			ulMaxNumErrPorFic;
	TCHAR			*fileName = NULL;
	BOOL			bRet = FALSE;
	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);

	DEBUGMSG(ZONE_INIT, (_T("=> erLeerXMLConfGestorErrores\r\n")));

	try
	{
		bResult = TRUE;

		myNode = new NodeDetails();
		currentNode = new NodeDetails();
		xml_tmp = new CXMLdriverApp();

		ASSERT((myNode != NULL) && (currentNode != NULL) && (xml_tmp != NULL));

		ulMaxNumFic = 0UL;
		ulMaxNumErrPorFic = 0UL;
		i = 0;

		memset(tcbMaxNumFich, 0, sizeof(tcbMaxNumFich));
		memset(tcbMaxNumErrFich, 0, sizeof(tcbMaxNumErrFich));

		fileName = _wcsdup(F_FCFGERRO);
		if (fileName != NULL)
		{
			bRet = xml_tmp->loadXMLFile(fileName);
			if (bRet == TRUE)
			{
				bRet = xml_tmp->findNode(TEXT("config_gest_errores"), *myNode);
				if (bRet == TRUE)
				{
					bRet = xml_tmp->nextNode(*currentNode);
					if (bRet == TRUE)
					{
						bRet = xml_tmp->getParameter(TEXT("max_num_ficheros"), tcbMaxNumFich);
						if (bRet == FALSE)
						{
							//Warning: no se ha encontrado el nodo
							Error.Warning(warningMISSING_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						}
						bRet = xml_tmp->getParameter(TEXT("max_num_errores_ficheros"), tcbMaxNumErrFich);
						if (bRet == FALSE)
						{
							//Warning: no se ha encontrado el nodo
							Error.Warning(warningMISSING_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						}

						ulMaxNumFic = wcstoul(tcbMaxNumFich, &tcCadena, 10);
						if ((ulMaxNumFic == 0) || (ulMaxNumFic > DEFAULT_MAX_NUM_FIC))
						{
							ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorErrores: invalid node 'max_num_ficheros' value (read: %d, default will be set: %d)\r\n"), ulMaxNumFic, DEFAULT_MAX_NUM_FIC));
							Error.Warning(warningINCORRECT_VALUE_OF_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
							ulMaxNumFic = DEFAULT_MAX_NUM_FIC;
						}
						FaultTable->SetNumFilesControl(ulMaxNumFic);

						ulMaxNumErrPorFic = wcstoul(tcbMaxNumErrFich, &tcCadena, 10);
						if ((ulMaxNumErrPorFic == 0) || ((ulMaxNumErrPorFic > DEFAULT_MAX_NUM_ERR_FIC)))
						{
							ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorErrores: invalid node 'max_num_errores_ficheros' value (read: %d, default will be set: %d)\r\n"), ulMaxNumErrPorFic, DEFAULT_MAX_NUM_ERR_FIC));
							Error.Warning(warningINCORRECT_VALUE_OF_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
							ulMaxNumErrPorFic = DEFAULT_MAX_NUM_ERR_FIC;
						}
						FaultTable->SetFileSizeControl(ulMaxNumErrPorFic);
						bResult = TRUE;
					}
					else //no se ha accedido al nodo
					{
						//Esto no es un error grave porque se mantendr�n los valores por defecto de los par�metros de configuraci�n del G. Errores
						Error.Warning(warningMISSING_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorErrores: node 'config_gest_errores' not found in 'FCfgErro.xml' (default values will be used)\r\n")));
						bResult = TRUE;
					}
				}
				else //no se ha encontrado el nodo
				{
					//Esto no es un error grave porque se mantendr�n los valores por defecto de los par�metros de configuraci�n del G. Errores
					Error.Warning(warningMISSING_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
					ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorErrores: node 'config_gest_errores' not found in 'FCfgErro.xml' (default values will be used)\r\n")));
					bResult = TRUE;
				}
			}
			else
			{
				ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfGestorErrores: file 'FCfgErro.xml' access failed!\r\n")));
				Error.Set(errorIMPOSSIBLE_ACCESS_TO_ERROR_MNGR_CONFIG_XML);
				bResult = FALSE;
			}

			free(fileName);
			fileName = NULL;
		}
		else
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfGestorErrores: _wcsdup() fails!\r\n")));
			Error.Set(errorWCSDUP_FAILS_IT_MIGHT_NOT_HAVE_ENOUGHT_FREE_MEMORY);
			bResult = FALSE;
		}

		delete(myNode);
		myNode = NULL;
		delete(currentNode);
		currentNode = NULL;
		delete(xml_tmp);
		xml_tmp = NULL;
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erLeerXMLConfGestorErrores\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erLeerXMLConfGestorErrores\r\n")));
	return bResult;
}

//  ==========================================================
//   
///
/// Funci�n encargada de leer los datos de configuraci�n del gestor 
///	de eventos del fichero de configuraci�n del G. de Eventos
/// 
/// \return BOOL
/// \retval TRUE si todo ha ido bien
/// \retval FALSE si algo ha fallado
///
static BOOL erLeerXMLConfGestorEventos()
{
	BOOL			bResult;
	NodeDetails		*myNode = NULL;
	NodeDetails		*currentNode = NULL;
	CXMLdriverApp	*xml_tmp = NULL;
	TCHAR			tcbMaxNumFichEvent[NODE_VALUE_LENGTH];
	TCHAR			tcbMaxNumEventFich[NODE_VALUE_LENGTH];
	TCHAR			*tcCadena;
	ULONG			ulMaxNumFic;
	ULONG			ulMaxNumEventPorFic;
	TCHAR			*fileName = NULL;
	BOOL			bRet = FALSE;

	DEBUGMSG(ZONE_INIT, (_T("=> erLeerXMLConfGestorEventos\r\n")));

	try
	{
		bResult = TRUE;

		myNode = new NodeDetails();
		currentNode = new NodeDetails();
		xml_tmp = new CXMLdriverApp();

		ASSERT((myNode != NULL) && (currentNode != NULL) && (xml_tmp != NULL));

		ulMaxNumFic = 0UL;
		ulMaxNumEventPorFic = 0UL;

		memset(tcbMaxNumFichEvent, 0, sizeof(tcbMaxNumFichEvent));
		memset(tcbMaxNumEventFich, 0, sizeof(tcbMaxNumEventFich));

		fileName = _wcsdup(F_FCFGEVEN);
		if (fileName != NULL)
		{
			bRet = xml_tmp->loadXMLFile(fileName);
			if (bRet == TRUE)
			{
				bRet = xml_tmp->findNode(TEXT("config_gest_eventos"), *myNode);
				if (bRet == TRUE)
				{
					bRet = xml_tmp->nextNode(*currentNode);
					if (bRet == TRUE)
					{
						bRet = xml_tmp->getParameter(TEXT("max_num_ficheros"), tcbMaxNumFichEvent);
						if (bRet == FALSE)
						{
							//Warning: no se ha encontrado el nodo
							Error.Warning(warningMISSING_EVENT_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						}
						bRet = xml_tmp->getParameter(TEXT("max_num_event_fic"), tcbMaxNumEventFich);
						if (bRet == FALSE)
						{
							//Warning: no se ha encontrado el nodo
							Error.Warning(warningMISSING_EVENT_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						}

						ulMaxNumFic = wcstoul(tcbMaxNumFichEvent, &tcCadena, 10);
						if ((ulMaxNumFic == 0) || (ulMaxNumFic > DEFAULT_MAX_NUM_FIC_EVENT))
						{
							ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorEventos: invalid node 'max_num_ficheros' value (read: %d, default will be set: %d)\r\n"), ulMaxNumFic, DEFAULT_MAX_NUM_FIC_EVENT));
							Error.Warning(warningINCORRECT_VALUE_OF_EVENT_MANAGER_SIZE_CONTROL_CONFIG_DATA);
							ulMaxNumFic = DEFAULT_MAX_NUM_FIC_EVENT;
						}
						CLogger::GetLoggerReference()->SetNumFilesControl(ulMaxNumFic);

						ulMaxNumEventPorFic = wcstoul(tcbMaxNumEventFich, &tcCadena, 10);
						if ((ulMaxNumEventPorFic == 0) || (ulMaxNumEventPorFic > DEFAULT_MAX_NUM_EVENT_FIC))
						{
							ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorEventos: invalid node 'max_num_event_fic' value (read: %d, default will be set: %d)\r\n"), ulMaxNumEventPorFic, DEFAULT_MAX_NUM_EVENT_FIC));
							Error.Warning(warningINCORRECT_VALUE_OF_ERROR_MANAGER_SIZE_CONTROL_CONFIG_DATA);
							ulMaxNumEventPorFic = DEFAULT_MAX_NUM_EVENT_FIC;
						}
						CLogger::GetLoggerReference()->SetFileSizeControl(ulMaxNumEventPorFic);
						bResult = TRUE;
					}
					else //no se ha encontrado el nodo
					{
						//Esto no es un error grave porque se mantendr�n los valores por defecto de los par�metros de configuraci�n del G. Errores
						ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorEventos: node 'config_gest_eventos' not found in 'FCfgEven.xml' (default values will be used)\r\n")));
						Error.Warning(warningMISSING_EVENT_MANAGER_SIZE_CONTROL_CONFIG_DATA);
						bResult = TRUE;
					}
				}
				else
				{
					//Esto no es un error grave porque se mantendr�n los valores por defecto de los par�metros de configuraci�n del G. Errores
					ERRORMSG(ZONE_WARNING, (TEXT("erLeerXMLConfGestorEventos: node 'config_gest_eventos' not found in 'FCfgEven.xml' (default values will be used)\r\n")));
					Error.Warning(warningMISSING_EVENT_MANAGER_SIZE_CONTROL_CONFIG_DATA);
					bResult = TRUE;
				}
			}
			else
			{
				ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfGestorEventos: file 'FCfgEven.xml' access failed!\r\n")));
				Error.Set(errorIMPOSSIBLE_ACCESS_TO_EVENT_MNGR_CONFIG_XML);
				bResult = FALSE;
			}
			
			free(fileName);
			fileName = NULL;
		}
		else
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfGestorEventos: _wcsdup() fails!\r\n")));
			Error.Set(errorWCSDUP_FAILS_IT_MIGHT_NOT_HAVE_ENOUGHT_FREE_MEMORY);
			bResult = FALSE;
		}

		delete(myNode);
		myNode = NULL;
		delete(currentNode);
		currentNode = NULL;
		delete(xml_tmp);
		xml_tmp = NULL;
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erLeerXMLConfGestorEventos\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erLeerXMLConfGestorEventos\r\n")));
	return bResult;
}

//  ==========================================================
//   
///
/// Funci�n encargada de leer los datos de configuraci�n de
/// las variables de pila del fichero de configuraci�n del G. de Errores
/// 
/// \return BOOL
/// \retval TRUE si todo ha ido bien
/// \retval FALSE si algo ha fallado
///
static BOOL erLeerXMLConfPilaErrores()
{
	BOOL			bResult = TRUE;
	NodeDetails		*myNode;
	NodeDetails		*currentNode;
	CXMLdriverApp	*xml_tmp;
	INT32			i = 0;
	THandle			handle = UNDEFINED_HANDLE;
	TCHAR			tcbVarName[NODE_VALUE_LENGTH];
	TCHAR			*fileName = NULL;
	BOOL			bRet = FALSE;
	INT32			iRet = 0;
	TSymType		tSymType = symtypeNULL;

	DEBUGMSG(ZONE_INIT, (_T("=> erLeerXMLConfPilaErrores\r\n")));

	try
	{
		myNode = new NodeDetails();
		currentNode = new NodeDetails();
		xml_tmp = new CXMLdriverApp();

		ASSERT((myNode != NULL) && (currentNode != NULL) && (xml_tmp != NULL));

		fileName = _wcsdup(F_FCFGERRO);
		if (fileName != NULL)
		{
			bRet = xml_tmp->loadXMLFile(fileName);
			if (bRet == TRUE)
			{
				bRet = xml_tmp->findNode(TEXT("config_pila_errores/variable"), *myNode);
				if (bRet == TRUE)
				{
					i = 0;
					while ((xml_tmp->nextNode(*currentNode) == TRUE) && (i < NUMBER_OF_PILE_VARS))
					{
						bRet = xml_tmp->getParameter(TEXT("name"), tcbVarName);
						if (bRet == FALSE)
						{
							//no se ha encontrado el nombre de la variable
							Error.Set(errorMISSING_PILE_VARS_NAME_NODE);
							return FALSE;
						}
						else
						{
							//Vamos a�adiendo entradas a la estructura pilaVars
							handle = UNDEFINED_HANDLE;
							iRet = cDB->GetHandle(&handle, tcbVarName);
							if (iRet != 0)
							{
								//Se ha producido alg�n error de Sw
								ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: Error al obtener el handle de la variable de pila '%s' (1)\r\n"), tcbVarName));
								Error.Set(iRet);
								return FALSE;
							}

							if (handle <= UNDEFINED_HANDLE)
							{
								//Error al registrar una variable
								ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: Error al obtener el handle de la variable de pila '%s' (2)\r\n"), tcbVarName));
								Error.Set(errorDB_HANDLE_NOT_VALID);
								return FALSE;
							}
							else
							{
								iRet = cDB->GetSymType(handle, &tSymType);
								if (iRet != 0)
								{
									//Se ha producido alg�n error de Sw
									ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: Error al obtener el tipo de la variable de pila '%s'\r\n"), tcbVarName));
									Error.Set(iRet);
									return FALSE;
								}
								else
								{
									if ((tSymType > symtypeNULL) && (tSymType <= symtypeBOOLEAN))
									{
										pilaVars[i].cType = tSymType;
										pilaVars[i].handle = handle;
									}
									else
									{
										//Error al registrar una variable
										ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: El tipo de la variable de pila %s no es v�lido: %d\r\n"), tcbVarName, tSymType));
										Error.Set(errorVAR_TYPE_NOT_VALID);
										return FALSE;
									}
								}
							}
						}
						i++;
					}

					if (i == 0) //no se ha leido ninguna variable, pero existe la entrada en el XML
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: No se ha encontrado ninguna variable de pila definida en el fichero 'FCfgErro.xml'\r\n")));
						Error.Set(errorTHERE_IS_NO_PILE_VARS_DEFINED_IN_CONFIG_XML);
						bResult = FALSE;
					}
					else if (i == NUMBER_OF_PILE_VARS) //hay X o m�s variables
					{
						//Hay X o m�s entradas en el fichero de configuraci�n 
						//Esto no es un error porque s�lo se coger�n las X primeras
						DEBUGMSG(ZONE_WARNING, (TEXT("erLeerXMLConfPilaErrores: Hay %d (es el limite) o mas variables de pila definidas en el fichero 'FCfgErro.xml'. Solo estas se tendran en cuenta\r\n"), NUMBER_OF_PILE_VARS));
					}
					else
					{
						while (i < NUMBER_OF_PILE_VARS)
						{
							//Marcamos como inv�lidas las entradas restantes
							pilaVars[i].handle = UNDEFINED_HANDLE;
							i++;
						}
					}
				}
				else
				{
					// We allow not to define it! So...
					DEBUGMSG(ZONE_WARNING, (TEXT("erLeerXMLConfPilaErrores: There's no 'config_pila_errores' node (or has NULL contents) information inside file '%s'\r\n"), fileName));
				}
			}
			else
			{
				ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: Error al acceder al fichero 'FCfgErro.xml'. Puede que no exista.\r\n")));
				Error.Set(errorIMPOSSIBLE_ACCESS_TO_ERROR_CONFIG_XML);
				bResult = FALSE;
			}

			free(fileName);
			fileName = NULL;
		}
		else
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erLeerXMLConfPilaErrores: La funci�n _wcsdup() devuelve error. Puede que no haya memoria disponible!\r\n")));
			Error.Set(errorWCSDUP_FAILS_IT_MIGHT_NOT_HAVE_ENOUGHT_FREE_MEMORY);
			bResult = FALSE;
		}

		delete(myNode);
		myNode = NULL;
		delete(currentNode);
		currentNode = NULL;
		delete(xml_tmp);
		xml_tmp = NULL;
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erLeerXMLConfPilaErrores\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_INIT, (_T("<= erLeerXMLConfPilaErrores\r\n")));
	return bResult;
}

/*************************** Threads de los gestores ******************************/

//  ==========================================================
//   
///
/// Funci�n encargada de guardar los valores de las variables de pila
/// 
/// \param lpvParam No se usa
/// \return DWORD
/// \retval 0UL cuando finaliza correctamente
///
static DWORD erStoreErrorPileVars()
{
	INT32	i = 0;
	TCHAR	tcAux[MAX_PATH];
	THandle	handle = UNDEFINED_HANDLE;
	INT32	iRet = 0;
	INT32	iValue = 0;
	float	fValue = 0.0f;
	BOOL	bValue = FALSE;

	for (i = 0; i < NUM_VARS; i++)
	{
		memset(tcAux, 0, sizeof(tcAux));
		handle = pilaVars[i].handle;
		if (handle > UNDEFINED_HANDLE)
		{
			switch (pilaVars[i].cType)
			{
				case symtypeNULL:
					// We allow these entries! So...
					//ERRORMSG(ZONE_WARNING, (TEXT("erStoreErrorPileVars: se ha encontrado una variable de pila de tipo NULL\r\n")));
					break;
				case symtypeINT:
					iRet = cDB->GetAsInt(&iValue, handle);
					if (iRet == 0)
					{
						pilaVars[i].pila.PutInt(iValue);
					}
					else
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erStoreErrorPileVars: IOMonitor::GetAsString() fails.\r\n")));
						Error.Set(iRet);
					}
					break;
				case symtypeFLOAT:
					iRet = cDB->GetAsFloat(&fValue, handle);
					if (iRet == 0)
					{
						pilaVars[i].pila.PutFloat(fValue);
					}
					else
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erStoreErrorPileVars: IOMonitor::GetAsString() fails.\r\n")));
						Error.Set(iRet);
					}
					break;
				case symtypeBOOLEAN:
					iRet = cDB->GetAsBool(&bValue, handle);
					if (iRet == 0)
					{
						pilaVars[i].pila.PutBool(bValue);
					}
					else
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erStoreErrorPileVars: IOMonitor::GetAsString() fails.\r\n")));
						Error.Set(iRet);
					}
					break;
				default:
					ERRORMSG(ZONE_WARNING, (TEXT("erStoreErrorPileVars: se ha encontrado una variable de pila de desconocido\r\n")));
					// Error.Set here ???
					break;
			}
		}
	}

	return 0UL;
}

//  ==========================================================
///
/// Thread de la comunicaci�n con el Gestor de Comunicaciones
/// 
/// \param[in] lpvParam No se usa
///
/// \return	DWORD dwRet
/// \retval 0UL cuando finaliza correctamente
/// \retval !=0 cuando no finaliza correctamente
///
static DWORD WINAPI erComManCommunicationThread(LPVOID lpvParam)
{
	DWORD	dwRet = 0UL;
	BOOL	bRet = FALSE;
	DWORD	dwStatus = 0UL;

	DEBUGMSG(ZONE_INIT, (TEXT("=> erComManCommunicationThread\r\n")));

	// Inicializa las colas de mensajes para lectura y escritura
	bRet = erAbrirMsgQueuesComManCommunication();
	if (bRet == FALSE)
	{
		ERRORMSG(ZONE_WARNING, (L"erComManCommunicationThread: erAbrirMsgQueuesComManCommunication() fails!\r\n"));
		m_bErrorManagerFinishOrder = TRUE;
		dwRet = 1UL;
	}
	else
	{
		// Inicializamos las variables globales que vamos a necesitar
		bResetColectivo = FALSE;
		for (UINT32 uiAux=0; uiAux<uiFaultListRCSize; uiAux++)
		{
			thFaultListRC[uiAux] = UNDEFINED_HANDLE;
		}
		dwNumErroresRC = 0UL;

		// --
		// INFINITE LOOP starts here
		// --

		while (m_bErrorManagerFinishOrder == FALSE)
		{
			dwStatus = erComManCommunication();
			if (dwStatus != 0UL)
			{
				ERRORMSG(ZONE_ERROR, (_T("erComManCommunicationThread: erComManCommunication() fails (returns %d)!\r\n"), dwStatus));
				m_bErrorManagerFinishOrder = TRUE;
				dwRet = 2UL;
			}
		}
	}

	g_tb_bFinished = TRUE; // <- if we finish, the whole APP too!

	if (thFaultListRC != NULL)
	{
		delete thFaultListRC;
		thFaultListRC = NULL;
		uiFaultListRCSize = 0;
	}

	if (thCommFaultList != NULL)
	{
		delete thCommFaultList;
		thCommFaultList = NULL;
		uiCommFaultListSize = 0;
	}

	if (thErrFaultList != NULL)
	{
		delete thErrFaultList;
		thErrFaultList = NULL;
		uiErrFaultListSize = 0;
	}

	DEBUGMSG(ZONE_EXIT, (TEXT("<= erComManCommunicationThread\r\n")));
	return (dwRet);
}


//  ==========================================================
///
/// Thread que visualiza los errores del sistema a trav�s del display de 7 segmentos
/// 
/// \param[in] lpvParam No se usa
///
/// \return DWORD
/// \retval 0UL cuando finaliza correctamente
/// \retval !=0 cuando no finaliza correctamente
///
static DWORD WINAPI erDisplayErrorsThread(LPVOID lpvParam)
{
	DWORD			dwRet = 0UL;
	INT				iRet = 0;
	BOOL			bRet = FALSE;

	INT32			iNumErrActivos = 0;
	TCHAR			tcbErrorName[SYMLEN];
	char			cbCadena[MAX_PATH * 2];
	char			cbAux[MAX_PATH];
	INT32			iNumCaracteres = 0;
	THandle*		thList = NULL;
	UINT32			uiListSize = 0;
	INT32			iErrorNumber = 0;
	UINT32			uiErrMax = 0;

	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);

	DEBUGMSG(ZONE_INIT, (TEXT("=> erDisplayErrorsThread\r\n")));

	// Abrimos el driver de los displays de 7 segmentos

	hDrv = CreateFile(L"D7S1:", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == hDrv) 
	{
		ERRORMSG(ZONE_ERROR, (L"erDisplayErrorsThread: Failed to open D7SDriver. GetLastError() returns %d\r\n", GetLastError()));
		return(1UL);
	}

	// --
	// INFINITE LOOP starts here
	// --

	while (m_bErrorManagerFinishOrder == FALSE)
	{
#if ENABLE_TIMING_COUNT
		dwTicksBegin5 = GetTickCount();
#endif

		strcpy (cbCadena, ". . . . ");

		//Primero leemos los valores de los errores de los algoritmos
		uiErrMax = FaultTable->MaxSymbol();

		if (uiListSize < uiErrMax)
		{
			if (thList != NULL)
			{
				free(thList);
				thList = NULL;
				uiListSize = 0;
			}

			thList = (THandle*)malloc(sizeof(THandle) * uiErrMax);
			uiListSize = uiErrMax;
		}

		for (UINT32 uiIdx=0; uiIdx<uiListSize; uiIdx++)
		{
			thList[uiIdx] = UNDEFINED_HANDLE;
		}

		iNumErrActivos = FaultTable->GetSortedActiveFaults(thList, uiErrMax);
		if (iNumErrActivos > 0)
		{
			strcpy (cbCadena, "ea_");

			iErrorNumber = 0;
			if (thList[iErrorNumber] >= 0)
			{
				while ((iErrorNumber < iNumErrActivos) && (thList[iErrorNumber] >= 0))
				{
					bRet = FaultTable->Name(thList[iErrorNumber], tcbErrorName);
					if ((bRet == TRUE) && (wcscmp(tcbErrorName, TEXT("")) != 0))
					{
						iNumCaracteres = wcstombs(cbAux, tcbErrorName, sizeof(cbAux)/sizeof(char));
					}
					else
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erDisplayErrorsThread: FaultTable::Name(%d) fails!\r\n"), thList[iErrorNumber]));
						Error.Set(errorFAULTTABLE_OBTAIN_ERROR_NAME_FUNCTION_FAILS);
					}

					if (iNumCaracteres > 0)
					{
						strcat (cbCadena, cbAux);
						strcat (cbCadena, "_");
					}

					if (iErrorNumber == CODE_SWERR)
					{
						//Sacamos los c�digos de los errores de SW que se han producido
						Error.strGetSwErrorCode(cbAux);
						iRet = strcmp(cbAux, "");
						if (iRet != 0)
						{
							strcat (cbCadena, cbAux);
						}
					}
					else
					{
						if (iErrorNumber == CODE_WARN)
						{
							//Sacamos los c�digos de los errores de SW que se han producido
							Error.strGetWarningCode(cbAux);
							iRet = strcmp(cbAux, "");
							if (iRet != 0)
							{
								strcat (cbCadena, cbAux);
							}
						}
						else
						{
							if (iErrorNumber == CODE_SYSERR)
							{
								//Sacamos los c�digos de los errores de SW que se han producido
								Error.strGetSysErrCode(cbAux);
								iRet = strcmp(cbAux, "");
								if (iRet != 0)
								{
									strcat (cbCadena, cbAux);
								}
							}
						}
					}

					iErrorNumber++;
				}
			}
			else
			{
				if (uiListSize < 1)
				{
					if (thList != NULL)
					{
						free(thList);
						thList = NULL;
						uiListSize = 0;
					}

					thList = (THandle*)malloc(sizeof(THandle));
					uiListSize = 1;
				}

				thList[0] = UNDEFINED_HANDLE;

				//Puede que se haya producido un error antes de que se pusiese en 
				//marcha el Gestor de Ficheros de Error. Si es ese el caso, 
				//GetSortedActiveFaults() nos dir� que hay un error activo y en
				//thList[0] tendremos el c�digo del error de Sw que se ha producido,
				//de forma que se podr� sacar por el display de 7 segmentos

				iNumErrActivos = FaultTable->GetSortedActiveFaults(thList, 1);
				if (iNumErrActivos == 1)
				{
					if (thList[0] < (-1))
					{
						strcpy (cbCadena, "ea_SwError_");
						strcat (cbCadena, "-");
						iErrorNumber = thList[0] + 1;
						iErrorNumber = -(iErrorNumber); //los c�digos de error son negativos
						iRet = iErrorNumber / 100;
						sprintf (cbAux, "%d_", iRet);
						strcat (cbCadena, cbAux);
						iErrorNumber = iErrorNumber % 100;
						iRet = iErrorNumber / 10;
						sprintf (cbAux, "%d_", iRet);
						strcat (cbCadena, cbAux);
						iRet = iErrorNumber % 10;
						sprintf (cbAux, "%d_", iRet);
						strcat (cbCadena, cbAux);
					}
					else
					{
						ERRORMSG(ZONE_ERROR, (TEXT("erDisplayErrorsThread: Se ha producido un SwError, pero el valor de thList[0] es: %d"), thList[0]));
						Error.Set(errorSW_ERROR_CODE_NOT_VALID);
					}
				}
			}
		}
		else // iNumErrActivos is 0
		{
			if (uiListSize < 1)
			{
				if (thList != NULL)
				{
					free(thList);
					thList = NULL;
					uiListSize = 0;
				}

				thList = (THandle*)malloc(sizeof(THandle));
				uiListSize = 1;
			}

			thList[0] = UNDEFINED_HANDLE;

			//Puede que se haya producido un error antes de que se registrase
			//el SwError. Si es ese el caso, GetSortedActiveFaults() nos dir�
			//que hay un error activo y en thList[0] tendremos el c�digo del
			//error de Sw que se ha producido, de forma que se podr� sacar
			//por el display de 7 segmentos

			iNumErrActivos = FaultTable->GetSortedActiveFaults(thList, 1);
			if (iNumErrActivos == 1)
			{
				if (thList[0] < (-1))
				{
					strcpy (cbCadena, "ea_SwError_");
					strcat (cbCadena, "-");
					iErrorNumber = thList[0] + 1;
					iErrorNumber = -(iErrorNumber); //los c�digos de error son negativos
					iRet = iErrorNumber / 100;
					sprintf (cbAux, "%d_", iRet);
					strcat (cbCadena, cbAux);
					iErrorNumber = iErrorNumber % 100;
					iRet = iErrorNumber / 10;
					sprintf (cbAux, "%d_", iRet);
					strcat (cbCadena, cbAux);
					iRet = iErrorNumber % 10;
					sprintf (cbAux, "%d_", iRet);
					strcat (cbCadena, cbAux);
				}
				else
				{
					ERRORMSG(ZONE_ERROR, (TEXT("erDisplayErrorsThread: Se ha producido un SwError, pero el valor de thList[0] es: %d"), thList[0]));
					Error.Set(errorSW_ERROR_CODE_NOT_VALID);
					dwRet = 2UL;
					break;
				}
			}
		}

		//Va ha enviar la cadena cada TIME_FOR_DISPLAY_ERROR_THREAD ms
		if (erVisualizarErroresDisplay(cbCadena) != 0)
		{
			Error.Warning(warningERROR_DISPLAYING_ERROR_CODES);
		}

#if ENABLE_TIMING_COUNT
		dwDiff5 = GetTickCount() - dwTicksBegin5;
		dwTicksBegin5 = 0UL;

		if (dwDiff5 > dwMax5) dwMax5 = dwDiff5;
		if (dwDiff5 < dwMin5) dwMin5 = dwDiff5;

		dwHits5[MIN(dwDiff5, dim(dwHits5) - 1)]++;
#endif

		// This thread is not managed with events! But, it's OK!
		Sleep(TIME_FOR_DISPLAY_ERROR_THREAD);
	}

	// Se supone que no se va a llegar aqui nunca (la funci�n de sacar errores
	// por los displays debe mantenerse activa siempre)

	bRet = CloseHandle(hDrv);
	if (bRet == FALSE)
	{
		ERRORMSG(ZONE_ERROR, (L"erDisplayErrorsThread: CloseHandle failed. GetLastError() returns %d\r\n", GetLastError()));
		dwRet = 3UL;
	}

	if (thList != NULL)
	{
		free(thList);
		thList = NULL;
		uiListSize = 0;
	}

	DEBUGMSG(ZONE_EXIT, (TEXT("<= erDisplayErrorsThread\r\n")));
	return(dwRet);
}

//  ==========================================================
//   
///
/// Funci�n encargada de crear o abrir las colas de mensajes de 
/// comunicaci�n con el Gestor de Comunicaciones
/// 
/// \return BOOL
/// \retval TRUE si todo ha ido bien
/// \retval FALSE si algo ha fallado
///
static BOOL erAbrirMsgQueuesComManCommunication()
{
	BOOL bRet = TRUE;

	try
	{
		if (mqSetupMsgQueues(MQ_ComMan_ErrMan, &hMQReadFromComM,
							 MQ_ErrMan_ComMan, &hMQWriteToComM,
							 MAX(sizeof(RESPUESTA_DN), sizeof(CONSULTA_ERRMAN)),
							 L"erAbrirMsgQueuesComManCommunication",
							 TRUE) != 0)
		{
			bRet = FALSE;
		}
	}
	catch(...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erAbrirMsgQueuesComManCommunication\r\n"));
		throw;
	}

	return bRet;
}

//  ==========================================================
///
/// Funci�n encargada de leer el MessageQueue enganchado al G. de Comms
/// para ver si hay alguna petici�n desde alg�n proceso externo (HMI, SCADA, etc.)
/// (infinite loop, not event managed!). Por eso las variables 'pesadas' son
/// est�ticas (evita creaci�n/eliminaci�n cada vez que la funci�n se ejecuta).
/// 
/// \return DWORD
/// \retval 0UL si todo va bien
/// \retval !=0 si algo falla
///
static DWORD erComManCommunication()
{
	DWORD			dwRet = 0UL;
	INT32			iRet = 0;
	BOOL			bRet = FALSE;
	
	DWORD			dwBytesRead = 0UL;
	DWORD			dwFlags = 0UL;
	MSGQUEUEINFO	stErrMsgQueueInfo = {0};
	DWORD			dwCurrentMessages = 0UL;

	SYSTEMTIME		systemTime = {0};
	USHORT			usNumErr = (USHORT)0;
	UINT16			NumErrAct = (UINT16)0;

	THandle			tHandle = UNDEFINED_HANDLE;
	TCHAR			*tcEndPtr = NULL;
	BOOL			bErrorRst = FALSE;
	UINT32			uiNumErrRegistrados = 0;
	INT32			i = 0;
	INT32			j = 0;
	INT32			ParList[1];
	BOOL			bImprovedClient = FALSE; // <-  used inside LEER_ERR_ACT

	static CONSULTA_ERRMAN	consErrMan = {0};
	static RESPUESTA_DN		respuesta = {0};
	static TCHAR			tcAux[MAX_PATH];
	static BOOL				bMessageShowed = FALSE;

	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);

	DEBUGMSG(ZONE_RUN, (TEXT("=> erComManCommunication\r\n")));

	try
	{
		// ---
		// 1. Checks the "reset colectivo"
		// ---

		if (bResetColectivo == TRUE)
		{
			if (iNumCicles == THREAD_IDLE_NUM_CYCLES) // <- esperamos THREAD_IDLE_NUM_CYCLES ciclos a continuar con el siguiente error
			{
#if ENABLE_TIMING_COUNT
				dwTicksBegin = GetTickCount();
#endif

				// Vamos a ir reseteando los errores restantes uno por uno (uno en cada llamada)

				UINT32 uiIdx = dwNumErrReseteadosRC;

				if ((thFaultListRC[uiIdx] != UNDEFINED_HANDLE) && (dwNumErroresRC > 0))
				{
					bRet = FaultTable->Active(thFaultListRC[uiIdx]);
					if (bRet == TRUE)
					{
						if (bResetColectivoHMI == TRUE)
						{
							bRet = FaultTable->Reset(thFaultListRC[uiIdx], faultPASSW_ADMINISTRADOR, faultLOCAL);
							if (bRet == FALSE)
							{
								DEBUGMSG(ZONE_WARNING, (TEXT("erComManCommunication: No se ha podido reponer el error %lu\r\n"), thFaultListRC[uiIdx]));
							}
						}
					}
					
					uiIdx++;
					dwNumErroresRC--;
					dwNumErrReseteadosRC++;
				}
				
				if ((dwNumErroresRC == 0UL) || (thFaultListRC[uiIdx] == UNDEFINED_HANDLE))
				{
					// Ya hemos repuesto todos los errores
					for (UINT32 uiAux=0; uiAux<uiFaultListRCSize; uiAux++)
					{
						thFaultListRC[uiAux] = UNDEFINED_HANDLE;
					}

					dwNumErroresRC = 0UL;
					bResetColectivo = FALSE;
					bResetColectivoHMI = FALSE;
					dwNumErrReseteadosRC = 0UL;
				}

				iNumCicles = 0;

#if ENABLE_TIMING_COUNT
				dwDiff = GetTickCount() - dwTicksBegin;
				dwTicksBegin = 0UL;

				if (dwDiff > dwMax) dwMax = dwDiff;
				if (dwDiff < dwMin)	dwMin = dwDiff;

				dwHits[MIN(dwDiff, dim(dwHits) - 1)]++;
#endif
			}
			else
			{
				iNumCicles++;
			}
		}
		else
		{
			// Ya hemos repuesto todos los errores
			for (UINT32 uiAux=0; uiAux<uiFaultListRCSize; uiAux++)
			{
				thFaultListRC[uiAux] = UNDEFINED_HANDLE;
			}

			dwNumErroresRC = 0UL;
			bResetColectivo = FALSE;
			bResetColectivoHMI = FALSE;
			dwNumErrReseteadosRC = 0UL;
		}

		// ---
		// 2. Waits the message queue signal (maximum waiting time is 3 seconds)
		// ---

		// No se bloquea: si no hay nada, sale en un cierto tiempo (para poder
		// hacer, por ejemplo, la continuaci�n de un 'reset colectivo')

		dwRet = WaitForSingleObject(hMQReadFromComM, COMMANAG_LISTEN_TIMEOUT);
		if (dwRet != (DWORD)WAIT_OBJECT_0)
		{
			if (dwRet == (DWORD)WAIT_TIMEOUT)
			{
				return(0UL);
			}
			else 
			{
				if (dwRet == (DWORD)WAIT_FAILED)
				{
					ERRORMSG(ZONE_WARNING, (_T("erComManCommunication: WaitForSingleObject() fails. GetLastError %u\r\n"), GetLastError()));
				}
				else
				{
					ERRORMSG(ZONE_WARNING, (_T("erComManCommunication: WaitForSingleObject() exit for unknown reason %d\r\n"), dwRet));
				}

				return(1UL);
			}
		}

		// We were waiting the event, and, meanwhile, the application could have finished.
		// Let's check...

		if (m_bErrorManagerFinishOrder == TRUE)
		{
			// We finish too!
			return(0UL);
		}

		// ---
		// 3. Process messages from the MQ
		// ---

		DEBUGMSG(ZONE_RUN, (TEXT("erComManCommunication: Ha pasado algo en el MQ. Leyendo.... \r\n")));

		// Test if message queue has something
		if (hMQReadFromComM == INVALID_HANDLE_VALUE)
		{
			ERRORMSG(ZONE_ERROR, (TEXT("erComManCommunication: hMQReadFromComM is INVALID_HANDLE_VALUE\r\n")));
			return(2UL);
		}
		else
		{
			memset((void *)&stErrMsgQueueInfo, 0, sizeof(stErrMsgQueueInfo));
			stErrMsgQueueInfo.dwSize = sizeof(stErrMsgQueueInfo);

			bRet = GetMsgQueueInfo(hMQReadFromComM, &stErrMsgQueueInfo);
			if (bRet == FALSE) 
			{
				ERRORMSG(ZONE_ERROR, (TEXT("erComManCommunication: Testing empty state: ERROR: %d. (6 = INVALID_HANDLE)\r\n"), GetLastError()));
				return(3UL);
			}

#if DO_MESSAGE_QUEUE_VERIFICATION
			static BOOL bVerificationDone = FALSE;
			if (bVerificationDone == FALSE)
			{
				mqCheckReadersWritters(hMQReadFromComM, L"erComManCommunication", MQ_ComMan_ErrMan);
				mqCheckReadersWritters(hMQWriteToComM, L"erComManCommunication", MQ_ErrMan_ComMan);
				bVerificationDone = TRUE;
			}
#endif

#if ENABLE_TIMING_COUNT
			dwTicksBegin = GetTickCount();
#endif

			dwCurrentMessages = stErrMsgQueueInfo.dwCurrentMessages;
			if (dwCurrentMessages == 0UL)
			{
				DEBUGMSG(ZONE_WARNING, (TEXT("erComManCommunication: msg queue signaled, but empty!\r\n"))); 
				ResetEvent (hMQReadFromComM);
			}
			else
			{
				if ((dwCurrentMessages > LAUNCH_WARNING_MQ_MESSAGES) && (bMessageShowed == FALSE))
				{
					DEBUGMSG(ZONE_WARNING, (TEXT("erComManCommunication: MQ 'CommM_ErrM' has too much messages (%lu)!\r\n"), dwCurrentMessages));
					bMessageShowed = TRUE;
				}

				if (dwCurrentMessages == 0)
				{
					bMessageShowed = FALSE;
				}

				UINT8 uiMessagesToManage = (UINT8)MIN(dwCurrentMessages, LAUNCH_WARNING_MQ_MESSAGES);

				while (uiMessagesToManage > 0)
				{
					uiMessagesToManage--;

					memset(&consErrMan, 0, sizeof(consErrMan));
					memset(&respuesta, 0, sizeof(respuesta));
					memset(tcAux, 0, sizeof(tcAux));

					// Now read the contents of the message queue
					bRet = ReadMsgQueue(hMQReadFromComM, &consErrMan, (DWORD)sizeof(consErrMan), &dwBytesRead, 0, &dwFlags);
					if (bRet == TRUE)
					{											
						// Preparamos la fuente y el comando contestado para la respuesta
						respuesta.bySource = ERR_MAN;
						
						respuesta.iCmdoConstdo = consErrMan.cons.iComando;

						//Aqui se deberia hacer una consulta al monitor de la BD para leer o escribir los datos recibidos
						//Y desp�es escribir en el message queue el resultado de la consulta
						//memset(respuesta.tcbString, 0, sizeof(respuesta.tcbString));
						DEBUGMSG(ZONE_RUN, (TEXT("erComManCommunication: recibido comando %d"), consErrMan.cons.iComando));

						// ---
						// Message from HMI
						// ---

						if (consErrMan.bySource == HMI)
						{
							respuesta.byDestination = HMI;

							switch (consErrMan.cons.iComando)
							{
								case RESET_ERROR_INDIV_INSTAL:
								case RESET_ERROR_INDIV_ADMIN:

									// There's no difference between them. Note that this could be
									// dangerous for InstalaT used inside HMI, because all errors could
									// be reset... (but HMI is going to die!)

									tHandle = (THandle)wcstol(consErrMan.cons.tcbIdError, &tcEndPtr, 10);
									if ((tHandle > UNDEFINED_HANDLE) && ((UINT32)tHandle < FaultTable->MaxSymbol()))
									{
										bRet = FaultTable->Active(tHandle);
										if (bRet == TRUE)
										{
											if (m_ptrUsrCommand != NULL)
											{
												// We just allow 'faultALL' errors for INSTAL requests (from HMI)
												// But resets requests from WebHMI are all ADMIN! So, from WebHMI
												// all errors can be reseted!

												iRet = m_ptrUsrCommand->Invoke(CMD_START,
																			   ParList,
																			   0,
																			   faultLOCAL,
																			   tHandle);
												if (iRet != 0)
												{
													swprintf(respuesta.tcbString, TEXT("ER-ER: no se ha podido reponer el error %d"), tHandle);
												}
												else
												{
													wcscpy(respuesta.tcbString, TEXT("ER-OK."));
												}
											}
											else
											{
												swprintf(respuesta.tcbString, TEXT("ER-ER: no hay gesti�n activa para este comando"));
											}
										}
										else
										{
											swprintf(respuesta.tcbString, TEXT("ER-ER: el error %d no est� activo."), tHandle);
										}
									}
									else
									{
										swprintf(respuesta.tcbString, TEXT("ER-ER: el valor %s no es un identificador de error v�lido"), consErrMan.cons.tcbIdError);
									}

									respuesta.bEnd = TRUE;
									break;

								case RESET_ERROR_COLECT:

									if (bResetColectivo == FALSE)
									{
										usNumErr = 0;
										uiNumErrRegistrados = FaultTable->MaxSymbol();
										
										if (uiCommFaultListSize < uiNumErrRegistrados)
										{
											if (thCommFaultList != NULL)
											{
												delete thCommFaultList;
												thCommFaultList = NULL;
												uiCommFaultListSize = 0;
											}

											thCommFaultList = (THandle*)malloc(sizeof(THandle) * uiNumErrRegistrados);
											uiCommFaultListSize = uiNumErrRegistrados;
										}

										for (UINT32 uiAux=0; uiAux<uiCommFaultListSize; uiAux++)
										{
											thCommFaultList[uiAux] = UNDEFINED_HANDLE;
										}

										usNumErr = FaultTable->GetSortedActiveFaults(thCommFaultList, uiNumErrRegistrados);
										bErrorRst = FALSE;

										if (usNumErr > 0)
										{
											UINT32 uiIdx = 0;

											if (usNumErr > MAX_NUMBER_OF_RESETS_FIRST_TIME)
											{
												// Hay m�s de MAX_NUMBER_OF_RESETS_FIRST_TIME errores activos, vamos a hacer el reseteo poco a poco
												dwNumErroresRC = (DWORD)(usNumErr - MAX_NUMBER_OF_RESETS_FIRST_TIME);
												usNumErr = MAX_NUMBER_OF_RESETS_FIRST_TIME;
											}
											else
											{
												// Hay menos de dos errores activoshacemos el reseteo ahora
												dwNumErroresRC = 0UL;
											}

											while ((thCommFaultList[uiIdx] != UNDEFINED_HANDLE) && (uiIdx < usNumErr))
											{
												// No necesitamos mirar si ese error esta activo, porque ya lo sabemos
												bRet = FaultTable->Reset(thCommFaultList[uiIdx], faultPASSW_ADMINISTRADOR, faultLOCAL);
												if (bRet == FALSE)
												{
													if (bErrorRst == TRUE)
													{
														// concatenamos el id del error al mensaje
														memset(tcAux, 0, sizeof(tcAux));
														swprintf(tcAux, TEXT(", %d"), thCommFaultList[uiIdx]);
														wcscat(respuesta.tcbString, tcAux);
													}
													else
													{
														memset(tcAux, 0, sizeof(tcAux));
														swprintf(tcAux, TEXT("ER-ER: no se ha podido reponer el(los) error(es): %d"), thCommFaultList[uiIdx]);
														wcscpy(respuesta.tcbString, tcAux);
													}

													// Si uno o m�s errores no pueden ser repuestos, se pone bErrorRst = TRUE
													bErrorRst = TRUE;
												}
												
												uiIdx++;
											}

											if (dwNumErroresRC > 0)
											{
												uiIdx = 0;

												if (uiFaultListRCSize < dwNumErroresRC)
												{
													if (thFaultListRC != NULL)
													{
														delete thFaultListRC;
														thFaultListRC = NULL;
														uiFaultListRCSize = 0;
													}

													thFaultListRC = (THandle*)malloc(sizeof(THandle) * dwNumErroresRC);
													uiFaultListRCSize = dwNumErroresRC;
												}

												for (UINT32 uiAux=0; uiAux<uiFaultListRCSize; uiAux++)
												{
													thFaultListRC[uiAux] = UNDEFINED_HANDLE;
												}

												while ((thCommFaultList[uiIdx + MAX_NUMBER_OF_RESETS_FIRST_TIME] != UNDEFINED_HANDLE) && (uiIdx < dwNumErroresRC))
												{
													thFaultListRC[uiIdx] = thCommFaultList[uiIdx + MAX_NUMBER_OF_RESETS_FIRST_TIME];
													uiIdx++;
												}

												bResetColectivo = TRUE;
												bResetColectivoHMI = TRUE;
												dwNumErrReseteadosRC = 0;
											}
											else
											{
												bResetColectivo = FALSE;
											}

											if (bErrorRst == FALSE)
											{
												wcscpy(respuesta.tcbString, TEXT("ER-OK."));
											}
										}
										else
										{
											wcscpy(respuesta.tcbString, TEXT("ER-ER: no hay errores activos"));
										}
									}
									else
									{
										wcscpy(respuesta.tcbString, TEXT("ER-ER: se est� llevando a cabo un reset colectivo"));
									}

									respuesta.bEnd = TRUE;
									break;

								case LEER_ERR_ACT:

									if ((iRet = erProcessRequest(&consErrMan, &respuesta)) != 0)
									{
										ERRORMSG(ZONE_WARNING, (L"erComManCommunication: erProcessRequest() fails (returns %d)!\r\n"));
									}
									break;

								default:
									ERRORMSG(ZONE_WARNING, (_T("erComManCommunication: Unknown command from HMI %d\r\n"), consErrMan.cons.iComando));
									Error.Warning(warningUNKNOWN_COMMAND_FROM_HMI);
									wcscpy(respuesta.tcbString, UNKNOWN_CMD);
									respuesta.bEnd = TRUE;
									break;
							}
						}

						// ---
						// Message from OPC
						// ---

						else if (consErrMan.bySource == OPC)
						{
							respuesta.byDestination = OPC;

							ERRORMSG(ZONE_WARNING, (_T("erComManCommunication: Unknown command from OPC %d\r\n"), consErrMan.cons.iComando));
							Error.Warning(warningUNKNOWN_COMMAND_FROM_OPC);
							wcscpy(respuesta.tcbString, UNKNOWN_CMD);
							respuesta.bEnd = TRUE;
						}

						// ---
						// Message from UNKNOWN
						// ---

						else
						{
							respuesta.bySource = UNKNOWN_ORIGIN;
							respuesta.byDestination = UNKNOWN_ORIGIN;
							ERRORMSG(ZONE_WARNING, (_T("erComManCommunication: command %d received from unknown source: %d\r\n"), consErrMan.cons.iComando, consErrMan.bySource));
							wcscpy(respuesta.tcbString, TEXT("Emisor desconocido\r\n"));
							respuesta.bEnd = TRUE;
						}

						// Writting the answer

						bRet = WriteMsgQueue(hMQWriteToComM, &respuesta, (DWORD)sizeof(respuesta), 0, (DWORD)NULL);
						if (bRet == FALSE) 
						{
							mqCheckWriteError(GetLastError(), L"erComManCommunication", NULL);
							dwRet = 4UL;
							break;
						}
					}
					else 
					{
						mqCheckReadError(GetLastError(), L"erComManCommunication");
						dwRet = 5UL;
						break;
					}

				} // <- while ends
			}

#if ENABLE_TIMING_COUNT
			dwDiff = GetTickCount() - dwTicksBegin;
			dwTicksBegin = 0UL;

			if (dwDiff > dwMax) dwMax = dwDiff;
			if (dwDiff < dwMin)	dwMin = dwDiff;

			dwHits[MIN(dwDiff, dim(dwHits) - 1)]++;
#endif
		}
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erComManCommunication\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_RUN, (TEXT("<= erComManCommunication\r\n")));
	return dwRet;
}

//  ==========================================================
//   
///
/// Funci�n que visualiza la cadena que se le pasa como par�metro
/// a trav�s del display de 7 segmentos
/// 
/// \param[in] cbCadenaError Cadena de caracteres a visualizar
/// \return INT32
/// \retval 0 cuando finaliza correctamente
///
static INT32 erVisualizarErroresDisplay(const CHAR *cbCadenaError)
{
	DWORD	dwWritten = (DWORD)0;
	BOOL	bReturnCode = FALSE;
	BOOL	bRet = FALSE;

	if (cbCadenaError == NULL)
	{
		ERRORMSG(ZONE_WARNING, (L"erVisualizarErroresDisplay: Incorrect parameter!\r\n"));
		return(-1);
	}

	// Saca una cadena por el display
	// Primero con el WriteFile
	bReturnCode = WriteFile(hDrv, (LPVOID)cbCadenaError, (DWORD)strlen(cbCadenaError), &dwWritten, NULL);
	if (bReturnCode != TRUE)
	{
		ERRORMSG(ZONE_ERROR,(L"erVisualizarErroresDisplay: WriteFile failed. GetLastError() returns %d\r\n", GetLastError()));
		Error.SetSysErr(syserrorD7SDRIVER_WRITEFILE_FAILS);
		bRet = CloseHandle(hDrv);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erVisualizarErroresDisplay: CloseHandle failed. GetLastError() returns %d\r\n", GetLastError()));
			Error.SetSysErr(syserrorD7SDRIVER_CLOSEHANDLE_FAILS);
		}

		return (-2);
	}

	if (dwWritten != (DWORD)strlen(cbCadenaError))
	{
		ERRORMSG(ZONE_ERROR, (L"erVisualizarErroresDisplay: WriteFile failed: no. of bytes written (%x) != no. of bytes to write (%x)\r\n", dwWritten, (DWORD)strlen(cbCadenaError)));
		Error.SetSysErr(syserrorD7SDRIVER_WRITEFILE_FAILS);
		bRet = CloseHandle(hDrv);
		if (bRet == FALSE)
		{
			ERRORMSG(ZONE_ERROR, (L"erVisualizarErroresDisplay: CloseHandle failed. GetLastError() returns %d\r\n", GetLastError()));
			Error.SetSysErr(syserrorD7SDRIVER_CLOSEHANDLE_FAILS);
		}

		return (-3);
	}

	return (0);
}

//  ==========================================================
//   
///
/// Checks if the Control has launched the "ChkControlEnd" event.
/// This event signals the OPC (and others) to start, which will send requests
/// to DB or ErrorManager.
/// Substitutes the old use of global 'bInitializationFinished' variable.
/// 
///	\return BOOL
/// \retval TRUE if event "ChkControlEnd" has been signaled
/// \retval FALSE if it's still not signaled
///
static BOOL erHasControlEnded()
{
	static HANDLE	hErrMChkControlEnd = NULL;
	static BOOL		bErrMChkControlEnd = FALSE;

	try
	{
		if (bErrMChkControlEnd == FALSE)
		{
			// Check the signal that CTRL sends to OPC to start.
			// Once sent, we also have to answer, correctly, the
			// OPC incoming requests! If we don't answer, it has no
			// sense to send the signal!

			// 1.- Creates the event

			if (hErrMChkControlEnd == NULL)
			{
				hErrMChkControlEnd = CreateEvent(NULL, TRUE, FALSE, TEXT("ChkControlEnd"));
				if (hErrMChkControlEnd == NULL) 
				{
					ERRORMSG(ZONE_ERROR, (TEXT("erHasControlEnded: CreateEvent(hChkControlEnd) fails!\r\n")));
					throw 1UL;
				}												
			}

			// 2.- Asks for it

			// Note: for backward compatibility with "no signaled AlgManager"
			// we will also compare with the 'bInitializationFinished' global
			// variable value.

			if ((WaitForSingleObject(hErrMChkControlEnd, 0) == (DWORD)WAIT_OBJECT_0) )
			{
				DEBUGMSG(ZONE_WARNING, (TEXT("erHasControlEnded: first request received. All allowed...\r\n")));
				bErrMChkControlEnd = TRUE;
			}
		}
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erHasControlEnded\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_EXIT, (_T("<= erHasControlEnded\r\n")));
	return(bErrMChkControlEnd);
}

//  ==========================================================
//   
/// See ErrManag.h for more information
///
/// Notes: by now, it only implements 'HMI->ERR_MAN::LEER_ERR_ACT'
/// in a non thread-safe code! Future will be better...
///
static INT32 erProcessRequest(const CONSULTA_ERRMAN* request, RESPUESTA_DN* response)
{
	INT32 iFRet = -1;
	CFaultTable* FaultTable = CFaultTable::getFaultTable();
	ASSERT (FaultTable);


	DEBUGMSG(ZONE_RUN, (L"=> erProcessRequest\r\n"));

	try
	{
		// 0. Parameters check
		//
		if ((request == NULL) || (response == NULL))
		{
			ERRORMSG(ZONE_ERROR, (L"erProcessRequest: wrong parameters! Nothing done\r\n"));
		}
		else
		{
			// 1. We prepare some fields of the answer right here

			memset((void *)response, 0, sizeof(RESPUESTA_DN));
			response->byDestination = request->bySource;
			response->bySource = ERR_MAN;
			response->iCmdoConstdo = request->cons.iComando;

			// 2. Detecting the originator of the request
			//
			switch (request->bySource)
			{
				// 2.1. ---
			case HMI:
				// ---

				switch (request->cons.iComando)
				{
					// 2.1.1. ---
				case LEER_ERR_ACT:
					// ---

					{
						// Special variable declaration for this case
						// This is not thread-safe!!! TO_DO: Review it if we want to
						// run it in 'direct call' mode (it's safe if only ErrMan
						// thread uses it)

						static THandle*	thLFaultList = NULL;
						static UINT32	uiLFaultListSize = 0;
						static TCHAR	tszInfo[MAX_PATH * 8]; // <- why is it so short ??? changed from 4 to 8
						static TCHAR	tcAux[MAX_PATH];

						USHORT			usNumErr = (USHORT)0;
						BOOL			bImprovedClient = FALSE;
						UINT16			NumErrAct = (UINT16)0;
						TCHAR*			tcEndPtr = NULL;

						// We will build the output string contents with the current
						// active alarm information, and we'll send them. Let's go!

						memset(tszInfo, 0, sizeof(tszInfo));
						memset(tcAux, 0, sizeof(tcAux));

						// 1. First number in the output string => number of current active alarms

						usNumErr = FaultTable->GetActiveFaultsNumber();
						ASSERT(usNumErr >= 0); // Debe ser mayor o igual que cero
						swprintf(tszInfo, TEXT("%d$"), usNumErr);

						// We get the number of alarms that the client wants to read (for example,
						// the WebHMI will ask for a maximum of N, 20 at this revision) [it's an
						// optional information sent from the client, using the string 'tcbIdError'
						// of the request]. Default value will be NUMBER_OF_ERRORS_TO_READ
						// (inherited from HMI), and the maximum is also set/checked here!

						if (wcscmp(request->cons.tcbIdError, TEXT("")) != 0)
						{
							// There's something in the string. Let's parse it...

							// This SW revision allows:
							// (1) just a number of alarms to check (we'll receive "a_number")
							// (2) just a symbol, indicating new/improved request (we'll receive "$")
							// (3) options 1 and 2 at the same time (we'll receive "a_number"+"$" or "$" + "a_number")
							// Note that messages in the traces will only work for the first client connected!

							static TCHAR auxRequestString[MAX_PATH];
							static BOOL bShowed = FALSE;
							TCHAR* cpAux = NULL;

							// As the string is manipulated, we copy it (the origin is CONST!)
							wcscpy(auxRequestString, request->cons.tcbIdError);

							if ((cpAux = wcsstr(auxRequestString, TEXT("$"))) != NULL)
							{
								// We've found the '$'. So, it's an improved client (it doesn't
								// need the name of the alarm because it already has a map with
								// the names -read from FCfgErro.xml-)

								if (bShowed == FALSE)
								{
									DEBUGMSG(1, (TEXT("erProcessRequest: Received HMI.LEER_ERR_ACT.IMPROVED!\r\n")));
									bShowed = TRUE;
								}

								bImprovedClient = TRUE;

								// And we delete the '$' from the string...

								if (!wcsncmp(auxRequestString, L"$", 1))
								{
									 // '$' is the first TCHAR, so, we 'delete' it...
									wcscpy(auxRequestString, &(auxRequestString[1]));
								}
								else
								{
									// '$' is the last TCHAR, so, we 'delete' it...
									auxRequestString[wcslen(auxRequestString)-1] = 0;
								}
							}
							else
							{
								if (bShowed == FALSE)
								{
									DEBUGMSG(1, (TEXT("erProcessRequest: Received HMI.LEER_ERR_ACT.CLASSIC\r\n")));
									bShowed = TRUE;
								}

								bImprovedClient = FALSE;
							}

							NumErrAct = (UINT16)wcstoul(auxRequestString, &tcEndPtr, 10);
							if ((NumErrAct <= 0) || (NumErrAct > MAX_NUMBER_OF_ERRORS_TO_READ))
							{
								NumErrAct = MAX_NUMBER_OF_ERRORS_TO_READ; // por defecto s�lo vamos a leer NUMBER_OF_ERRORS_TO_READ
							}
						}
						else
						{
							NumErrAct = MAX_NUMBER_OF_ERRORS_TO_READ; // por defecto s�lo vamos a leer NUMBER_OF_ERRORS_TO_READ
						}

						// Dynamic memory allocation (only grows up, if necessary)...

						if (uiLFaultListSize < NumErrAct)
						{
							if (thLFaultList != NULL)
							{
								thLFaultList = (THandle*)realloc(thLFaultList, sizeof(THandle) * NumErrAct);
							}
							else
							{
								thLFaultList = (THandle*)malloc(sizeof(THandle) * NumErrAct);
							}

							uiLFaultListSize = NumErrAct;
						}

						for (UINT32 uiAux=0; uiAux<uiLFaultListSize; uiAux++)
						{
							thLFaultList[uiAux] = UNDEFINED_HANDLE;
						}

						// Let's get the alarm info, if any, right now!

						// Noted afterwards... 'usNumErr' is overwritten. Maybe we should check
						// that previous value is '>=' than the 'GetSortedActiveFaults' returned
						// value... of check also the value in 'NumErrAct' (TO_REVIEW)

						usNumErr = FaultTable->GetSortedActiveFaults(thLFaultList, NumErrAct);
						if (usNumErr > 0)
						{
							// Si hay errores activos, vamos a enviar sus datos ordenados por antig�edad
							
							// Answer format is: "alarm_handle$alarm_name$activation_date" for each alarm.
							// The current 'tszInfo' content is "#&", where '#' is the current number of
							// active alarms.

							static const UINT32 uiSizeOfDest = sizeof(tszInfo) / sizeof(TCHAR); // <- number of TCHARs available in destination string
							static BOOL bErrorMessageShown = FALSE;
							static TCHAR tcAux2[MAX_PATH];
							
							BOOL bNoMoreFreeSpace = FALSE;
							UINT32 i = 0;

							while ((thLFaultList[i] != UNDEFINED_HANDLE) && (i < usNumErr) && (bNoMoreFreeSpace == FALSE))
							{
								// 1. Concatenamos el HANDLE de la alarma

								memset(tcAux, 0, sizeof(tcAux));

								if (i == 0)
								{
									swprintf(tcAux, TEXT("%d$"), thLFaultList[i]);
								}
								else
								{
									swprintf(tcAux, TEXT("$%d$"), thLFaultList[i]);
								}

								// 2. Concatenamos el nombre de la alarma (s�lo en consulta cl�sica)

								// When using 'improved' requests, we won't look for the name
								// of the alarm; we just add the '_' to the response, which means that
								// the client will look for the name of the alarm itself... (so, the
								// response will be shorter, in TCHARs number, or the client will be
								// allowed to ask for more alarms than before)

								// Noted afterwards... there's no limit/size check of destination
								// variable (in 'tcAux') and, if alarms with extra info are not used
								// correctly (which is highly possible because these alarms have been
								// programmed specially for DLLs) -with huge number of information-,
								// the program could freeze right here! So... TO_REVIEW!

								BOOL bRet = FALSE;

								if (bImprovedClient == FALSE)
								{
									memset(tcAux2, 0, sizeof(tcAux2));
									bRet = FaultTable->Name(thLFaultList[i], tcAux2);
								}
								else
								{
									swprintf(tcAux2, TEXT("_"));
									bRet = TRUE;
								}

								if ((bRet == TRUE) && (wcscmp(tcAux2, TEXT("")) != 0))
								{
									wcscat(tcAux, tcAux2);

									// 2.1. Miramos si es un 'SwError' (it has special information)

									if (thLFaultList[i] == CODE_SWERR)
									{	
										// Sacamos los c�digos de los errores de 'SwError' que se han producido
										memset(tcAux2, 0, sizeof(tcAux2));
										Error.wcsGetSwErrorCode(tcAux2, 10);
										if (wcscmp(tcAux2, TEXT("")) != 0)
										{
											wcscat(tcAux, tcAux2);
										}
									}
									else
									{
										// 2.2. Miramos si es un 'Warning' (it has special information)

										if (thLFaultList[i] == CODE_WARN)
										{
											// Sacamos los c�digos de los errores de 'Warning' que se han producido
											memset(tcAux2, 0, sizeof(tcAux2));
											Error.wcsGetWarningCode(tcAux2, 10);
											if (wcscmp(tcAux2, TEXT("")) != 0)
											{
												wcscat(tcAux, tcAux2);
											}
										}
										else
										{
											// 2.3. Miramos si es un 'SysError' (it has special information)

											if (thLFaultList[i] == CODE_SYSERR)
											{
												// Sacamos los c�digos de los errores de 'SysError' que se han producido
												memset(tcAux2, 0, sizeof(tcAux2));
												Error.wcsGetSysErrCode(tcAux2, 10);
												if (wcscmp(tcAux2, TEXT("")) != 0)
												{
													wcscat(tcAux, tcAux2);
												}
											}
											else
											{
												// 2.3. Miramos si es una alarma con "extra info"

												// We have an alarm not in the "special three" group.
												// So, let's check if it has extra info or not to be
												// published...

												INT32 iRet = 0;
												memset(tcAux2, 0, sizeof(tcAux2));
												if ((iRet = FaultTable->GetExtraInfo(thLFaultList[i], tcAux2, sizeof(tcAux2))) == 0)
												{
													if (wcscmp(tcAux2, TEXT("")) != 0)
													{
														wcscat(tcAux, tcAux2);
													}
												}
												else
												{
													// call fails! let's trace a message...
													ERRORMSG(1, (TEXT("erProcessRequest: FaultTable->GetExtraInfo(%d) fails (returns %d)!\r\n"), thLFaultList[i], iRet));
												}
											}
										}
									}

									wcscat(tcAux, TEXT("$"));
								}
								else
								{
									// No alarm name has been found (when not improved client)!
									wcscat(tcAux, TEXT("$"));
								}

								// 3. Concatenamos la fecha/hora de activaci�n de la alarma

								SYSTEMTIME systemTime = {0};
								if (FaultTable->GetActivatedTime(thLFaultList[i], &systemTime) == FALSE)
								{
									// No se ha podido leer la fecha de activaci�n
									wcscat(tcAux, TEXT("_$_"));
								}
								else
								{
									// 3.1. Date first (YY-MM-DD format)...

									memset(tcAux2, 0, sizeof(tcAux2));
									swprintf(tcAux2, TEXT("%4d-%02d-%02d$"), systemTime.wYear, systemTime.wMonth, systemTime.wDay);
									wcscat(tcAux, tcAux2);

									// 3.2. Hour secondly...

									memset(tcAux2, 0, sizeof(tcAux2));
									swprintf(tcAux2, TEXT("%02d:%02d:%02d"), systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
									wcscat(tcAux, tcAux2);
								}

								// 4. If free space, we add the current alarm info to the response string...

								if (wcslen(tcAux) > (uiSizeOfDest - wcslen(tszInfo)))
								{
									// Ep! No more free space in destination string! We finish the call...
									// (and we show the message just one, to avoid high number of traces!)

									if (bErrorMessageShown == FALSE)
									{
										ERRORMSG(ZONE_ERROR, (_T("erProcessRequest: Response length too large! (%d/%d alarms returned)\r\n"), i, usNumErr));
										bErrorMessageShown = TRUE;
									}

									bNoMoreFreeSpace = TRUE; // <- makes the 'while' to finish
									continue;
								}
								else
								{
									wcscat(tszInfo, tcAux);
								}

								// 5. We go to next alarm info...

								i++;
							}

							if (bNoMoreFreeSpace == FALSE)
							{
								bErrorMessageShown = FALSE;
							}
						}
						else
						{
							// The current 'tszInfo' content is "#&", where '#' is the current number of
							// active alarms. If '#' is 0, we add the following info to the answer...

							wcscat(tszInfo, TEXT("No hay errores activos"));
						}

						// We have to be sure that size of destination is bigger or equal to origin
						// (note that, in this release, there is no problem...)

						wcscpy(response->tcbString, tszInfo);
						response->bEnd = TRUE;
						iFRet = 0;
					}
					break;

					// 2.1.2. ---
				default:
					// ---
					// TO_BE_DONE
					break;
				}
				break;
			
				// 2.3. ---
			case OPC:
				// ---
				// TO_BE_DONE
				break;

				// 2.4. ---
			default:
				// ---
				// TO_BE_DONE
				break;
			}
		}
	}
	catch (...)
	{
		ERRORMSG(ZONE_ERROR, (L"Rethrowing exception in erProcessRequest\r\n"));
		throw;
	}

	DEBUGMSG(ZONE_RUN, (L"<= erProcessRequest\r\n"));
	return iFRet;
}
