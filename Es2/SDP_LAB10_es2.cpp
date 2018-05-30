/**
* Name:    Francesco
* Surname: Longo
* ID:      223428
* Lab:     10
* Ex:      2
*
* Compare N directory trees.
*
* A C program is run with N parameters.
* Each parameter indicates a relative or an absolute path to a file
* system directory tree.
*
* The program has to compare the content of all directories trees to
* decide whether they have the same content or not.
*
* Two directories trees have the same content *if and only if* all
* directory entries (files and sub-directories) have the same name
* (excluding the path leading to the root, which differ but for
* self-comparisons).
*
* Upon termination the program has to state whether all directories have
* the same content or not.
*
* Suggestions
* -----------
*
* - The main program run one "reading" thread for each directory tree
*   plus one unique "comparing" thread.
* - Each "reading" thread visits one of the directory tree.
*   It is possible to supposed that in case of equivalent directory
*   trees, all visits proceed using the same order, i.e., they deliver
*   all entries in the same order.
* - Reading threads synchronize themselves for each entry they find,
*   waiting for each other before moving to the next entry.
* - For each entry "reading" thread activate the "comparing" thread.
* - The "comparing" thread compares the name of all entries received.
*   It stops all other threads (and the program) in case the entries are
*   not equal.
*   Otherwise, it returns the control to the "reading" threads.
*
* Observations
* ------------
*
* Notice that there are at least 3 possible termination conditions
* to manage:
* - Directories are indeed equivalent.
*   This should lead to a successful termination.
* - Directories differ.
*   This can be intercepted by the comparing thread.
* - Directories are (partially) equivalent but they include a
*   different number of entries.
*   In this case it the following situation must be avoided:
*   one thread terminates its reading task whereas all other threads are
*   waiting for it.
*
**/

#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif // !UNICODE

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif // !_CRT_SECURE_NO_WARNINGS

// DEBUG set to 1 allows a verbose output
#define DEBUG 0

// include
#include <Windows.h>
#include <tchar.h>
#include <assert.h>
#include <stdio.h>

// define
#define TYPE_FILE 1
#define TYPE_DIR 2
#define TYPE_DOT 3

// name rules for semaphores
#define readerSemaphoreNameTemplate _T("lab10es02_ReaderSemaphore")
#define comparatorSemaphoreName _T("lab10es02_ComparatorSemaphore")

// typedef
// structure that will be passed as parameter to a reader thread
typedef	struct {
	volatile BOOL terminate;	// can be set by comparator
	volatile BOOL done;			// can be set by reader
	LPHANDLE readerSemaphore;	// reader waits on it
	LPHANDLE comparatorSemaphore;	// comparator waits on it
	TCHAR entry[MAX_PATH];		// the path to be compared
	LPTSTR path;				// the initial path to explore, is used also to compare entries (this part is discarded)
} READERPARAM;

// structure that will be passed as parameter to the comparator thread
typedef	struct {
	DWORD nReaders;
	LPREADERPARAM params;			// the array of parameters (shared with the readers)
	LPHANDLE comparatorSemaphore;	// a shortcut to the comparator semaphore
	LPHANDLE readersSemaphore;		// a shortcut to the readers semaphore
} COMPARATORPARAM;

typedef COMPARATORPARAM* LPCOMPARATORPARAM;
typedef READERPARAM* LPREADERPARAM;

// prototypes
BOOL visitDirectoryRecursiveAndDo(LPREADERPARAM param, LPTSTR path, DWORD level, BOOL(*toDo)(LPREADERPARAM, LPTSTR));
static DWORD FileType(LPWIN32_FIND_DATA pFileData);
DWORD WINAPI comparatorThreadFunction(LPVOID param);
DWORD WINAPI readerThreadFunction(LPVOID param);
LPTSTR getEntryPartialPath(LPREADERPARAM param);
BOOL whatToDo(LPREADERPARAM param, LPTSTR entry);
LPWSTR getErrorMessageAsString(DWORD errorCode);
int Return(int seconds, int value);

// main
INT _tmain(INT argc, LPTSTR argv[]) {
	LPHANDLE readersHandles;
	HANDLE comparatorHandle;
	DWORD nReaders;
	DWORD i;

	// parameters for reader
	LPREADERPARAM readerParams;

	// parameter for comparator
	COMPARATORPARAM comparatorParam;

	// semaphore on which the comparator waits
	HANDLE comparatorSemaphore;

	// array of semaphores: each reader waits on his own semaphore
	LPHANDLE readersSemaphore;

	TCHAR semaphoreName[MAX_PATH];

	// check arguments number
	if (argc < 2) {
		_ftprintf(stderr, _T("Usage: %s list_of_folders_to_be_visited\n"), argv[0]);
		return 1;
	}

	// how much readers are needed
	nReaders = argc - 1;

	// allocate thread handles array
	readersHandles = (LPHANDLE)malloc(nReaders * sizeof(HANDLE));
	if (readersHandles == NULL) {
		_ftprintf(stderr, _T("Error allocating space for threads\n"));
		return 2;
	}

	// allocate semaphore array 
	readersSemaphore = (LPHANDLE)malloc(nReaders * sizeof(HANDLE));
	if (readersSemaphore == NULL) {
		_ftprintf(stderr, _T("Error allocating the semaphores\n"));
		free(readersHandles);
		return 3;
	}

	// allocate parameters array 
	readerParams = (LPREADERPARAM)malloc(nReaders * sizeof(READERPARAM));
	if (readerParams == NULL) {
		_ftprintf(stderr, _T("Error allocating params\n"));
		free(readersHandles);
		free(readersSemaphore);
		return 4;
	}

	// create the comparator semaphore (counter values from 0 to nReaders, initially blocked)
	comparatorSemaphore = CreateSemaphore(NULL, 0, nReaders, comparatorSemaphoreName);
	if (comparatorSemaphore == INVALID_HANDLE_VALUE) {
		_ftprintf(stderr, _T("Error Creating the semaphores\n"));
		free(readersHandles);
		free(readersSemaphore);
		free(readerParams);
		return 5;
	}

	// for each reader
	for (i = 0; i < nReaders; i++) {
		// create a name for the semaphore
		_stprintf(semaphoreName, _T("%s%u"), readerSemaphoreNameTemplate, i);

		// create the semaphore (can have counter 0 or 1, initially 1 --> free)
		readersSemaphore[i] = CreateSemaphore(NULL, 1, 1, semaphoreName);
		if (readersSemaphore[i] == INVALID_HANDLE_VALUE) {
			// on failure, terminate everything
			_ftprintf(stderr, _T("Error Creating a semaphore\n"));
			free(readersHandles);
			free(readersSemaphore);
			free(readerParams);
			CloseHandle(comparatorSemaphore);
			return 6;
		}

		// create the parameter
		readerParams[i].comparatorSemaphore =	&comparatorSemaphore;	// a reference to the comparator semaphore (to signal on it)
		readerParams[i].readerSemaphore =		&readersSemaphore[i];	// a reference to its own semaphore (to wait on)
		readerParams[i].done =					FALSE;					// this reader has not yet ended his work
		readerParams[i].terminate =				FALSE;					// this thread must not terminate (for now)
		readerParams[i].path =					argv[i + 1];			// set the initial path
												
		// create the thread
		readersHandles[i] = CreateThread(0, 0, readerThreadFunction, &readerParams[i], 0, NULL);
		if (readersHandles[i] == INVALID_HANDLE_VALUE) {
			// on failure, terminate everything
			_ftprintf(stderr, _T("Error creating reader thread\n"));
			free(readersHandles);
			free(readersSemaphore);
			free(readerParams);
			CloseHandle(comparatorSemaphore);
			return 6;
		}
	}

	// create the parameter for the comparator
	comparatorParam.nReaders =				nReaders;				// how many readers
	comparatorParam.params =				readerParams;			// need to access the array of reader params (to see entry, done and set terminate)
	comparatorParam.comparatorSemaphore =	&comparatorSemaphore;	// a reference to the comparator semaphore (to wait on)
	comparatorParam.readersSemaphore =		readersSemaphore;		// a reference to the array of reader semaphores (to signal on them)
																
	// create the comparator thread
	comparatorHandle = CreateThread(NULL, 0, comparatorThreadFunction, &comparatorParam, 0, NULL);
	if (comparatorHandle == INVALID_HANDLE_VALUE) {
		// on failure, terminate everything
		_ftprintf(stderr, _T("Error creating comparator thread\n"));
		free(readersHandles);
		free(readersSemaphore);
		free(readerParams);
		CloseHandle(comparatorSemaphore);
		return 7;
	}

	// wait the termination of the readers
	WaitForMultipleObjects(nReaders, readersHandles, TRUE, INFINITE);
	
	for (i = 0; i < nReaders; i++) {
		// want to be sure that it is not null
		assert(readersHandles[i]);
		CloseHandle(readersHandles[i]);
	}

	free(readersHandles);

	// want to be sure that it is not null
	assert(comparatorHandle);

	// wait the termination of the comparator
	WaitForSingleObject(comparatorHandle, INFINITE);

	CloseHandle(comparatorHandle);

	free(readerParams);
	free(readersSemaphore);

	return 0;
}

DWORD WINAPI readerThreadFunction(LPVOID param) {
	LPREADERPARAM readerParam = (LPREADERPARAM)param;
	BOOL earlyTermination;

	earlyTermination = !visitDirectoryRecursiveAndDo(readerParam, readerParam->path, 0, whatToDo);

	// the visit finished
	_tprintf(_T("reader: finished. Early termination: %d\n"), earlyTermination);

	if (!earlyTermination) {
		// wait the last signal on the semaphore, to be sure that comparator is not still working on shared data
		WaitForSingleObject(*readerParam->readerSemaphore, INFINITE);
	}
	
	// this reader finished
	readerParam->done = 1;
	
	// release the semaphore, so that the comparator can continue
	ReleaseSemaphore(*readerParam->comparatorSemaphore, 1, NULL);

	// terminate te thread
	return 0;
}

DWORD WINAPI comparatorThreadFunction(LPVOID param) {
	LPCOMPARATORPARAM comparatorParam = (LPCOMPARATORPARAM)param;
	DWORD i;
	BOOL someContinue, someFinished, different;
	LPTSTR strToCompare;

	different = FALSE;
	someContinue = TRUE;

	while (!different && someContinue) {
		// reset the flags
		someContinue = someFinished = FALSE;
		different = FALSE;
		strToCompare = NULL;

		// wait nReader times on comparator semaphore (one time for each reader)
		for (i = 0; i < comparatorParam->nReaders; i++) {
			WaitForSingleObject(*comparatorParam->comparatorSemaphore, INFINITE);
		}

		// all the readers produced something, and cannot modify their params, because they are blocked on their semaphore
		for (i = 0; i < comparatorParam->nReaders; i++) {
			// check if this reader has finished
			if (comparatorParam->params[i].done) {
				someFinished = TRUE;
			} else {
				someContinue = TRUE;
				// compare the partial paths
				if (strToCompare == NULL) {
					_tprintf(_T("comparator: first entry:   %s\n"), comparatorParam->params[i].entry);

					// the first entry to compare
					strToCompare = getEntryPartialPath(&comparatorParam->params[i]);
				} else {
					_tprintf(_T("comparator: another entry: %s\n"), comparatorParam->params[i].entry);

					// this entry must be compared with the first one (only the partial path, ignoring the starting point
					if (_tcsncmp(strToCompare, getEntryPartialPath(&comparatorParam->params[i]), MAX_PATH) != 0) {
						different = TRUE;
						break;
					}
				}
			}
		}

		if (someFinished && someContinue) {
			_tprintf(_T("comparator: some finished and some continue\n"));

			different = TRUE;
		}

		if (different) {
			_tprintf(_T("comparator: different\n"));

			for (i = 0; i < comparatorParam->nReaders; i++) {
				// comunicate to each reader that it must terminate
				comparatorParam->params[i].terminate = TRUE;
			}
		}

		// signal on each reader semaphore
		for (i = 0; i < comparatorParam->nReaders; i++) {
			ReleaseSemaphore(comparatorParam->readersSemaphore[i], 1, NULL);
		}
	}

	_tprintf(_T("Comparator result: the subtrees are %s\n"), different ? _T("different") : _T("equal"));
	
	// terminate the thread
	return 0;
}

BOOL whatToDo(LPREADERPARAM param, LPTSTR entry) {
	// wait on its semaphore
	WaitForSingleObject(*param->readerSemaphore, INFINITE);

	_tprintf(_T("reader: entry: %s\n"), entry);

	// copy the entry in the shared structure
	_tcsncpy(param->entry, entry, MAX_PATH);

	// signal on the comparator semaphore
	ReleaseSemaphore(*param->comparatorSemaphore, 1, NULL);

	if (param->terminate) {
		_tprintf(_T("reader: they told me to terminate\n"));
		// false will break the recursion
		return FALSE;
	} else {
		return TRUE;
	}
}

BOOL visitDirectoryRecursiveAndDo(LPREADERPARAM param, LPTSTR path, DWORD level, BOOL(*toDo)(LPREADERPARAM, LPTSTR)) {
	LPTSTR path1 = param->path;
	WIN32_FIND_DATA findFileData;
	HANDLE hFind;
	TCHAR searchPath[MAX_PATH];
	TCHAR newPath[MAX_PATH];

	// build the searchPath string, to be able to search inside path: searchPath = path\*
	_sntprintf(searchPath, MAX_PATH - 1, _T("%s\\*"), path);
	searchPath[MAX_PATH - 1] = 0;

	// search inside path
	hFind = FindFirstFile(searchPath, &findFileData);
	if (hFind == INVALID_HANDLE_VALUE) {
		_ftprintf(stderr, _T("FindFirstFile failed. Error: %x\n"), GetLastError());
		// return true so can continue on other subtrees
		return FALSE;
	}

	do {
		// generate a new path by appending to path the name of the found entry
		_sntprintf(newPath, MAX_PATH, _T("%s\\%s"), path, findFileData.cFileName);
		if (!toDo(param, newPath)) {
			// break the recursion
			FindClose(hFind);
			return FALSE;
		}

		// check the type of file
		DWORD fType = FileType(&findFileData);

		if (fType == TYPE_FILE) {
			// this is a file
			_tprintf(_T("FILE %s "), path1);
		}

		if (fType == TYPE_DIR) {
			// this is a directory
			_tprintf(_T("DIR %s\n"), path1);

			// recursive call to the new paths
			if (!visitDirectoryRecursiveAndDo(param, newPath, level + 1, toDo)) {
				// break the recursion
				FindClose(hFind);
				return FALSE;
			}
		}
	} while (FindNextFile(hFind, &findFileData));

	FindClose(hFind);

	return TRUE;
}

static DWORD FileType(LPWIN32_FIND_DATA pFileData) {
	BOOL IsDir;
	DWORD FType;

	FType = TYPE_FILE;

	IsDir = (pFileData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	if (IsDir) {
		if (lstrcmp(pFileData->cFileName, _T(".")) == 0 || lstrcmp(pFileData->cFileName, _T("..")) == 0) {
			FType = TYPE_DOT;
		}
		else {
			FType = TYPE_DIR;
		}
	}

	return FType;
}

LPTSTR getEntryPartialPath(LPREADERPARAM param) {
	DWORD pathLen;

	pathLen = (DWORD)_tcsnlen(param->path, MAX_PATH);

	// return a pointer to the entry path after the initial path
	// "initialPath\dir1\entry.txt" --> "\dir1\entry.txt"
	return &param->entry[pathLen];
}

int Return(int seconds, int value) {
	Sleep(seconds * 1000);
	return value;
}

LPWSTR getErrorMessageAsString(DWORD errorCode) {
	LPWSTR errString = NULL;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		0,
		errorCode,
		0,
		(LPWSTR)&errString,
		0,
		0);

	return errString;
}
