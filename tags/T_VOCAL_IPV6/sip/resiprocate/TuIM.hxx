#if !defined(UDPTRANSPORT_HXX)
#define UDPTRANSPORT_HXX

#include "sip2/sipstack/Security.hxx"

namespace Vocal2
{

class TuIM
{
   public:

      class PageCallback
      {
         public:
            virtual void receivedPage(const Data& msg, const Uri& from, 
                                      const Data& signedBy,  Security::SignatureStatus sigStatus,
                                      bool wasEncryped  ) = 0; 
            virtual ~PageCallback();
      };
      
      class ErrCallback
      {
         public:
            virtual void sendPageFailed(const Uri& dest );
            virtual ~ErrCallback();
      };
      
      TuIM(SipStack* stack, 
           const Uri& aor, 
           const Uri& contact,
           PageCallback* pageCallback, 
           ErrCallback* errCallback);
      
      void sendPage(const Data& text, const Uri& dest, bool sign, const Data& encryptFor );

      void process();
      
   private:
      PageCallback* mPageCallback;
      ErrCallback* mErrCallback; 
      SipStack* mStack;
      Uri mAor;
      Uri mContact;
};

}

#endif


/* ====================================================================
 * The Vovida Software License, Version 1.0 
 */

