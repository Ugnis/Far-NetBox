#pragma once

#include "FtpListResult.h"

#include "FtpControlSocket.h"
#include "ApiLog.h"

#ifndef MPEXT_NO_ZLIB
#include <zlib.h>
#endif

class CFtpControlSocket;
class CAsyncProxySocketLayer;
class CAsyncSslSocketLayer;
#ifndef MPEXT_NO_GSS
class CAsyncGssSocketLayer;
#endif

class CTransferSocket : public CAsyncSocketEx, public CApiLog
{
public:
  CFtpListResult * m_pListResult;

public:
  CTransferSocket(CFtpControlSocket * pOwner, int nMode);
  virtual ~CTransferSocket();

public:
  int m_nInternalMessageID;
  virtual void Close();
  virtual BOOL Create(BOOL bUseSsl);
  BOOL m_bListening;
  CFile * m_pFile;
  t_transferdata m_transferdata;
  void SetActive();
  int CheckForTimeout(int delay);
#ifndef MPEXT_NO_GSS
  void UseGSS(CAsyncGssSocketLayer * pGssLayer);
#endif
#ifndef MPEXT_NO_ZLIB
  bool InitZlib(int level);
#endif

public:
  virtual void OnReceive(int nErrorCode);
  virtual void OnAccept(int nErrorCode);
  virtual void OnConnect(int nErrorCode);
  virtual void OnClose(int nErrorCode);
  virtual void OnSend(int nErrorCode);
  virtual void SetState(int nState);

protected:
  virtual int OnLayerCallback(rde::list<t_callbackMsg> & callbacks);
  int ReadDataFromFile(char * buffer, int len);
  virtual void LogSocketMessageRaw(int nMessageType, LPCTSTR pMsg);
  virtual void ConfigureSocket();
  bool Activate();
  void Start();
  
  CFtpControlSocket * m_pOwner;
  CAsyncProxySocketLayer * m_pProxyLayer;
  CAsyncSslSocketLayer * m_pSslLayer;
#ifndef MPEXT_NO_GSS
  CAsyncGssSocketLayer * m_pGssLayer;
#endif
  void UpdateStatusBar(bool forceUpdate);
  BOOL m_bSentClose;
  int m_bufferpos;
  char * m_pBuffer;
#ifndef MPEXT_NO_ZLIB
  char * m_pBuffer2; // Used by zlib transfers
#endif
  BOOL m_bCheckTimeout;
  CTime m_LastActiveTime;
  int m_nTransferState;
  int m_nMode;
  int m_nNotifyWaiting;
  bool m_bActivationPending;

  void CloseAndEnsureSendClose(int Mode);
  void EnsureSendClose(int Mode);
  void CloseOnShutDownOrError(int Mode);
  void LogError(int Error);
  void SetBuffers();

  LARGE_INTEGER m_LastUpdateTime;

#ifndef MPEXT_NO_ZLIB
  z_stream m_zlibStream;
  bool m_useZlib;
#endif
};

