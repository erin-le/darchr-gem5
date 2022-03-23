/*
 * Copyright (c) 2020 The Regents of the University of California.
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

#ifndef __ACCL_GRAPH_SEGA_APPLY_ENGINE_HH__
#define __ACCL_GRAPH_SEGA_APPLY_ENGINE_HH__

#include <queue>
#include <unordered_map>

#include "accl/graph/base/base_apply_engine.hh"
#include "accl/graph/sega/lock_dir.hh"
#include "accl/graph/sega/push_engine.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "params/ApplyEngine.hh"
#include "sim/clocked_object.hh"
#include "sim/port.hh"

namespace gem5
{


class ApplyEngine : public BaseApplyEngine
{
  private:
    PushEngine* pushEngine;
    LockDirectory* lockDir;

  protected:
    virtual bool sendApplyNotif(uint32_t prop,
        uint32_t degree, uint32_t edgeIndex) override;
    virtual bool acquireAddress(Addr addr) override;
    virtual bool releaseAddress(Addr addr) override;

  public:
    PARAMS(ApplyEngine);
    ApplyEngine(const ApplyEngineParams &params);
};

}

#endif // __ACCL_GRAPH_SEGA_APPLY_ENGINE_HH__