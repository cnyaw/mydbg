#include "mydbg.h"

extern PROCESS_INFORMATION g_piDbgee;

SourceFiles_t g_sourceFiles;

void DisplaySourceLines_i(const std::string &fn, SourceLines_t &lines, int LineNumber)
{
  LineNumber -= 1;
  int from = (std::max)(0, LineNumber - 8);
  int to = LineNumber + 8;
  char buff[32];
  int n = strlen(itoa(to, buff, 10));   // Max digits of line number.
  for (int i = from; i < to && i < (int)lines.size(); i++) {
    LINE &ln = lines[i];
    if (0 == ln.address) {
      LONG displacement;
      IMAGEHLP_LINE64 li = { 0 };
      li.SizeOfStruct = sizeof(li);
      if (!SymGetLineFromName64(g_piDbgee.hProcess, NULL, (PSTR)fn.c_str(), i + 1, &displacement, &li)) {
        printf("SymGetLineFromName64 failed, %d\n", GetLastError());
        return;
      }
      ln.address = li.Address;
    }
    printf("0x%x %c%*d %s", (unsigned int)ln.address, i == LineNumber ? '*' : ' ', n, i + 1, ln.line.c_str());
  }
}

bool LoadSourceFile(const std::string &fn)
{
  FILE *f = fopen(fn.c_str(), "rt");
  if (!f) {
    return false;
  }
  SourceLines_t lines;
  char buff[1024];
  while (!feof(f)) {
    if (fgets(buff, sizeof(buff), f)) {
      LINE ln;
      ln.line = buff;
      ln.address = 0;
      lines.push_back(ln);
    }
  }
  fclose(f);
  g_sourceFiles[fn] = lines;
  return true;
}

bool DisplaySourceLines(const std::string &fn, int LineNumber)
{
  SourceFiles_t::iterator it = g_sourceFiles.find(fn);
  if (g_sourceFiles.end() != it) {
    DisplaySourceLines_i(fn, it->second, LineNumber);
    return true;
  } else if (LoadSourceFile(fn)) {
    return DisplaySourceLines(fn, LineNumber);
  }
  return false;
}
