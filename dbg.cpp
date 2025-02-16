#include "mydbg.h"
#include "mydbghelp.h"

extern int g_dbgState;
extern PROCESS_INFORMATION g_piDbgee;
extern DEBUG_EVENT g_debugEvent;

std::string g_LastBreakSource;
int g_LastBreakLine;

DWORD64 g_tmpBpAddr;

void ClearCpuSingleStepFlag()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  if (ctx.EFlags & 0x100) {
    ctx.EFlags ^= 0x100;
    SetThreadContext(g_piDbgee.hThread, &ctx);
  }
}

void Continue()
{
  g_dbgState = DBGS_NONE;
  ContinueDebugEvent(g_debugEvent.dwProcessId, g_debugEvent.dwThreadId, DBG_CONTINUE);
}

void DumpCallStacks()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_FULL;
  GetThreadContext(g_piDbgee.hThread, &ctx);

  STACKFRAME sf = {0};
  sf.AddrPC.Offset = ctx.Eip;
  sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrStack.Offset = ctx.Esp;
  sf.AddrStack.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = ctx.Ebp;
  sf.AddrFrame.Mode = AddrModeFlat;

  while (true) {
    if (!StackWalk(IMAGE_FILE_MACHINE_I386, g_piDbgee.hProcess, g_piDbgee.hThread, &sf, &ctx, 0, SymFunctionTableAccess, SymGetModuleBase, 0)) {
      break;
    }

    if (0 == sf.AddrFrame.Offset) {
      break;
    }

    DWORD displacement;
    IMAGEHLP_LINE64 li = {0};
    li.SizeOfStruct = sizeof(li);

    if (!SymGetLineFromAddr64(g_piDbgee.hProcess, sf.AddrPC.Offset, &displacement, &li)) {
      printf("0x%x\n", (unsigned int)sf.AddrPC.Offset);
    } else {
      char buff[sizeof(SYMBOL_INFO) + 256] = {0};
      SYMBOL_INFO *psi = (SYMBOL_INFO*)buff;
      psi->SizeOfStruct = sizeof(SYMBOL_INFO);
      psi->MaxNameLen = 256;
      DWORD64 displacement2 = 0;
      if (!SymFromAddr(g_piDbgee.hProcess, sf.AddrPC.Offset, &displacement2, psi)) {
        printf("%s:%d\n", li.FileName, li.LineNumber);
      } else {
        printf("%s:%d!%s\n", li.FileName, li.LineNumber, psi->Name);
      }
    }
  }
}

ULONG64 GetVariableAddress(PSYMBOL_INFO pSymInfo)
{
  if (pSymInfo->Flags & SYMFLAG_REGREL) {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL;
    GetThreadContext(g_piDbgee.hThread, &ctx);
    return ctx.Ebp + pSymInfo->Address; // In 32-bits mode, variable address is related to EBP.
  } else {
    return pSymInfo->Address;
  }
}

std::string GetBaseTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo)
{
  DWORD type;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_BASETYPE, &type);
  ULONG64 length;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_LENGTH, &length);
  switch (type) {
    case btVoid:
      return "void";
    case btChar:
      return "char";
    case btWChar:
      return "wchar_t";
    case btInt:
      switch (length) {
        case 2:
          return "short";
        case 8:
          return "long long";
      }
      return "int";
    case btUInt:
      switch (length) {
        case 1:
          return "unsigned char";
        case 2:
          return "unsigned short";
        case 8:
          return "unsigned long long";
      }
      return "unsigned int";
    case btFloat:
      if (8 == length) {
        return "double";
      }
      return "float";
    case btBool:
      return "bool";
    case btLong:
      return "long";
    case btULong:
      return "unsigned long";
  }
  return "BaseType";
}

std::string GetBaseTypeValue(ULONG typeId, PSYMBOL_INFO pSymInfo, const char *pData)
{
  DWORD type;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_BASETYPE, &type);
  ULONG64 length;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_LENGTH, &length);
  char buff[32];
  switch (type) {
    case btChar:
      sprintf(buff, "%c", *pData);
      return buff;
    case btWChar:
      sprintf(buff, "%d", (int)*pData);
      return buff;
    case btInt:
      switch (length) {
        case 2:
          sprintf(buff, "%d", (int)*(short*)pData);
          return buff;
        case 8:
          sprintf(buff, "%I64d", *(long long*)pData);
          return buff;
      }
      sprintf(buff, "%d", *(int*)pData);
      return buff;
    case btUInt:
      switch (length) {
        case 1:
          sprintf(buff, "%u", (unsigned int)*pData);
          return buff;
        case 2:
          sprintf(buff, "%u", (unsigned int)*(unsigned short*)pData);
          return buff;
        case 8:
          sprintf(buff, "%I64u", *(unsigned long long*)pData);
          return buff;
      }
      sprintf(buff, "%u", *(unsigned int*)pData);
      return buff;
    case btFloat:
      if (8 == length) {
        sprintf(buff, "%lf", *(double*)pData);
      } else {
        sprintf(buff, "%f", *(float*)pData);
      }
      return buff;
    case btBool:
      if (0 == *pData) {
        return "true";
      } else {
        return "false";
      }
    case btLong:
      sprintf(buff, "%l", *(long*)pData);
      return buff;
    case btULong:
      sprintf(buff, "%lu", *(unsigned long*)pData);
      return buff;
  }
  return "";
}

std::string GetArrayTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo)
{
  DWORD containTypeId;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_TYPEID, &containTypeId);
  DWORD count;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_COUNT, &count);
  std::string typeName = GetVariableTypeName(containTypeId, pSymInfo);
  char buff[64];
  sprintf(buff, "%u", (unsigned int)count);
  return typeName + "[" + buff + "]";
}

std::string GetPointTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo)
{
  DWORD containTypeId;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_TYPEID, &containTypeId);
  std::string typeName = GetVariableTypeName(containTypeId, pSymInfo);
  return typeName + "*";
}

std::string GetUdtTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo)
{
  WCHAR *pName;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_SYMNAME, &pName);
  int NeedLen = WideCharToMultiByte(CP_ACP, 0, pName, -1, NULL, 0, NULL, NULL);
  std::string buff;
  buff.resize(NeedLen);
  WideCharToMultiByte(CP_ACP, 0, pName, -1, (char*)buff.c_str(), NeedLen, NULL, NULL);
  LocalFree(pName);
  return buff;
}

std::string GetVariableTypeName(ULONG typeId, PSYMBOL_INFO pSymInfo)
{
  // https://debuginfo.com/articles/dbghelptypeinfo.html
  DWORD symTag;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_SYMTAG, &symTag);
  switch (symTag) {
    case SymTagUDT:
    case SymTagEnum:
      return GetUdtTypeName(typeId, pSymInfo);
    case SymTagFunctionType:
      return "<func>";
    case SymTagPointerType:
      return GetPointTypeName(typeId, pSymInfo);
    case SymTagArrayType:
      return GetArrayTypeName(typeId, pSymInfo);
    case SymTagBaseType:
      return GetBaseTypeName(typeId, pSymInfo);
  }
  char buff[32];
  sprintf(buff, "<unknown>%d", (int)symTag);
  return buff;
}

std::string GetVariableValue(ULONG typeId, PSYMBOL_INFO pSymInfo, const std::string &data)
{
  DWORD symTag;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeId, TI_GET_SYMTAG, &symTag);
  char buff[32];
  switch (symTag) {
    case SymTagBaseType:
      return GetBaseTypeValue(typeId, pSymInfo, data.data());
    case SymTagPointerType:
      sprintf(buff, "0x%x", *(unsigned int*)data.data());
      return buff;
  }
  std::string value;
  for (size_t i = 0; i < data.size(); i++) {
    sprintf(buff, "%02x ", (unsigned char)data[i]);
    value += buff;
  }
  return value;
}

static BOOL CALLBACK StaticEnumLocals(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
  if (SymTagData == pSymInfo->Tag) {
    ULONG64 addr = GetVariableAddress(pSymInfo);
    std::string mem;
    mem.resize(SymbolSize);
    ReadProcessMemory(g_piDbgee.hProcess, (LPCVOID)addr, (LPVOID)mem.data(), SymbolSize, NULL);
    std::string value = GetVariableValue(pSymInfo->TypeIndex, pSymInfo, mem);
    std::string type = GetVariableTypeName(pSymInfo->TypeIndex, pSymInfo);
    printf("%08x %s %s %s\n", (unsigned int)addr, type.c_str(), pSymInfo->Name, value.c_str());
  }
  return TRUE;
}

void DumpGlobals()
{
  DWORD64 BaseMod = SymGetModuleBase64(g_piDbgee.hProcess, GetCurrIp());
  SymEnumSymbols(g_piDbgee.hProcess, BaseMod, NULL, StaticEnumLocals, NULL);
}

void DumpLocals()
{
  IMAGEHLP_STACK_FRAME sf = {0};
  sf.InstructionOffset = GetCurrIp();
  SymSetContext(g_piDbgee.hProcess, &sf, NULL);
  SymEnumSymbols(g_piDbgee.hProcess, 0, NULL, StaticEnumLocals, NULL);
}

DWORD64 GetCurrIp()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  return ctx.Eip;
}

bool GetSourceLineByAddr(DWORD64 Addr, std::string &fn, int &LineNumber, DWORD &displacement)
{
  IMAGEHLP_LINE64 li = {0};
  li.SizeOfStruct = sizeof(li);

  if (!SymGetLineFromAddr64(g_piDbgee.hProcess, Addr, &displacement, &li)) {
    DWORD ec = GetLastError();
    switch (ec) {
      case 126:
        printf("Debug info in current module has not loaded.\n");
        break;
      case 487:
        printf("No debug info in current module.\n");
        break;
      default:
        printf("SymGetLineFromAddr64 failed. LastError: %d\n", ec);
        break;
    }
    return false;
  }

  fn = li.FileName;
  LineNumber = li.LineNumber;

  return true;
}

bool IsCurrSourceLineChanged(std::string &fn, int &LineNumber)
{
  DWORD displacement = 0;
  if (!GetSourceLineByAddr(GetCurrIp(), fn, LineNumber, displacement)) {
    return false;
  } else {
    return fn != g_LastBreakSource || LineNumber != g_LastBreakLine;
  }
}

bool HandleSoftBreakSingleStep(const BREAK_POINT* bp)
{
  // 1. write back 0xcc
  // 2. enter break

  unsigned char cc = 0xcc;
  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp->address, &cc, 1, NULL); // Write 0xcc to bp address.

  return true;
}

void SetCpuSingleStepFlag()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  if (!(ctx.EFlags & 0x100)) {
    ctx.EFlags |= 0x100;                // Enable single-step flag.
    SetThreadContext(g_piDbgee.hThread, &ctx);
  }
}

void DoStepInto()
{
  SetCpuSingleStepFlag();
  Continue();
  g_dbgState = DBGS_STEP_INTO;
}

bool HandleStepIntoSingleStep()
{
  //
  // Single step loop until current source line is different to saved source line.
  //

  std::string fn;
  int LineNumber = 0;
  if (!IsCurrSourceLineChanged(fn, LineNumber)) {
    DoStepInto();
    return true;                        // Return true to continue.
  }

  return false;
}

bool HandleStepOutBreak(const BREAK_POINT *bp)
{
  //
  // 1. handle soft break as usual.
  // 2. remove temp bp.
  // 3. do a step into(until IsCurrSourceLineChanged).
  //

  if (bp && HandleSoftBreak(bp)) {
    RemoveTempBreakPoint(g_tmpBpAddr);
    StepInto();
    return true;
  }
  return false;
}

bool IsCallInstruction(DWORD64 addr, int &Length)
{
  //
  // Table of CALL instruction pattern, format: OP1, OP2, Length.
  // If OP2 == 0 then discard OP2.
  //

  static const unsigned char CALL_INST[] = {
    0x9A, 0x00, 7,  // CALL FAR seg16:abs32
    0xE8, 0x00, 5,  // CALL rel32
    0xFF, 0x10, 2,  // CALL dword ptr [EAX]
    0xFF, 0x11, 2,  // CALL dword ptr [ECX]
    0xFF, 0x12, 2,  // CALL dword ptr [EDX]
    0xFF, 0x13, 2,  // CALL dword ptr [EBX]
    0xFF, 0x14, 3,  // CALL dword ptr [REG * SCALE + BASE]
    0xFF, 0x15, 6,  // CALL dword ptr [abs32]
    0xFF, 0x16, 2,  // CALL dword ptr [ESI]
    0xFF, 0x17, 2,  // CALL dword ptr [EDI]
    0xFF, 0x50, 3,  // CALL dword ptr [EAX + off8]
    0xFF, 0x51, 3,  // CALL dword ptr [ECX + off8]
    0xFF, 0x52, 3,  // CALL dword ptr [EDX + off8]
    0xFF, 0x53, 3,  // CALL dword ptr [EBX + off8]
    0xFF, 0x54, 4,  // CALL dword ptr [REG * SCALE + BASE + off8]
    0xFF, 0x55, 3,  // CALL dword ptr [EBP + off8]
    0xFF, 0x56, 3,  // CALL dword ptr [ESI + off8]
    0xFF, 0x57, 3,  // CALL dword ptr [EDI + off8]
    0xFF, 0x90, 6,  // CALL dword ptr [EAX + off32]
    0xFF, 0x91, 6,  // CALL dword ptr [ECX + off32]
    0xFF, 0x92, 6,  // CALL dword ptr [EDX + off32]
    0xFF, 0x93, 6,  // CALL dword ptr [EBX + off32]
    0xFF, 0x94, 7,  // CALL dword ptr [REG * SCALE + BASE + off32]
    0xFF, 0x95, 6,  // CALL dword ptr [EBP + off32]
    0xFF, 0x96, 6,  // CALL dword ptr [ESI + off32]
    0xFF, 0x97, 6,  // CALL dword ptr [EDI + off32]
    0xFF, 0xD0, 2,  // CALL EAX
    0xFF, 0xD1, 2,  // CALL ECX
    0xFF, 0xD2, 2,  // CALL EDX
    0xFF, 0xD3, 2,  // CALL EBX
    0xFF, 0xD4, 2,  // CALL ESP
    0xFF, 0xD5, 2,  // CALL EBP
    0xFF, 0xD6, 2,  // CALL ESI
    0xFF, 0xD7, 2   // CALL EDI
  };

  unsigned char code[2];
  ReadProcessMemory(g_piDbgee.hProcess, (LPVOID)addr, code, 2, NULL);

  for (int i = 0; i < sizeof(CALL_INST) / 3; i++) {
    unsigned char OP1 = CALL_INST[3 * i + 0];
    unsigned char OP2 = CALL_INST[3 * i + 1];
    if (OP1 == code[0] && (0 == OP2 || OP2 == code[1])) {
      Length = CALL_INST[3 * i + 2];
      return true;
    }
  }

  return false;
}

void DoStepOver()
{
  //
  // If current instruction is a CALL instruction, then set a temp bp
  // at next instruction and go. Otherwise enable single step flag and go.
  //

  int Length = 0;                       // Length of the call instructions.
  if (IsCallInstruction(GetCurrIp(), Length)) {
    g_tmpBpAddr = GetCurrIp() + Length;
    AddTempBreakPoint(g_tmpBpAddr);
  } else {
    SetCpuSingleStepFlag();
  }

  Continue();
  g_dbgState = DBGS_STEP_OVER;
}

bool HandleStepOverSingleStep()
{
  //
  // Single step loop until current source line is different to saved source line.
  //

  std::string fn;
  int LineNumber = 0;
  if (!IsCurrSourceLineChanged(fn, LineNumber)) {
    DoStepOver();
    return true;                        // Return true to continue.
  }

  return false;
}

bool HandleStepOverBreak(const BREAK_POINT *bp)
{
  if (bp && HandleSoftBreak(bp)) {
    RemoveTempBreakPoint(g_tmpBpAddr);
    return HandleStepOverSingleStep();
  }
  return false;
}

void Go()
{
  ClearCpuSingleStepFlag();
  Continue();
}

void SetCurrIp(DWORD64 ip)
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  ctx.Eip = ip;
  SetThreadContext(g_piDbgee.hThread, &ctx);
}

bool HandleSoftBreak(const BREAK_POINT* bp)
{
  //
  // 1, write back saved op.
  // 2. ip--
  // 3. set single step flag.
  //

  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp->address, &bp->saveCode, 1, NULL);
  SetCurrIp(GetCurrIp() - 1);
  SetCpuSingleStepFlag();

  return true;
}

void SaveCurrSourceLine()
{
  std::string fn;
  int LineNumber = 0;
  DWORD displacement = 0;
  if (GetSourceLineByAddr(GetCurrIp(), fn, LineNumber, displacement)) {
    g_LastBreakSource = fn;
    g_LastBreakLine = LineNumber;
  }
}

bool SetNextStatement(DWORD64 addr)
{
  std::string fn;
  int LineNumber = 0;
  DWORD displacement = 0;
  if (GetSourceLineByAddr(addr, fn, LineNumber, displacement)) {
    return SetNextStatement(fn, LineNumber);
  }
  return false;
}

bool SetNextStatement(const std::string &func)
{
  SYMBOL_INFO sym = {0};
  sym.SizeOfStruct = sizeof(sym);
  if (SymFromName(g_piDbgee.hProcess, (LPSTR)func.c_str(), &sym)) {
    return SetNextStatement(sym.Address);
  }
  return false;
}

bool SetNextStatement(const std::string &fn, int LineNumber)
{
  LONG displacement;
  IMAGEHLP_LINE64 li = { 0 };
  li.SizeOfStruct = sizeof(li);
  if (SymGetLineFromName64(g_piDbgee.hProcess, NULL, (PSTR)fn.c_str(), LineNumber, &displacement, &li)) {
    SetCurrIp(li.Address);
    printf("Set next statement at %s:%d(%x)\n", fn.c_str(), LineNumber, (unsigned int)li.Address);
    DisplaySourceLines(fn, LineNumber);
    return true;
  }
  return false;
}

void StepInto()
{
  SaveCurrSourceLine();
  DoStepInto();
}

bool StepOut()
{
  //
  // 1. find current function.
  // 2. set a temp bp at the end of the function.
  // 3. go.
  //

  SYMBOL_INFO si = {0};
  si.SizeOfStruct = sizeof(si);

  DWORD64 displacement = 0;
  if (!SymFromAddr(g_piDbgee.hProcess, GetCurrIp(), &displacement, &si)) {
    printf("StepOut: SymFromAddr failed. LastError %d\n", GetLastError());
    return false;
  }

  g_tmpBpAddr = si.Address + si.Size - 1;
  if (!AddTempBreakPoint(g_tmpBpAddr)) {
    return false;
  }

  Go();
  g_dbgState = DBGS_STEP_OUT;

  return true;
}

void StepOver()
{
  SaveCurrSourceLine();
  DoStepOver();
}
