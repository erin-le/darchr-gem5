/*
 * Copyright (c) 2018 The Regents of The University of California
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
 *
 * Authors: Bradley Wang
 */

#include "cpu/flexcpu/inflight_inst.hh"
#include "debug/FlexPipeView.hh"

using namespace std;

using PCState = TheISA::PCState;

using VecElem = TheISA::VecElem;

InflightInst::InflightInst(ThreadContext* backing_context,
                           TheISA::ISA* backing_isa,
                           MemIface* backing_mem_iface,
                           X86Iface* backing_x86_iface,
                           InstSeqNum seq_num,
                           InstSeqNum issue_seq_num,
                           const PCState& pc_,
                           StaticInstPtr inst_ref):
    backingContext(backing_context),
    backingISA(backing_isa),
    backingMemoryInterface(backing_mem_iface),
    backingX86Interface(backing_x86_iface),
    _status(Empty),
    _seqNum(seq_num),
    _issueSeqNum(issue_seq_num),
    _pcState(pc_),
    predicate(true)
{
    _timingRecord.creationTick = curTick();
    staticInst(inst_ref);
}

InflightInst::~InflightInst()
{
    // _traceData is nullptr by default, and one (should be) uniquely allocated
    // and assigned to each InflightInst instance. Therefore we simply delete
    // the object along with the instance, when the instance is no longer
    // needed.
    if (_traceData) delete _traceData;
}

void InflightInst::pipeTrace()
{
#if TRACING_ON
    // Output the information about the lifecycle of a dynamic instruction in
    // the pipeline including issueSeqNum, which is unique to each created
    // InflightInst, followed by pc, micro pc, the static instruction if
    // exists, and the Tick's when the instruction's state changes.
    if (DTRACE(FlexPipeView)) {
        if (!static_cast<bool>(instRef)) {
            DPRINTFR(FlexPipeView,
                     "pipe;%llu;%llx;%llx;null;%d;"
                     "%llu;%llu;%llu;%llu;%llu;%llu;%llu;%llu;%llu\n",
                     this->issueSeqNum(),
                     this->_pcState.pc(),
                     this->_pcState.upc(),
                     this->predicate,
                     this->_timingRecord.creationTick,
                     this->_timingRecord.decodeTick,
                     this->_timingRecord.issueTick,
                     this->_timingRecord.beginExecuteTick,
                     this->_timingRecord.effAddredTick,
                     this->_timingRecord.beginMemoryTick,
                     this->_timingRecord.completionTick,
                     this->_timingRecord.commitTick,
                     this->_timingRecord.squashTick);
        } else {
            DPRINTFR(FlexPipeView,
                     "pipe;%llu;%llx;%llx;%s;%d;"
                     "%llu;%llu;%llu;%llu;%llu;%llu;%llu;%llu;%llu\n",
                     this->issueSeqNum(),
                     this->_pcState.pc(),
                     this->_pcState.upc(),
                     this->instRef->disassemble(this->_pcState.npc()),
                     this->predicate,
                     this->_timingRecord.creationTick,
                     this->_timingRecord.decodeTick,
                     this->_timingRecord.issueTick,
                     this->_timingRecord.beginExecuteTick,
                     this->_timingRecord.effAddredTick,
                     this->_timingRecord.beginMemoryTick,
                     this->_timingRecord.completionTick,
                     this->_timingRecord.commitTick,
                     this->_timingRecord.squashTick);
        }
    }
#endif
}

void
InflightInst::addBeginExecCallback(function<void()> callback)
{
    beginExecCallbacks.push_back(move(callback));
}

void
InflightInst::addBeginExecDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isExecuting()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addBeginExecCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingDependencies;

        if (!inst_ptr->remainingDependencies) {
            inst_ptr->notifyReady();
        }
    });
}

void
InflightInst::addCommitCallback(function<void()> callback)
{
    commitCallbacks.push_back(move(callback));
}

void
InflightInst::addCommitDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isCommitted()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addCommitCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingDependencies;

        if (!inst_ptr->remainingDependencies) {
            inst_ptr->notifyReady();
        }
    });
}

void
InflightInst::addCompletionCallback(function<void()> callback)
{
    completionCallbacks.push_back(move(callback));
}

void
InflightInst::addDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isComplete()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addCompletionCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingDependencies;

        if (!inst_ptr->remainingDependencies) {
            inst_ptr->notifyReady();
        }
    });
}


void
InflightInst::addEffAddrCalculatedCallback(function<void()> callback)
{
    effAddrCalculatedCallbacks.push_back(move(callback));
}

void
InflightInst::addMemReadyCallback(function<void()> callback)
{
    memReadyCallbacks.push_back(move(callback));
}

void
InflightInst::addMemCommitDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isCommitted()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingMemDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addCommitCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingMemDependencies;

        if (!inst_ptr->remainingMemDependencies) {
            inst_ptr->notifyMemReady();
        }
    });
}

void
InflightInst::addMemDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isComplete()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingMemDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addCompletionCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingMemDependencies;

        if (!inst_ptr->remainingMemDependencies) {
            inst_ptr->notifyMemReady();
        }
    });
}

void
InflightInst::addMemEffAddrDependency(InflightInst& parent)
{
    if (parent.isSquashed() || parent.isEffAddred()) return;

    // If the parent instruction is not predicated then the dependency doesn't
    // exist
    if (!parent.readPredicate()) return;

    ++remainingMemDependencies;

    weak_ptr<InflightInst> weak_this = shared_from_this();
    parent.addEffAddrCalculatedCallback([weak_this]() {
        shared_ptr<InflightInst> inst_ptr = weak_this.lock();
        if (!inst_ptr) return;

        --inst_ptr->remainingMemDependencies;

        if (!inst_ptr->remainingMemDependencies) {
            inst_ptr->notifyMemReady();
        }
    });
}

void
InflightInst::addReadyCallback(function<void()> callback)
{
    readyCallbacks.push_back(move(callback));
}

void
InflightInst::addRetireCallback(function<void()> callback)
{
    retireCallbacks.push_back(move(callback));
}

void
InflightInst::addSquashCallback(function<void()> callback)
{
    squashCallbacks.push_back(move(callback));
}

void
InflightInst::commitToTC()
{
    // If the instruction is predicated to false, we don't need to update the
    // registers.
    if (!this->readPredicate()) {
        TheISA::PCState pc = pcState();
        instRef->advancePC(pc);
        backingContext->pcState(pc);
        backingContext->getCpuPtr()->probeInstCommit(instRef, pc.instAddr());
        return;
    }

    const int8_t num_dsts = instRef->numDestRegs();
    for (int8_t dst_idx = 0; dst_idx < num_dsts; ++dst_idx) {
        const RegId& dst_reg = instRef->destRegIdx(dst_idx);

        // If aarch32 needs to deal with PC register, we don't support aarch32
        // To support aarch32, resultValid of PC register is never valid, so
        // if dst_reg is the PC register, we need to skip.

        const GenericReg& result = getResult(dst_idx);

        switch (dst_reg.classValue()) {
          case IntRegClass:
            backingContext->setIntReg(dst_reg.index(), result.asIntReg());
            break;
          case FloatRegClass:
            backingContext->setFloatReg(dst_reg.index(),
                                        result.asFloatRegBits());
            break;
          case VecRegClass:
            backingContext->setVecReg(dst_reg, result.asVecReg());
            break;
          case VecElemClass:
            backingContext->setVecElem(dst_reg, result.asVecElem());
            break;
          case CCRegClass:
            backingContext->setCCReg(dst_reg.index(), result.asCCReg());
            break;
          case MiscRegClass:
            backingContext->setMiscReg(dst_reg.index(), result.asMiscReg());
            break;
          default:
            break;
        }
    }

    TheISA::PCState pc = pcState();
    instRef->advancePC(pc);
    backingContext->pcState(pc);

    for (size_t i = 0; i < miscResultIdxs.size(); i++)
        backingISA->setMiscReg(
            miscResultIdxs[i], miscResultVals[i], backingContext);

    backingContext->getCpuPtr()->probeInstCommit(instRef, pc.instAddr());
}

bool
InflightInst::effAddrOverlap(const InflightInst& other) const
{
    assert(accessedPAddrsValid && other.accessedPAddrsValid
        && (!_isSplitMemReq || accessedSplitPAddrsValid)
        && (!other._isSplitMemReq || other.accessedSplitPAddrsValid));

    return accessedPAddrs.intersects(other.accessedPAddrs)
        || (_isSplitMemReq
         && accessedSplitPAddrs.intersects(other.accessedPAddrs))
        || (other._isSplitMemReq
         && accessedPAddrs.intersects(other.accessedSplitPAddrs))
        || (_isSplitMemReq && other._isSplitMemReq
         && accessedSplitPAddrs.intersects(other.accessedSplitPAddrs));
}

bool
InflightInst::effPAddrSuperset(const InflightInst& other) const
{
    assert(accessedPAddrsValid && other.accessedPAddrsValid
        && (!_isSplitMemReq || accessedSplitPAddrsValid)
        && (!other._isSplitMemReq || other.accessedSplitPAddrsValid));

    // In order to simplify this calculation, we also make the assumption
    // MinorCPU seems to make where split requests will not span page
    // boundaries, so physical addresses will remain contiguous.
    assert(!_isSplitMemReq
        || accessedPAddrs.end() + 1 == accessedSplitPAddrs.start());
    assert(!other._isSplitMemReq
        || other.accessedPAddrs.end() + 1
           == other.accessedSplitPAddrs.start());

    const Addr our_start = accessedPAddrs.start();
    const Addr our_end = _isSplitMemReq ?
        accessedSplitPAddrs.end() : accessedPAddrs.end();

    const Addr other_start = other.accessedPAddrs.start();
    const Addr other_end = other._isSplitMemReq ?
        other.accessedSplitPAddrs.end() : other.accessedPAddrs.end();

    return our_start <= other_start && other_end <= our_end;
}

void
InflightInst::forwardDestRegsFromProducers()
{
    const int8_t num_dsts = instRef->numDestRegs();
    for (int8_t dst_idx = 0; dst_idx < num_dsts; ++dst_idx) {
        const DataSource data_src = destRegPrevProducer[dst_idx];
        auto producer = data_src.producer.lock();
        auto producer_dst_idx = data_src.resultIdx;

        if (producer) {
            assert(!producer->isSquashed());
            assert(producer->issueSeqNum() < this->issueSeqNum());
            switch (instRef->destRegIdx(dst_idx).classValue()) {
                case IntRegClass:
                    setIntRegOperand(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asIntReg());
                    break;
                case FloatRegClass:
                    setFloatRegOperandBits(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asFloatRegBits());
                    break;
                case VecRegClass:
                    setVecRegOperand(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asVecReg());
                    break;
                case VecElemClass:
                    setVecElemOperand(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asVecElem());
                    break;
                case VecPredRegClass:
                    setVecPredRegOperand(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asVecPredReg());
                    break;
                case CCRegClass:
                    setCCRegOperand(staticInst().get(), dst_idx,
                       producer->getResult(producer_dst_idx).asCCReg());
                    break;
                case MiscRegClass:
                    // no need to forward misc reg values
                    break;
                default:
                    panic("Unknown register class: %d",
                            (int)instRef->destRegIdx(dst_idx).classValue());
            }

        } else {
            auto reg_idx = staticInst()->destRegIdx(dst_idx).flatIndex();
            switch (instRef->destRegIdx(dst_idx).classValue()) {
                case IntRegClass:
                    setIntRegOperand(staticInst().get(), dst_idx,
                                     backingContext->readIntReg(reg_idx));
                    break;
                case FloatRegClass:
                    setFloatRegOperandBits(staticInst().get(), dst_idx,
                                        backingContext->readFloatReg(reg_idx));
                    break;
                case VecRegClass:
                    setVecRegOperand(staticInst().get(), dst_idx,
                        backingContext->readVecReg(
                            staticInst()->destRegIdx(dst_idx)));
                    break;
                case VecElemClass:
                    setVecElemOperand(staticInst().get(), dst_idx,
                        backingContext->readVecElem(
                            staticInst()->destRegIdx(dst_idx)));
                    break;
                case VecPredRegClass:
                    setVecPredRegOperand(staticInst().get(), dst_idx,
                        backingContext->readVecPredReg(
                            staticInst()->destRegIdx(dst_idx)));
                    break;
                case CCRegClass:
                    setCCRegOperand(staticInst().get(), dst_idx,
                                    backingContext->readCCReg(reg_idx));
                    break;
                case MiscRegClass:
                    // no need to forward misc reg values
                    break;
                default:
                    panic("Unknown register class: %d",
                            (int)instRef->destRegIdx(dst_idx).classValue());
            }
        }
    }
}

void
InflightInst::notifyCommitted()
{
    assert(!isSquashed());

    _timingRecord.commitTick = curTick();
    status(Committed);

    for (function<void()>& callback_func : commitCallbacks) {
        callback_func();
    }
    for (function<void()>& callback_func : retireCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifyComplete()
{
    _timingRecord.completionTick = curTick();
    status(Complete);

    for (function<void()>& callback_func : completionCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifyDecoded()
{
    _timingRecord.decodeTick = curTick();
    status(Decoded);
}

void
InflightInst::notifyEffAddrCalculated()
{
    _timingRecord.effAddredTick = curTick();
    status(EffAddred);

    for (function<void()>& callback_func : effAddrCalculatedCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifyExecuting()
{
    _timingRecord.beginExecuteTick = curTick();
    status(Executing);

    for (function<void()>& callback_func : beginExecCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifyIssued()
{
    _timingRecord.issueTick = curTick();
    status(Issued);
}

void
InflightInst::notifyMemorying()
{
    _timingRecord.beginMemoryTick = curTick();
    status(Memorying);
}

void
InflightInst::notifyMemReady()
{
    for (function<void()>& callback_func : memReadyCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifyReady()
{
    for (function<void()>& callback_func : readyCallbacks) {
        callback_func();
    }
}

void
InflightInst::notifySquashed()
{
    if (isSquashed()) return;

    assert(!isCommitted());

    _timingRecord.squashTick = curTick();
    _squashed = true;

    for (function<void()>& callback_func : squashCallbacks) {
        callback_func();
    }
    for (function<void()>& callback_func : retireCallbacks) {
        callback_func();
    }
}

void
InflightInst::setDataSource(int8_t src_idx, DataSource source)
{
    sources[src_idx] = source;
}

void
InflightInst::setDestRegPrevProducer(int this_dst_idx, DataSource data_src)
{
    destRegPrevProducer[this_dst_idx] = data_src;
}

const StaticInstPtr&
InflightInst::staticInst(const StaticInstPtr& inst_ref)
{
    instRef = inst_ref;

    if (inst_ref) {
        const int8_t num_dsts = inst_ref->numDestRegs();
        for (int8_t i = 0; i < num_dsts; ++i) {
            // TODO consider setting class to corresponding RegId, and filling
            // values in, in-case of conditional register access.
            // Fill results table with dummy values
            results.push_back(GenericReg(0, IntRegClass));
            resultValid.push_back(false);
        }

        const int8_t num_srcs = inst_ref->numSrcRegs();
        for (int8_t i = 0; i < num_srcs; ++i) {
            sources.push_back({});
        }
    }

    return instRef;
}

// BEGIN ExecContext interface functions

RegVal
InflightInst::readIntRegOperand(const StaticInst* si, int op_idx)
{
    // Sanity check
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isIntReg());

    if (reg_id.isZeroReg()) return 0;

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asIntReg();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readIntReg(reg_id.index());
    }
}


void
InflightInst::setIntRegOperand(const StaticInst* si, int dst_idx, RegVal val)
{
    const RegId& reg_id = si->destRegIdx(dst_idx);
    assert(reg_id.isIntReg());

    results[dst_idx].set(reg_id.isZeroReg() ? 0 : val, IntRegClass);
    resultValid[dst_idx] = true;
}


RegVal
InflightInst::readFloatRegOperandBits(const StaticInst* si, int op_idx)
{
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isFloatReg());

    if (reg_id.isZeroReg()) return 0;

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asFloatRegBits();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readFloatReg(reg_id.index());
    }
}

void
InflightInst::setFloatRegOperandBits(const StaticInst* si, int dst_idx,
                                     RegVal val)
{
    const RegId& reg_id = si->destRegIdx(dst_idx);
    assert(reg_id.isFloatReg());

    results[dst_idx].set(reg_id.isZeroReg() ? 0 : val, FloatRegClass);
    resultValid[dst_idx] = true;
}

const TheISA::VecRegContainer&
InflightInst::readVecRegOperand(const StaticInst* si, int op_idx) const
{
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isVecReg());

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asVecReg();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readVecReg(reg_id);
    }
}

TheISA::VecRegContainer&
InflightInst::getWritableVecRegOperand(const StaticInst* si, int op_idx)
{
    // TODO this one may produce bad results, need to rethink this, since if
    //      we write to a srcreg, then the dependencies might not be matched
    //      correctly, nor will the changed state necessarily be applied to the
    //      committed state during commit time.

    const RegId& reg_id = si->destRegIdx(op_idx);
    assert(reg_id.isVecReg());

    // This function returns a reference to the vector register, and the ISA
    // will modify directly on the vector without using other functions that
    // write to register. Prior values of the register are not used as well.
    // So, we can treat this function the same as other set register functions.
    // Therefore, it is necessary to set the type of the register to vector,
    // and to set that the register is updated.
    results[op_idx].setAsVecReg();
    resultValid[op_idx] = true;
    return this->getResult(op_idx).asVecReg();
}

void
InflightInst::setVecRegOperand(const StaticInst* si, int dst_idx,
                               const TheISA::VecRegContainer& val)
{
    assert(si->destRegIdx(dst_idx).isVecReg());

    // NOTE This assignment results in two calls to copy the VecRegContainer
    // object, one to construct the temporary GenericReg, the other to
    // perform the copy into the results table. There may be a more efficient
    // way to do this, perhaps with an in-place "set" function.
    results[dst_idx].set(val, VecRegClass);
}

ConstVecLane8
InflightInst::readVec8BitLaneOperand(const StaticInst* si, int op_idx) const
{
    panic("readVec8BitLaneOperand() not implemented!");
    // To make sure compiler doesn't complain
    return backingContext->readVec8BitLaneReg(si->srcRegIdx(op_idx));
    // TODO
}

ConstVecLane16
InflightInst::readVec16BitLaneOperand(const StaticInst* si, int op_idx) const
{
    panic("readVec16BitLaneOperand() not implemented!");
    // To make sure compiler doesn't complain
    return backingContext->readVec16BitLaneReg(si->srcRegIdx(op_idx));
    // TODO
}

ConstVecLane32
InflightInst::readVec32BitLaneOperand(const StaticInst* si, int op_idx) const
{
    panic("readVec32BitLaneOperand() not implemented!");
    // To make sure compiler doesn't complain
    return backingContext->readVec32BitLaneReg(si->srcRegIdx(op_idx));
    // TODO
}

ConstVecLane64
InflightInst::readVec64BitLaneOperand(const StaticInst* si, int op_idx) const
{
    panic("readVec64BitLaneOperand() not implemented!");
    // To make sure compiler doesn't complain
    return backingContext->readVec64BitLaneReg(si->srcRegIdx(op_idx));
    // TODO
}


void
InflightInst::setVecLaneOperand(const StaticInst* si, int dst_idx,
                        const LaneData<LaneSize::Byte>& val)
{
    panic("setVecLaneOperand() not implemented!");
    // TODO
}

void
InflightInst::setVecLaneOperand(const StaticInst* si, int dst_idx,
                        const LaneData<LaneSize::TwoByte>& val)
{
    panic("setVecLaneOperand() not implemented!");
    // TODO
}

void
InflightInst::setVecLaneOperand(const StaticInst* si, int dst_idx,
                        const LaneData<LaneSize::FourByte>& val)
{
    panic("setVecLaneOperand() not implemented!");
    // TODO
}

void
InflightInst::setVecLaneOperand(const StaticInst* si, int dst_idx,
                        const LaneData<LaneSize::EightByte>& val)
{
    panic("setVecLaneOperand() not implemented!");
    // TODO
}

VecElem
InflightInst::readVecElemOperand(const StaticInst* si, int op_idx) const
{
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isVecElem());

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asVecElem();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readVecElem(reg_id);
    }
}

void
InflightInst::setVecElemOperand(const StaticInst* si, int dst_idx,
                                const VecElem val)
{
    const RegId& reg_id = si->destRegIdx(dst_idx);
    assert(reg_id.isVecElem());

    results[dst_idx].set(val, VecElemClass);
    resultValid[dst_idx] = true;
}

const TheISA::VecPredRegContainer&
InflightInst::readVecPredRegOperand(const StaticInst *si, int idx) const
{
    panic("readVecPredRegOperand() not implemented!");
    return *(VecPredRegContainer*)nullptr;
    // TODO
}

TheISA::VecPredRegContainer&
InflightInst::getWritableVecPredRegOperand(const StaticInst *si, int idx)
{
    panic("getWritableVecPredRegOperand() not implemented!");
    return *(VecPredRegContainer*)nullptr;
    // TODO
}

void
InflightInst::setVecPredRegOperand(const StaticInst *si, int idx,
                                   const TheISA::VecPredRegContainer& val)
{
    panic("setVecPredRegOperand() not implemented!");
    // TODO
}

RegVal
InflightInst::readCCRegOperand(const StaticInst* si, int op_idx)
{
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isCCReg());

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asCCReg();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readCCReg(reg_id.index());
    }
}

void
InflightInst::setCCRegOperand(const StaticInst* si, int dst_idx, RegVal val)
{
    assert(si->destRegIdx(dst_idx).isCCReg());

    results[dst_idx].set(val, CCRegClass);
    resultValid[dst_idx] = true;
}

RegVal
InflightInst::readMiscRegOperand(const StaticInst* si, int op_idx)
{
    const RegId& reg_id = si->srcRegIdx(op_idx);
    assert(reg_id.isMiscReg());

    const DataSource& source = sources[op_idx];
    const shared_ptr<InflightInst> producer = source.producer.lock();

    if (producer) {
        // If the producing instruction is still in the buffer, grab the result
        // assuming that it has been produced, and our index is in bounds.
        assert(producer->isComplete());
        assert(source.resultIdx >= 0
            && source.resultIdx < producer->results.size()
            && producer->resultValid[source.resultIdx]);
        return producer->getResult(source.resultIdx).asMiscReg();
    } else {
        // Either the producing instruction has already been committed, or no
        // dependency was in-flight at the time that this instruction was
        // issued.
        return backingContext->readMiscReg(reg_id.index());
    }
}

void
InflightInst::setMiscRegOperand(const StaticInst* si, int dst_idx, RegVal val)
{
    assert(si->destRegIdx(dst_idx).isMiscReg());

    results[dst_idx].set(val, MiscRegClass);
    resultValid[dst_idx] = true;
}

RegVal
InflightInst::readMiscReg(int misc_reg)
{
    return backingISA->readMiscReg(misc_reg, backingContext);
}

void
InflightInst::setMiscReg(int misc_reg, RegVal val)
{
    // O3 hides multiple writes to the same misc reg during execution, but due
    // to potential side effects of each access, I don't think we should be
    // hiding them. Instead, we will replay each write sequentially during
    // commit time.

    miscResultIdxs.push_back(misc_reg);
    miscResultVals.push_back(val);
    // Values should be applied to the ISA/TC at commit time, when we know the
    // instruction will not be squashed.
}

PCState
InflightInst::pcState() const
{
    return _pcState;
}

void
InflightInst::pcState(const PCState& val)
{
    _pcState = val;
}

Fault
InflightInst::readMem(Addr addr, uint8_t *data, unsigned int size,
                      Request::Flags flags, const vector<bool>& byte_enable)
{
    if (backingMemoryInterface) {
        return backingMemoryInterface->readMem(shared_from_this(), addr, data,
                                               size, flags, byte_enable);
    } else {
        panic("Attempted to readMem() without a memory interface!");
        return NoFault;
    }
}

Fault
InflightInst::initiateMemRead(Addr addr, unsigned int size,
                              Request::Flags flags,
                              const vector<bool>& byte_enable)
{
    if (backingMemoryInterface) {
        if (_traceData) _traceData->setMem(addr, size, flags);
        return backingMemoryInterface->initiateMemRead(shared_from_this(),
            addr, size, flags, byte_enable);
    } else {
        panic("Attempted to initiateMemRead() without a memory interface!");
        return NoFault;
    }
}

Fault
InflightInst::writeMem(uint8_t* data, unsigned int size, Addr addr,
                       Request::Flags flags, uint64_t* res,
                       const vector<bool>& byte_enable)
{
    if (backingMemoryInterface) {
        if (_traceData) _traceData->setMem(addr, size, flags);
        return backingMemoryInterface->writeMem(shared_from_this(), data, size,
                                                addr, flags, res, byte_enable);
    } else {
        panic("Attempted to writeMem() without a memory interface!");
        return NoFault;
    }
}

void
InflightInst::setStCondFailures(unsigned int sc_failures)
{
    panic("setStCondFailures() not implemented!");
    // TODO
}

unsigned int
InflightInst::readStCondFailures() const
{
    panic("readStCondFailures() not implemented!");
    return 0;
    // TODO
}

void
InflightInst::syscall(int64_t callnum, Fault* fault)
{
    TheISA::PCState pc = backingContext->pcState();
    backingContext->syscall(callnum, fault);
    TheISA::PCState newpc = backingContext->pcState();
    assert(pc == newpc);
    if (pc != newpc) {
        pcState(newpc);
    }
}

ThreadContext*
InflightInst::tcBase()
{
    return backingContext;
}

bool
InflightInst::readPredicate() const
{
    return predicate;
}

void
InflightInst::setPredicate(bool val)
{
    predicate = val;
    if (_traceData) _traceData->setPredicate(val);
}

bool
InflightInst::readMemAccPredicate() const
{
    panic("readMemAccPredicate() not implemented!");
    return false;
    // TODO
}

void
InflightInst::setMemAccPredicate(bool val)
{
    panic("setMemAccPredicate() not implemented!");
    // TODO
}

void
InflightInst::demapPage(Addr vaddr, uint64_t asn)
{
    if (backingX86Interface) {
        backingX86Interface->demapPage(vaddr, asn);
    } else {
        panic("Attempted to demapPage() without a X86 interface!");
    }
}

void
InflightInst::armMonitor(Addr address)
{
    if (backingX86Interface) {
        backingX86Interface->armMonitor(address);
    } else {
        panic("Attempted to armMonitor() without a X86 interface!");
    }
}

bool
InflightInst::mwait(PacketPtr pkt)
{
    if (backingX86Interface) {
        return backingX86Interface->mwait(pkt);
    } else {
        panic("Attempted to mwait() without a X86 interface!");
        return false;
    }
}

void
InflightInst::mwaitAtomic(ThreadContext* tc)
{
    if (backingX86Interface) {
        backingX86Interface->mwaitAtomic(tc);
    } else {
        panic("Attempted to mwaitAtomic() without a X86 interface!");
    }
}

AddressMonitor*
InflightInst::getAddrMonitor()
{
    if (backingX86Interface) {
        return backingX86Interface->getAddrMonitor();
    } else {
        panic("Attempted to getAddrMonitor() without a X86 interface!");
    }
}

// END ExecContext interface functions
