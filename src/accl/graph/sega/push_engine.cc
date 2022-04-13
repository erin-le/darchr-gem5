/*
 * Copyright (c) 2021 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "accl/graph/sega/push_engine.hh"

#include "debug/MPU.hh"
#include "mem/packet_access.hh"

namespace gem5
{

PushEngine::PushEngine(const PushEngineParams &params):
    BaseMemEngine(params),
    reqPort(name() + ".req_port", this),
    baseEdgeAddr(params.base_edge_addr),
    pushReqQueueSize(params.push_req_queue_size),
    nextAddrGenEvent([this] { processNextAddrGenEvent(); }, name()),
    nextPushEvent([this] { processNextPushEvent(); }, name())
{}

Port&
PushEngine::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "req_port") {
        return reqPort;
    } else if (if_name == "mem_port") {
        return BaseMemEngine::getPort(if_name, idx);
    } else {
        return SimObject::getPort(if_name, idx);
    }
}

void
PushEngine::startup()
{
    uint8_t* first_update_data = new uint8_t [4];
    uint32_t* tempPtr = (uint32_t*) first_update_data;
    *tempPtr = 0;

    // PacketPtr first_update = createUpdatePacket(0, 4, first_update_data);
    PacketPtr first_update = createUpdatePacket<uint32_t>(0, (uint32_t) 0);

    if (!reqPort.blocked()) {
        reqPort.sendPacket(first_update);
    }
}

void
PushEngine::ReqPort::sendPacket(PacketPtr pkt)
{
    panic_if(_blocked, "Should never try to send if blocked MemSide!");
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt))
    {
        blockedPacket = pkt;
        _blocked = true;
    }
}

bool
PushEngine::ReqPort::recvTimingResp(PacketPtr pkt)
{
    panic("recvTimingResp called on the request port.");
}

void
PushEngine::ReqPort::recvReqRetry()
{
    panic_if(!(_blocked && blockedPacket), "Received retry without a blockedPacket");

    DPRINTF(MPU, "%s: Received a reqRetry.\n", __func__);

    _blocked = false;
    sendPacket(blockedPacket);

    if (!_blocked) {
        blockedPacket = nullptr;
    }
}

bool
PushEngine::recvWLItem(WorkListItem wl)
{
    // If there are no outdoing edges, no need to generate and push
    // updates. Therefore, we only need to return true.
    if (wl.degree == 0) {
        DPRINTF(MPU, "%s: Received a leaf. Respective information: %s.\n",
                    __func__, wl.to_string());
        return true;
    }

    assert((pushReqQueueSize == 0) ||
        (pushReqQueue.size() <= pushReqQueueSize));
    if ((pushReqQueueSize != 0) && (pushReqQueue.size() == pushReqQueueSize)) {
        return false;
    }

    Addr start_addr = baseEdgeAddr + (wl.edgeIndex * sizeof(Edge));
    Addr end_addr = start_addr + (wl.degree * sizeof(Edge));
    uint32_t value = wl.prop;

    pushReqQueue.emplace_back(start_addr, end_addr, sizeof(Edge), peerMemoryAtomSize, value);

    assert(!pushReqQueue.empty());
    if ((!nextAddrGenEvent.scheduled()) &&
        (!memReqQueueFull())) {
        schedule(nextAddrGenEvent, nextCycle());
    }
    return true;
}

void
PushEngine::processNextAddrGenEvent()
{

    Addr aligned_addr, offset;
    int num_edges;

    PushPacketInfoGen &curr_info = pushReqQueue.front();
    std::tie(aligned_addr, offset, num_edges) = curr_info.nextReadPacketInfo();
    DPRINTF(MPU, "%s: Current packet information generated by "
                "PushPacketInfoGen. aligned_addr: %lu, offset: %lu, "
                "num_edges: %d.\n", __func__, aligned_addr, offset, num_edges);

    PacketPtr pkt = createReadPacket(aligned_addr, peerMemoryAtomSize);
    reqOffsetMap[pkt->req] = offset;
    reqNumEdgeMap[pkt->req] = num_edges;
    reqValueMap[pkt->req] = curr_info.value();

    enqueueMemReq(pkt);

    if (curr_info.done()) {
        DPRINTF(MPU, "%s: Current PushPacketInfoGen is done.\n", __func__);
        pushReqQueue.pop_front();
        DPRINTF(MPU, "%s: Popped curr_info from pushReqQueue. "
                    "pushReqQueue.size() = %u.\n",
                    __func__, pushReqQueue.size());
    }

    if (memReqQueueFull()) {
        if (!pushReqQueue.empty()) {
            requestAlarm(1);
        }
        return;
    }

    if ((!nextAddrGenEvent.scheduled()) && (!pushReqQueue.empty())) {
        schedule(nextAddrGenEvent, nextCycle());
    }
}

void
PushEngine::respondToAlarm()
{
    assert(!nextAddrGenEvent.scheduled());
    schedule(nextAddrGenEvent, nextCycle());
    DPRINTF(MPU, "%s: Responded to an alarm.\n", __func__);
}

bool
PushEngine::handleMemResp(PacketPtr pkt)
{
    memRespQueue.push_back(pkt);

    if ((!nextPushEvent.scheduled()) && (!memRespQueue.empty())) {
        schedule(nextPushEvent, nextCycle());
    }
    return true;
}

// TODO: Add a parameter to allow for doing multiple pushes at the same time.
void
PushEngine::processNextPushEvent()
{
    PacketPtr pkt = memRespQueue.front();
    uint8_t* data = pkt->getPtr<uint8_t>();

    Addr offset = reqOffsetMap[pkt->req];
    assert(offset < peerMemoryAtomSize);
    uint32_t value = reqValueMap[pkt->req];

    DPRINTF(MPU, "%s: Looking at the front of the queue. pkt->Addr: %lu, "
                "offset: %lu\n",
            __func__, pkt->getAddr(), offset);

    Edge* curr_edge = (Edge*) (data + offset);

    // TODO: Implement propagate function here
    uint32_t update_value = value + 1;
    DPRINTF(MPU, "%s: Sending an update to %lu with value: %d.\n",
            __func__, curr_edge->neighbor, update_value);

    PacketPtr update = createUpdatePacket<uint32_t>(
                            curr_edge->neighbor, update_value);

    if (!reqPort.blocked()) {
        reqPort.sendPacket(update);
        DPRINTF(MPU, "%s: Sent a push update to addr: %lu with value: %d.\n",
                                __func__, curr_edge->neighbor, update_value);
        reqOffsetMap[pkt->req] = reqOffsetMap[pkt->req] + sizeof(Edge);
        assert(reqOffsetMap[pkt->req] <= peerMemoryAtomSize);
        reqNumEdgeMap[pkt->req]--;
        assert(reqNumEdgeMap[pkt->req] >= 0);
    }

    if (reqNumEdgeMap[pkt->req] == 0) {
        reqOffsetMap.erase(pkt->req);
        reqNumEdgeMap.erase(pkt->req);
        reqValueMap.erase(pkt->req);
        delete pkt;
        memRespQueue.pop_front();
    }

    if (!nextPushEvent.scheduled() && !memRespQueue.empty()) {
        schedule(nextPushEvent, nextCycle());
    }
}

template<typename T> PacketPtr
PushEngine::createUpdatePacket(Addr addr, T value)
{
    RequestPtr req = std::make_shared<Request>(
                addr, sizeof(T), 0, _requestorId);
    // Dummy PC to have PC-based prefetchers latch on; get entropy into higher
    // bits
    req->setPC(((Addr) _requestorId) << 2);

    // FIXME: MemCmd::UpdateWL
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);

    pkt->allocate();
    // pkt->setData(data);
    pkt->setLE<T>(value);

    return pkt;
}

}
