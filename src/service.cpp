#include "service.hpp"
#include "string.h"

MTConnectService::MTConnectService() :
mIsService(false)
{
}

void MTConnectService::setName(const char *aName)
{
  strncpy(mName, aName, 78);
  mName[79] = '\0';
}

void MTConnectService::initialize(int aArgc, const char *aArgv[])
{
  if (mIsService)
    gLogger = new ServiceLogger();
  else
    gLogger = new Logger();
}

#ifdef WIN32

#include <internal.hpp>
#include <strsafe.h>

#pragma comment(lib, "advapi32.lib")

#define SVC_ERROR                        ((DWORD)0xC0000001L)
#define SVC_WARNING                        ((DWORD)0x90000001L)
#define SVC_INFO                        ((DWORD)0x50000001L)

SERVICE_STATUS          gSvcStatus; 
SERVICE_STATUS_HANDLE   gSvcStatusHandle; 
HANDLE                  ghSvcStopEvent = NULL;

VOID WINAPI SvcCtrlHandler( DWORD ); 
VOID WINAPI SvcMain( DWORD, LPTSTR * ); 

VOID ReportSvcStatus( DWORD, DWORD, DWORD );
VOID SvcInit( DWORD, LPTSTR * ); 
VOID SvcReportEvent( LPTSTR );

MTConnectService *gService = NULL;



//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None
//
int MTConnectService::main(int argc, const char *argv[]) 
{ 
    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.

  if(argc > 1) {
    if (stricmp( argv[1], "install") == 0 )
    {
      install(argc - 2, argv + 2);
      return 0;
      } else if (stricmp( argv[1], "debug") == 0 ) {
        initialize(argc - 2, argv + 2);
        start();
        return 0;
      }
    }

    mIsService = true;
    SERVICE_TABLE_ENTRY DispatchTable[] = 
    { 
      {  mName, (LPSERVICE_MAIN_FUNCTION) SvcMain }, 
        { NULL, NULL } 
    }; 

    gService = this;

    if (!StartServiceCtrlDispatcher( DispatchTable )) 
    { 
      SvcReportEvent("StartServiceCtrlDispatcher"); 
    } 

    return 0;
  } 

//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
  void MTConnectService::install(int argc, const char *argv[])
  {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    char szPath[MAX_PATH];

    if( !GetModuleFileName( NULL, szPath, MAX_PATH ) )
    {
      printf("Cannot install service (%d)\n", GetLastError());
      return;
    }

  // Get a handle to the SCM database. 

    schSCManager = OpenSCManager( 
      NULL,                    // local computer
      NULL,                    // ServicesActive database 
      SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager) 
    {
      printf("OpenSCManager failed (%d)\n", GetLastError());
      return;
    }

    schService = OpenService(schSCManager, mName, SC_MANAGER_ALL_ACCESS);
    if (schService != NULL) {
      if (! ChangeServiceConfig( 
        schService,            // handle of service 
        SERVICE_NO_CHANGE,     // service type: no change 
        SERVICE_NO_CHANGE,  // service start type 
        SERVICE_NO_CHANGE,     // error control: no change 
        szPath,                  // binary path: no change 
        NULL,                  // load order group: no change 
        NULL,                  // tag ID: no change 
        NULL,                  // dependencies: no change 
        NULL,                  // account name: no change 
        NULL,                  // password: no change 
        NULL) )                // display name: no change
      {
        printf("ChangeServiceConfig failed (%d)\n", GetLastError());
      }
      else printf("Service updated successfully.\n"); 
    } else {

      // Create the service

      schService = CreateService( 
        schSCManager,              // SCM database 
        mName,                   // name of service 
        mName,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_DEMAND_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

      if (schService == NULL) 
      {
        printf("CreateService failed (%d)\n", GetLastError()); 
        CloseServiceHandle(schSCManager);
        return;
      }
      else printf("Service installed successfully\n"); 
    }

    CloseServiceHandle(schService); 
    CloseServiceHandle(schSCManager);

    HKEY software;
    LONG res = RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE", &software);
    if (res != ERROR_SUCCESS)
    {
      printf("Could not open software key: %d\n", res);
      return;
    }

    HKEY mtc;
    res = RegOpenKey(software, "MTConnect", &mtc);
    RegCloseKey(software);
    if (res != ERROR_SUCCESS)
    {
      printf("Could not open MTConnect, creating: %d\n", res);
      res = RegCreateKey(software, "MTConnect", &mtc);
      if (res != ERROR_SUCCESS)
      {
        printf("Could not create MTConnect: %d\n", res);
        return;
      }
    }

  // Create Service Key
    HKEY adapter;
    res = RegOpenKey(mtc, mName, &adapter);
    RegCloseKey(mtc);
    if (res != ERROR_SUCCESS)
    {
      printf("Could not open %s, creating: %d\n", mName, res);
      res = RegCreateKey(mtc, mName, &adapter);
      if (res != ERROR_SUCCESS)
      {
        printf("Could not create %s: %d\n", mName, res);
        return;
      }
    }

    char arguments[2048];
  // TODO: create registry entries for arguments to be passed in later to create the adapter multi_sz
    int d = 0;
    for (int i = 0; i < argc; i++) { 
      strcpy(arguments + d, argv[i]);
      d += strlen(arguments + d) + 1;
    }

    arguments[d] = '\0';
    RegSetValueEx(adapter, "Arguments", 0, REG_MULTI_SZ, (const BYTE*) arguments, d);
    RegCloseKey(adapter);
  }

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
  VOID WINAPI SvcMain( DWORD dwArgc, LPTSTR *lpszArgv )
  {
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandler( 
      gService->name(), 
      SvcCtrlHandler);

    if( !gSvcStatusHandle )
    { 
      SvcReportEvent("RegisterServiceCtrlHandler"); 
      return; 
    } 

    // These SERVICE_STATUS members remain as set here

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
    gSvcStatus.dwServiceSpecificExitCode = 0;    

    // Report initial status to the SCM

    ReportSvcStatus( SERVICE_START_PENDING, NO_ERROR, 3000 );

    // Perform service-specific initialization and work.

    SvcInit( dwArgc, lpszArgv );
  }

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
  VOID SvcInit( DWORD dwArgc, LPTSTR *lpszArgv)
  {
  // Get the real arguments from the registry
    HKEY software;
    LONG res = RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE", &software);
    if (res != ERROR_SUCCESS)
    {
      printf("Could not open software key: %d\n", res);
      return;
    }

    HKEY mtc;
    res = RegOpenKey(software, "MTConnect", &mtc);
    if (res != ERROR_SUCCESS)
    {
      SvcReportEvent("RegOpenKey: Could not open MTConnect");
      RegCloseKey(software);
    }

    // Create Service Key
    HKEY adapter;
    res = RegOpenKey(mtc, gService->name(), &adapter);
    RegCloseKey(software);
    if (res != ERROR_SUCCESS)
    {
      SvcReportEvent("RegOpenKey: Could not open Adapter");
      RegCloseKey(software);
    }

    char *argp[64];
    BYTE arguments[2048];
    DWORD len, type, argc = 0;
    res = RegQueryValueEx(adapter, "Arguments", 0, &type, (BYTE*) arguments, &len);
    if (res == ERROR_SUCCESS)
    {
      DWORD i = 0;
      while (i < len) {
        argp[argc] = (char*) arguments + i;
        i += strlen((char*) arguments + i) + 1;
        argc++;
      }
      argp[argc] = 0;
    }

    gService->initialize(argc, (const char**) argp);

    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with 
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

    ghSvcStopEvent = CreateEvent(NULL,    // default security attributes
      TRUE,    // manual reset event
      FALSE,   // not signaled
      NULL);   // no name

    if ( ghSvcStopEvent == NULL)
    {
      ReportSvcStatus( SERVICE_STOPPED, NO_ERROR, 0 );
      return;
    }

    // Report running status when initialization is complete.

    ReportSvcStatus( SERVICE_RUNNING, NO_ERROR, 0 );

  // TO_DO: Perform work until service stops.
    gService->start();

    while(1)
    {
        // Check whether to stop the service.

      WaitForSingleObject(ghSvcStopEvent, INFINITE);

      ReportSvcStatus( SERVICE_STOPPED, NO_ERROR, 0 );
      return;
    }
  }

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
  VOID ReportSvcStatus( DWORD dwCurrentState,
    DWORD dwWin32ExitCode,
    DWORD dwWaitHint)
  {
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
      gSvcStatus.dwControlsAccepted = 0;
    else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ( (dwCurrentState == SERVICE_RUNNING) ||
      (dwCurrentState == SERVICE_STOPPED) )
      gSvcStatus.dwCheckPoint = 0;
    else gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    SetServiceStatus( gSvcStatusHandle, &gSvcStatus );
  }

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
  VOID WINAPI SvcCtrlHandler( DWORD dwCtrl )
  {
   // Handle the requested control code. 

    switch(dwCtrl) 
    {  
      case SERVICE_CONTROL_STOP: 
      ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
      SetEvent(ghSvcStopEvent);
      if (gService != NULL)
        gService->stop();
      ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

      return;

      case SERVICE_CONTROL_INTERROGATE: 
      break; 

      default: 
      break;
    } 

  }

//
// Purpose: 
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
// 
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
  VOID SvcReportEvent(LPTSTR szFunction) 
  { 
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    char Buffer[80];

    hEventSource = RegisterEventSource(NULL, gService->name());

    if( NULL != hEventSource )
    {
      sprintf(Buffer, "%-60s failed with %d", szFunction, GetLastError());

      lpszStrings[0] = gService->name();
      lpszStrings[1] = Buffer;

      ReportEvent(hEventSource,        // event log handle
        EVENTLOG_ERROR_TYPE, // event type
        0,                   // event category
        SVC_ERROR,           // event identifier
        NULL,                // no security identifier
        2,                   // size of lpszStrings array
        0,                   // no binary data
        lpszStrings,         // array of strings
        NULL);               // no binary data

      DeregisterEventSource(hEventSource);
    }
  }

  VOID SvcLogEvent(WORD aType, DWORD aId, LPSTR aText) 
  { 
    HANDLE hEventSource;
    LPCTSTR lpszStrings[3];

    hEventSource = RegisterEventSource(NULL, gService->name());

    if( NULL != hEventSource )
    {
      lpszStrings[0] = gService->name();
      lpszStrings[1] = "\n\n";
      lpszStrings[2] = aText;

      ReportEvent(hEventSource,        // event log handle
        aType, // event type
        0,                   // event category
        aId,           // event identifier
        NULL,                // no security identifier
        3,                   // size of lpszStrings array
        0,                   // no binary data
        lpszStrings,         // array of strings
        NULL);               // no binary data

      DeregisterEventSource(hEventSource);
    }
  }


  void ServiceLogger::error(const char *aFormat, ...)
  {
    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start (args, aFormat);
    SvcLogEvent(EVENTLOG_ERROR_TYPE, SVC_ERROR, (LPSTR) format(buffer, LOGGER_BUFFER_SIZE, aFormat, args));
    va_end (args);
  }

  void ServiceLogger::warning(const char *aFormat, ...)
  {
    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start (args, aFormat);
    SvcLogEvent(EVENTLOG_WARNING_TYPE, SVC_WARNING, (LPSTR) format(buffer, LOGGER_BUFFER_SIZE, aFormat, args));
    va_end (args);
  }

  void ServiceLogger::info(const char *aFormat, ...)
  {
    char buffer[LOGGER_BUFFER_SIZE];
    va_list args;
    va_start (args, aFormat);
    SvcLogEvent(EVENTLOG_INFORMATION_TYPE, SVC_INFO, (LPSTR) format(buffer, LOGGER_BUFFER_SIZE, aFormat, args));
    va_end (args);
  }


#else
  int MTConnectService::main(int argc, const char *argv[]) 
  { 
    initialize(argc - 1, argv + 1);
    start();
    return 0;
  } 

  void MTConnectService::install(int argc, const char *argv[])
  {
  }

#endif