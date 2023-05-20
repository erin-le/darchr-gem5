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

#ifndef __ACCL_GRAPH_BASE_GRAPH_WORKLOAD_HH__
#define  __ACCL_GRAPH_BASE_GRAPH_WORKLOAD_HH__

#include <bitset>
#include <deque>
#include <tuple>

#include "accl/graph/base/data_structs.hh"
#include "accl/graph/sega/work_directory.hh"
#include "mem/packet.hh"


namespace gem5
{

class GraphWorkload
{
  public:
    GraphWorkload() {}
    ~GraphWorkload() {}

    virtual void init(PacketPtr pkt, WorkDirectory* dir) = 0;
    virtual uint32_t reduce(uint32_t update, uint32_t value) = 0;
    virtual uint32_t propagate(uint32_t value, uint32_t weight) = 0;
    virtual uint32_t apply(WorkListItem& wl) = 0;
    virtual void iterate() = 0;
    virtual void interIterationInit(WorkListItem& wl) = 0;
    virtual bool activeCondition(WorkListItem new_wl, WorkListItem old_wl) = 0;
    virtual std::string printWorkListItem(const WorkListItem wl) = 0;
};

class BFSWorkload : public GraphWorkload
{
  private:
    uint64_t initAddr;
    uint32_t initValue;

  public:
    BFSWorkload(uint64_t init_addr, uint32_t init_value):
        initAddr(init_addr), initValue(init_value)
    {}

    ~BFSWorkload() {}

    virtual void init(PacketPtr pkt, WorkDirectory* dir);
    virtual uint32_t reduce(uint32_t update, uint32_t value);
    virtual uint32_t propagate(uint32_t value, uint32_t weight);
    virtual uint32_t apply(WorkListItem& wl);
    virtual void iterate() {}
    virtual void interIterationInit(WorkListItem& wl) {}
    virtual bool activeCondition(WorkListItem new_wl, WorkListItem old_wl);
    virtual std::string printWorkListItem(const WorkListItem wl);
};

class BFSVisitedWorkload : public BFSWorkload
{
  public:
    BFSVisitedWorkload(Addr init_addr, uint32_t init_value):
        BFSWorkload(init_addr, init_value)
    {}
    virtual uint32_t propagate(uint32_t value, uint32_t weight) override;
};

class CCWorkload : public BFSVisitedWorkload
{
  public:
    CCWorkload(): BFSVisitedWorkload(0, 0) {}
    virtual void init(PacketPtr pkt, WorkDirectory* dir);
};

class SSSPWorkload : public BFSWorkload
{
  public:
    SSSPWorkload(Addr init_addr, uint32_t init_value):
        BFSWorkload(init_addr, init_value)
    {}
    virtual uint32_t propagate(uint32_t value, uint32_t weight) override;
};

class PRWorkload : public GraphWorkload
{
  private:
    float alpha;
    float threshold;

  public:
    PRWorkload(float alpha, float threshold):
        alpha(alpha), threshold(threshold)
    {}

    ~PRWorkload() {}

    virtual void init(PacketPtr pkt, WorkDirectory* dir);
    virtual uint32_t reduce(uint32_t update, uint32_t value);
    virtual uint32_t propagate(uint32_t value, uint32_t weight);
    virtual uint32_t apply(WorkListItem& wl);
    virtual void iterate() {}
    virtual void interIterationInit(WorkListItem& wl) {};
    virtual bool activeCondition(WorkListItem new_wl, WorkListItem old_wl);
    virtual std::string printWorkListItem(const WorkListItem wl);
};

class BSPPRWorkload : public GraphWorkload
{
  private:
    int numNodes;
    float alpha;
    float prevError;
    float error;

  public:
    BSPPRWorkload(int num_nodes, float alpha):
        numNodes(num_nodes), alpha(alpha), prevError(0), error(0)
    {}

    ~BSPPRWorkload() {}

    virtual void init(PacketPtr pkt, WorkDirectory* dir);
    virtual uint32_t reduce(uint32_t update, uint32_t value);
    virtual uint32_t propagate(uint32_t value, uint32_t weight);
    virtual uint32_t apply(WorkListItem& wl);
    virtual void iterate() { prevError = error; error = 0; }
    virtual void interIterationInit(WorkListItem& wl);
    virtual bool activeCondition(WorkListItem new_wl, WorkListItem old_wl);
    virtual std::string printWorkListItem(const WorkListItem wl);

    float getError() { return prevError; }
};

class BSPBCWorkload : public GraphWorkload
{
  private:
    Addr initAddr;
    uint32_t initValue;

    int currentDepth;

    uint32_t depthMask;
    uint32_t countMask;
  public:
    BSPBCWorkload(Addr init_addr, uint32_t init_value):
        initAddr(init_addr), initValue(init_value),
        currentDepth(0), depthMask(4278190080), countMask(16777215)
    {}

    ~BSPBCWorkload() {}

    virtual void init(PacketPtr pkt, WorkDirectory* dir);
    virtual uint32_t reduce(uint32_t update, uint32_t value);
    virtual uint32_t propagate(uint32_t value, uint32_t weight);
    virtual uint32_t apply(WorkListItem& wl);
    virtual void iterate() { currentDepth++; }
    virtual void interIterationInit(WorkListItem& wl);
    virtual bool activeCondition(WorkListItem new_wl, WorkListItem old_wl);
    virtual std::string printWorkListItem(const WorkListItem wl);
};

}

#endif // __ACCL_GRAPH_BASE_GRAPH_WORKLOAD_HH__