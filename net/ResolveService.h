#ifndef RESOLVE_SERVICE_H
#define RESOLVE_SERVICE_H

#include "ServiceBase.h"
#include "AddressResolver.h"
#include "../base/BaseTypes.h"
#include "../base/ScopePtr.h"
#include "../thread/Atomic.h"
#include "../thread/Thread.h"
#include "../thread/Event.h"
#include "../thread/Mutex.h"
#include <algorithm>
#include <functional>
#include <list>
#include <string>

namespace bitwave {
namespace net {

    // a service class for resolve address
    class ResolveService : public BasicService<ResolveService>
    {
    public:
        typedef std::tr1::function<void (const std::string&,
                const std::string&, const ResolveResult&)> ResolveHandler;

        class AsyncResolver
        {
        public:
            AsyncResolver(const std::string& nodename,
                          const std::string& servname,
                          const ResolveHint& hint,
                          const ResolveHandler& handler)
                : nodename_(nodename),
                  servname_(servname),
                  hint_(hint),
                  handler_(handler),
                  result_()
            {
            }

            void Resolve()
            {
                try
                {
                    result_ = ResolveAddress(nodename_, servname_, hint_);
                }
                catch(const AddressResolveException&)
                {
                    // we do nothing, because can not resolve the address
                }
            }

            void CallHandler()
            {
                handler_(nodename_, servname_, result_);
            }

        private:
            std::string nodename_;
            std::string servname_;
            ResolveHint hint_;
            ResolveHandler handler_;
            ResolveResult result_;
        };

        ResolveService()
            : thread_exit_flag_(0),
              resolve_thread_(),
              event_(),
              resolver_list_(),
              resolver_list_mutex_(),
              result_list_(),
              result_list_mutex_()
        {
            InitResolveThread();
        }

        ~ResolveService()
        {
            AtomicAdd(&thread_exit_flag_, 1);
            event_.SetEvent();
            resolve_thread_->Join();
        }

        // request an async resolve
        void AsyncResolve(const std::string& nodename,
                          const std::string& servname,
                          const ResolveHint& hint,
                          const ResolveHandler& handler)
        {
            PrepareResolve(nodename, servname, hint, handler);
            RequestResolve();
        }

        // prepare a resolver, and do not resolve immediately,
        // call RequestResolve to begin resolvers
        void PrepareResolve(const std::string& nodename,
                            const std::string& servname,
                            const ResolveHint& hint,
                            const ResolveHandler& handler)
        {
            // add new resolve request data in resolver_list_
            SpinlocksMutexLocker locker(resolver_list_mutex_);
            AsyncResolver new_resolver(nodename, servname, hint, handler);
            resolver_list_.push_back(new_resolver);
        }

        // request resolve all prepared AsyncResolvers by PrepareResolve
        void RequestResolve()
        {
            // SetEvent then ResolveThread could begin resolve
            event_.SetEvent();
        }

    private:
        typedef ScopePtr<Thread> ThreadPtr;
        typedef std::list<AsyncResolver> AsyncResolverList;

        virtual void DoRun()
        {
            ProcessResult();
        }

        void InitResolveThread()
        {
            resolve_thread_.Reset(new Thread(std::tr1::bind(&ResolveService::ResolveThread, this)));
        }

        unsigned ResolveThread()
        {
            while (true)
            {
                if (event_.WaitForever())
                {
                    if (AtomicAdd(&thread_exit_flag_, 0))
                        break;

                    AsyncResolverList data;

                    {
                        // get all resolve request data
                        SpinlocksMutexLocker locker(resolver_list_mutex_);
                        data.swap(resolver_list_);
                    }

                    std::for_each(data.begin(), data.end(),
                            std::mem_fun_ref(&AsyncResolver::Resolve));

                    {
                        SpinlocksMutexLocker locker(result_list_mutex_);
                        result_list_.splice(result_list_.end(), data);
                    }
                }
            }

            return 0;
        }

        void ProcessResult()
        {
            AsyncResolverList result;

            {
                SpinlocksMutexLocker locker(result_list_mutex_);
                result.swap(result_list_);
            }

            std::for_each(result.begin(), result.end(),
                    std::mem_fun_ref(&AsyncResolver::CallHandler));
        }

        volatile long thread_exit_flag_;
        ThreadPtr resolve_thread_;
        AutoResetEvent event_;
        AsyncResolverList resolver_list_;
        SpinlocksMutex resolver_list_mutex_;
        AsyncResolverList result_list_;
        SpinlocksMutex result_list_mutex_;
    };

} // namespace net
} // namespace bitwave

#endif // RESOLVE_SERVICE_H
