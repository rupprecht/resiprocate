
#include <memory>
#include "sip2/util/compat.hxx"
#include "sip2/util/Data.hxx"
#include "sip2/util/Socket.hxx"
#include "sip2/util/Logger.hxx"
#include "sip2/sipstack/UdpTransport.hxx"
#include "sip2/sipstack/SipMessage.hxx"
#include "sip2/sipstack/Preparse.hxx"


#define VOCAL_SUBSYSTEM Subsystem::TRANSPORT

using namespace std;
using namespace Vocal2;

const int UdpTransport::MaxBufferSize = 8192;

UdpTransport::UdpTransport(const Data& sendhost, int portNum, const Data& nic, Fifo<Message>& fifo) : 
   Transport(sendhost, portNum, nic , fifo)
{
   DebugLog (<< "Creating udp transport host=" << sendhost << " port=" << portNum << " nic=" << nic);
   
   mFd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

   if ( mFd == INVALID_SOCKET )
   {
      InfoLog (<< "Failed to open socket: " << portNum);
	  throw Exception("Failed to open UDP port", __FILE__,__LINE__);
   }
   
   sockaddr_in addr;
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY); // !jf! 
   addr.sin_port = htons(portNum);
   
   if ( bind( mFd, (struct sockaddr*) &addr, sizeof(addr)) == SOCKET_ERROR )
   {
      int err = errno;
      if ( err == EADDRINUSE )
      {
         ErrLog (<< "UDP port " << portNum << " already in use");
         throw Exception("UDP port already in use", __FILE__,__LINE__);
      }
      else
      {
         ErrLog (<< "Could not bind to port: " << portNum);
         throw Exception("Could not use UDP port", __FILE__,__LINE__);
      }
   }

   bool ok = makeSocketNonBlocking(mFd);
   if ( !ok )
   {
	    ErrLog (<< "Could not make UDP port non blocking " << portNum);
		throw Exception("Cont not use UDP port", __FILE__,__LINE__);
   }
}


UdpTransport::~UdpTransport()
{
}


void 
UdpTransport::process(FdSet& fdset)
{
   // pull buffers to send out of TxFifo
   // receive datagrams from fd
   // preparse and stuff into RxFifo


   // how do we know that buffer won't get deleted on us !jf!
   if (mTxFifo.messageAvailable())
   {
      std::auto_ptr<SendData> sendData = std::auto_ptr<SendData>(mTxFifo.getNext());
      //DebugLog (<< "Sending message on udp.");

      assert( &(*sendData) );
      assert( sendData->destination.port != 0 );
   
      sockaddr_in addrin;
      addrin.sin_addr = sendData->destination.ipv4;
      addrin.sin_port = htons(sendData->destination.port);
      addrin.sin_family = AF_INET;

      int count = sendto(mFd, 
                         sendData->data.data(), sendData->data.size(),  
                         0, // flags
                         (const sockaddr*)&addrin, sizeof(sockaddr_in) );

      if ( count == SOCKET_ERROR )
      {
         int err = errno;
         InfoLog (<< strerror(err));
         InfoLog (<< "Failed sending to " << sendData->destination);
         fail(sendData->transactionId);
      }
      else
      {
         if (count == int(sendData->data.size()) )
         {
            ok(sendData->transactionId);
         }
         else
         {  
            ErrLog (<< "UDPTransport - send buffer full" );
            fail(sendData->transactionId);
         }
      }
   }

   struct sockaddr_in from;

   // !jf! this may have to change - when we read a message that is too big
   if ( !fdset.readyToRead(mFd) )
   {
      return;
   }

   //should this buffer be allocated on the stack and then copied out, as it
   //needs to be deleted every time EWOULDBLOCK is encountered
   char* buffer = new char[MaxBufferSize];
   socklen_t fromLen = sizeof(from);

   // !jf! how do we tell if it discarded bytes 
   // !ah! we use the len-1 trick :-(

   //DebugLog( << "starting recvfrom" );
   int len = recvfrom( mFd,
                       buffer,
                       MaxBufferSize,
                       0 /*flags */,
                       (struct sockaddr*)&from,
                       &fromLen);
   if ( len == SOCKET_ERROR )
   {
      int err = errno;
      switch (err)
      {
	  case WSANOTINITIALISED:
		  assert(0);
		  break;

         case EWOULDBLOCK:
            DebugLog (<< " UdpTransport recvfrom got EWOULDBLOCK");
            break;

	    case 0:
            DebugLog (<< " UdpTransport recvfrom got error 0 ");
            break;

 	    //case 9:
            DebugLog (<< " UdpTransport recvfrom got error 9 ");
            break;

         default:
            ErrLog(<<"Error receiving, errno="<<err << " " << strerror(err) );
            break;
      }
   }
   //DebugLog( << "completed recvfrom" );

   if (len == 0 || len == SOCKET_ERROR)
   {
      delete[] buffer; 
      buffer=0;
      return;
   }

   if (len == MaxBufferSize)
   {
      InfoLog(<<"Datagram exceeded max length "<<MaxBufferSize);
      delete [] buffer; buffer=0;
      return;
   }

   buffer[len]=0; // null terminate the buffer string just to make debug easier and reduce errors

   //DebugLog ( << "UDP Rcv : " << len << " b" );
   //DebugLog ( << Data(buffer, len).escaped().c_str());

   SipMessage* message = new SipMessage(SipMessage::FromWire);

   // set the received from information into the received= parameter in the
   // via

   // It is presumed that UDP Datagrams are arriving atomically and that
   // each one is a unique SIP message


   // Save all the info where this message came from
   Tuple tuple;
   tuple.ipv4 = from.sin_addr;
   tuple.port = ntohs(from.sin_port);
   tuple.transport = this;
   tuple.transportType = transport();
   message->setSource(tuple);

   // Tell the SipMessage about this datagram buffer.
   message->addBuffer(buffer);

   // This is UDP, so, initialise the preparser with this
   // buffer.

   int err = mPreparse.process(*message,buffer, len);
	
   if (err)
   {
      InfoLog(<<"Preparse Rejecting datagram as unparsable / fragmented.");
      DebugLog(<< Data(buffer, len));
      delete message; 
      message=0; 
      return;
   }

   if ( !mPreparse.isHeadersComplete() )
   {
      InfoLog(<<"Rejecting datagram as unparsable / fragmented.");
      DebugLog(<< Data(buffer, len));
      delete message; 
      message=0;
      return;
   }

   // no pp error
   int used = mPreparse.nBytesUsed();

   if (used < len)
   {
      // body is present .. add it up.
      // NB. The Sip Message uses an overlay (again)
      // for the body. It ALSO expects that the body
      // will be contiguous (of course).
      // it doesn't need a new buffer in UDP b/c there
      // will only be one datagram per buffer. (1:1 strict)

      message->setBody(buffer+used,len-used);
      //DebugLog(<<"added " << len-used << " byte body");
   }

   stampReceived(message);

   mStateMachineFifo.add(message);
}


void 
UdpTransport::buildFdSet( FdSet& fdset )
{
   fdset.setRead(mFd);
    
   //if (mTxFifo.messageAvailable())
   //{
   //  fdset.setWrite(mFd);
   //}
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */