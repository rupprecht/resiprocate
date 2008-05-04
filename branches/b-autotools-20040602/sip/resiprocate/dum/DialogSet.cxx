#include "resiprocate/dum/BaseCreator.hxx"
#include "resiprocate/dum/DialogSet.hxx"
#include "resiprocate/dum/Dialog.hxx"
#include "resiprocate/dum/DialogUsageManager.hxx"
#include "DialogUsageManager.hxx"
#include "ClientAuthManager.hxx"
#include "resiprocate/os/Logger.hxx"

#define RESIPROCATE_SUBSYSTEM Subsystem::DUM

using namespace resip;
using namespace std;

DialogSet::DialogSet(BaseCreator* creator, DialogUsageManager& dum) :
   mMergeKey(),
   mDialogs(),
   mCreator(creator),
   mId(creator->getLastRequest()),
   mDum(dum),
   mCancelled(false)
{
   assert(!creator->getLastRequest().isExternal());
}

DialogSet::DialogSet(const SipMessage& request, DialogUsageManager& dum) : 
   mMergeKey(request),
   mDialogs(),
   mCreator(NULL),
   mId(request),
   mDum(dum),
   mCancelled(false)
{
   assert(request.isRequest());
   assert(request.isExternal());
   mDum.mMergedRequests.insert(mMergeKey);
}

DialogSet::~DialogSet()
{
   if (mMergeKey != MergedRequestKey::Empty)
   {
      mDum.mMergedRequests.erase(mMergeKey);
   }

   delete mCreator;
   for(DialogMap::iterator it = mDialogs.begin(); it != mDialogs.end(); it++)
   {
      delete it->second;
   }   
}

DialogSetId
DialogSet::getId()
{
   return mId;
}

void
DialogSet::addDialog(Dialog *dialog)
{
   mDialogs[dialog->getId()] = dialog;
}

BaseCreator* 
DialogSet::getCreator() 
{
   return mCreator;
}

Dialog* 
DialogSet::findDialog(const SipMessage& msg)
{
   DialogId id(msg);
   return findDialog(id);
}

bool
DialogSet::empty() const
{
   return mDialogs.empty();
}

void
DialogSet::dispatch(const SipMessage& msg)
{
   assert(msg.isRequest() || msg.isResponse());
   Dialog* dialog = findDialog(msg);
   if (dialog == 0)
   {
      // !jf! This could throw due to bad header in msg, should we catch and rethrow
      // !jf! if this threw, should we check to delete the DialogSet? 
      dialog = new Dialog(mDum, msg, *this);
      if (mCancelled)
      {
         dialog->cancel();
      }
      InfoLog (<< "Created a new dialog: " << *dialog);
   }     

   if (mDum.mClientAuthManager && !mCancelled)
   {
      if (getCreator())
      {
         if ( mDum.mClientAuthManager->handle( getCreator()->getLastRequest(), msg ) )
         {
            InfoLog( << "about to retransmit request with digest credentials" );
            
            mDum.send(getCreator()->getLastRequest());
            
            return;
         }
      }
      else
      {
         assert(0);
         // need to get the last message from the usage some how 
      }
   }

   dialog->dispatch(msg);
}

Dialog* 
DialogSet::findDialog(const DialogId id)
{
   DialogMap::iterator i = mDialogs.find(id);
   if (i == mDialogs.end())
   {
      return 0;
   }
   else
   {
      return i->second;
   }
}

void
DialogSet::cancel()
{
   mCancelled = true;
   for (DialogMap::iterator i = mDialogs.begin(); i != mDialogs.end(); ++i)
   {
      i->second->cancel();
   }
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