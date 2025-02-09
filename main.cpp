#include "mydbg.h"

int g_dbgState = DBGS_NONE;

PROCESS_INFORMATION g_piDbgee = { 0 };
DEBUG_EVENT g_debugEvent;

unsigned int g_addrDump = 0;

void DumpMemory(unsigned int addr, unsigned int count)
{
  if (0 < addr) {
    g_addrDump = addr;
  }

  unsigned int addrTag = g_addrDump, extra = 0;
  if (0 < g_addrDump && 0 != (g_addrDump % 16)) {
    addrTag = g_addrDump - (g_addrDump % 16);
    extra = g_addrDump - addrTag;
  }

  unsigned int total = count + extra;
  std::string mem;
  mem.resize(total);

  ReadProcessMemory(g_piDbgee.hProcess, (LPCVOID)g_addrDump, (LPVOID)mem.data(), total, NULL);

  for (unsigned int i = 0; i < total;++i) {
    printf("%08X  ", addrTag);
    unsigned int j;
    for (j = 0; j < 16 && i + j < total; ++j) {
      if (addrTag + j >= g_addrDump) {
        printf("%02X", (unsigned char)mem[addrTag - g_addrDump + j]);
      } else {
        printf("  ");
      }
      printf(7 == j ? "-" : " ");
    }
    if (16 != j) {
      for (int k = j; k < 16;++k) {
        printf("   ");
      }
    }
    printf(" ");
    for (j = 0; j < 16 && i + j < total; ++j) {
      if (addrTag + j >= g_addrDump) {
          unsigned char c = mem[addrTag - g_addrDump + j];
          if (' ' <= c && '~' >= c) {
            printf("%c", c);
          } else {
            printf(".");
          }
      } else {
        printf(" ");
      }
    }
    i += j - 1;
    addrTag += 16;
    printf("\n");
  }

  g_addrDump += count;
}

void DumpRegisters()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  printf("EAX = 0x%08x, EBX = 0x%08x, ECX = 0x%08x, EDX = 0x%08x, ESI = 0x%08x, EDI = 0x%08x\n", ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi);
  printf("EIP = 0x%08x, EBP = 0x%08x, ESP = 0x%08x, EFlags = 0x%08x\n", ctx.Eip, ctx.Ebp, ctx.Esp, ctx.EFlags);
}

void ShowCommandHelp()
{
  printf("mydbg source level debugger commands:\n");
  printf("toggle bp\tb|B address|function|source lineno\n");
  printf("call stacks\tc|C\n");
  printf("dump\t\td|D [range]\n");
  printf("go\t\tg|G\n");
  printf("globals\t\tlg|LG\n");
  printf("locals\t\tl|L\n");
  printf("set next st\ts|S address|function|source lineno\n");
  printf("step into\tt|T\n");
  printf("step out\to|O\n");
  printf("step over\tp|P\n");
  printf("quit\t\tq|Q\n");
  printf("registers\tr|R\n");
  printf("    range = address [count]\n");
  printf("    address: hex, count: dec\n");
  printf("    source: full path, lineno: dec(from 1)\n");
  printf("    default range count: 128\n");
}

void HandleUserCommand()
{
  printf(">");

  char buff[256];
  if (!fgets(buff , sizeof(buff), stdin)) {
    return;
  }

  std::string str(buff);
  str.erase(0, str.find_first_not_of(" \t\r\n")); // Trim space.
  str.erase(str.find_last_not_of(" \t\r\n") + 1);
  if (str.empty()) {
    ShowCommandHelp();
    return;
  }

  switch (str[0]) {
    case 'b': case 'B':                 // Toggle break point.
      {
        char key[2];
        char fn[MAX_PATH];
        unsigned int addr;
        if (3 == sscanf(str.c_str(), "%1s %99s %d", key, fn, &addr)) {
          ToggleBreakPoint(fn, addr);
        } else if (2 == sscanf(str.c_str(), "%1s %x", key, &addr)) {
          ToggleBreakPoint(addr);
        } else if (2 == sscanf(str.c_str(), "%1s %99s", key, fn)) {
          ToggleBreakPoint(fn);
        } else {
          printf("invalid b cmd\n");
        }
      }
      break;
    case 'c': case 'C':
      DumpCallStacks();
      break;
    case 'd': case 'D':                 // Dump memory.
      {
        unsigned int addr = 0, count = 128;
        sscanf(str.c_str() + 1, "%x %d", &addr, &count);
        DumpMemory(addr, count);
      }
      break;
    case 'g': case 'G':                 // Go, exit break and continue run.
      Go();
      break;
    case 'l': case 'L':
      if ('g' == str[1] || 'G' == str[1]) {
        DumpGlobals();
      } else {
        DumpLocals();
      }
      break;
    case 's': case 'S':                 // Set next statement.
      {
        char key[2];
        char fn[MAX_PATH];
        unsigned int addr;
        if (3 == sscanf(str.c_str(), "%1s %99s %d", key, fn, &addr)) {
          SetNextStatement(fn, addr);
        } else if (2 == sscanf(str.c_str(), "%1s %x", key, &addr)) {
          SetNextStatement(addr);
        } else if (2 == sscanf(str.c_str(), "%1s %99s", key, fn)) {
          SetNextStatement(fn);
        } else {
          printf("invalid s cmd\n");
        }
      }
      break;
    case 't': case 'T':
      StepInto();
      break;
    case 'o': case 'O':
      StepOut();
      break;
    case 'p': case 'P':
      StepOver();
      break;
    case 'q': case 'Q':
      HandleProcessExited();
      break;
    case 'r': case 'R':
      DumpRegisters();
      break;
    default:
      ShowCommandHelp();
      break;
  }
}

void DebuggerMainLoop()
{
  while (true) {
    switch (g_dbgState) {
      case DBGS_BREAK:
        HandleUserCommand();
        break;
      case DBGS_EXIT_PROCESS:
        return;
      default:
        DebugEventLoop();
        break;
    }
  }
}

int main()
{
  STARTUPINFO si = { 0 };
  si.cb = sizeof(si);

  if (!CreateProcess(TEXT("D:\\vs.net\\testc2\\bin\\Debug\\testc2.exe"), NULL, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, NULL, NULL, &si, &g_piDbgee)) {
    printf("CreateProcess failed: %u\n", GetLastError());
    return -1;
  }
  printf("pid=%d, tid=%d\n", g_piDbgee.dwProcessId, g_piDbgee.dwThreadId);

  DebuggerMainLoop();

  return 0;
}
