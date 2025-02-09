#include "mydbg.h"

extern int g_dbgState;
extern PROCESS_INFORMATION g_piDbgee;
extern DEBUG_EVENT g_debugEvent;
extern DWORD64 g_tmpBpAddr;

bool OnDllLoaded(const LOAD_DLL_DEBUG_INFO &pi)
{
  printf("LOAD_DLL_DEBUG_EVENT\n");
  DWORD64 moduleAddress = SymLoadModule64(g_piDbgee.hProcess, pi.hFile, NULL, NULL, (DWORD64)pi.lpBaseOfDll, 0);
  if (0 != moduleAddress) {
    printf("\tSymLoadModule64 0x%0x ok.\n", pi.lpBaseOfDll);
  } else {
    printf("\tSymLoadModule64 failed.\n");
  }
  CloseHandle(pi.hFile);
  return true;
}

bool OnDllUnloaded(const UNLOAD_DLL_DEBUG_INFO &pi)
{
  printf("UNLOAD_DLL_DEBUG_EVENT\n");
  SymUnloadModule64(g_piDbgee.hProcess, (DWORD64)pi.lpBaseOfDll);
  printf("\tSymUnloadModule64.\n");
  return true;
}

bool OnBreakPoint()
{
  std::string fn;
  int LineNumber = 0;
  DWORD displacement = 0;
  if (GetSourceLineByAddr(GetCurrIp(), fn, LineNumber, displacement)) {
    printf("at %s:%d\n", fn.c_str(), LineNumber);
    DisplaySourceLines(fn, LineNumber);
    g_dbgState = DBGS_BREAK;
    return false;
  }
  return true;
}

bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
  if (EXCEPTION_SINGLE_STEP == pi.ExceptionRecord.ExceptionCode) {
    if (DBGS_STEP_INTO == g_dbgState && HandleStepIntoSingleStep()) {
      return true;
    }
    if (DBGS_STEP_OVER == g_dbgState && HandleStepOverSingleStep()) {
      return true;
    }
  }
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode) {
    const BREAK_POINT *bp = FindBreakPoint((DWORD64)pi.ExceptionRecord.ExceptionAddress);
    if (DBGS_STEP_OUT == g_dbgState && HandleStepOutBreak(bp)) {
      return true;
    }
    if (DBGS_STEP_OVER == g_dbgState && HandleStepOverBreak(bp)) {
      return true;
    }
  }
  printf("EXCEPTION_DEBUG_EVENT. Code: 0x%x, Addr: 0x%x ", pi.ExceptionRecord.ExceptionCode, pi.ExceptionRecord.ExceptionAddress);
  if (pi.dwFirstChance) {
    printf("(First chance)\n");
  } else {
    printf("(Second chance)\n");
  }
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode || EXCEPTION_SINGLE_STEP == pi.ExceptionRecord.ExceptionCode) {
    if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode) {
      printf("\tEXCEPTION_BREAKPOINT. ");
      const BREAK_POINT *bp = FindBreakPoint((DWORD64)pi.ExceptionRecord.ExceptionAddress);
      if (bp && HandleSoftBreak(bp)) {
        return OnBreakPoint();
      }
    } else {
      printf("\tEXCEPTION_SINGLE_STEP. ");
      const BREAK_POINT *bp = FindBreakPoint((DWORD64)pi.ExceptionRecord.ExceptionAddress);
      if (bp) {
        HandleSoftBreakSingleStep(bp);
      }
    }
    return OnBreakPoint();
  }
  return true;
}

bool OnOutputDebugString(const OUTPUT_DEBUG_STRING_INFO &pi)
{
  BYTE* pBuffer = (BYTE*)malloc(pi.nDebugStringLength);
  ReadProcessMemory(g_piDbgee.hProcess, pi.lpDebugStringData, pBuffer, pi.nDebugStringLength, NULL);
  printf("OUTPUT_DEBUG_STRING_EVENT: '%s'\n", pBuffer);
  free(pBuffer);
  return true;
}

bool OnProcessCreated(const CREATE_PROCESS_DEBUG_INFO &pi)
{
  printf("CREATE_PROCESS_DEBUG_EVENT\n");
  if (SymInitialize(g_piDbgee.hProcess, NULL, FALSE)) {
    printf("\tSymInitialize ok.\n");
    DWORD64 moduleAddress = SymLoadModule64(g_piDbgee.hProcess, pi.hFile, NULL, NULL, (DWORD64)pi.lpBaseOfImage, 0);
    if (0 != moduleAddress) {
      printf("\tSymLoadModule64 0x%x ok.\n", pi.lpBaseOfImage);
    } else {
      printf("\tSymLoadModule64 failed.\n");
    }
    ToggleBreakPointAtEntryPoint();
  } else {
    printf("\tSymInitialize failed.\n");
  }
  CloseHandle(pi.hFile);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool OnProcessExited(const EXIT_PROCESS_DEBUG_INFO &pi)
{
  printf("EXIT_PROCESS_DEBUG_EVENT. Code: %u\n", pi.dwExitCode);
  HandleProcessExited();
  return false;
}

bool OnRipEvent(const RIP_INFO&)
{
  printf("RIP_EVENT\n");
  return true;
}

bool OnThreadCreated(const CREATE_THREAD_DEBUG_INFO &pi)
{
  printf("CREATE_THREAD_DEBUG_EVENT\n");
  CloseHandle(pi.hThread);
  return true;
}

bool OnThreadExited(const EXIT_THREAD_DEBUG_INFO&)
{
  printf("EXIT_THREAD_DEBUG_EVENT\n");
  return true;
}

bool DispatchDebugEvent(const DEBUG_EVENT &debugEvent)
{
  switch (debugEvent.dwDebugEventCode) {
    case CREATE_PROCESS_DEBUG_EVENT:
      return OnProcessCreated(debugEvent.u.CreateProcessInfo);

    case CREATE_THREAD_DEBUG_EVENT:
      return OnThreadCreated(debugEvent.u.CreateThread);

    case EXCEPTION_DEBUG_EVENT:
      return OnException(debugEvent.u.Exception);

    case EXIT_PROCESS_DEBUG_EVENT:
      return OnProcessExited(debugEvent.u.ExitProcess);

    case EXIT_THREAD_DEBUG_EVENT:
      return OnThreadExited(debugEvent.u.ExitThread);

    case LOAD_DLL_DEBUG_EVENT:
      return OnDllLoaded(debugEvent.u.LoadDll);

    case UNLOAD_DLL_DEBUG_EVENT:
      return OnDllUnloaded(debugEvent.u.UnloadDll);

    case OUTPUT_DEBUG_STRING_EVENT:
      return OnOutputDebugString(debugEvent.u.DebugString);

    case RIP_EVENT:
      return OnRipEvent(debugEvent.u.RipInfo);

    default:
      printf("Unknown debug event.\n");
      return false;
  }
}

void DebugEventLoop()
{
  while (WaitForDebugEvent(&g_debugEvent, INFINITE)) {
    if (DispatchDebugEvent(g_debugEvent)) {
      ContinueDebugEvent(g_debugEvent.dwProcessId, g_debugEvent.dwThreadId, DBG_CONTINUE);
    } else {
      break;
    }
  }
}

void HandleProcessExited()
{
  SymCleanup(g_piDbgee.hProcess);
  printf("\tSymCleanup.\n");
  CloseHandle(g_piDbgee.hThread);
  CloseHandle(g_piDbgee.hProcess);
  memset(&g_piDbgee, 0, sizeof(g_piDbgee));
  g_dbgState = DBGS_EXIT_PROCESS;
}
