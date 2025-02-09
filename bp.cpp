#include "mydbg.h"

extern PROCESS_INFORMATION g_piDbgee;

std::vector<BREAK_POINT> g_bp;

void AddBreakPoint_i(const std::string &fn, int LineNumber, DWORD64 addr)
{
  BREAK_POINT bp;
  bp.fn = fn;
  bp.LineNumber = LineNumber;
  bp.address = addr;
  ReadProcessMemory(g_piDbgee.hProcess, (LPVOID)addr, &bp.saveCode, 1, NULL);
  g_bp.push_back(bp);
  unsigned char cc = 0xcc;
  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)addr, &cc, 1, NULL); // Write 0xcc to bp address.
}

bool AddBreakPoint(const std::string &fn, int LineNumber)
{
  LONG displacement;
  IMAGEHLP_LINE64 li = { 0 };
  li.SizeOfStruct = sizeof(li);
  if (SymGetLineFromName64(g_piDbgee.hProcess, NULL, (PSTR)fn.c_str(), LineNumber, &displacement, &li)) {
    AddBreakPoint_i(fn, LineNumber, li.Address);
    printf("Add breakpoint at %s:%d(%x)\n", fn.c_str(), LineNumber, (unsigned int)li.Address);
    return true;
  }
  return false;
}

bool AddTempBreakPoint(DWORD64 addr)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.address == addr) {
      return false;
    }
  }
  AddBreakPoint_i("", 0, addr);
  return true;
}

bool FindBreakPoint(const std::string &fn, int LineNumber)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.LineNumber == LineNumber && bp.fn == fn) {
      return true;
    }
  }
  return false;
}

const BREAK_POINT* FindBreakPoint(DWORD64 addr)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.address == addr) {
      return &bp;
    }
  }
  return NULL;
}

void RemoveBreakPoint_i(int i)
{
  const BREAK_POINT &bp = g_bp[i];
  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp.address, &bp.saveCode, 1, NULL); // Write back saved OP code.
  g_bp.erase(g_bp.begin() + i);
}

bool RemoveBreakPoint(const std::string &fn, int LineNumber)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.LineNumber == LineNumber && bp.fn == fn) {
      RemoveBreakPoint_i(i);
      printf("Remove breakpoint at %s:%d(%x)\n", fn.c_str(), LineNumber, (unsigned int)bp.address);
      return true;
    }
  }
  return false;
}

bool RemoveTempBreakPoint(DWORD64 addr)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.address == addr) {
      RemoveBreakPoint_i(i);
      return true;
    }
  }
  return false;
}

bool ToggleBreakPoint(DWORD64 addr)
{
  std::string fn;
  int LineNumber = 0;
  DWORD displacement = 0;
  if (GetSourceLineByAddr(addr, fn, LineNumber, displacement)) {
    return ToggleBreakPoint(fn, LineNumber);
  }
  return false;
}

bool ToggleBreakPoint(const std::string &func)
{
  SYMBOL_INFO sym = {0};
  sym.SizeOfStruct = sizeof(sym);
  if (SymFromName(g_piDbgee.hProcess, (LPSTR)func.c_str(), &sym)) {
    return ToggleBreakPoint(sym.Address);
  }
  return false;
}

bool ToggleBreakPoint(const std::string &fn, int LineNumber)
{
  if (FindBreakPoint(fn, LineNumber)) {
    return RemoveBreakPoint(fn, LineNumber);
  } else {
    return AddBreakPoint(fn, LineNumber);
  }
}

bool ToggleBreakPointAtEntryPoint()
{
  static const char* names[] = {"main", "wmain", "WinMain", "wWinMain"};
  for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
    if (ToggleBreakPoint(names[i])) {
      return true;
    }
  }
  return false;
}
