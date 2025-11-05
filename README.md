
# 從頭實作一個 WIN32 Source Level 除錯器

https://good-ed.blogspot.com/2025/02/win32-source-level.html

WIN32 console mode c/c++ debugger

# 1. WIN32 除錯事件及迴圈

## 1.1 最小的 WIN32 除錯器

Windows 提供 WIN32 API 支援除錯器 (Debugger) 的實作，以下是一個最小的 WIN32 環境底下的除錯器。主要分為兩個部份。一、啟動被除錯的程式 (Debuggee)，二、除錯事件迴圈 (Debug Event Loop)。

```c++
#include <windows.h>

void main()
{
  CreateProcess(..., DEBUG_ONLY_THIS_PROCESS ,...);
  while (WaitForDebugEvent(...)) {
    if (EXIT_PROCESS) {
      break;
    }
    ContinueDebugEvent(...);
  }
}
```

以上幾行程式碼雖然不能作什麼事，但已經是一個最小的 WIN32 除錯器了。接下來我們會一步一步建立出一個 Source Level Debugger 的各個主要功能。

## 1.2 啟動被除錯的程式

使用 CreateProcess 啟動被除錯程式，被除錯程式會作為除錯器的子行程。當除錯器關閉時，被除錯程式的行程也會隨之終止。除錯器呼叫的 CreateProcess 必須加上 DEBUG_ONLY_THIS_PROCESS 參數，才能對子行程除錯。加上 CREATE_NEW_CONSOLE 參數開啟新的文字視窗，以避免被除錯程式和除錯器的輸出混雜在一起。若被除錯程式或除錯器是有 GUI 的視窗程式，則此參數是否有加上則無影響。

```c++
STARTUPINFO si = { 0 };
si.cb = sizeof(si);

if (!CreateProcess(DEBUGEE_FILE_NAME, NULL, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE, NULL, NULL, &si, &g_piDbgee)) {
  printf("CreateProcess failed: %u\n", GetLastError());
  return -1;
}
```

## 1.3 DEBUG_EVENT 結構

除錯事件 (Debug Event) 和除錯主迴圈 (Debug Main Loop) 依靠一個 DEBUG_EVENT 結構作中介，DEBUG_EVENT 結構定義在 WinBase.h，如下。

```c++
typedef struct _DEBUG_EVENT {
  DWORD dwDebugEventCode;
  DWORD dwProcessId;
  DWORD dwThreadId;
  union {
    EXCEPTION_DEBUG_INFO Exception;
    CREATE_THREAD_DEBUG_INFO CreateThread;
    CREATE_PROCESS_DEBUG_INFO CreateProcessInfo;
    EXIT_THREAD_DEBUG_INFO ExitThread;
    EXIT_PROCESS_DEBUG_INFO ExitProcess;
    LOAD_DLL_DEBUG_INFO LoadDll;
    UNLOAD_DLL_DEBUG_INFO UnloadDll;
    OUTPUT_DEBUG_STRING_INFO DebugString;
    RIP_INFO RipInfo;
  } u;
} DEBUG_EVENT
```

根據 DEBUG_EVENT 結構的 dwDebugEventCode 欄位的不同值，再對應 union 結構 u 裡的相應子結構可得到更詳盡的事件訊息。

dwDebugEventCode 欄位的可能值如下：

- `CREATE_PROCESS_DEBUG_EVENT` 被除程式行程建立時。
- `CREATE_THREAD_DEBUG_EVENT` 新建一執行緒時。
- `EXCEPTION_DEBUG_EVENT` 異常發生時。
- `EXIT_PROCESS_DEBUG_EVENT` 被除錯程式行程結束時。
- `EXIT_THREAD_DEBUG_EVENT` 執行緒結束時。
- `LOAD_DLL_DEBUG_EVENT` 載入一DLL時。
- `OUTPUT_DEBUG_STRING_EVENT` 呼叫 OutputDebugString 輸出除錯訊息時。
- `RIP_EVENT` 系統發生錯誤時。
- `UNLOAD_DLL_DEBUG_EVENT` 卸載一 DLL 時。

### 1.3.1 CREATE_PROCESS_DEBUG_EVENT

當被除錯程式以 CreateProcess 啟動後，即收到此事件。因為行程只會被建立一次，因此此事件也只會產生一次。這是行程建立後觸發的事件，因此可在此事件中作必要的初始化動作。比如載入模組除錯資訊、設定初始中斷點等。

### 1.3.2 EXIT_PROCESS_DEBUG_EVENT

當被除錯程式的行程結束時，即收到此事件。此事件對應 CREATE_PROCESS_DEBUG_EVENT 事件，且只會觸發一次。可在此事件中處理釋放資源等動作，比如釋放在除錯過程中所配置的資源等。

### 1.3.3 LOAD_DLL_DEBUG_EVENT

動態載入的 DLL，每載入一個 DLL 就會產生一個 LOAD_DLL_DEBUG_EVENT 事件。

### 1.3.4 UNLOAD_DLL_DEBUG_EVENT

對應於 LOAD_DLL_DEBUG_EVENT 事件，動態載入的 DLL 卸載時產生一個 UNLOAD_DLL_DEBUG_EVENT 事件。

### 1.3.5 CREATE_THREAD_DEBUG_EVENT

每創建一個執行緒時，會產生一個 CREATE_THREAD_DEBUG_EVENT 事件。除錯器以 CreateProcess 啟動創建一個被除錯程式子行程，並且由系統創建一個主執行緒，但並不會產生一個對應的 CREATE_THREAD_DEBUG_EVENT 事件。

### 1.3.6 EXIT_THREAD_DEBUG_EVENT

對應於 CREATE_THREAD_DEBUG_EVENT 事件，執行緒結束時時會產生一個 EXIT_THREAD_DEBUG_EVENT 事件。

### 1.3.7 OUTPUT_DEBUG_STRING_EVENT

在程式中使用 OutputDebugString 輸出的訊息會觸發此事件。

有一點要注意的是，OutputDebugStringW 是 Windows 提供的寬字元版本，其內部實作會依照當前編碼環境將字串轉換為 ASCII 格式，並呼叫 OutputDebugStringA 進行處理。然而，這僅在某些環境或編碼設置下會發生，具體情況依賴系統的字串處理設定。所以底下我們在實作 OUTPUT_DEBUG_STRING_EVENT 事件處理時，都是以 ASCII 字串處理。如果除錯器想要取得 UNICODE 字串，除錯器事件迴圈必需以 WaitForDebugEventEx 來等待並處理除錯事件。

### 1.3.8 RIP_EVENT

系統發生錯誤時，產生此事件。

### 1.3.9 EXCEPTION_DEBUG_EVENT

程式發生異常時，會觸發此事件。中斷異常，也就是程式裡的中斷點 (Break Point) 就是以此事件處理。中斷異常有兩類，一、程式斷點造成的異常，二、CPU 單步 (Single Step) 執行造成的異常。

## 1.4 除錯事件迴圈

除錯事件迴圈就好比是視窗訊息迴圈，是用來驅動除錯器核心功能。主要透過 WaitForDebugEvent 等待除錯事件發生的迴圈，事件發生時再依據 DEBUG_EVENT 結構的 dwDebugEventCode 欄位的值作分發處理。正常情況下，每處理完一個除錯事件後，再呼叫 ContinueDebugEvent 傳入 DBG_CONTINUE 參數表示己處理完畢此除錯事件，讓程式繼續執行，並等待下一個除錯事件的發生。

```c++
DEBUG_EVENT debugEvent;
while (WaitForDebugEvent(&debugEvent, INFINITE)) {
  if (DispatchDebugEvent(debugEvent)) {
    ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, DBG_CONTINUE);
  } else {
    break;
  }
}
```

# 2. Dispatch Debug Event

底下是簡單的 DispatchDebugEvent 實作，依據 DEBUG_EVENT 結構的 dwDebugEventCode 欄位的值，分發到不同事件處理函式作進一步處理。此函數若回傳 false，則會終止當前事件處理並結束當前事件循環，但並不會直接結束整個除錯過程，除非程式內部有額外邏輯處理來停止整體除錯器的運行。否則會呼叫 ContinueDebugEvent 以 DBG_CONTINUE 參數，讓被除錯程式繼續執行，並等待下一個除錯事件。

```c++
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
```

底下就主要幾個除錯事件作出處理器的骨架，後面會持續完善功能。

## 2.1 OnProcessCreated

被除錯程式行建立時觸發此事件。後面講到中斷時，會再此加入除錯資訊的相關初始化，以及建立初始軟體中斷。目前只把沒有使用到的 HANDLE 都關閉。

```c++
bool OnProcessCreated(const CREATE_PROCESS_DEBUG_INFO* pi)
{
  printf("CREATE_PROCESS_DEBUG_EVENT\n");
  CloseHandle(pi->hFile);
  CloseHandle(pi->hThread);
  CloseHandle(pi->hProcess);
  return true;
}
```

## 2.2 OnProcessExited

被除錯程式行程結束時，關閉 CreateProcess 記錄的 HANDLE。OnProcessExited 回傳 false 的原因是，此回傳值一直往上層傳遞到達除錯事件迴圈時，會讓除錯事件迴圈跳出並最終結束除錯器的執行。

```c++
bool OnProcessExited(const EXIT_PROCESS_DEBUG_INFO*)
{
  printf("EXIT_PROCESS_DEBUG_EVENT\n");
  CloseHandle(g_piDbgee.hThread);
  CloseHandle(g_piDbgee.hProcess);
  memset(&g_piDbgee, 0, sizeof(g_piDbgee));
  return false;
}
```

## 2.3 OnThreadCreated

關閉沒有使用到的 HANDLE。

```c++
bool OnThreadCreated(const CREATE_THREAD_DEBUG_INFO* pi)
{
  printf("CREATE_THREAD_DEBUG_EVENT\n");
  CloseHandle(pi->hThread);
  return true;
}
```

## 2.4 OnDllLoaded

此處只關閉沒有使用到的 HANDLE，之後會添加除錯資訊的載入。

```c++
bool OnDllLoaded(const LOAD_DLL_DEBUG_INFO* pi)
{
  printf("LOAD_DLL_DEBUG_EVENT\n");
  CloseHandle(pi->hFile);
  return true;
}
```

## 2.5 OnOutputDebugString

因為很簡單，在這裡直接實作 OnOutputDebugString 事件處理器，顯示 OutputDebugString 的輸入字串。因為除錯器行程是被除錯程式行程的父行程，所以可以合法的透過 ReadProcessMemory 從被除錯程式行程讀取放在記憶體中的 OutputDebugString 的字串資料。

```c++
bool OnOutputDebugString(const OUTPUT_DEBUG_STRING_INFO* pi)
{
  BYTE* pBuffer = (BYTE*)malloc(pi->nDebugStringLength);
  ReadProcessMemory(g_piDbgee.hProcess, pi->lpDebugStringData, pBuffer, pi->nDebugStringLength, NULL);
  printf("OUTPUT_DEBUG_STRING_EVENT: '%s'\n", pBuffer);
  free(pBuffer);
  return true;
}
```

# 3. 使用者指令交互模式

當被除錯程式執行過程中，遇到一個中斷點而停下來時，除錯器進入與使用者的指令交互模式，等待使用者輸入除錯指令。

## 3.1 Debugger Event Loop

首先稍微重整一下程式，新增一個 DebugEventLoop。DebugEventLoop 是除錯事件迴圈的包裝。

```c++
void DebugEventLoop()
{
  DEBUG_EVENT debugEvent;
  while (WaitForDebugEvent(&debugEvent, INFINITE)) {
    if (DispatchDebugEvent(debugEvent)) {
      ContinueDebugEvent(debugEvent.dwProcessId, debugEvent.dwThreadId, DBG_CONTINUE);
    } else {
      break;
    }
  }
}
```

## 3.2 除錯器狀態機

建立一個簡單的除錯器狀態機，用來表示當前除錯器狀態。底下定義 DEBUGGER_STATE：

```c++
enum DEBUGGER_STATE {
  DBGS_NONE = 0,
  DBGS_BREAK,
  DBGS_EXIT_PROCESS = 100
};
```

- `DBGS_BREAK` 表示中斷狀態，可以接收使用者指令。
- `DBGS_EXIT_PROCESS` OnProcessExited 事件發生時進入此狀態，跳出並結束除錯事件迴圈。

以後陸續添加其它狀態。

## 3.3 Debugger Main Loop

這個除錯器主迴圈很單純，當進入中斷模式時，允許使用者輸入指令作交互，例如顯示暫存器內容、檢視記憶體內容、設定軟體中斷或繼續執行等。如果是 DBGS_EXIT_PROCESS 狀態，則表示被除錯程式己結束，所以直接跳出並結束除錯器程式執行。其它的狀態都是進入除錯事件迴圈 DebugEventLoop，等待並處理除錯事件。

```c++
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
```

## 3.4 Handle User Command

處理使用者輸入，並執行輸入的指令。

```c++
void HandleUserCommand()
{
  printf("-");
  char buff[256];
  if (!fgets(buff , sizeof(buff), stdin)) {
    return;
  }

  std::string str(buff);
  str.erase(0, str.find_first_not_of(" \t\r\n")); // Trim space.
  str.erase(str.find_last_not_of(" \t\r\n") + 1);
  if (str.empty()) {
    return;
  }

  switch (str[0]) {
    case 'g': case 'G':
      Go();
      break;
  }
}
```

目前只簡單加一個繼續執行的 Go 的指令，以後再陸續添加其它指令。

## 3.5 Go

Go 很簡單，就是將除錯器狀態回復為 DBGS_NONE，並且呼叫 ContinueDebugEvent 讓被除錯程式可以繼續往下執行，並等待下一個除錯事件發生。

將除錯器狀態設為 DBGS_NONE 的原因是，這樣下一輪 DebuggerMainLoop 執行時，才能再進入除錯器事件迴圈，讓 DebugEventLoop 處理相關事件。

```c++
void Go()
{
  g_dbgState = DBGS_NONE;
  ContinueDebugEvent(g_debugEvent.dwProcessId, g_debugEvent.dwThreadId, DBG_CONTINUE);
}
```

## 3.6 OnException

在 OnException 裡判斷 ExceptionRecord.ExceptionCode 是否為一個 EXCEPTION_BREAKPOINT 造成的中斷，若是則進入 DBGS_BREAK 模式，等待使用者輸入指令。

除錯器狀態設為 DBGS_BREAK 並且回傳 false 的原因是，如此 DebugEventLoop 才能跳出，並在 DebuggerMainLoop 裡進入等待使用者輸入交互指令的循環。

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
  printf("EXCEPTION_DEBUG_EVENT. Code: 0x%x, Addr: 0x%x ", pi.ExceptionRecord.ExceptionCode, pi.ExceptionRecord.ExceptionAddress);
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode) {
    g_dbgState = DBGS_BREAK;
    return false;
  }
  return true;
}
```

# 4. 測試用的被除錯程式

## 4.1 測試程式

這是一個很簡單的用來測試用的被除錯程式，檔名叫作 test.c，是一個 WIN32 Console 程式。

```c++
#include <stdio.h>
#include <windows.h>

int add(int a, int b)
{
  int c = a + b;
  return c;
}

int main()
{
  printf("Hello world!\n");
  OutputDebugStringA("hello world!");
  __debugbreak();
  int sum = add(2, 3);
  printf("%d\n", sum);
  return 0;
}
```

因為我們還沒有實作動態設置軟體中斷點的處理，這裡先使用 __debugbreak 的呼叫。等到實作了軟體中斷之後，就可以把這個 __debugbreak 拿掉。在使用者交互模式中，可以透過除錯器動態增減中斷點，這樣可以在程式執行時根據需要來控制程式的執行流程。

__debugbreak 是 Visual Studio 提供的一個函數，用於在程式中插入中斷點。當執行到此函數時，會觸發中斷並暫停程式的執行。需要注意的是，這個方法主要在 Microsoft 編譯器中有效，其他編譯器可能會使用不同的方法來觸發斷點。除了以 __debugbreak 在程式內插入一個中斷點外，也能用內嵌組合語言 __asm {int 3} 的方式插入一個int 3，也就是 0xcc 的指令。

## 4.2 除錯測試程式

修改除錯器的 CreateProcess，直接寫死測試用被除錯程式的路徑檔名。以後功能更完善後，則可將要被除錯的程式作為參數導入除錯器。

```c++
...
if (!CreateProcess(TEXT("D:\\testc\\bin\\Debug\\testc.exe"), ...)) {
  printf("CreateProcess failed: %u\n", GetLastError());
  return -1;
}
...
```

## 4.3 執行除錯器測試

重新編譯除錯器及測試程式後，執行除錯器，可以看到如下結果。

```shell
CREATE_PROCESS_DEBUG_EVENT
LOAD_DLL_DEBUG_EVENT
LOAD_DLL_DEBUG_EVENT
LOAD_DLL_DEBUG_EVENT
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x77b157f8 (First chance)
>g
EXCEPTION_DEBUG_EVENT. Code: 0x406d1388, Addr: 0x76f99e14 (First chance)
OUTPUT_DEBUG_STRING_EVENT: 'hello world!'
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d7c (First chance)
>g
EXIT_PROCESS_DEBUG_EVENT. Code: 0
```

第一個 EXCEPTION_DEBUG_EVENT 是 WIN32 自動替被除錯程式加上的中斷點。第二個 EXCEPTION_DEBUG_EVENT 是 OutputDebugString 內部實作產生的事件，緊接其後可看到 OUTPUT_DEBUG_STRING_EVENT 事件及其輸出。第三個 EXCEPTION_DEBUG_EVENT 才是我們測試程式裡加上的 __debugbreak 呼叫所產生的中斷點。

每次遇到一個 EXCEPTION_DEBUG_EVENT 中斷下來時，我們就按一下 'g' 讓程式繼續執行下去。

# 5. 異常

## 5.1 異常種類

錯誤異常恢復執行時是從發生異常的那條指令繼續執行，而陷阱異常恢復執行時是從發生異常的那條指令的下一條指令繼續執行。

以下是 WinBase.h 中的定義。

- `EXCEPTION_ACCESS_VIOLATION` 0xC0000005
- `EXCEPTION_ARRAY_BOUNDS_EXCEEDED` 0xC000008C
- `EXCEPTION_BREAKPOINT` 0x80000003
- `EXCEPTION_DATATYPE_MISALIGNMENT` 0x80000002
- `EXCEPTION_FLT_DENORMAL_OPERAND` 0xC000008D
- `EXCEPTION_FLT_DIVIDE_BY_ZERO` 0xC000008E
- `EXCEPTION_FLT_INEXACT_RESULT` 0xC000008F
- `EXCEPTION_FLT_INVALID_OPERATION` 0xC0000090
- `EXCEPTION_FLT_OVERFLOW` 0xC0000091
- `EXCEPTION_FLT_STACK_CHECK` 0xC0000092
- `EXCEPTION_FLT_UNDERFLOW` 0xC0000093
- `EXCEPTION_ILLEGAL_INSTRUCTION` 0xC000001D
- `EXCEPTION_IN_PAGE_ERROR` 0xC0000006
- `EXCEPTION_INT_DIVIDE_BY_ZERO` 0xC0000094
- `EXCEPTION_INT_OVERFLOW` 0xC0000095
- `EXCEPTION_INVALID_DISPOSITION` 0xC0000026
- `EXCEPTION_NONCONTINUABLE_EXCEPTION` 0xC0000025
- `EXCEPTION_PRIV_INSTRUCTION` 0xC0000096
- `EXCEPTION_SINGLE_STEP` 0x80000004
- `EXCEPTION_STACK_OVERFLOW` 0xC00000FD

前面處理的 EXCEPTION_DEBUG_EVENT 除錯事件，其實就是因為觸發了一個 EXCEPTION_BREAKPOINT 異常，以及之後實作單步執行等功能時的 EXCEPTION_SINGLE_STEP 異常。當然還有其它種類異常可能發生，但是除錯器主要關心的就是這二種。

## 5.2 異常的處理

1. 程式發生異常時，被 Windows 捕獲進入內核態。
2. Windows 檢查此程式是否正被除錯中，若是則發出一個 EXCEPTION_DEBUG_EVENT 事件給除錯器，否則到 4。這是除錯器第一次收到該事件 (First Chance)。
3. 除錯器收到 EXCEPTION_DEBUG_EVENT 事件，最後如果以 DBG_CONTINUE 參數呼叫 ContinueDebugEvent，表示除錯器己處理此異常，程式繼續執行。如果是以 DBG_EXCEPTION_NOT_HANDLED 參數呼叫 ContinueDebugEvent，表示沒有處理此異常。
4. Windows 回到用戶態，尋找可處理該異常的處理器。如果找到了並處理完畢，則程式繼續執行，否則到 5。
5. Windows 回到內核態，檢查此程式是否正被除錯中，若是再發出一個 EXCEPTION_DEBUG_EVENT 事件給除錯器，否則到 7。這是除錯器第二次收到該事件 (Second Chance)。
6. 除錯器收到 EXCEPTION_DEBUG_EVENT 事件，最後如果以 DBG_CONTINUE 參數呼叫 ContinueDebugEvent，表示除錯器己處理此異常，程式繼續執行。如果是以 DBG_EXCEPTION_NOT_HANDLED 參數呼叫 ContinueDebugEvent，表示沒有處理此異常。
7. 異常沒有被處理，程式以應用程式錯誤結束執行。

First Chance 例外是指程式首次遇到異常時，除錯器有機會介入進行處理。如果除錯器選擇不處理，則該異常會被視為 Second Chance 例外，並觸發進一步的錯誤處理或終止程式。

## 5.3 OutputDebugString

前面'執行除錯器測試'一節的執行結果中，總共看到三個 EXCEPTION_DEBUG_EVENT。第一個是 Windows 送給除錯器的第一個初始化 EXCEPTION_DEBUG_EVENT 事件，第三個是我們在測試程式中加上的 __debugbreak 所產生的 EXCEPTION_DEBUG_EVENT 事件。而第二個 EXCEPTION_DEBUG_EVENT 事件則是 OutputDebugString 底層利用異常機制來實作 OUTPUT_DEBUG_STRING_EVENT 事件所產生出來的。

# 6. 除錯符號資訊

除錯器在除錯程式時，要顯示程式中斷處的程式碼、顯示變數型別及內容值等功能及操作，都需要除錯符號資訊的支援才能作到。

## 6.1 PDB 格式

除錯符號資訊有很多種格式，Visual Studio 預設使用PDB格式。

## 6.2 使用除錯資訊

Windows 提供 DbgHelp (Debug Help Library) 及 DIA (Debug Interface Access) 用來讀取除錯資訊。DIA 是基於 COM 的 API，DbgHelp 則像是一般 WIN32 API 的形式。底下所有內容都是以 DbgHelp 的 API 的方式讀取除錯資訊。

## 6.3 載入除錯資訊

每一個被除錯的程式行程都對應一個DbgHelp符號處理器，在使用前使用 SymInitialize 初始化。如下，修改前面定義的 OnProcessCreated 函數，添加載入行程模組的除錯資訊。

```c++
bool OnProcessCreated(const CREATE_PROCESS_DEBUG_INFO &pi)
{
...
  if (SymInitialize(g_piDbgee.hProcess, NULL, FALSE)) {
    printf("\tSymInitialize ok.\n");
    DWORD64 moduleAddress = SymLoadModule64(g_piDbgee.hProcess, pi.hFile, NULL, NULL, (DWORD64)pi.lpBaseOfImage, 0);
    if (0 != moduleAddress) {
      printf("\tSymLoadModule64 0x%x ok.\n", pi.lpBaseOfImage);
    } else {
      printf("\tSymLoadModule64 failed.\n");
    }
  } else {
    printf("\tSymInitialize failed.\n");
  }
...
}
```

SymInitialize 第一個參數 hProcess 是 CreateProcess 時儲存的被除錯程式行程的 HANDLE。第三個參數 fInvadeProcess 為 FALSE 時，表示 hProcess 不一定要是一個有效的 HANDLE，只要是能表示一個唯一的數即可。第二個參數 UserSearchPath 為 NULL，表示下面我們會自己以 SymLoadModule64 載入需要的模組除錯資訊。

在 OnProcessCreated 中，SymInitialize 初始化後，即以 SymLoadModule64 載入行程除錯資訊。底下在 OnDllLoaded 中，再各別載入 DLL 模組的除錯資訊。

```c++
bool OnDllLoaded(const LOAD_DLL_DEBUG_INFO &pi)
{
...
  DWORD64 moduleAddress = SymLoadModule64(g_piDbgee.hProcess, pi.hFile, NULL, NULL, (DWORD64)pi.lpBaseOfDll, 0);
  if (0 != moduleAddress) {
    printf("\tSymLoadModule64 0x%0x ok.\n", pi.lpBaseOfDll);
  } else {
    printf("\tSymLoadModule64 failed.\n");
  }
...
}
```

# 7. 顯示中斷點原始碼

## 7.1 以中斷點位址取得原始碼檔名及行號

在 OnException 中，如果異常是一個軟體中斷點 (EXCEPTION_BREAKPOINT) 或是單步執行中斷 (EXCEPTION_SINGLE_STEP)，則我們就嘗試以中斷點位址取得對應的原始碼檔名及行號。這樣每次程式中斷時，就能把中斷位置的程式碼顯示出來。

以中斷點位址取得原始碼檔名及行號是透過 SymGetLineFromAddr64 這個 API 作到。第一個參數是被除錯程式的行程 HANDLE，第二個參數是中斷點位址。第三個參數是徧移量，如果成功取得指定位址的原始碼檔名及行號，此欄位回傳此行程式碼從指定位址開始共佔多少 BYTE 指令。第四個參數是 IMAGEHLP_LINE64 結構，用來接收 API 回傳的資訊。

如果呼叫失敗，以 GetLastError 的回傳值用來判斷失敗原因。如果 GetLastError 回傳 126，表示目前模組的除錯資訊並沒有載入。如果 GetLastError 回傳 487，表示找不到指定的除錯資訊。

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
...
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode || EXCEPTION_SINGLE_STEP == pi.ExceptionRecord.ExceptionCode) {
    IMAGEHLP_LINE64 lineInfo = { 0 };
    lineInfo.SizeOfStruct = sizeof(lineInfo);
    DWORD displacement = 0;
    if (SymGetLineFromAddr64(g_piDbgee.hProcess, (DWORD64)pi.ExceptionRecord.ExceptionAddress, &displacement, &lineInfo)) {
      printf(" at %s:%d\n", lineInfo.FileName, lineInfo.LineNumber);
    } else {
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
    }
  }
...
}
```

## 7.2 顯示中斷點前後幾行程式碼

有了上一節以中斷點位址取得原始碼檔名及行號的資訊，就能夠把中斷點前後幾行的程式碼都顯示出來，增加除錯的便利性。每一行程式碼除了顯示原始碼文字外，再加上行號以及位址，便於後續要切換軟體中斷點之用。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d7c (First chance)
        EXCEPTION_BREAKPOINT.
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     __debugbreak();
0x443d7d *15     int sum = add(2, 3);
0x443d8c  16     printf("%d\n", sum);
0x443d9d  17     return 0;
0x443d9f  18 }
>
```

# 8. 顯示暫存器狀態

## 8.1 取得並顯示暫存器狀態

取得目前執行緒的暫存器的狀態是使用 GetThreadContext 這個 API，傳入的執行緒 HANDLE 是 CreateProcess 時儲存下來的。在使用 GetThreadContext 函數獲取執行緒的暫存器狀態時，請注意該執行緒必須處於暫停狀態 (例如被除錯器中斷)。若執行緒處於執行狀態，則無法獲取其暫存器狀態。為了正確使用此函數，需先確保執行緒已經停止執行。

GetThreadContext 傳入的參數除了指定要讀取暫存器狀態的執行緒 HANDLE，第二個參數 CONTEXT 的 ContextFlags 欄位必須事先指定要讀取的暫存器類型。根據 winnt.h 的註釋內容，ContextFlags 有如下幾種定義在 WinNT.h 中的旗標。

- `CONTEXT_FULL` 所有暫存器。
- `CONTEXT_CONTROL` SegSs, Rsp, SegCs, Rip, and EFlags。
- `CONTEXT_INTEGER` Rax, Rcx, Rdx, Rbx, Rbp, Rsi, Rdi, and R8-R15。
- `CONTEXT_SEGMENTS` SegDs, SegEs, SegFs, and SegGs。
- `CONTEXT_DEBUG_REGISTERS` Dr0-Dr3 and Dr6-Dr7。
- `CONTEXT_MMX_REGISTERS` Mm0/St0-Mm7/St7 and Xmm0-Xmm15。

DumpRegisters 實作如下。

```c++
void DumpRegisters()
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  printf("EAX = 0x%08x, EBX = 0x%08x, ECX = 0x%08x, EDX = 0x%08x, ESI = 0x%08x, EDI = 0x%08x\n", ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi);
  printf("EIP = 0x%08x, EBP = 0x%08x, ESP = 0x%08x, EFlags = 0x%08x\n", ctx.Eip, ctx.Ebp, ctx.Esp, ctx.EFlags);
}
```

## 8.2 修改 HandleUserCommand 支援 Dump Registers

把 DumpRegisters 添加入 HandleUserCommand，修改如下。

```c++
void HandleUserCommand()
{
...
    case 'r': case 'R':
      DumpRegisters();
      break;
...
}
```

根據顯示的中斷點的原始程式碼，再配合顯示目前暫存器狀態，已經可以讓我們觀察中斷時的程式狀態。底下是一個測試的結果。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d7c (First chance)
        EXCEPTION_BREAKPOINT.
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     __debugbreak();
0x443d7d *15     int sum = add(2, 3);
0x443d8c  16     printf("%d\n", sum);
0x443d9d  17     return 0;
0x443d9f  18 }
>r
EAX = 0x00000000, EBX = 0x00223000, ECX = 0xe4b5bfcd, EDX = 0x00000000, ESI = 0x004012c0, EDI = 0x004012c0
EIP = 0x00443d7d, EBP = 0x0019ff08, ESP = 0x0019ff04, EFlags = 0x00000246
>
```

# 9. Step Into

## 9.1 Step Into 原理

Source Level Debugger 必備的三大程式追蹤功能中，Step Into 是最簡單的。實作 Step Into 並不需要擁有軟體中斷的功能，只需要用到目前為止我們所知道的知識及己實作的功能就夠了。

Step Info 就是從目前的原始程式碼行號執行到下一行號，如果目前這一行的程式碼是一個函式呼叫，則執行進入此函式內部。簡單講就是直接執行，直到目前的原始碼檔名或行號不同為止。

整理如下。

1. 把目前的原始碼檔名及行號記錄下來。
2. 開啟 CPU 單步執行旗標。
3. 繼續執行。
4. 觸發單步執行中斷 (EXCEPTION_SINGLE_STEP) 時，檢查目前原始碼檔名及行號是否和 1 不同。若是則結束 Step Into，等待使用者下一個指令，否則到 2。

因為每次單步執行中斷後，CPU 會自己清除單步執行旗標，所以才會每次跳到 2 再重開旗標，之後再繼續執行 Step Into 流程。

```c++
void StepInto()
{
  SaveCurrSourceLine();
  SetCpuSingleStepFlag();
  Continue();
  g_dbgState = DBGS_STEP_INTO;
}
```

新增一個除錯器狀態 DBGS_STEP_INTO。

```c++
enum DEBUGGER_STATE {
  DBGS_NONE = 0,
  DBGS_BREAK,
  DBGS_STEP_INTO,
  ...
};
```

## 9.2 在 OnException 中處理 Step Into 單步執行

修改在 OnException 裡關於 EXCEPTION_SINGLE_STEP 的中斷情況。

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
  if (EXCEPTION_SINGLE_STEP == pi.ExceptionRecord.ExceptionCode && DBGS_STEP_INTO == g_dbgState && HandleStepIntoSingleStep()) {
    return true;
  }
...
}
```

HandleStepIntoSingleStep 實作也很簡單。如果目前的原始碼檔名或行號沒變化，就再作一次 Step Into。持續這個動作，直到目前的原始碼檔名或行號變化為止。

```c++
bool HandleStepIntoSingleStep()
{
  std::string fn;
  int LineNumber = 0;
  if (!IsCurrSourceLineChanged(fn, LineNumber)) {
    StepInto();
    return true;
  }
  return false;
}
```

## 9.3 修改 HandleUserCommand 支援 Step Into

最後修改 HandleUserCommand，加入 Step Into 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 't': case 'T':
      StepInto();
      break;
...
}
```

測試結果如下。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d7c (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:15
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     __debugbreak();
0x443d7d *15     int sum = add(2, 3);
0x443d8c  16     printf("%d\n", sum);
0x443d9d  17     return 0;
0x443d9f  18 }
>t
No debug info in current module.
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x443d40 (First chance)
        EXCEPTION_SINGLE_STEP. at d:\vs.net\testc2\main.cpp:5
0x443d40   1 #include <stdio.h>
0x443d40   2 #include <windows.h>
0x443d40   3
0x443d40   4 int add(int a, int b)
0x443d40 * 5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12     printf("Hello world!\n");
>
```

# 10. 顯示記憶體內容

## 10.1 Dump Memory

顯示記憶體內容是將使用 ReadProcessMemeory 讀取到的被除錯程式的記錄體內容呈現出來，協助除錯程式。

```c++
void DumpMemory(unsigned int addr, unsigned int count)
{
  // 略
}
```

## 10.2 修改 HandleUserCommand 支援 Dump Memory

修改 HandleUserCommand，加入 Dump Memory 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 'd': case 'D':                 // Dump memory.
      {
        unsigned int addr = 0, count = 128;
        sscanf(str.c_str() + 1, "%x %d", &addr, &count);
        DumpMemory(addr, count);
      }
      break;
...
}
```

測試結果如下。

```shell
>d 400000
00400000  4D 5A 90 00 03 00 00 00-04 00 00 00 FF FF 00 00  MZ..............
00400010  B8 00 00 00 00 00 00 00-40 00 00 00 00 00 00 00  ........@.......
00400020  00 00 00 00 00 00 00 00-00 00 00 00 00 00 00 00  ................
00400030  00 00 00 00 00 00 00 00-00 00 00 00 D8 00 00 00  ................
00400040  0E 1F BA 0E 00 B4 09 CD-21 B8 01 4C CD 21 54 68  ........!..L.!Th
00400050  69 73 20 70 72 6F 67 72-61 6D 20 63 61 6E 6E 6F  is program canno
00400060  74 20 62 65 20 72 75 6E-20 69 6E 20 44 4F 53 20  t be run in DOS
00400070  6D 6F 64 65 2E 0D 0D 0A-24 00 00 00 00 00 00 00  mode....$.......
```

# 11. 設置軟體中斷

## 11.1 新增軟體中斷

底下定義一個軟體中斷結構，以及用來儲存所有軟體中斷的列表。

```c++
struct BREAK_POINT
{
  std::string fn;
  int LineNumber;                       // 1-based.
  DWORD64 address;
  unsigned char saveCode;
};

std::vector<BREAK_POINT> g_bp;          // Save all break points.
```

前面在講測試程式時提到在程式裡呼叫 __debugbreak，也就是插入一個 0xcc，也就是插入一個軟體中斷 int 3。新增一個軟體中斷就是動態在一個指定原始碼檔案及行號的位置 (位址) 填入一個 0xcc。問題是如果直接在指定位址填入一個 0xcc 把原來的那個 BYTE 蓋掉，那麼當程式中斷下來執行完這條 0xcc 指令之後再繼續執行時，就有可能造成後續的程式全都產生嚴重錯誤。因為原來被蓋掉的那個 BYTE 也是一個需要被執行的指令或較長指令的一部份，少執行了這個 BYTE 就會發生問題。所以在寫入 0xcc 之前，要先把原來的那個 BYTE 存起來，以備後續恢復執行。

新增一個軟體中斷是以指定的原始碼檔名加行號，內部透過 SymGetLineFromName64 取得此原始碼檔名加行號的位址，並從此位址把原指令 BYTE 備份起來後再寫入一個 0xcc 去替換原指令 BYTE，即完成一個軟體中斷的設置。

```c++
bool AddBreakPoint(const std::string &fn, int LineNumber)
{
  LONG displacement;
  IMAGEHLP_LINE64 li = { 0 };
  li.SizeOfStruct = sizeof(li);
  if (SymGetLineFromName64(g_piDbgee.hProcess, NULL, (PSTR)fn.c_str(), LineNumber, &displacement, &li)) {
    BREAK_POINT bp;
    bp.fn = fn;
    bp.LineNumber = LineNumber;
    bp.address = li.Address;
    ReadProcessMemory(g_piDbgee.hProcess, (LPVOID)li.Address, &bp.saveCode, 1, NULL);
    g_bp.push_back(bp);
    unsigned char cc = 0xcc;
    WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)li.Address, &cc, 1, NULL); // Write 0xcc to bp address.
    printf("Add breakpoint at %s:%d(%x)\n", fn.c_str(), LineNumber, (unsigned int)li.Address);
    return true;
  }
  return false;
}
```

## 11.2 移除軟體中斷

移除一個軟體中斷的動作只需和新增的動作相反，而且更簡單，只要把事先記錄的指令 BYTE 寫回原來的位址，再把此軟體中斷的記錄清除即可。

```c++
bool RemoveBreakPoint(const std::string &fn, int LineNumber)
{
  for (size_t i = 0; i < g_bp.size(); i++) {
    const BREAK_POINT &bp = g_bp[i];
    if (bp.LineNumber == LineNumber && bp.fn == fn) {
      WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp.address, &bp.saveCode, 1, NULL); // Write back saved OP code.
      printf("Remove breakpoint at %s:%d(%x)\n", fn.c_str(), LineNumber, (unsigned int)bp.address);
      g_bp.erase(g_bp.begin() + i);
      return true;
    }
  }
 return false;
}
```

## 11.3 EXCEPTION_BREAKPOINT 中斷處理

軟體中斷 (int 3) 的處理分為兩部份。一、當 OnException 以 EXCEPTION_BREAKPOINT 中斷時的處理，二、當 OnException 以 EXCEPTION_SINGLE_STEP 中斷時的處理。

當 OnException 以 EXCEPTION_BREAKPOINT 中斷時，表示 CPU 執行到一個 0xcc，也就是 int 3 指令。我們要先判斷這個軟體中斷是我們動態以上面的 AddBreakPoint 加入的，還是使用者在程式裡以 __debugbreak 或 __asm {int 3} 內嵌等方式插入的。如果是後者，我們就不作特別處理。如果是前者，也就是這個中斷是我們動態加入的，就必需要作額外的處理。

整理如下。

1. 首先要把事先記錄的指令 BYTE 寫回原位址。
2. IP 要減 1。因為 CPU 執行了 0xcc 這條指令後 IP 就自動加 1 了，若要再執行 1 所寫回的原始指令 BYTE，IP 要退回。
3. 設定 CPU 單步執行旗標。這樣，下次單步執行中斷後，我們才有機會作第二部份的處理。
4. 繼續執行。

```c++
bool HandleSoftBreak(const BREAK_POINT* bp)
{
  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp->address, &bp->saveCode, 1, NULL);
  SetCurrIp(GetCurrIp() - 1);
  SetCpuSingleStepFlag();

  return true;
}
```

SetCurrIp 的實作如下。

```c++
void SetCurrIp(DWORD64 ip)
{
  CONTEXT ctx;
  ctx.ContextFlags = CONTEXT_CONTROL;
  GetThreadContext(g_piDbgee.hThread, &ctx);
  ctx.Eip = ip;
  SetThreadContext(g_piDbgee.hThread, &ctx);
}
```

## 11.4 EXCEPTION_SINGLE_STEP 中斷處理

單步執行中斷時，我們一樣要先判斷目前是否是上一步驟處理我們動態加入的軟體中斷後所觸發的單步執行，或者是因為 Step Into 或其它情況觸發的單步執行中斷。如果是後者，則不作額外的處理。如果是前者，也就是這個中斷是我們動態加入的，就必需要作額外的處理。

處理其實很簡單，就是再把 0xcc 寫到軟體中斷位址。因為在上一步驟，事先記錄的指令 BYTE 的寫回去後已經執行過了。再把 0xcc 寫到軟體中斷位址一次，這樣當下次程式再執行到這此處時，才可以再中斷一次。

```c++
bool HandleSoftBreakSingleStep(const BREAK_POINT* bp)
{
  unsigned char cc = 0xcc;
  WriteProcessMemory(g_piDbgee.hProcess, (LPVOID)bp->address, &cc, 1, NULL);
  return true;
}
```

## 11.5 修改 OnException 支援軟體中斷

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
...
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
...
}
```

OnBreakPoint 是把顯示原始碼等操作的一個重整包裝。

```c++
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
```

## 11.6 修改 HandleUserCommand 支援軟體中斷

修改 HandleUserCommand，加入切換軟體中斷的支援。

```c++
void HandleUserCommand()
{
...
    case 'b': case 'B':                 // Toggle break point.
      {
        char fn[MAX_PATH];
        int lineno = -1;
        if (3 == sscanf(str.c_str(), "%1s %99s %d", key, fn, &addr)) {
          ToggleBreakPoint(fn, addr);
        } else if (2 == sscanf(str.c_str(), "%1s %x", key, &addr)) {
          ToggleBreakPoint(addr);
        } else if (2 == sscanf(str.c_str(), "%1s %99s", key, fn)) {
          ToggleBreakPoint(fn);
        }
      }
      break;
...
}
```

這裡支援三種方式設置軟體中斷。一、以原始碼檔名及行號，二、以指定位址，三、以函數名稱。另外使用 ToggleBreakPoint 的方式切換軟體中斷設置，如果指定位置沒有設置軟體中斷就以 AddBreakPoint 新增一個，否則就以 RemoveBreakPoint 移除它。

```c++
bool ToggleBreakPoint(const std::string &fn, int LineNumber)
{
  if (FindBreakPoint(fn, LineNumber)) {
    return RemoveBreakPoint(fn, LineNumber);
  } else {
    return AddBreakPoint(fn, LineNumber);
  }
}
```

底下為測試結果。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d60 (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:11
0x443d40   3
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60 *11 {
0x443d64  12   printf("Hello world!\n");
0x443d71  13   OutputDebugStringA("hello world!");
0x443d7c  14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>b 443d7c
Add breakpoint at d:\vs.net\testc2\main.cpp:14(443d7c)
>g
EXCEPTION_DEBUG_EVENT. Code: 0x406d1388, Addr: 0x76f99e14 (First chance)
OUTPUT_DEBUG_STRING_EVENT: 'hello world!'
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d7c (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:14
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12   printf("Hello world!\n");
0x443d71  13   OutputDebugStringA("hello world!");
0x443d7c *14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>b 443d7c
Remove breakpoint at d:\vs.net\testc2\main.cpp:14(443d7c)
>
```

# 12. 程式啟動時在 main 中斷

## 12.1 程式啟動時在 main 中斷

現在我們已經實作了可以隨時切換軟體中斷的功能，因此測試程式裡的 __debugbreak 可以移除了。

但是現在又有個問題。如果把測試程式裡的 __debugbreak 拿掉後，程式一開始執行就會停不下來，直到程式結束。所以這裡要作個小修改，讓被除錯程式啟動時，自動斷在進入點 main。這樣我們就有機會在這個時候設置其它中斷點。

在 Windows 之下的程式的進入點總共有四種名稱，分別是：

- main
- wmain
- WinMain
- wWinMain

ToggleBreakPointAtEntryPoint 以 SymFromName 一個一個檢查是否能找到符號，若是則在此符號的位址設置一個軟體中斷。

```c++
bool ToggleBreakPointAtEntryPoint()
{
  static const char* names[] = {"main", "wmain", "WinMain", "wWinMain"};
  SYMBOL_INFO sym = {0};
  sym.SizeOfStruct = sizeof(sym);
  for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
    if (SymFromName(g_piDbgee.hProcess, (LPSTR)names[i], &sym)) {
      return ToggleBreakPoint(sym.Address);
    }
  }
  return false;
}
```

## 12.2 修改 OnProcessCreate 在程式進入點插入中斷

```c++
bool OnProcessCreated(const CREATE_PROCESS_DEBUG_INFO &pi)
{
...
  if (SymInitialize(g_piDbgee.hProcess, NULL, FALSE)) {
    ...
    ToggleBreakPointAtEntryPoint();
    ...
  }
...
}
```

底下是執行結果。

```shell
CREATE_PROCESS_DEBUG_EVENT
        SymInitialize ok.
        SymLoadModule64 0x400000 ok.
Add breakpoint at d:\vs.net\testc2\main.cpp:11(443d60)
LOAD_DLL_DEBUG_EVENT
        SymLoadModule64 0x77a00000 ok.
LOAD_DLL_DEBUG_EVENT
        SymLoadModule64 0x767d0000 ok.
LOAD_DLL_DEBUG_EVENT
        SymLoadModule64 0x76e40000 ok.
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x77b157f8 (First chance)
        EXCEPTION_BREAKPOINT. No debug info in current module.
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d60 (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:11
0x443d40   3
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60 *11 {
0x443d64  12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     int sum = add(2, 3);
0x443d8b  15     printf("%d\n", sum);
0x443d9c  16     return 0;
0x443d9e  17 }
>
```

# 13. Step Out

## 13.1 Step Out 原理

Step Out 可以結合軟體中斷和 Step Into 作出來。

原理如下。

1. 以當前 IP 找到目前所在 FUNCTION。
2. 設置一個暫時的軟體中斷在 FUNCTION 尾巴，也就是 RET 指令的位置。
3. 繼續執行。
4. 觸發 EXCEPTION_BREAKPOINT 中斷時，照原來的方式處理。
5. 移除暫時的軟體中斷。
6. 作一次 Step Into，執行到下一行。

實作如下。

```c++
bool StepOut()
{
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
```

注意這裡用一個變數 g_tmpBpAddr，把暫時的軟體中斷位址記錄下來，以便觸發暫時的軟體中斷並處理完畢後，再把它移除。

## 13.2 修改 OnException 支援 Step Out

如下修改，在開頭檢查是否是 DBGS_STEP_OUT 狀態，若是則以 HandleStepOut 處理。

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
...
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode) {
    const BREAK_POINT *bp = FindBreakPoint((DWORD64)pi.ExceptionRecord.ExceptionAddress);
    if (DBGS_STEP_OUT == g_dbgState && HandleStepOutBreak(bp)) {
      return true;
    }
  }
...
}
```

HandleStepOutBreak 的前半部如軟體中斷的處理相同，完成後再把暫時的軟體中斷移除，並作一次 Step Into。實作如下。

```c++
bool HandleStepOutBreak(const BREAK_POINT *bp)
{
  if (bp && HandleSoftBreak(bp)) {
    RemoveTempBreakPoint(g_tmpBpAddr);
    StepInto();
    return true;
  }
  return false;
}
```

## 13.3 修改 HandleUserCommand 支援 Step Out

修改 HandleUserCommand，加入 Step Out 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 'o': case 'O':
      StepOut();
      break;
...
}
```

測試結果如下。

```shell
>t
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x443d64 (First chance)
        EXCEPTION_SINGLE_STEP. at d:\vs.net\testc2\main.cpp:12
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64 *12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     int sum = add(2, 3);
0x443d8b  15     printf("%d\n", sum);
0x443d9c  16     return 0;
0x443d9e  17 }
>t
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x401040 (First chance)
        EXCEPTION_SINGLE_STEP. at f:\rtm\vctools\crt_bld\self_x86\crt\src\printf.c:49
>o
EXCEPTION_DEBUG_EVENT. Code: 0x406d1388, Addr: 0x76f99e14 (First chance)
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x443d6e (First chance)
        EXCEPTION_SINGLE_STEP. at d:\vs.net\testc2\main.cpp:12
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64 *12     printf("Hello world!\n");
0x443d71  13     OutputDebugStringA("hello world!");
0x443d7c  14     int sum = add(2, 3);
0x443d8b  15     printf("%d\n", sum);
0x443d9c  16     return 0;
0x443d9e  17 }
>
```

# 14. Step Over

## 14.1 Step Over 原理

Step Over 一次執行一行程式碼，和 Step Into 很像，只在遇到一個函式呼叫時的處理不同。Step Into 遇到一個函式呼叫時進入函式執行，Step Over 遇到函式時不會進入執行，而是把函式當成一條語句執行過去，不管這一行的程式碼有多少個函式呼叫。這也是 Step Over 會比 Step Into 及 Step Out 稍微複雜的原因。

以下是 Step Over 的原理。

1. 把目前的原始碼檔名及行號記錄下來。
2. 如果當前指令是一個函式呼叫，則在函式呼叫指令的最後插入一個暫時的軟體中斷，跳到 4。
3. 設定 CPU 單步執行旗標。
4. 繼續執行。
5. 觸發 EXCEPTION_BREAKPOINT 中斷時，照原來的方式處理。移除暫時的軟體中斷。
6. 觸發單步執行中斷 (EXCEPTION_SINGLE_STEP) 時，檢查目前原始碼檔名及行號是否和1不同。若是則結束 Step Over，等待使用者下一個指令。否則到 2。

```c++
void StepOver()
{
  SaveCurrSourceLine();
  DoStepOver();
}
```

DoStepOver 實作如下。

```c++
void DoStepOver()
{
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
```

此處新增一個除錯器狀態 DBGS_STEP_OVER。

```c++
enum DEBUGGER_STATE {
  ...
  DBGS_STEP_OUT,
  DBGS_STEP_OVER,
  ...
};
```

## 14.2 檢查 CALL 指令

檢查指令是否為一個函式呼叫，就是在檢查是否為一個 CALL 指令。CALL 指令有很多種形式，我們把所有可能的呼叫形式都找出來建表。用笨方法只要一個一個比對檢查，就能判斷當前指令是否為一個函式呼叫。

```c++
bool IsCallInstruction(DWORD64 addr, int &Length)
{
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
```

## 14.3 修改 OnException 支援 Step Over

Step Over 有用到暫時的軟體中斷及以單步執行中斷，所以必須針對兩種不同中斷處理。

```c++
bool OnException(const EXCEPTION_DEBUG_INFO &pi)
{
  if (EXCEPTION_SINGLE_STEP == pi.ExceptionRecord.ExceptionCode) {
    if (DBGS_STEP_OVER == g_dbgState && HandleStepOverSingleStep()) {
      return true;
    }
  }
  if (EXCEPTION_BREAKPOINT == pi.ExceptionRecord.ExceptionCode) {
    const BREAK_POINT *bp = FindBreakPoint((DWORD64)pi.ExceptionRecord.ExceptionAddress);
    ...
    if (DBGS_STEP_OVER == g_dbgState && HandleStepOverBreak(bp)) {
      return true;
    }
  }
...
}
```

單步中斷的處理如下。

```c++
bool HandleStepOverSingleStep()
{
  std::string fn;
  int LineNumber = 0;
  if (!IsCurrSourceLineChanged(fn, LineNumber)) {
    DoStepOver();
    return true;                        // Return true to continue.
  }

  return false;
}
```

軟體中斷的處理如下。

```c++
bool HandleStepOverBreak(const BREAK_POINT *bp)
{
  if (bp && HandleSoftBreak(bp)) {
    RemoveTempBreakPoint(g_tmpBpAddr);
    return HandleStepOverSingleStep();
  }
  return false;
}
```

## 14.4 修改 HandleUserCommand 支援 Step Over

最後修改 HandleUserCommand，加入 Step Over 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 'p': case 'P':
      StepOver();
      break;
...
}
```

底下為測試結果。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x443d64 (First chance)
        EXCEPTION_SINGLE_STEP. at d:\vs.net\testc2\main.cpp:12
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64 *12   printf("Hello world!\n");
0x443d71  13   OutputDebugStringA("hello world!");
0x443d7c  14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>p
EXCEPTION_DEBUG_EVENT. Code: 0x406d1388, Addr: 0x76f99e14 (First chance)
EXCEPTION_DEBUG_EVENT. Code: 0x80000004, Addr: 0x443d71 (First chance)
        EXCEPTION_SINGLE_STEP. at d:\vs.net\testc2\main.cpp:13
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12   printf("Hello world!\n");
0x443d71 *13   OutputDebugStringA("hello world!");
0x443d7c  14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>
```

# 15. Set Next Statement

## 15.1 Set Next Statement 原理

除了 Step Into、Step Out 及 Step Over 之外，Set Next Statement 也是一個好用的除錯指令。Set Next Statement 的使用方法和設置一個軟體中斷完全一樣，差別在於 Set Next Statement 是直接把程式 IP 強制設定為指定的位址。因此 Set Next Statement 是一個有副作用的指令，因為它直接跳過所有中間過程，直接把 IP 改成指定的位址。所以在使用時你必須知道你自己正在作什麼，因為強制改變 IP，很有可能造成程式狀態錯誤。雖然如此，有時候 Set Next Statement 還是一個有用的指令。因為可以讓你很暴力的改變程式流程，直接跳到你想要的地方繼續執行。

Set Next Statement 的實作排常直接，找到目的位址後，以 SetCurrIp 把 IP 設為此位址即可。最後再把此位址的程式碼檔案行號顯示出來。

```c++
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
```

## 15.2 修改 HandleUserCommand 支援 Set Next Statement

修改 HandleUserCommand，加入 Set Next Statement 指令的支援。

```c++
void HandleUserCommand()
{
...
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
...
}
```

底下是執行結果。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x443d60 (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:11
0x443d40   3
0x443d40   4 int add(int a, int b)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60 *11 {
0x443d64  12   printf("Hello world!\n");
0x443d71  13   OutputDebugStringA("hello world!");
0x443d7c  14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>s 443d71
Set next statement at d:\vs.net\testc2\main.cpp:13(443d71)
0x443d40   5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12   printf("Hello world!\n");
0x443d71 *13   OutputDebugStringA("hello world!");
0x443d7c  14   int sum = add(2, 3);
0x443d8b  15   printf("%d\n", sum);
0x443d9c  16   return 0;
0x443d9e  17 }
>s d:\vs.net\testc2\main.cpp 5
Set next statement at d:\vs.net\testc2\main.cpp:5(443d40)
0x443d40   1 #include <stdio.h>
0x443d40   2 #include <windows.h>
0x443d40   3
0x443d40   4 int add(int a, int b)
0x443d40 * 5 {
0x443d44   6   int c = a + b;
0x443d4d   7   return c;
0x443d50   8 }
0x443d50   9
0x443d50  10 int main()
0x443d60  11 {
0x443d64  12   printf("Hello world!\n");
>s printf
Set next statement at f:\rtm\vctools\crt_bld\self_x86\crt\src\printf.c:49(401040)
>
```

# 16. 顯示函式呼叫堆疊

程式在斷點停下來時，若可以知道程式呼叫函式的執行過程，有時對於除錯過程會很有幫助。

## 16.1 堆疊

堆疊 (Stack) 是一種先進後出 (First In Last Out) 的資料結構，在組合語言程式裡是用來作為資料暫存、函式的參數傳遞及作為函式區域變數等用途。

主要有二個操作：

- `PUSH` ESP 減少，將資料壓入堆疊。若 ESP 為 0，當資料要寫入位址為 0 的記憶體時，會觸發異常。
- `POP` 將資料從堆疊彈出，ESP 增加。同樣的，若 ESP 為 0，執行 POP 指令會引發錯誤，導致程序異常結束。

底下是個簡單的例子。一開始堆疊為空，接著推入 2，再推入 3。接著彈出 3 至 EAX，再彈出 2 到 EBX。最後堆疊又回到空的狀態。

```text
+---+   初始狀態
|   |

+---+   PUSH 2
| 2 |

+---+   PUSH 3
| 2 |
| 3 |

+---+   POP EAX
| 2 |

+---+   POP EBX
|   |
```

## 16.2 函式呼叫的過程

要了解如何解析 Call Stacks，需先了解 C/C++ 的函式呼叫是怎麼作。

底下是一個簡單的 C 語言程式。定義一個簡單的函式 add，以呼叫 add 的語句。

```c++
int add(int a, int b) {
  int c = a + b;
  return c;
}

int main() {
  ...
  int sum = add(2, 3);
  ...
}
```

呼叫 add 的程式碼初編譯成如下組合語言。

```asm
push 3
push 2
call _add
add esp, 8
```

可以看到 3 和 2 這兩個參數初推入堆疊，再以 CALL 指令呼叫 _add。請注意參數是從右往左推入堆疊，所以是先 PUSH 3，再 PUSH 2。函式呼叫完成後，下一條指令給 ESP 加 8，回復回狀。這裡加 8 是假設資料是 32 位元，所以作了兩次 PUSH，共佔 64 位元，也就是 8 個 BYTES。

編譯成組合語言後，函式 add 的樣子大概如下。

```asm
_add:
  push ebp
  move ebp, esp
  sub esp, 4
  ...
  mov esp, ebp
  pop ebp
  ret
```

一開頭先作一個把當前 EBP 推入堆疊儲存起來的動作，因為接下來就要把當前的 ESP 的值賦給 EBP 的動作。為什麼這麼作？因為我們的函式參數及區域變數都放在堆疊裡面，需要靠 EBP 來存取。為什麼不透過 ESP 存取參數及區域變數呢？因為函式執行過程中還是有可能對堆疊作任意操作而改變了 ESP 的值。而 EBP 的值一直到函式呼叫結束都不會再變化了，所以我們可以放心的以 EBP (也就是當初進入函式時的 ESP 的值) 來存取參數及區域變數。

下一條指令作了 ESP 減 4 的動作。這 4 個 BYTES 是保留變區域變數 c 的。

最後一點補充。當以 CALL 指令呼叫 add 時，CPU 會自動把 CALL 指令的下一條指令的位址 push 到堆疊。這樣當 add 執行到最後的 RET 要返回時，CPU 就可以再從堆疊裡把這個返回位址取出，賦給 EIP 後繼續執行。

```text
高位址
+-----+
|  3  | b
|  2  | a
| ret |
| ebp | <--- ebp 
|  c  | <--- esp
低位址
```

以上的堆疊狀態就是從傳遞參數，到呼叫 add，進入 add 後完成 Stack Frame 的整個初始化後的狀態。

## 16.3 Stack Frame

上面根據從傳遞參數，到呼叫 add，進入 add 後完成的 Stack Frame，只要透過 EBP 就能存取參數及區域變數。

例如要把參數 a 的值取出賦給 eax。因為參數 a 在 EBP 之上的兩個單位，所以以 EBP 加 8 為索引，即可從堆疊取出參數 a 的值。

```asm
mov eax, [ebp + 8]  ; Read a
```

例如要把區域參數 c 的值取出賦給 EBX。因為區域參數 c 在 EBP 之下的一個單位，所以以 EBP 減 4 為索引，即可從堆疊取出區域參數 c 的值。

```asm
mov ebx, [ebp - 4]  ; Read c
```

## 16.4 Dump Call Stacks 的原理

根據前面的了解，如果進入呼叫的函式裡後，能夠取得 EBP 的值，就能夠存取到函式的參數及區域變數。同樣的，假如可以取得上一層被呼叫函式的 EBP，也就能存取到上一層被呼叫函式的參數及區域變數。事實上這些都能作到，因為一進入函式，第一條指令作的就是把 EBP 存起來。也就是說，只要順著這條路徑回朔，就能作到Dump Call Stacks，以及能夠存取到每一層呼叫函式的參數及區域變數。

在 WIN32 之下可以透過 dbghelp 的 StackWalk 來實作此功能。只需要事先填好 STACKFRAME 結構中的 EIP、ESP 及 EBP，接著以一個迴圈不斷的呼叫 StackWalk 即可一路回朔 Call Stacks。

實作如下。

```c++
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
```

## 16.5 修改 HandleUserCommand 支援 Dump Call Stacks

修改 HandleUserCommand，加入 Dump Call Stacks 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 'c': case 'C':
      DumpCallStacks();
      break;
...
}
```

測試結果如下。

```shell
EXCEPTION_DEBUG_EVENT. Code: 0x80000003, Addr: 0x401040 (First chance)
        EXCEPTION_BREAKPOINT. at d:\vs.net\testc2\main.cpp:11
0x401020   3
0x401020   4 int add(int a, int b)
0x401020   5 {
0x401024   6   int c = a + b;
0x40102d   7   return c;
0x401030   8 }
0x401030   9
0x401030  10 int main()
0x401040 *11 {
0x401044  12   printf("Hello world!\n");
0x401051  13   OutputDebugStringA("hello world!");
0x40105c  14   int sum = add(2, 3);
0x40106b  15   printf("%d\n", sum);
0x40107c  16   return 0;
0x40107e  17 }
>c
d:\vs.net\testc2\main.cpp:11!main
f:\rtm\vctools\crt_bld\self_x86\crt\src\crt0.c:318!__tmainCRTStartup
f:\rtm\vctools\crt_bld\self_x86\crt\src\crt0.c:187!mainCRTStartup
0x76325d49
0x777acdeb
0x777acd71
>
```

# 17. 顯示區域變數型別及內容

## 17.1 SYMBOL_INFO 結構

在除錯器裡要顯示區域變數及全域變數的型別及其內容等，必須要有除錯符號資訊才行，除錯符號資訊記錄了這些變數的名稱、型別、位址、長度等資訊。透過這些資料就可以在除錯程式時，動態顯示當前變數的狀態。

符號資訊是由定義在 dbghelp.h 中的 SYMBOL_INFO 結構得到。

```c++
typedef struct _SYMBOL_INFO {
  ULONG   SizeOfStruct;
  ULONG   TypeIndex;
  ULONG64 Reserved[2];
  ULONG   Index;
  ULONG   Size;
  ULONG64 ModBase;
  ULONG   Flags;
  ULONG64 Value;
  ULONG64 Address;
  ULONG   Register;
  ULONG   Scope;
  ULONG   Tag;
  ULONG   NameLen;
  ULONG   MaxNameLen;
  CHAR    Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;
```

## 17.2 列出區域變數

透過 API SymEnumSymbols 的幫助，可以找到目前可見的區域變數。呼叫時傳入一個 EnumSymbolsCallback，SymEnumSymbols 會把區域變數一個一個透過這個 Callback 函式通知我們
。因為 SymEnumSymbols 會連函式都一起找出來，所以在 StaticEnumLocals 裡面，我們多作一個檢查，檢查此符號是否為一個資料類別，以此過濾掉其它的符號。

```c++
static BOOL CALLBACK StaticEnumLocals(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
  if (SymTagData == pSymInfo->Tag) {
    printf("%08x %4u %s\n", (unsigned int)pSymInfo->Address, SymbolSize, pSymInfo->Name);
  }
  return TRUE;
}

void DumpLocals()
{
  IMAGEHLP_STACK_FRAME sf = {0};
  sf.InstructionOffset = GetCurrIp();
  SymSetContext(g_piDbgee.hProcess, &sf, NULL);
  SymEnumSymbols(g_piDbgee.hProcess, 0, NULL, StaticEnumLocals, NULL);
}
```

## 17.3 讀取區域變數記憶體內容

在 StaticEnumLocals 裡，我們可以透過 pSymInfo 傳遞來的資訊取得區域變數的位址資訊。得到位址後，就能透過 ReadProcessMemeory 讀取區域變數的記憶體內容。

前面說明過，函式參數及區域變數都是存放在堆疊中，且都是相對於 EBP。其實這句話只對了一半，正確來講是在 32-bits 模式下，函式參數及區域變數都是存放在堆疊中，且都是相對於 EBP。如果是在 64-bits 模式下，函式參數及區域變數還是存放在堆疊中，只不過不是相對於 EBP，而是要看 pSymInfo->Register 這個欄位告訴我們是那個暫存器。

```c++
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
```

## 17.4 取得區域變數資料型別

要取得區域變數資料型別是一件很瑣碎複雜的工作，主要透過 SymGetTypeInfo 這個 API，函式原型如下。

```c++
BOOL
IMAGEAPI
SymGetTypeInfo(
    IN  HANDLE          hProcess,
    IN  DWORD64         ModBase,
    IN  ULONG           TypeId,
    IN  IMAGEHLP_SYMBOL_TYPE_INFO GetType,
    OUT PVOID           pInfo
    );
```

hProcess 同上面一樣是被除錯程式的 HANDLE。ModeBase 使用 pSymInfo->ModeBase 欄位，TypeId 使用 pSymInfo->TypeIndex 欄位。GetType 參數可以是如下定義的數值之一。不同的 GetType 參數，可以讀取不同的符號資訊。pInfo 的返回值及返回值的型別會根據我們傳入的 GetType 不同而不同。IMAGEHLP_SYMBOL_TYPE_INFO 定義於 dbghelp.h，如下。

```c++
typedef enum _IMAGEHLP_SYMBOL_TYPE_INFO {
    TI_GET_SYMTAG,
    TI_GET_SYMNAME,
    TI_GET_LENGTH,
    TI_GET_TYPE,
    TI_GET_TYPEID,
    TI_GET_BASETYPE,
    TI_GET_ARRAYINDEXTYPEID,
    TI_FINDCHILDREN,
    TI_GET_DATAKIND,
    TI_GET_ADDRESSOFFSET,
    TI_GET_OFFSET,
    TI_GET_VALUE,
    TI_GET_COUNT,
    TI_GET_CHILDRENCOUNT,
    TI_GET_BITPOSITION,
    TI_GET_VIRTUALBASECLASS,
    TI_GET_VIRTUALTABLESHAPEID,
    TI_GET_VIRTUALBASEPOINTEROFFSET,
    TI_GET_CLASSPARENTID,
    TI_GET_NESTED,
    TI_GET_SYMINDEX,
    TI_GET_LEXICALPARENT,
    TI_GET_ADDRESS,
    TI_GET_THISADJUST,
    TI_GET_UDTKIND,
    TI_IS_EQUIV_TO,
    TI_GET_CALLING_CONVENTION,
} IMAGEHLP_SYMBOL_TYPE_INFO;
```

這裡我們只會使用到下面這幾種，後面使用到時會再陸續說明。

- `TI_GET_SYMTAG` 符號 Tag，回傳資料型別 DWORD。
- `TI_GET_SYMNAME` 符號名稱，回傳資料型別 WCHAR*。
- `TI_GET_BASETYPE` 符號基礎型別，回傳資料型別 DWORD。
- `TI_GET_LENGTH` 符號長度，回傳資料型別 ULONG64。
- `TI_GET_TYPEID` 符號 Type Index，回傳資料型別 DWORD。
- `TI_GET_COUNT` Array 的元素個數，回傳資料型別 DWORD。

### 17.5 TI_GET_SYMTAG

首先以 TI_GET_SYMTAG 取得變數的符號的 Tag，得到符號 Tag 後我們就能得知變數的類型。同樣的符號的 Tag 種類也非常多，由以下於 cvconst.h 中的 SymTagEnum 定義。

```c++
enum SymTagEnum {
    SymTagNull,
    SymTagExe,
    SymTagCompiland,
    SymTagCompilandDetails,
    SymTagCompilandEnv,
    SymTagFunction,
    SymTagBlock,
    SymTagData,
    SymTagAnnotation,
    SymTagLabel,
    SymTagPublicSymbol,
    SymTagUDT,
    SymTagEnum,
    SymTagFunctionType,
    SymTagPointerType,
    SymTagArrayType,
    SymTagBaseType,
    SymTagTypedef,
    SymTagBaseClass,
    SymTagFriend,
    SymTagFunctionArgType,
    SymTagFuncDebugStart,
    SymTagFuncDebugEnd,
    SymTagUsingNamespace,
    SymTagVTableShape,
    SymTagVTable,
    SymTagCustom,
    SymTagThunk,
    SymTagCustomType,
    SymTagManagedType,
    SymTagDimension,
    SymTagCallSite,
    SymTagInlineSite,
    SymTagBaseInterface,
    SymTagVectorType,
    SymTagMatrixType,
    SymTagHLSLType,
    SymTagCaller,
    SymTagCallee,
    SymTagExport,
    SymTagHeapAllocationSite,
    SymTagCoffGroup,
    SymTagInlinee,
    SymTagTaggedUnionCase,
};
```

同樣的我們也只會使用到下面這幾種，其餘的我們都當作 unknown 型別不于處理。

- `SymTagBaseType` 基礎型別，如 int、long、float 等。
- `SymTagFunctionType` 函數型別。
- `SymTagPointerType` 指標型別。
- `SymTagArrayType` 陣列型別。
- `SymTagUDT` 使用者自訂義型別 (User Defined Type)。
- `SymTagEnum` Enum 型別。

底下我們定義 GetVariableTypeName 以一指定變數的 typeIndex 取得其對應的 C/C++ 型別名稱。

```c++
std::string GetVariableTypeName(ULONG typeIndex, PSYMBOL_INFO pSymInfo)
{
  DWORD symTag;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_SYMTAG, &symTag);
  switch (symTag) {
    case SymTagUDT:
    case SymTagEnum:
      return GetUdtTypeName(typeIndex, pSymInfo);
    case SymTagFunctionType:
      return "<func>";
    case SymTagPointerType:
      return GetPointTypeName(typeIndex, pSymInfo);
    case SymTagArrayType:
      return GetArrayTypeName(typeIndex, pSymInfo);
    case SymTagBaseType:
      return GetBaseTypeName(typeIndex, pSymInfo);
  }
  return "<unknown>";
}
```

SymTagFunctionType 類型的變數型別名稱我們簡單的顯示 <func>，其它的以下進一步說明。

## 17.6 讀取 SymTagBaseType 型別名稱

讀取 SymTagBaseType 的型別名稱是透用使用 TI_GET_BASETYPE 參數呼叫 SymGetTypeInfo，回傳的內容由 cvconst.h 中的 BasicType 定義。

```c++
enum BasicType {
    btNoType   = 0,
    btVoid     = 1,
    btChar     = 2,
    btWChar    = 3,
    btInt      = 6,
    btUInt     = 7,
    btFloat    = 8,
    btBCD      = 9,
    btBool     = 10,
    btLong     = 13,
    btULong    = 14,
    btCurrency = 25,
    btDate     = 26,
    btVariant  = 27,
    btComplex  = 28,
    btBit      = 29,
    btBSTR     = 30,
    btHresult  = 31,
    btChar16   = 32,  // char16_t
    btChar32   = 33,  // char32_t
    btChar8    = 34   // char8_t
};
```

同樣的我們只處理一般 C/C++ 程式會出現的型別類型。底下列出我們有處理的類型及 C/C++ 對應的型別。

- `btVoid` void。
- `btChar` char。
- `btWChar` wchar_t。
- `btBool` bool。
- `btLong` long。
- `btULong` unsigned long。

下面三個型別類型需再配合變數的長度資訊才能作正確判斷。變數資料長度由 TI_GET_LENGTH 參數呼叫 SymGetTypeInfo 取得。

- `btInt` 長度 2 時為 short，長度 4 時為 int，長度 8 時為 long long。
- `btUInt` 長度 1 時為 unsigned char，長度 2 時為 unsigned short，長度 4 時為 unsigned int，長度 8 時為 unsigned long long。
- `btFloat` 長度 4 時為 float，長度 8 時為 double。

## 17.7 讀取 SymTagPointerType 型別名稱

SymTagPointerType 類型的指標型別變數，需要再以 TI_GET_TYPEID 參數取得指標內含的真正的資料型別 typeIndex，再以這個 typeIndex 呼叫 GetVariableTypeName 並在最後加上一個 * 號，表示此是一個指標型別變數。

```c++
std::string GetPointTypeName(ULONG typeIndex, PSYMBOL_INFO pSymInfo)
{
  DWORD containTypeIndex;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_TYPEID, &containTypeIndex);
  std::string typeName = GetVariableTypeName(containTypeIndex, pSymInfo);
  return typeName + "*";
}
```

## 17.8 讀取 SymTagArrayType 型別名稱

同樣的 SymTagArrayType 類型的陣列型別變數，也需要以 TI_GET_TYPEID 參數取得陣列元素的真正型別，以及以 TI_GET_COUNT 參數取得陣列素個數。

```c++
std::string GetArrayTypeName(ULONG typeIndex, PSYMBOL_INFO pSymInfo)
{
  DWORD containTypeIndex;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_TYPEID, &containTypeIndex);
  DWORD count;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_COUNT, &count);
  std::string typeName = GetVariableTypeName(containTypeIndex, pSymInfo);
  char buff[64];
  sprintf(buff, "%u", (unsigned int)count);
  return typeName + "[" + buff + "]";
}
```

## 17.9 讀取 SymTagUDT 及 SymTagEnum 型別名稱

SymTagUDT 及 SymTagEnum 類型的變數，都是以 TI_GET_SYMNAME 取得符號名稱。

```c++
std::string GetUdtTypeName(ULONG typeIndex, PSYMBOL_INFO pSymInfo)
{
  WCHAR *pName;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_SYMNAME, &pName);
  int NeedLen = WideCharToMultiByte(CP_ACP, 0, pName, -1, NULL, 0, NULL, NULL);
  std::string buff;
  buff.resize(NeedLen);
  WideCharToMultiByte(CP_ACP, 0, pName, -1, (char*)buff.c_str(), NeedLen, NULL, NULL);
  LocalFree(pName);
  return buff;
}
```

## 17.10 顯示變數內容

GetVariableValue 的實作很類似 GetVariableTypeName，差別在於回傳變數型別名稱的地方修改成根據變數型別格式化變數的記憶體內容。為簡化起見，這裡只處理 SymTagBaseType 類型的變數內容顯示，以及 SymTagPointerType 類型的變數內容只顯示指標指向的位址。其餘的類型，一律以 16 進制顯示。

```c++
std::string GetBaseTypeValue(ULONG typeIndex, PSYMBOL_INFO pSymInfo, const char *pData)
{
  // 略
}

std::string GetVariableValue(ULONG typeIndex, PSYMBOL_INFO pSymInfo, const std::string &data)
{
  DWORD symTag;
  SymGetTypeInfo(g_piDbgee.hProcess, pSymInfo->ModBase, typeIndex, TI_GET_SYMTAG, &symTag);
  char buff[32];
  switch (symTag) {
    case SymTagBaseType:
      return GetBaseTypeValue(typeIndex, pSymInfo, data.data());
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
```

## 17.11 修改 StaticEnumLocals

StaticEnumLocals 修改如下。

```c++
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
```

## 17.12 修改 HandleUserCommand 支援 DumpLocals

最後修改 HandleUserCommand，加入 DumpLocals 指令的支援。

```c++
void HandleUserCommand()
{
...
    case 'l': case 'L':
      DumpLocals();
      break;
...
}
```

底下是測試結果。

```shell
0x449010   4 int add(int a, int b)
0x449010   5 {
0x449014   6   int c = a + b;
0x44901d   7   return c;
0x449020   8 }
0x449020   9
0x449020  10 int main(int argc, char** argv)
0x449030  11 {
0x449034 *12   printf("Hello world!\n");
0x449041  13   OutputDebugStringA("hello world!");
0x44904c  14   int sum = add(2, 3);
0x44905b  15   printf("%d\n", sum);
0x44906c  16   return 0;
0x44906e  17 }
>l
0019ff10 int argc 1
0019ff14 char** argv 0x9c18b0
0019ff04 int sum 10229936
>d 9c18b0
009C18B0  B8 18 9C 00 00 00 00 00-44 3A 5C 76 73 2E 6E 65  ........D:\vs.ne
009C18C0  74 5C 74 65 73 74 63 32-5C 62 69 6E 5C 44 65 62  t\testc2\bin\Deb
009C18D0  75 67 5C 74 65 73 74 63-32 2E 65 78 65 00 FD FD  ug\testc2.exe...
009C18E0  FD FD AB AB AB AB AB AB-AB AB EE FE EE FE EE FE  ................
009C18F0  00 00 00 00 00 00 00 00-D7 C9 74 E7 55 69 00 18  ..........t.Ui..
009C1900  90 18 9C 00 E8 19 9C 00-FC 01 46 00 75 00 00 00  ..........F.u...
009C1910  AC 00 00 00 02 00 00 00-0B 00 00 00 FD FD FD FD  ................
009C1920  08 1A 9C 00 68 1A 9C 00-D0 1A 9C 00 48 1B 9C 00  ....h.......H...
>
```

# 18. 顯示全域變數型別及內容

以前面完成的顯示區域變數型別及內容的功能為基礎，要顯示全域變數型別及內容，只需作一個小小的改動。這個改動就是：當呼叫 SymEnumSymbols 時，BaseOfDll 傳入模組的基底位址。而模組的基底位址，可以透過 SymGetModuleBase64 取得。

```c++
void DumpGlobals()
{
  DWORD64 BaseMod = SymGetModuleBase64(g_piDbgee.hProcess, GetCurrIp());
  SymEnumSymbols(g_piDbgee.hProcess, BaseMod, NULL, StaticEnumLocals, NULL);
}
```

其餘部份的修改都和 DumpLocals 類似，這裡就不再贅述。
