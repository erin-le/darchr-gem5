/*# Copyright (c) 2019 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Nima Ganjehloo
*/

//gem5 gemeral includes
#include "base/logging.hh"
#include "debug/Verilator.hh"

//gem5 model includes
#include "verilator_mem_black_box.hh"

//MASTER PORT DEFINITIONS
//send packet to gem5 memory system,schedules an event
void
VerilatorMemBlackBox::VerilatorMemBlackBoxPort
  ::sendTimingPacket(PacketPtr pkt)
{
  panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
  //send packet or block it and save packet for a retry
  if (!sendTimingReq(pkt)) {
    DPRINTF(Verilator, "Packet for addr: %#x blocked\n", pkt->getAddr());
    blockedPacket = pkt;
  }
}

//sends a packet to gem5 memory system, recieves response
//immidiately at end of call chain
bool
VerilatorMemBlackBox::VerilatorMemBlackBoxPort
  ::sendAtomicPacket(PacketPtr pkt)
{
  panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");
  //send packet or block it and save packet for a retry
  //block if response latency is non zero
  //
  //....i dont think blocking the packet here is right. do atomics ever get
  //request retries?
  if (!sendAtomic(pkt)) {
    DPRINTF(Verilator, "ATOMIC MEMORY RESPONSE RECIEVED\n");
    //let blackbox determine how to deal with returned data
    return owner->handleResponse(pkt);
  }else{
    DPRINTF(Verilator, "Packet for addr: %#x blocked\n", pkt->getAddr());
    blockedPacket = pkt;
  }
  return false;
}

//gem5 memory model has reponded to our memory request
bool
VerilatorMemBlackBox::VerilatorMemBlackBoxPort
    ::recvTimingResp( PacketPtr pkt )
{
  DPRINTF(Verilator, "MEMORY RESPONSE RECIEVED\n");
  //let blackbox determine how to deal with returned data
  return owner->handleResponse(pkt);
}

//retry sending a packet if it failed
void
VerilatorMemBlackBox::VerilatorMemBlackBoxPort
  ::recvReqRetry()
{
  //we have to had saved the failed packet before doing a retry
  assert(blockedPacket != nullptr);

  //move data around
  PacketPtr pkt = blockedPacket;
  blockedPacket = nullptr;

  //try request again
  sendAtomicPacket(pkt);
}

//used for configuring simulated device
BaseMasterPort&
VerilatorMemBlackBox::getMasterPort(
        const std::string& if_name, PortID idx )
{
  panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

  // This is the name from the Python SimObject declaration (SimpleMemobj.py)
  if (if_name == "inst_port") {
    return instPort;
  } else if (if_name == "data_port") {
    return dataPort;
  } else {
    // pass it along to our super class
    return MemObject::getMasterPort(if_name, idx);
  }
}


//Create for memblkbx for gem5
VerilatorMemBlackBox*
VerilatorMemBlackBoxParams::create()
{
  return new VerilatorMemBlackBox(this);
}

//setup our black box
VerilatorMemBlackBox::VerilatorMemBlackBox(
        VerilatorMemBlackBoxParams *params) :
    MemObject(params),
    instPort(params->name + ".inst_port", this),
    dataPort(params->name + ".data_port", this)
{
    VerilatorMemBlackBox::singleton =  this;
}

//sets up a instruction fetch request for gem5 memory system
void
VerilatorMemBlackBox::doFetch(int imem_address)
{
  //dinocpu only uses 4 byte instructions so make 4 byte request with address
  //specified by dinocpu
  RequestPtr ifetch_req = std::make_shared<Request>(
    imem_address,
    4,
    Request::INST_FETCH,
    0);

  DPRINTF(Verilator, "Sending fetch for addr (pa: %#x)\n",
    ifetch_req->getPaddr());

  //create a read request packet
  PacketPtr pkt = new Packet(ifetch_req, MemCmd::ReadReq);

  DPRINTF(Verilator, " -- pkt addr: %#x\n", pkt->getAddr());

  //allocate space for instruction and send through instPort
  pkt->allocate();
  instPort.sendAtomicPacket(pkt);
}

//sets up a memory request for memory instructions to gem5 memory sytem
void
VerilatorMemBlackBox::doMem(int dmem_address, int dmem_writedata,
        unsigned char dmem_memread,unsigned char dmem_memwrite,
        const svBitVecVal* dmem_maskmode, unsigned char dmem_sext)
{
    //by default assume lw or sw (4 byte request)
  unsigned int maskmode = 4;
    //are we reading or writing?
  bool read = dmem_memread;
  bool write = dmem_memwrite;
  //determine size to access if we arent a lw or sw
  if ( *dmem_maskmode == 2 )
    maskmode = 4;
  else if ( *dmem_maskmode == 1 )
    maskmode = 2;
  else
    maskmode = 1;

  //make request for corresponding byte size at the specified address provided
  //by dinocpu
  RequestPtr data_req = std::make_shared<Request>(
    dmem_address,
    maskmode,
    Request::PHYSICAL,
    0);

  DPRINTF(Verilator, "Sending data request for addr (pa: %#x)\n",
    data_req->getPaddr());

  //Is the packet a read or write request?
  PacketPtr pkt = read || !(read || write)? Packet::createRead(data_req)
    : Packet::createWrite(data_req);

  DPRINTF(Verilator, " -- pkt addr: %#x\n", pkt->getAddr());

  //not sure if I need this. This is for non 4 byte writes.
  uint8_t * data = new uint8_t[4];
  if (write){
    DPRINTF(Verilator, "WRITING %x AS %d BYTES\n", dmem_writedata, maskmode);
    for (int i = 0; i < 4; ++i){
      data[i] = dmem_writedata & (0xFF << i);
    }

    DPRINTF(Verilator, "DATA TO WRITE IS %x %x %x %x\n", data[0], data[1],
            data[2], data[3]);
  }

  //allocate space for memory request
  pkt->allocate();
  pkt->setData(data);
  delete[] data;

  //send request
  dataPort.sendAtomicPacket(pkt);
}

//handles a successful response for a memory request
bool
VerilatorMemBlackBox::handleResponse( PacketPtr pkt )
{
  DPRINTF(Verilator, "Got response for addr %#x\n", pkt->getAddr());

  //see if we are a inst fetch or a read only mem instruction
  if (pkt->req->isInstFetch()) {
    //Get returned data from gem5 mem system and set it to blackbox
    //"wires"
    DPRINTF(Verilator, "Handling response for IFETCH\n");
    imemResp = *pkt->getConstPtr<uint32_t>();
  //  blkbox->imem_dataout = *pkt->getConstPtr<uint32_t>();
    DPRINTF(Verilator, "Instruction is %#x\n", imemResp);
  } else if (pkt->isRead()){
    //Get returned data from gem5 mem system and set it to blackbox
    //"wires"
    DPRINTF(Verilator, "Handling response for data read\n");
    dmemResp = *pkt->getConstPtr<uint32_t>();
//    blkbox->dmem_dataout = *pkt->getConstPtr<uint32_t>();
    DPRINTF(Verilator, "Data is %#x\n", dmemResp);
  }

  return true;
}

//setup static var
VerilatorMemBlackBox *
VerilatorMemBlackBox::singleton = nullptr;

//give caller (dpi in this case) access to this class
VerilatorMemBlackBox *
VerilatorMemBlackBox::getSingleton()
{
  return VerilatorMemBlackBox::singleton;
}

//setup reference to this class for use with dpi
void
VerilatorMemBlackBox::startup()
{
  DPRINTF(Verilator, "MEM BLACKBOX STARTUP\n");
  VerilatorMemBlackBox::singleton = this;
}

uint32_t
VerilatorMemBlackBox::getDmemResp()
{
  return dmemResp;
}

uint32_t
VerilatorMemBlackBox::getImemResp()
{
  return imemResp;
}
