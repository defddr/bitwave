#ifndef BIT_PEER_CONNECTION_H
#define BIT_PEER_CONNECTION_H

#include "BitNetProcessor.h"
#include "BitRequestList.h"
#include "BitDownloadingInfo.h"
#include "../base/BaseTypes.h"
#include "../net/TimerService.h"
#include "../sha1/Sha1Value.h"
#include "../timer/Timer.h"
#include <assert.h>
#include <memory>
#include <string>
#include <list>

namespace bitwave {
namespace core {

    class BitData;
    class BitCache;
    class BitPeerData;
    class BitPeerConnection;
    class BitUploadDispatcher;
    class BitDownloadDispatcher;

    // BitPeerConnection's parent interface
    class PeerConnectionOwner
    {
    public:
        virtual bool NotifyInfoHash(const std::tr1::shared_ptr<BitPeerConnection>& child, const Sha1Value& info_hash) = 0;
        virtual void NotifyHandshakeOk(const std::tr1::shared_ptr<BitPeerConnection>& child) = 0;
        virtual void NotifyConnectionDrop(const std::tr1::shared_ptr<BitPeerConnection>& child) = 0;
    };

    // manage peer connection and all operations
    class BitPeerConnection :
        private NotCopyable,
        public BitDownloadingInfo::Observer,
        public std::tr1::enable_shared_from_this<BitPeerConnection>
    {
    public:
        BitPeerConnection(const net::AsyncSocket& socket,
                          PeerConnectionOwner *owner);

        BitPeerConnection(const std::shared_ptr<BitData>& bitdata,
                          net::IoService& io_service,
                          PeerConnectionOwner *owner);

        ~BitPeerConnection();

        void SetOwner(PeerConnectionOwner *owner)
            { owner_ = owner; }
        void SetBitData(const std::tr1::shared_ptr<BitData>& bitdata)
            { bitdata_ = bitdata; }
        void SetCache(const std::tr1::shared_ptr<BitCache>& cache)
            { cache_ = cache; }
        void SetDownloadDispatcher(const std::tr1::shared_ptr<BitDownloadDispatcher>& dispatcher)
            { download_dispatcher_ = dispatcher; }
        void SetUploadDispatcher(const std::tr1::shared_ptr<BitUploadDispatcher>& dispatcher)
            { upload_dispatcher_ = dispatcher; }

        void Connect(const net::Address& remote_address,
                     const net::Port& remote_listen_port);
        void Receive();

        bool UploadBlock(int index, int begin, int length,
                         bool read_ok, const char *block);

        void ProcessProtocol(const char *data, std::size_t size);
        void OnConnect();
        void OnDisconnect();
        void Complete();

    private:
        // peer wire protocol unpack ruler
        class PeerProtocolUnpackRuler
        {
        public:
            static bool CanUnpack(const char *stream,
                    std::size_t size, std::size_t *pack_len);
        };

        struct ConnectionState
        {
            ConnectionState()
            {
                Clear();
            }

            void Clear()
            {
                am_choking = true;
                am_interested = false;
                peer_choking = true;
                peer_interested = false;
            }

            bool am_choking;
            bool am_interested;
            bool peer_choking;
            bool peer_interested;
        };

        class RequestTimeouter : private NotCopyable
        {
        public:
            static const int time_out_millisecond = 60 * 1000;

            explicit RequestTimeouter(net::IoService& io_service)
                : timer_service(io_service)
            {
                assert(timer_service);
            }

            ~RequestTimeouter()
            {
                Reset();
            }

            template<typename TimeOutCallback>
            void ApplyTimeOut(BitRequestList::Iterator it,
                              const TimeOutCallback& callback)
            {
                Timer *timer = new Timer(time_out_millisecond);
                TimeOutPair time_out(it, timer);
                time_out_list_.push_back(time_out);
                timer->SetCallback(callback);
                AddToTimerService(timer);
            }

            void CancelTimeOut(BitRequestList::Iterator it)
            {
                TimeOutList::iterator i = time_out_list_.begin();

                while (i != time_out_list_.end() && i->it != it)
                    ++i;

                if (i != time_out_list_.end())
                {
                    RemoveFromTimerService(i->timer.get());
                    time_out_list_.erase(i);
                }
            }

            void AddToTimerService(Timer *timer)
            {
                timer_service->AddTimer(timer);
            }

            void RemoveFromTimerService(Timer *timer)
            {
                timer_service->DelTimer(timer);
            }

            void Reset()
            {
                TimeOutList::iterator it = time_out_list_.begin();
                while (it != time_out_list_.end())
                {
                    RemoveFromTimerService(it->timer.get());
                    ++it;
                }

                time_out_list_.clear();
            }

        private:
            struct TimeOutPair
            {
                TimeOutPair(BitRequestList::Iterator i, Timer *t)
                    : it(i),
                      timer(t)
                {
                }

                BitRequestList::Iterator it;
                std::tr1::shared_ptr<Timer> timer;
            };

            typedef std::list<TimeOutPair> TimeOutList;

            net::ServicePtr<net::TimerService> timer_service;
            TimeOutList time_out_list_;
        };

        typedef BitNetProcessor<PeerProtocolUnpackRuler,
                                BitPeerConnection> NetProcessor;

        virtual void DownloadingNewPiece(std::size_t piece_index) { }
        virtual void CompleteNewPiece(std::size_t piece_index);
        virtual void DownloadingFailed(std::size_t piece_index);

        void ClearNetProcessor();
        bool ProcessHandshake(const char *data);
        void ProcessMessage(const char *data, std::size_t len);
        void ProcessKeepAlive();
        void ProcessChoke(bool choke);
        void ProcessInterested(bool interested);
        void ProcessHave(const char *data, std::size_t len);
        void ProcessBitfield(const char *data, std::size_t len);
        void ProcessRequest(const char *data, std::size_t len);
        void ProcessPiece(const char *data, std::size_t len);
        void ProcessCancel(const char *data, std::size_t len);

        void PreparePeerData(const std::string& peer_id);
        void DropConnection();
        void SendHandshake();
        void SendKeepAlive();
        void SendNoPayloadMessage(char id);
        void SendHave(int piece_index);
        void SendBitfield();
        void SendRequest(int index, int begin, int length);
        void SendPiece(int index, int begin, int length, const char *block);
        void SendCancel(int index, int begin, int length);
        void OnHandshake();

        void SetInterested(bool interested);
        void SetChoke(bool choke);
        void HavePiece(std::size_t piece_index);
        void RequestPieceBlock();

        void PendingUploadRequest();
        void PostRequest(BitRequestList::Iterator it);
        void RequestTimeOut(BitRequestList::Iterator it);
        void DeleteOutStandingRequest(BitRequestList::Iterator it);
        void CancelRequest(BitRequestList::Iterator it);
        void ReturnAllRequests();

        void InitTimers();
        void SetKeepAliveTimer();
        void SetDisconnectTimer();
        void ClearTimers();

        Timer keep_alive_timer_;
        Timer disconnect_timer_;
        PeerConnectionOwner *owner_;
        ConnectionState connection_state_;
        BitRequestList peer_request_;
        BitRequestList wait_request_;
        BitRequestList requesting_list_;
        RequestTimeouter request_timeouter_;
        std::tr1::shared_ptr<BitCache> cache_;
        std::tr1::shared_ptr<BitData> bitdata_;
        std::tr1::shared_ptr<BitPeerData> peer_data_;
        std::tr1::shared_ptr<NetProcessor> net_processor_;
        std::tr1::shared_ptr<BitUploadDispatcher> upload_dispatcher_;
        std::tr1::shared_ptr<BitDownloadDispatcher> download_dispatcher_;
    };

} // namespace core
} // namespace bitwave

#endif // BIT_PEER_CONNECTION_H
