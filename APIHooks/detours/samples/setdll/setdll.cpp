//////////////////////////////////////////////////////////////////////////////
//
//	Module:		setdll.exe
//	File:		setdll.cpp
//
//	Copyright:	1996-2001, Microsoft Corporation
//
//  Microsoft Research Detours Package, Version 1.5 (Build 46)
//
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <detours.h>

////////////////////////////////////////////////////////////// Error Messages.
//
VOID AssertMessage(PCSTR szMsg, PCSTR szFile, DWORD nLine)
{
	printf("ASSERT(%s) failed in %s, line %d.", szMsg, szFile, nLine);
}

#define ASSERT(x)   \
do { if (!(x)) { AssertMessage(#x, __FILE__, __LINE__); DebugBreak(); }} while (0)
	;


//////////////////////////////////////////////////////////////////////////////
//
static BOOLEAN 	s_fRemove = FALSE;
static CHAR		s_szDllPath[MAX_PATH] = "";

//////////////////////////////////////////////////////////////////////////////
//
static BOOL CALLBACK ListBywayCallback(PVOID pContext,
									   PCHAR pszFile,
									   PCHAR *ppszOutFile)
{
    (void)pContext;
    
	*ppszOutFile = pszFile;
	if (pszFile) {
		printf("    %s\n", pszFile);
	}
	return TRUE;
}

static BOOL CALLBACK ListFileCallback(PVOID pContext,
									  PCHAR pszOrigFile,
									  PCHAR pszFile,
									  PCHAR *ppszOutFile)
{
    (void)pContext;
    
	*ppszOutFile = pszFile;
	printf("    %s -> %s\n", pszOrigFile, pszFile);
	return TRUE;
}

static BOOL CALLBACK AddBywayCallback(PVOID pContext,
									  PCHAR pszFile,
									  PCHAR *ppszOutFile)
{
	PBOOL pbAddedDll = (PBOOL)pContext;
	if (!pszFile && !*pbAddedDll) {						// Add new byway.
		*pbAddedDll = TRUE;
		*ppszOutFile = s_szDllPath;
	}
	return TRUE;
}

BOOL SetFile(PCHAR pszPath)
{
	BOOL bGood = TRUE;
	HANDLE hOld = INVALID_HANDLE_VALUE;
	HANDLE hNew = INVALID_HANDLE_VALUE;
	PDETOUR_BINARY pBinary = NULL;
	
	CHAR szOrg[MAX_PATH];
	CHAR szNew[MAX_PATH];
	CHAR szOld[MAX_PATH];

	szOld[0] = '\0';
	szNew[0] = '\0';
			
	strcpy(szOrg, pszPath);
	strcpy(szNew, szOrg);
	strcat(szNew, "#");
	strcpy(szOld, szOrg);
	strcat(szOld, "~");

	printf("  %s:\n", pszPath);
	
	hOld = CreateFile(szOrg,
					  GENERIC_READ,
					  FILE_SHARE_READ,
					  NULL,
					  OPEN_EXISTING,
					  FILE_ATTRIBUTE_NORMAL,
					  NULL);
				
	if (hOld == INVALID_HANDLE_VALUE) {
		printf("Couldn't open input file: %s, error: %d\n",
			   szOrg, GetLastError());
		bGood = FALSE;
		goto end;
	}

	hNew = CreateFile(szNew,
					  GENERIC_WRITE | GENERIC_READ, 0, NULL, CREATE_ALWAYS,
					  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hNew == INVALID_HANDLE_VALUE) {
		printf("Couldn't open output file: %s, error: %d\n",
			   szNew, GetLastError());
		bGood = FALSE;
		goto end;
	}
	
	if ((pBinary = DetourBinaryOpen(hOld)) == NULL) {
		printf("DetourBinaryOpen failed: %d\n", GetLastError());
		goto end;
	}

	if (hOld != INVALID_HANDLE_VALUE) {
		CloseHandle(hOld);
		hOld = INVALID_HANDLE_VALUE;
	}
				
	{
		BOOL bAddedDll = FALSE;
		
		DetourBinaryResetImports(pBinary);
		
		if (!s_fRemove) {
			if (!DetourBinaryEditImports(pBinary,
										 &bAddedDll,
										 AddBywayCallback, NULL, NULL, NULL)) {
				printf("DetourBinaryEditImports failed: %d\n", GetLastError());
			}
		}
		
		if (!DetourBinaryEditImports(pBinary, NULL,
									 ListBywayCallback, ListFileCallback,
									 NULL, NULL)) {

			printf("DetourBinaryEditImports failed: %d\n", GetLastError());
		}
		
		if (!DetourBinaryWrite(pBinary, hNew)) {
			printf("DetourBinaryWrite failed: %d\n", GetLastError());
			bGood = FALSE;
		}
		
		DetourBinaryClose(pBinary);
		pBinary = NULL;
			
		if (hNew != INVALID_HANDLE_VALUE) {
			CloseHandle(hNew);
			hNew = INVALID_HANDLE_VALUE;
		}

		if (bGood) {
			if (!DetourBinaryBind(szNew, ".", ".")) {
				printf("Warning: Couldn't bind to tracedll: %d\n", GetLastError());
			}
			
			if (!DeleteFile(szOld)) {
				DWORD dwError = GetLastError();
				if (dwError != ERROR_FILE_NOT_FOUND) {
					printf("Warning: Couldn't delete %s: %d\n", szOld, dwError);
					bGood = FALSE;
				}
			}
			if (!MoveFile(szOrg, szOld)) {
				printf("Error: Couldn't back up %s to %s: %d\n",
					   szOrg, szOld, GetLastError());
				bGood = FALSE;
			}
			if (!MoveFile(szNew, szOrg)) {
				printf("Error: Couldn't install %s as %s: %d\n",
					   szNew, szOrg, GetLastError());
				bGood = FALSE;
			}
		}
		
		DeleteFile(szNew);
	}

	
  end:
	if (pBinary) {
		DetourBinaryClose(pBinary);
		pBinary = NULL;
	}
	if (hNew != INVALID_HANDLE_VALUE) {
		CloseHandle(hNew);
		hNew = INVALID_HANDLE_VALUE;
	}
	if (hOld != INVALID_HANDLE_VALUE) {
		CloseHandle(hOld);
		hOld = INVALID_HANDLE_VALUE;
	}
	return bGood;
}

//////////////////////////////////////////////////////////////////////////////
//
void PrintUsage(void)
{
	printf("Usage:\n"
		   "    setdll [options] binary_files\n"
		   "Options:\n"
		   "    -d:file.dll  : Add file.dll binary files\n"
		   "    -r           : Remove extra DLLs from binary files\n"
		   "    -?           : This help screen.\n");
}

//////////////////////////////////////////////////////////////////////// main.
//
int CDECL main(int argc, char **argv)
{
	BOOL fNeedHelp = FALSE;

	for (int arg = 1; arg < argc; arg++) {
		if (argv[arg][0] == '-' || argv[arg][0] == '/') {
			CHAR *argn = argv[arg] + 1;
			CHAR *argp = argn;
			while (*argp && *argp != ':')
				argp++;
			if (*argp == ':')
				*argp++ = '\0';
			
			switch (argn[0]) {

			  case 'd':									// Set DLL
			  case 'D':
				sprintf(s_szDllPath, "%s", argp);
				break;

			  case 'r':
			  case 'R':
				s_fRemove = TRUE;
				break;

			  case 'h':
			  case '?':
				fNeedHelp = TRUE;
				break;
				
			  default:
				fNeedHelp = TRUE;
				printf("Bad argument: %s:%s\n", argn, argp);
				break;
			}
		}
	}
	if (argc == 1) {
		fNeedHelp = TRUE;
	}
	if (!s_fRemove && s_szDllPath[0] == 0) {
		fNeedHelp = TRUE;
	}
	if (fNeedHelp) {
		PrintUsage();
		return 1;
	}

	if (s_fRemove) {
		printf("Removing extra DLLs from binary files.\n");
	}
	else {
		printf("Adding %hs to binary files.\n", s_szDllPath);
	}
	
	for (arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-' && argv[arg][0] != '/') {
			SetFile(argv[arg]);
		}
	}
	return 0;
}

// End of File
