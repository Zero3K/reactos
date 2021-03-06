/*
 *  FOR.C - for internal batch command.
 *
 *
 *  History:
 *
 *    16-Jul-1998 (Hans B Pufal)
 *        Started.
 *
 *    16-Jul-1998 (John P Price)
 *        Separated commands into individual files.
 *
 *    19-Jul-1998 (Hans B Pufal)
 *        Implementation of FOR.
 *
 *    27-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *        Added config.h include.
 *
 *    20-Jan-1999 (Eric Kohl)
 *        Unicode and redirection safe!
 *
 *    01-Sep-1999 (Eric Kohl)
 *        Added help text.
 *
 *    23-Feb-2001 (Carl Nettelblad <cnettel@hem.passagen.se>)
 *        Implemented preservation of echo flag. Some other for related
 *        code in other files fixed, too.
 *
 *    28-Apr-2005 (Magnus Olsen <magnus@greatlord.com>)
 *        Remove all hardcoded strings in En.rc
 */

#include "precomp.h"


/* FOR is a special command, so this function is only used for showing help now */
INT cmd_for(LPTSTR param)
{
    TRACE("cmd_for(\'%s\')\n", debugstr_aw(param));

    if (!_tcsncmp(param, _T("/?"), 2))
    {
        ConOutResPaging(TRUE, STRING_FOR_HELP1);
        return 0;
    }

    ParseErrorEx(param);
    return 1;
}

/* The stack of current FOR contexts.
 * NULL when no FOR command is active */
PFOR_CONTEXT fc = NULL;

/* Get the next element of the FOR's list */
static BOOL GetNextElement(TCHAR **pStart, TCHAR **pEnd)
{
    TCHAR *p = *pEnd;
    BOOL InQuotes = FALSE;
    while (_istspace(*p))
        p++;
    if (!*p)
        return FALSE;
    *pStart = p;
    while (*p && (InQuotes || !_istspace(*p)))
        InQuotes ^= (*p++ == _T('"'));
    *pEnd = p;
    return TRUE;
}

/* Execute a single instance of a FOR command */
/* Just run the command (variable expansion is done in DoDelayedExpansion) */
#define RunInstance(Cmd) \
    ExecuteCommandWithEcho((Cmd)->Subcommands)

/* Check if this FOR should be terminated early */
#define Exiting(Cmd) \
    /* Someone might have removed our context */ \
    (bCtrlBreak || (fc != (Cmd)->For.Context))
/* Take also GOTO jumps into account */
#define ExitingOrGoto(Cmd) \
    (Exiting(Cmd) || (bc && bc->current == NULL))

/* Read the contents of a text file into memory,
 * dynamically allocating enough space to hold it all */
static LPTSTR ReadFileContents(FILE *InputFile, TCHAR *Buffer)
{
    SIZE_T Len = 0;
    SIZE_T AllocLen = 1000;

    LPTSTR Contents = cmd_alloc(AllocLen * sizeof(TCHAR));
    if (!Contents)
    {
        WARN("Cannot allocate memory for Contents!\n");
        return NULL;
    }

    while (_fgetts(Buffer, CMDLINE_LENGTH, InputFile))
    {
        ULONG_PTR CharsRead = _tcslen(Buffer);
        while (Len + CharsRead >= AllocLen)
        {
            LPTSTR OldContents = Contents;
            Contents = cmd_realloc(Contents, (AllocLen *= 2) * sizeof(TCHAR));
            if (!Contents)
            {
                WARN("Cannot reallocate memory for Contents!\n");
                cmd_free(OldContents);
                return NULL;
            }
        }
        _tcscpy(&Contents[Len], Buffer);
        Len += CharsRead;
    }

    Contents[Len] = _T('\0');
    return Contents;
}

/* FOR /F: Parse the contents of each file */
static INT ForF(PARSED_COMMAND *Cmd, LPTSTR List, TCHAR *Buffer)
{
    LPTSTR Delims = _T(" \t");
    TCHAR Eol = _T(';');
    INT SkipLines = 0;
    DWORD Tokens = (1 << 1);
    BOOL RemainderVar = FALSE;
    TCHAR StringQuote = _T('"');
    TCHAR CommandQuote = _T('\'');
    LPTSTR Variables[32];
    TCHAR *Start, *End;
    INT i;
    INT Ret = 0;

    if (Cmd->For.Params)
    {
        TCHAR Quote = 0;
        TCHAR *Param = Cmd->For.Params;
        if (*Param == _T('"') || *Param == _T('\''))
            Quote = *Param++;

        while (*Param && *Param != Quote)
        {
            if (*Param <= _T(' '))
            {
                Param++;
            }
            else if (_tcsnicmp(Param, _T("delims="), 7) == 0)
            {
                Param += 7;
                /* delims=xxx: Specifies the list of characters that separate tokens */
                Delims = Param;
                while (*Param && *Param != Quote)
                {
                    if (*Param == _T(' '))
                    {
                        TCHAR *FirstSpace = Param;
                        Param += _tcsspn(Param, _T(" "));
                        /* Exclude trailing spaces if this is not the last parameter */
                        if (*Param && *Param != Quote)
                            *FirstSpace = _T('\0');
                        break;
                    }
                    Param++;
                }
                if (*Param == Quote)
                    *Param++ = _T('\0');
            }
            else if (_tcsnicmp(Param, _T("eol="), 4) == 0)
            {
                Param += 4;
                /* eol=c: Lines starting with this character (may be
                 * preceded by delimiters) are skipped. */
                Eol = *Param;
                if (Eol != _T('\0'))
                    Param++;
            }
            else if (_tcsnicmp(Param, _T("skip="), 5) == 0)
            {
                /* skip=n: Number of lines to skip at the beginning of each file */
                SkipLines = _tcstol(Param + 5, &Param, 0);
                if (SkipLines <= 0)
                    goto error;
            }
            else if (_tcsnicmp(Param, _T("tokens="), 7) == 0)
            {
                Param += 7;
                /* tokens=x,y,m-n: List of token numbers (must be between
                 * 1 and 31) that will be assigned into variables. */
                Tokens = 0;
                while (*Param && *Param != Quote && *Param != _T('*'))
                {
                    INT First = _tcstol(Param, &Param, 0);
                    INT Last = First;
                    if (First < 1)
                        goto error;
                    if (*Param == _T('-'))
                    {
                        /* It's a range of tokens */
                        Last = _tcstol(Param + 1, &Param, 0);
                        if (Last < First || Last > 31)
                            goto error;
                    }
                    Tokens |= (2 << Last) - (1 << First);

                    if (*Param != _T(','))
                        break;
                    Param++;
                }
                /* With an asterisk at the end, an additional variable
                 * will be created to hold the remainder of the line
                 * (after the last token specified). */
                if (*Param == _T('*'))
                {
                    RemainderVar = TRUE;
                    Param++;
                }
            }
            else if (_tcsnicmp(Param, _T("useback"), 7) == 0)
            {
                Param += 7;
                /* usebackq: Use alternate quote characters */
                StringQuote = _T('\'');
                CommandQuote = _T('`');
                /* Can be written as either "useback" or "usebackq" */
                if (_totlower(*Param) == _T('q'))
                    Param++;
            }
            else
            {
            error:
                error_syntax(Param);
                return 1;
            }
        }
    }

    /* Count how many variables will be set: one for each token,
     * plus maybe one for the remainder */
    fc->varcount = RemainderVar;
    for (i = 1; i < 32; i++)
        fc->varcount += (Tokens >> i & 1);
    fc->values = Variables;

    if (*List == StringQuote || *List == CommandQuote)
    {
        /* Treat the entire "list" as one single element */
        Start = List;
        End = &List[_tcslen(List)];
        goto single_element;
    }

    /* Loop over each file */
    End = List;
    while (!ExitingOrGoto(Cmd) && GetNextElement(&Start, &End))
    {
        FILE *InputFile;
        LPTSTR FullInput, In, NextLine;
        INT Skip;
    single_element:

        if (*Start == StringQuote && End[-1] == StringQuote)
        {
            /* Input given directly as a string */
            End[-1] = _T('\0');
            FullInput = cmd_dup(Start + 1);
        }
        else if (*Start == CommandQuote && End[-1] == CommandQuote)
        {
            /* Read input from a command */
            End[-1] = _T('\0');
            InputFile = _tpopen(Start + 1, _T("r"));
            if (!InputFile)
            {
                error_bad_command(Start + 1);
                return 1;
            }
            FullInput = ReadFileContents(InputFile, Buffer);
            _pclose(InputFile);
        }
        else
        {
            /* Read input from a file */
            TCHAR Temp = *End;
            *End = _T('\0');
            StripQuotes(Start);
            InputFile = _tfopen(Start, _T("r"));
            *End = Temp;
            if (!InputFile)
            {
                error_sfile_not_found(Start);
                return 1;
            }
            FullInput = ReadFileContents(InputFile, Buffer);
            fclose(InputFile);
        }

        if (!FullInput)
        {
            error_out_of_memory();
            return 1;
        }

        /* Loop over the input line by line */
        for (In = FullInput, Skip = SkipLines;
             !ExitingOrGoto(Cmd) && (In != NULL);
             In = NextLine)
        {
            DWORD RemainingTokens = Tokens;
            LPTSTR *CurVar = Variables;

            NextLine = _tcschr(In, _T('\n'));
            if (NextLine)
                *NextLine++ = _T('\0');

            if (--Skip >= 0)
                continue;

            /* Ignore lines where the first token starts with the eol character */
            In += _tcsspn(In, Delims);
            if (*In == Eol)
                continue;

            while ((RemainingTokens >>= 1) != 0)
            {
                /* Save pointer to this token in a variable if requested */
                if (RemainingTokens & 1)
                    *CurVar++ = In;
                /* Find end of token */
                In += _tcscspn(In, Delims);
                /* NULL-terminate it and advance to next token */
                if (*In)
                {
                    *In++ = _T('\0');
                    In += _tcsspn(In, Delims);
                }
            }
            /* Save pointer to remainder of line */
            *CurVar = In;

            /* Don't run unless the line had enough tokens to fill at least one variable */
            if (*Variables[0])
                Ret = RunInstance(Cmd);
        }

        cmd_free(FullInput);
    }

    return Ret;
}

/* FOR /L: Do a numeric loop */
static INT ForLoop(PARSED_COMMAND *Cmd, LPTSTR List, TCHAR *Buffer)
{
    enum { START, STEP, END };
    INT params[3] = { 0, 0, 0 };
    INT i;
    INT Ret = 0;
    TCHAR *Start, *End = List;

    for (i = 0; i < 3 && GetNextElement(&Start, &End); ++i)
        params[i] = _tcstol(Start, NULL, 0);

    i = params[START];
    /*
     * Windows' CMD compatibility:
     * Contrary to the other FOR-loops, FOR /L does not check
     * whether a GOTO has been done, and will continue to loop.
     */
    while (!Exiting(Cmd) &&
           (params[STEP] >= 0 ? (i <= params[END]) : (i >= params[END])))
    {
        _itot(i, Buffer, 10);
        Ret = RunInstance(Cmd);
        i += params[STEP];
    }

    return Ret;
}

/* Process a FOR in one directory. Stored in Buffer (up to BufPos) is a
 * string which is prefixed to each element of the list. In a normal FOR
 * it will be empty, but in FOR /R it will be the directory name. */
static INT ForDir(PARSED_COMMAND *Cmd, LPTSTR List, TCHAR *Buffer, TCHAR *BufPos)
{
    INT Ret = 0;
    TCHAR *Start, *End = List;

    while (!ExitingOrGoto(Cmd) && GetNextElement(&Start, &End))
    {
        if (BufPos + (End - Start) > &Buffer[CMDLINE_LENGTH])
            continue;
        memcpy(BufPos, Start, (End - Start) * sizeof(TCHAR));
        BufPos[End - Start] = _T('\0');

        if (_tcschr(BufPos, _T('?')) || _tcschr(BufPos, _T('*')))
        {
            WIN32_FIND_DATA w32fd;
            HANDLE hFind;
            TCHAR *FilePart;

            StripQuotes(BufPos);
            hFind = FindFirstFile(Buffer, &w32fd);
            if (hFind == INVALID_HANDLE_VALUE)
                continue;
            FilePart = _tcsrchr(BufPos, _T('\\'));
            FilePart = FilePart ? FilePart + 1 : BufPos;
            do
            {
                if (w32fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
                    continue;
                if (!(w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    != !(Cmd->For.Switches & FOR_DIRS))
                    continue;
                if (_tcscmp(w32fd.cFileName, _T(".")) == 0 ||
                    _tcscmp(w32fd.cFileName, _T("..")) == 0)
                    continue;
                _tcscpy(FilePart, w32fd.cFileName);
                Ret = RunInstance(Cmd);
            } while (!ExitingOrGoto(Cmd) && FindNextFile(hFind, &w32fd));
            FindClose(hFind);
        }
        else
        {
            Ret = RunInstance(Cmd);
        }
    }
    return Ret;
}

/* FOR /R: Process a FOR in each directory of a tree, recursively */
static INT ForRecursive(PARSED_COMMAND *Cmd, LPTSTR List, TCHAR *Buffer, TCHAR *BufPos)
{
    INT Ret = 0;
    HANDLE hFind;
    WIN32_FIND_DATA w32fd;

    if (BufPos[-1] != _T('\\'))
    {
        *BufPos++ = _T('\\');
        *BufPos = _T('\0');
    }

    Ret = ForDir(Cmd, List, Buffer, BufPos);

    /* NOTE (We don't apply Windows' CMD compatibility here):
     * Windows' CMD does not check whether a GOTO has been done,
     * and will continue to loop. */
    if (ExitingOrGoto(Cmd))
        return Ret;

    _tcscpy(BufPos, _T("*"));
    hFind = FindFirstFile(Buffer, &w32fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return Ret;
    do
    {
        if (!(w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (_tcscmp(w32fd.cFileName, _T(".")) == 0 ||
            _tcscmp(w32fd.cFileName, _T("..")) == 0)
            continue;
        Ret = ForRecursive(Cmd, List, Buffer, _stpcpy(BufPos, w32fd.cFileName));

    /* NOTE (We don't apply Windows' CMD compatibility here):
     * Windows' CMD does not check whether a GOTO has been done,
     * and will continue to loop. */
    } while (!ExitingOrGoto(Cmd) && FindNextFile(hFind, &w32fd));
    FindClose(hFind);

    return Ret;
}

INT
ExecuteFor(PARSED_COMMAND *Cmd)
{
    INT Ret;
    LPTSTR List;
    PFOR_CONTEXT lpNew;
    TCHAR Buffer[CMDLINE_LENGTH]; /* Buffer to hold the variable value */
    LPTSTR BufferPtr = Buffer;

    List = DoDelayedExpansion(Cmd->For.List);
    if (!List)
        return 1;

    /* Create our FOR context */
    lpNew = cmd_alloc(sizeof(FOR_CONTEXT));
    if (!lpNew)
    {
        WARN("Cannot allocate memory for lpNew!\n");
        cmd_free(List);
        return 1;
    }
    lpNew->prev = fc;
    lpNew->firstvar = Cmd->For.Variable;
    lpNew->varcount = 1;
    lpNew->values = &BufferPtr;

    Cmd->For.Context = lpNew;
    fc = lpNew;

    /* Run the extended FOR syntax only if extensions are enabled */
    if (bEnableExtensions)
    {
        if (Cmd->For.Switches & FOR_F)
        {
            Ret = ForF(Cmd, List, Buffer);
        }
        else if (Cmd->For.Switches & FOR_LOOP)
        {
            Ret = ForLoop(Cmd, List, Buffer);
        }
        else if (Cmd->For.Switches & FOR_RECURSIVE)
        {
            DWORD Len = GetFullPathName(Cmd->For.Params ? Cmd->For.Params : _T("."),
                                        MAX_PATH, Buffer, NULL);
            Ret = ForRecursive(Cmd, List, Buffer, &Buffer[Len]);
        }
        else
        {
            Ret = ForDir(Cmd, List, Buffer, Buffer);
        }
    }
    else
    {
        Ret = ForDir(Cmd, List, Buffer, Buffer);
    }

    /* Remove our context, unless someone already did that */
    if (fc == lpNew)
        fc = lpNew->prev;

    cmd_free(lpNew);
    cmd_free(List);
    return Ret;
}

/* EOF */
