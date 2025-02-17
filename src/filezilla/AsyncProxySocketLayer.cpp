// CAsyncProxySocketLayer by Tim Kosse (Tim.Kosse@gmx.de)
//                 Version 1.6 (2003-03-26)

// Feel free to use this class, as long as you don't claim that you wrote it
// and this copyright notice stays intact in the source files.
// If you use this class in commercial applications, please send a short message
// to tim.kosse@gmx.de

#include "stdafx.h"
#include "AsyncProxySocketLayer.h"
#include "atlconv.h" //Unicode<->Ascii conversion macros declared here
#include "misc/CBase64Coding.hpp"

//////////////////////////////////////////////////////////////////////
// Konstruktion/Destruktion
//////////////////////////////////////////////////////////////////////

CAsyncProxySocketLayer::CAsyncProxySocketLayer()
{
  m_nProxyOpID=0;
  m_nProxyOpState=0;
  m_pRecvBuffer=0;
  m_nRecvBufferLen=0;
  m_nRecvBufferPos=0;
  m_ProxyData.nProxyType=0;
  m_ProxyData.nProxyPort=0;
  m_nProxyPeerIp=0;
  m_nProxyPeerPort=0;
  m_pProxyPeerHost = NULL;
  m_pStrBuffer = NULL;
  m_ProxyData.pProxyHost = NULL;
  m_ProxyData.pProxyUser = NULL;
  m_ProxyData.pProxyPass = NULL;
  m_ProxyData.bUseLogon = FALSE;
  m_pProxyPeerHost = NULL;
}

CAsyncProxySocketLayer::~CAsyncProxySocketLayer()
{
  nb_free(m_ProxyData.pProxyHost);
  nb_free(m_ProxyData.pProxyUser);
  nb_free(m_ProxyData.pProxyPass);
  nb_free(m_pProxyPeerHost);
  ClearBuffer();
}

/////////////////////////////////////////////////////////////////////////////
// Member-Funktion CAsyncProxySocketLayer

void CAsyncProxySocketLayer::SetProxy(int nProxyType, const char * pProxyHost, int ProxyPort, bool bUseLogon, const char * pProxyUser, const char * pProxyPass)
{
  USES_CONVERSION;
  //Validate the parameters
  DebugAssert(!m_nProxyOpID);
  DebugAssert(pProxyHost && *pProxyHost);
  DebugAssert(ProxyPort>0);
  DebugAssert(ProxyPort<=65535);

  nb_free(m_ProxyData.pProxyHost);
  nb_free(m_ProxyData.pProxyUser);
  nb_free(m_ProxyData.pProxyPass);
  m_ProxyData.pProxyUser = NULL;
  m_ProxyData.pProxyPass = NULL;

  m_ProxyData.nProxyType = nProxyType;
  m_ProxyData.pProxyHost = static_cast<char *>(nb_calloc(1, strlen(pProxyHost)+1));
  strcpy(m_ProxyData.pProxyHost, pProxyHost);
  m_ProxyData.nProxyPort=ProxyPort;
  if (pProxyUser)
  {
    m_ProxyData.pProxyUser = static_cast<char *>(nb_calloc(1, strlen(pProxyUser)+1));
    strcpy(m_ProxyData.pProxyUser, pProxyUser);
  }
  if (pProxyPass)
  {
    m_ProxyData.pProxyPass = static_cast<char *>(nb_calloc(1, strlen(pProxyPass)+1));
    strcpy(m_ProxyData.pProxyPass, pProxyPass);
  }
  m_ProxyData.bUseLogon = bUseLogon;
}

void CAsyncProxySocketLayer::OnReceive(int nErrorCode)
{
  //Here we handle the responses from the SOCKS proxy
  if (!m_nProxyOpID)
  {
    TriggerEvent(FD_READ, nErrorCode, TRUE);
    return;
  }
  if (nErrorCode)
  {
    TriggerEvent(FD_READ, nErrorCode, TRUE);
  }
  if (!m_nProxyOpState) //We should not receive a response yet!
  {
    //Ignore it
    return;
  }
  if (m_ProxyData.nProxyType==PROXYTYPE_SOCKS4 || m_ProxyData.nProxyType==PROXYTYPE_SOCKS4A)
  {
    if (m_nProxyOpState==1) //Both for PROXYOP_CONNECT and PROXYOP_BIND
    {
      if (!m_pRecvBuffer)
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 8));
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos, 8-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==8)
      {
        if (m_pRecvBuffer[1]!=90 || m_pRecvBuffer[0]!=0)
        {
          ConnectionFailed(WSAECONNABORTED);
          return;
        }
        if (m_nProxyOpID==PROXYOP_CONNECT)
        {
          //OK, we are connected with the remote server
          ConnectionEstablished();
          return;
        }
        else
        {
          //Listen socket created
          m_nProxyOpState++;
          unsigned long ip;
          int port = 0;
          memcpy(&ip,&m_pRecvBuffer[4],4);
          if (!ip)
          { 
            //No IP return, use the IP of the proxy server
            SOCKADDR SockAddr;
            memset(&SockAddr,0,sizeof(SockAddr));
            int SockAddrLen=sizeof(SockAddr);
            if (GetPeerName(&SockAddr, &SockAddrLen ))
            {
              ip=((LPSOCKADDR_IN)&SockAddr)->sin_addr.S_un.S_addr;
            }
            else
            {
              ConnectionFailed(WSAECONNABORTED);
              return;
            }
          }
          memcpy(&port,&m_pRecvBuffer[2],2);
          t_ListenSocketCreatedStruct data;
          data.ip=ip;
          data.nPort=port;
          DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYSTATUS_LISTENSOCKETCREATED, (intptr_t)&data);
        }
        ClearBuffer();
      }
    }
    else if (m_nProxyOpID==PROXYOP_LISTEN)
    {
      if (!m_pRecvBuffer)
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 8));
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos,8-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==8)
      {
        if (m_pRecvBuffer[1]!=90 || m_pRecvBuffer[0]!=0)
        {
          ConnectionFailed(WSAECONNABORTED);
          return;
        }
        //Connection to remote server established
        ConnectionEstablished();
      }
    }
  }
  else if (m_ProxyData.nProxyType==PROXYTYPE_SOCKS5)
  {
    if (m_nProxyOpState==1) //Get respone to initialization message
    {
      if (!m_pRecvBuffer)
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 2));
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos,2-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==2)
      {
        if (m_pRecvBuffer[0]!=5)
        {
          ConnectionFailed(WSAECONNABORTED);
          return;
        }
        if (m_pRecvBuffer[1])
        { 
          //Auth needed
          if (m_pRecvBuffer[1]!=2)
          { 
            //Unknown auth type
            DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_AUTHTYPEUNKNOWN, 0);
            if (m_nProxyOpID==PROXYOP_CONNECT)
              TriggerEvent(FD_CONNECT, WSAECONNABORTED, TRUE);
            else
              TriggerEvent(FD_ACCEPT, WSAECONNABORTED, TRUE);
            Reset();
            ClearBuffer();
            return;
          }

          if (!m_ProxyData.bUseLogon)
          {
            DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_AUTHNOLOGON, 0);
            if (m_nProxyOpID==PROXYOP_CONNECT)
              TriggerEvent(FD_CONNECT, WSAECONNABORTED, TRUE);
            else
              TriggerEvent(FD_ACCEPT, WSAECONNABORTED, TRUE);
            Reset();
            ClearBuffer();
            return;
          }
          //Send authentication
          LPCSTR lpszAsciiUser = m_ProxyData.pProxyUser;
          LPCSTR lpszAsciiPass = m_ProxyData.pProxyPass;
          DebugAssert(strlen(lpszAsciiUser)<=255);
          DebugAssert(strlen(lpszAsciiPass)<=255);
          uint8_t *buffer = static_cast<uint8_t *>(nb_calloc(1, 3 + (lpszAsciiUser?strlen(lpszAsciiUser):0) + (lpszAsciiPass?strlen(lpszAsciiPass):0) + 1));
          sprintf((char *)buffer, "  %s %s", lpszAsciiUser?lpszAsciiUser:"", lpszAsciiPass?lpszAsciiPass:"");
          buffer[0]=1;
          buffer[1]=static_cast<uint8_t>(strlen(lpszAsciiUser));
          buffer[2+strlen(lpszAsciiUser)]=static_cast<uint8_t>(strlen(lpszAsciiPass));
          intptr_t len=3+strlen(lpszAsciiUser)+strlen(lpszAsciiPass);
          int res=SendNext(buffer,len);
          nb_free(buffer);
          if (res==SOCKET_ERROR || res<len)
          {
            if ((WSAGetLastError()!=WSAEWOULDBLOCK) || res<len)
            {
              ConnectionFailed(WSAGetLastError());
              return;
            }
          }
          ClearBuffer();
          m_nProxyOpState++;
          return;
        }
      }
      //No auth needed
      //Send connection request
      const char *lpszAsciiHost = m_pProxyPeerHost?m_pProxyPeerHost:"";
      char *command=static_cast<char *>(nb_calloc(1, 10+strlen(lpszAsciiHost)+1));
      memset(command,0,10+strlen(lpszAsciiHost)+1);
      command[0]=5;
      command[1]=(m_nProxyOpID==PROXYOP_CONNECT)?1:2;
      command[2]=0;
      command[3]=m_nProxyPeerIp?1:3;
      int len=4;
      if (m_nProxyPeerIp)
      {
        memcpy(&command[len],&m_nProxyPeerIp,4);
        len+=4;
      }
      else
      {
        command[len]=strlen(lpszAsciiHost);
        strcpy(&command[len+1], lpszAsciiHost);
        len += strlen(lpszAsciiHost) + 1;
      }
      memcpy(&command[len], &m_nProxyPeerPort, 2);
      len+=2;
      int res=SendNext(command,len);
      nb_free(command);
      if (res==SOCKET_ERROR || res<len)
      {
        if ( ( WSAGetLastError()!=WSAEWOULDBLOCK) || res<len)
        {
          ConnectionFailed(WSAGetLastError());
          return;
        }
      }
      m_nProxyOpState+=2;
      ClearBuffer();
      return;
    }
    else if (m_nProxyOpState==2)
    {
      //Response to the auth request
      if (!m_pRecvBuffer)
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 2));
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos, 2-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==2)
      {
        if (m_pRecvBuffer[1]!=0)
        {
          DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_AUTHFAILED, 0);
          if (m_nProxyOpID==PROXYOP_CONNECT)
            TriggerEvent(FD_CONNECT, WSAECONNABORTED, TRUE);
          else
            TriggerEvent(FD_ACCEPT, WSAECONNABORTED, TRUE);
          Reset();
          ClearBuffer();
          return;
        }
        const char * lpszAsciiHost = m_pProxyPeerHost?m_pProxyPeerHost:"";
        char *command = static_cast<char *>(nb_calloc(1, 10+strlen(lpszAsciiHost)+1));
        memset(command,0,10+strlen(lpszAsciiHost)+1);
        command[0]=5;
        command[1]=(m_nProxyOpID==PROXYOP_CONNECT)?1:2;
        command[2]=0;
        command[3]=m_nProxyPeerIp?1:3;
        int len=4;
        if (m_nProxyPeerIp)
        {
          memcpy(&command[len],&m_nProxyPeerIp,4);
          len+=4;
        }
        else
        {
          command[len]=strlen(lpszAsciiHost);
          strcpy(&command[len+1],lpszAsciiHost);
          len+=strlen(lpszAsciiHost)+1;
        }
        memcpy(&command[len],&m_nProxyPeerPort,2);
        len+=2;
        int res=SendNext(command,len);
        nb_free(command);
        if (res==SOCKET_ERROR || res<len)
        {
          if ((WSAGetLastError()!=WSAEWOULDBLOCK) || res<len)
          {
            ConnectionFailed(WSAGetLastError());
            return;
          }
        }
        m_nProxyOpState++;
        ClearBuffer();
        return;
      }
    }
    else if (m_nProxyOpState==3)
    {
      //Response to the connection request
      if (!m_pRecvBuffer)
      {
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 10));
        m_nRecvBufferLen=5;
      }
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos,m_nRecvBufferLen-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==m_nRecvBufferLen)
      {
        //Check for errors
        if (m_pRecvBuffer[1]!=0 || m_pRecvBuffer[0]!=5)
        {
          ConnectionFailed(WSAECONNABORTED);
          return;
        }
        if (m_nRecvBufferLen==5)
        { 
          //Check which kind of address the response contains
          if (m_pRecvBuffer[3]==1)
            m_nRecvBufferLen=10;
          else
          {
            char *tmp=static_cast<char *>(nb_calloc(1, m_nRecvBufferLen+=m_pRecvBuffer[4]+2));
            memcpy(tmp,m_pRecvBuffer,5);
            nb_free(m_pRecvBuffer);
            m_pRecvBuffer=tmp;
            m_nRecvBufferLen+=m_pRecvBuffer[4]+2;
          }
          return;
        }

        if (m_nProxyOpID==PROXYOP_CONNECT)
        {
          //OK, we are connected with the remote server
          ConnectionEstablished();
        }
        else
        {
          //Listen socket created
          m_nProxyOpState++;
          unsigned long ip;
          unsigned short port;
          DebugAssert(m_pRecvBuffer[3]==1);
          memcpy(&ip, &m_pRecvBuffer[4], 4);
          memcpy(&port, &m_pRecvBuffer[8], 2);
          t_ListenSocketCreatedStruct data;
          data.ip=ip;
          data.nPort=port;
          DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYSTATUS_LISTENSOCKETCREATED, (intptr_t)&data);
        }
        ClearBuffer();
      }
    }
    else if (m_nProxyOpState==4)
    {
      DebugAssert(m_nProxyOpID == PROXYOP_LISTEN);
      if (!m_pRecvBuffer)
        m_pRecvBuffer=static_cast<char *>(nb_calloc(1, 10));
      int numread=ReceiveNext(m_pRecvBuffer+m_nRecvBufferPos,10-m_nRecvBufferPos);
      if (numread==SOCKET_ERROR)
      {
        if (WSAGetLastError()!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(WSAGetLastError());
        }
        return;
      }
      m_nRecvBufferPos+=numread;
      if (m_nRecvBufferPos==10)
      {
        if (m_pRecvBuffer[1]!=0)
        {
          ConnectionFailed(WSAECONNABORTED);
          return;
        }
        //Connection to remote server established
        ConnectionEstablished();
      }
    }
  }
  if (m_ProxyData.nProxyType==PROXYTYPE_HTTP11)
  {
    DebugAssert(m_nProxyOpID==PROXYOP_CONNECT);
    char buffer[9];
    memset(buffer, 0, sizeof(buffer));
    for(;;)
    {
      int numread = ReceiveNext(buffer, m_pStrBuffer?1:8);
      if (numread==SOCKET_ERROR)
      {
        int nError=WSAGetLastError();
        if (nError!=WSAEWOULDBLOCK)
        {
          ConnectionFailed(nError);
        }
        return;
      }
      //Response begins with HTTP/
      if (!m_pStrBuffer)
      {
        m_pStrBuffer = static_cast<char *>(nb_calloc(1, strlen(buffer) + 1));
        strcpy(m_pStrBuffer, buffer);
      }
      else
      {
        char *tmp = m_pStrBuffer;
        m_pStrBuffer = static_cast<char *>(nb_calloc(1, strlen(tmp) + strlen(buffer) + 1));
        strcpy(m_pStrBuffer, tmp);
        strcpy(m_pStrBuffer + strlen(tmp), buffer);
        nb_free(tmp);
      }
      memset(buffer, 0, 9);
      const char start[] = "HTTP/";
      if (memcmp(start, m_pStrBuffer, (strlen(start)>strlen(m_pStrBuffer)) ? strlen(m_pStrBuffer) : strlen(start)))
      {
        char* str = static_cast<char *>(nb_calloc(1, strlen("No valid HTTP response") + 1));
        strcpy(str, "No valid HTTP response");
        ConnectionFailed(WSAECONNABORTED, str);
        return;
      }
      char *pos = strstr(m_pStrBuffer, "\r\n");
      if (pos)
      {
        char *pos2 = strstr(m_pStrBuffer, " ");
        if (!pos2 || *(pos2+1)!='2' || pos2>pos)
        {
          char *tmp = static_cast<char *>(nb_calloc(1, pos-m_pStrBuffer + 1));
          tmp[pos-m_pStrBuffer] = 0;
          strncpy(tmp, m_pStrBuffer, pos-m_pStrBuffer);
          ConnectionFailed(WSAECONNABORTED, tmp);
          return;
        }
      }
      if (strlen(m_pStrBuffer)>3 && !memcmp(m_pStrBuffer+strlen(m_pStrBuffer)-4, "\r\n\r\n", 4)) //End of the HTTP header
      {
        ConnectionEstablished();
        return;
      }
    }
  }
}

void CAsyncProxySocketLayer::ConnectionFailed(int nErrorCode, char * Str)
{
  DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_REQUESTFAILED, 0, Str);
  if (m_nProxyOpID == PROXYOP_CONNECT)
  {
    TriggerEvent(FD_CONNECT, nErrorCode, TRUE);
  }
  else
  {
    TriggerEvent(FD_ACCEPT, nErrorCode, TRUE);
  }
  Reset();
  ClearBuffer();
}

void CAsyncProxySocketLayer::ConnectionEstablished()
{
  int Event = (m_nProxyOpID == PROXYOP_CONNECT) ? FD_CONNECT : FD_ACCEPT;
  ClearBuffer();
  Reset();

  TriggerEvent(Event, 0, TRUE);
  TriggerEvent(FD_READ, 0, TRUE);
  TriggerEvent(FD_WRITE, 0, TRUE);
}

BOOL CAsyncProxySocketLayer::Connect( LPCTSTR lpszHostAddress, UINT nHostPort )
{
  if (!m_ProxyData.nProxyType)
    //Connect normally because there is no proxy
    return ConnectNext(lpszHostAddress, nHostPort);

  USES_CONVERSION;

  //Translate the host address
  DebugAssert(lpszHostAddress != NULL);

  if (m_ProxyData.nProxyType != PROXYTYPE_SOCKS4)
  {
    // We can send hostname to proxy, no need to resolve it
    
    //Connect to proxy server
    BOOL res = ConnectNext(A2CT(m_ProxyData.pProxyHost), m_ProxyData.nProxyPort);
    if (!res)
    {
      if (WSAGetLastError() != WSAEWOULDBLOCK)
      {
        DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_NOCONN, 0);
        return FALSE;
      }
    }
    m_nProxyPeerPort = htons((u_short)nHostPort);
    m_nProxyPeerIp = 0;
    nb_free(m_pProxyPeerHost);
    m_pProxyPeerHost = static_cast<char *>(nb_calloc(1, _tcslen(lpszHostAddress)+1));
    strcpy(m_pProxyPeerHost, T2CA(lpszHostAddress));
    m_nProxyOpID=PROXYOP_CONNECT;
    return TRUE;
  }

  SOCKADDR_IN sockAddr;
  memset(&sockAddr,0,sizeof(sockAddr));

  LPCSTR lpszAscii = T2A((LPTSTR)lpszHostAddress);
  sockAddr.sin_family = AF_INET;
  sockAddr.sin_addr.s_addr = inet_addr(lpszAscii);

  if (sockAddr.sin_addr.s_addr == INADDR_NONE)
  {
    LPHOSTENT lphost;
    lphost = gethostbyname(lpszAscii);
    if (lphost != NULL)
      sockAddr.sin_addr.s_addr = ((LPIN_ADDR)lphost->h_addr)->s_addr;
    else
    {
      //Can't resolve hostname
      DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_CANTRESOLVEHOST, 0);
      WSASetLastError(WSAEINVAL);
      return FALSE;
    }
  }

  sockAddr.sin_port = htons((u_short)nHostPort);
  BOOL res=CAsyncProxySocketLayer::Connect((SOCKADDR*)&sockAddr, sizeof(sockAddr));
  if (res || WSAGetLastError()==WSAEWOULDBLOCK)
  {
    nb_free(m_pProxyPeerHost);
    m_pProxyPeerHost = static_cast<char *>(nb_calloc(1, strlen(T2CA(lpszHostAddress))+1));
    strcpy(m_pProxyPeerHost, T2CA(lpszHostAddress));
  }
  return res;

}

BOOL CAsyncProxySocketLayer::Connect( const SOCKADDR* lpSockAddr, int nSockAddrLen )
{
  if (!m_ProxyData.nProxyType)
  {
    //Connect normally because there is no proxy
    return ConnectNext(lpSockAddr, nSockAddrLen );
  }

  LPSOCKADDR_IN sockAddr=(LPSOCKADDR_IN)lpSockAddr;

  //Save server details
  m_nProxyPeerIp=sockAddr->sin_addr.S_un.S_addr;
  m_nProxyPeerPort=sockAddr->sin_port;
  nb_free(m_pProxyPeerHost);
  m_pProxyPeerHost = NULL;

  m_nProxyOpID=PROXYOP_CONNECT;

  USES_CONVERSION;

  BOOL res = ConnectNext(A2T(m_ProxyData.pProxyHost), m_ProxyData.nProxyPort);
  if (!res)
  {
    if (WSAGetLastError()!=WSAEWOULDBLOCK)
    {
      DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_NOCONN, 0);
      return FALSE;
    }
  }

  return res;
}

void CAsyncProxySocketLayer::OnConnect(int nErrorCode)
{
  if (m_ProxyData.nProxyType==PROXYTYPE_NOPROXY)
  {
    TriggerEvent(FD_CONNECT, nErrorCode, TRUE);
    return;
  }
  DebugAssert(m_nProxyOpID);
  if (!m_nProxyOpID)
  {
    //This should not happen
    return;
  };

  if (nErrorCode)
  { //Can't connect to proxy
    DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_NOCONN, 0);
    if (m_nProxyOpID==PROXYOP_CONNECT)
      TriggerEvent(FD_CONNECT, nErrorCode, TRUE);
    else
      TriggerEvent(FD_ACCEPT, nErrorCode, TRUE);
    Reset();
    ClearBuffer();
    return;
  }
  if (m_nProxyOpID==PROXYOP_CONNECT || m_nProxyOpID==PROXYOP_LISTEN)
  {
    if (m_nProxyOpState)
      //Somehow OnConnect has been called more than once
      return;
    DebugAssert(m_ProxyData.nProxyType!=PROXYTYPE_NOPROXY);
    ClearBuffer();
    DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_NOERROR, 0);
    //Send the initial request
    if (m_ProxyData.nProxyType==PROXYTYPE_SOCKS4 || m_ProxyData.nProxyType==PROXYTYPE_SOCKS4A)
    { //SOCKS4 proxy
      //Send request
      LPCSTR lpszAscii = m_pProxyPeerHost?m_pProxyPeerHost:"";
      char *command=static_cast<char *>(nb_calloc(1, 9+strlen(lpszAscii)+1));
      memset(command,0,9+strlen(lpszAscii)+1);
      int len=9;
      command[0]=4;
      command[1]=(m_nProxyOpID==PROXYOP_CONNECT)?1:2; //CONNECT or BIND request
      memcpy(&command[2],&m_nProxyPeerPort,2); //Copy target address
      if (!m_nProxyPeerIp || m_ProxyData.nProxyType==PROXYTYPE_SOCKS4A)
      {
        DebugAssert(m_ProxyData.nProxyType==PROXYTYPE_SOCKS4A);
        DebugAssert(strcmp(lpszAscii, ""));
        //Set the IP to 0.0.0.x (x is nonzero)
        command[4]=0;
        command[5]=0;
        command[6]=0;
        command[7]=1;
        //Add host as URL
        strcpy(&command[9],lpszAscii);
        len+=strlen(lpszAscii)+1;
      }
      else
        memcpy(&command[4],&m_nProxyPeerIp,4);
      int res=SendNext(command,len); //Send command
      nb_free(command);
      int nErrorCode=WSAGetLastError();
      if (res==SOCKET_ERROR)//nErrorCode!=WSAEWOULDBLOCK)
      {
        ConnectionFailed((m_nProxyOpID == PROXYOP_CONNECT) && (nErrorCode == WSAEWOULDBLOCK) ? WSAECONNABORTED : nErrorCode);
        return;
      }
      else if (res<len)
      {
        ConnectionFailed(WSAECONNABORTED);
        return;
      }
    }
    else if (m_ProxyData.nProxyType==PROXYTYPE_SOCKS5)
    { //SOCKS5 proxy
      //Send initialization request
      uint8_t command[10];
      memset(command,0,10);
      command[0]=5;
      //CAsyncProxySocketLayer supports to logon types: No logon and
      //cleartext username/password (if set) logon
      command[1]=m_ProxyData.bUseLogon?2:1; //Number of logon types
      command[2]=m_ProxyData.bUseLogon?2:0; //2=user/pass, 0=no logon
      int len=m_ProxyData.bUseLogon?4:3; //length of request
      int res=SendNext(command,len);

      int nErrorCode=WSAGetLastError();
      if (res==SOCKET_ERROR)//nErrorCode!=WSAEWOULDBLOCK)
      {
        ConnectionFailed((m_nProxyOpID == PROXYOP_CONNECT) && (nErrorCode == WSAEWOULDBLOCK) ? WSAECONNABORTED : nErrorCode);
        return;
      }
      else if (res<len)
      {
        ConnectionFailed(WSAECONNABORTED);
        return;
      }
    }
    else if (m_ProxyData.nProxyType==PROXYTYPE_HTTP11)
    {
      char str[4096]; //This should be large enough

      char * pHost = NULL;
      if (m_pProxyPeerHost && *m_pProxyPeerHost)
      {
        pHost = static_cast<char *>(nb_calloc(1, strlen(m_pProxyPeerHost)+1));
        strcpy(pHost, m_pProxyPeerHost);
      }
      else
      {
        pHost = static_cast<char *>(nb_calloc(1, 16));
        sprintf(pHost, "%d.%d.%d.%d", m_nProxyPeerIp%256, (m_nProxyPeerIp>>8) % 256, (m_nProxyPeerIp>>16) %256, m_nProxyPeerIp>>24);
      }
      if (!m_ProxyData.bUseLogon)
        sprintf(str, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n", pHost, ntohs(m_nProxyPeerPort),
          pHost, ntohs(m_nProxyPeerPort));
      else
      {
        sprintf(str, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", pHost, ntohs(m_nProxyPeerPort),
          pHost, ntohs(m_nProxyPeerPort));

        char userpass[4096];
        sprintf(userpass, "%s:%s", m_ProxyData.pProxyUser?m_ProxyData.pProxyUser:"", m_ProxyData.pProxyPass?m_ProxyData.pProxyPass:"");

        char base64str[4096];

        CBase64Coding base64coding;
        base64coding.Encode(userpass, strlen(userpass), base64str);
        strcat(str, "Authorization: Basic ");
        strcat(str, base64str);
        strcat(str, "\r\nProxy-Authorization: Basic ");
        strcat(str, base64str);
        strcat(str, "\r\n\r\n");
      }
      nb_free(pHost);

      USES_CONVERSION;
      int numsent=SendNext(str, strlen(str) );
      int nErrorCode=WSAGetLastError();
      if (numsent==SOCKET_ERROR)//nErrorCode!=WSAEWOULDBLOCK)
      {
        ConnectionFailed((m_nProxyOpID == PROXYOP_CONNECT) && (nErrorCode == WSAEWOULDBLOCK) ? WSAECONNABORTED : nErrorCode);
        return;
      }
      else if (  numsent < static_cast<int>( strlen(str) )  )
      {
        ConnectionFailed(WSAECONNABORTED);
        return;
      }
      m_nProxyOpState++;
      return;
    }
    else
      DebugFail();
    //Now we'll wait for the response, handled in OnReceive
    m_nProxyOpState++;
  }
}

void CAsyncProxySocketLayer::ClearBuffer()
{
  nb_free(m_pStrBuffer);
  m_pStrBuffer = NULL;
  if (m_pRecvBuffer)
  {
    nb_free(m_pRecvBuffer);
    m_pRecvBuffer=0;
  }
  m_nRecvBufferLen=0;
  m_nRecvBufferPos=0;
}

BOOL CAsyncProxySocketLayer::Listen( int nConnectionBacklog)
{
  if (m_ProxyData.nProxyType==PROXYTYPE_NOPROXY)
    return ListenNext(nConnectionBacklog);

  USES_CONVERSION;

  //Connect to proxy server
  BOOL res = ConnectNext(A2T(m_ProxyData.pProxyHost), m_ProxyData.nProxyPort);
  if (!res)
  {
    if (WSAGetLastError()!=WSAEWOULDBLOCK)
    {
      DoLayerCallback(LAYERCALLBACK_LAYERSPECIFIC, PROXYERROR_NOCONN, 0);
      return FALSE;
    }
  }
  m_nProxyPeerPort=0;
  m_nProxyPeerIp=(unsigned int)nConnectionBacklog;

  m_nProxyOpID=PROXYOP_LISTEN;
  return TRUE;
}

BOOL CAsyncProxySocketLayer::GetPeerName(CString &rPeerAddress, UINT &rPeerPort)
{
  if (m_ProxyData.nProxyType==PROXYTYPE_NOPROXY)
  {
    return GetPeerNameNext(rPeerAddress, rPeerPort);
  }

  if (GetLayerState()==notsock)
  {
    WSASetLastError(WSAENOTSOCK);
    return FALSE;
  }
  else if (GetLayerState()!=connected)
  {
    WSASetLastError(WSAENOTCONN);
    return FALSE;
  }
  else if (!m_nProxyPeerIp || !m_nProxyPeerPort)
  {
    WSASetLastError(WSAENOTCONN);
    return FALSE;
  }

  DebugAssert(m_ProxyData.nProxyType);
  BOOL res=GetPeerNameNext( rPeerAddress, rPeerPort );
  if (res)
  {
    rPeerPort=ntohs(m_nProxyPeerPort);
    rPeerAddress.Format(L"%d.%d.%d.%d", m_nProxyPeerIp%256,(m_nProxyPeerIp>>8)%256,(m_nProxyPeerIp>>16)%256, m_nProxyPeerIp>>24);
  }
  return res;
}

BOOL CAsyncProxySocketLayer::GetPeerName( SOCKADDR* lpSockAddr, int* lpSockAddrLen )
{
  if (m_ProxyData.nProxyType==PROXYTYPE_NOPROXY)
  {
    return GetPeerNameNext(lpSockAddr, lpSockAddrLen);
  }

  if (GetLayerState()==notsock)
  {
    WSASetLastError(WSAENOTSOCK);
    return FALSE;
  }
  else if (GetLayerState()!=connected)
  {
    WSASetLastError(WSAENOTCONN);
    return FALSE;
  }
  else if (!m_nProxyPeerIp || !m_nProxyPeerPort)
  {
    WSASetLastError(WSAENOTCONN);
    return FALSE;
  }

  DebugAssert(m_ProxyData.nProxyType);
  BOOL res=GetPeerNameNext(lpSockAddr,lpSockAddrLen);
  if (res)
  {
    LPSOCKADDR_IN addr=(LPSOCKADDR_IN)lpSockAddr;
    addr->sin_port=m_nProxyPeerPort;
    addr->sin_addr.S_un.S_addr=m_nProxyPeerIp;
  }
  return res;
}

void CAsyncProxySocketLayer::Close()
{
  nb_free(m_ProxyData.pProxyHost);
  nb_free(m_ProxyData.pProxyUser);
  nb_free(m_ProxyData.pProxyPass);
  nb_free(m_pProxyPeerHost);
  m_ProxyData.pProxyHost = NULL;
  m_ProxyData.pProxyUser = NULL;
  m_ProxyData.pProxyPass = NULL;
  m_pProxyPeerHost = NULL;
  ClearBuffer();
  Reset();
  CloseNext();
}

void CAsyncProxySocketLayer::Reset()
{
  m_nProxyOpState=0;
  m_nProxyOpID=0;
}

int CAsyncProxySocketLayer::Send(const void* lpBuf, int nBufLen, int nFlags)
{
  if (m_nProxyOpID)
  {
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
  }

  return SendNext(lpBuf, nBufLen, nFlags);
}

int CAsyncProxySocketLayer::Receive(void* lpBuf, int nBufLen, int nFlags)
{
  if (m_nProxyOpID)
  {
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
  }

  return ReceiveNext(lpBuf, nBufLen, nFlags);
}

BOOL CAsyncProxySocketLayer::Accept( CAsyncSocketEx& rConnectedSocket, SOCKADDR* lpSockAddr /*=NULL*/, int* lpSockAddrLen /*=NULL*/ )
{
  if (!m_ProxyData.nProxyType)
    return AcceptNext(rConnectedSocket, lpSockAddr, lpSockAddrLen);

  GetPeerName(lpSockAddr, lpSockAddrLen);
  return TRUE;
}
