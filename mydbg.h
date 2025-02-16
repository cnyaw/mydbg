#pragma once

#include <stdio.h>

#include <map>
#include <string>
#include <vector>

#include <Windows.h>
#include <dbghelp.h>

enum DEBUGGER_STATE {
  DBGS_NONE = 0,
  DBGS_BREAK,
  DBGS_STEP_INTO,
  DBGS_STEP_OUT,
  DBGS_STEP_OVER,
  DBGS_EXIT_PROCESS = 100
};

struct LINE
{
  std::string line;
  DWORD64 address;
};

typedef std::vector<LINE> SourceLines_t;
typedef std::map<std::string, SourceLines_t> SourceFiles_t; // <FileName, [Lines]>

struct BREAK_POINT
{
  std::string fn;
  int LineNumber;                       // 1-based.
  DWORD64 address;
  unsigned char saveCode;
};

//
// Functions.
//

bool AddTempBreakPoint(DWORD64 addr);
void DebugEventLoop();
bool DisplaySourceLines(const std::string &fn, int LineNumber);
void DumpCallStacks();
void DumpGlobals();
void DumpLocals();
const BREAK_POINT* FindBreakPoint(DWORD64 addr);
DWORD64 GetCurrIp();
bool GetSourceLineByAddr(DWORD64 Addr, std::string &fn, int &LineNumber, DWORD &displacement);
std::string GetVariableTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo);
void Go();
bool HandleSoftBreak(const BREAK_POINT* bp);
bool HandleSoftBreakSingleStep(const BREAK_POINT* bp);
bool HandleStepIntoSingleStep();
bool HandleStepOutBreak(const BREAK_POINT *bp);
bool HandleStepOverBreak(const BREAK_POINT *bp);
bool HandleStepOverSingleStep();
void HandleProcessExited();
bool RemoveTempBreakPoint(DWORD64 addr);
bool SetNextStatement(DWORD64 addr);
bool SetNextStatement(const std::string &func);
bool SetNextStatement(const std::string &fn, int LineNumber);
void StepInto();
bool StepOut();
void StepOver();
bool ToggleBreakPoint(DWORD64 addr);
bool ToggleBreakPoint(const std::string &func);
bool ToggleBreakPoint(const std::string &fn, int LineNumber);
bool ToggleBreakPointAtEntryPoint();
