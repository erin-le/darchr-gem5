# Copyright (c) 2023 The Regents of the University of California
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

from gem5.components.cachehierarchies.abstract_cache_hierarchy import (
    AbstractCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.abstract_two_level_cache_hierarchy import (
    AbstractTwoLevelCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.l1dcache import L1DCache
from gem5.components.cachehierarchies.classic.caches.l1icache import L1ICache
from gem5.components.cachehierarchies.classic.caches.l2cache import L2Cache
from gem5.components.cachehierarchies.classic.caches.mmu_cache import MMUCache
from gem5.components.boards.abstract_board import AbstractBoard
from gem5.isas import ISA
from m5.objects import Cache, L2XBar, BaseXBar, SystemXBar, BadAddr, Port

from gem5.utils.override import *
from typing import Type, Optional


# class CustomRISCVMatchedCacheHierarchy(RISCVMatchedCacheHierarchy):

#     def __init__(
#         self,
#         l2_size:str,
#         config_dict
#         ):
#         super().__init__(
#             self=self,
#             l2_size=l2_size
#         )
#         self._config_json = config_dict


class RISCVMatchedCacheHierarchy(
    AbstractClassicCacheHierarchy, AbstractTwoLevelCacheHierarchy
):
    """

    A cache setup where each core has a private L1 Data and Instruction Cache,
    and a shared L2 cache.
    The HiFive board has a partially inclusive cache hierarchy, hence this hierarchy is chosen.
    The details of the cache hierarchy are in Table 7, page 36 of the datasheet.

    - L1 Instruction Cache:
        - 32 KiB 4-way set associative
    - L1 Data Cache
        - 32 KiB 8-way set associative
    - L2 Cache
        - 2 MiB 16-way set associative

    """

    def __init__(self, l2_size: str, config_json) -> None:
        """
        :param l2_size: The size of the L2 Cache (e.g., "256kB").
        :type l2_size: str
        """
        # self.config_json = config_dict
        AbstractClassicCacheHierarchy.__init__(self=self)
        if config_json:
            cache_config = config_json["cache"]
            AbstractTwoLevelCacheHierarchy.__init__(
                self,
                l1i_size="32KiB",
                l1i_assoc=4,
                l1d_size="32KiB",
                l1d_assoc=8,
                l2_size=cache_config["l2_size"],
                l2_assoc=cache_config["l2_assoc"],
            )
            self._config_json = cache_config

        else:
            AbstractTwoLevelCacheHierarchy.__init__(
                self,
                l1i_size="32KiB",
                l1i_assoc=4,
                l1d_size="32KiB",
                l1d_assoc=8,
                l2_size=l2_size,
                l2_assoc=16,
            )
            self._config_json = None

        self.membus = SystemXBar(width=64)
        self.membus.badaddr_responder = BadAddr()
        self.membus.default = self.membus.badaddr_responder.pio

    @overrides(AbstractClassicCacheHierarchy)
    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board: AbstractBoard) -> None:

        # Set up the system port for functional access from the simulator.
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        self.l1icaches = [
            L1ICache(size=self._l1i_size, assoc=self._l1i_assoc)
            for i in range(board.get_processor().get_num_cores())
        ]

        if self._config_json:
            l1d_resp_latency = self._config_json["l1dcache-response_latency"]
            l2_data_latency = self._config_json["l2cache-data_latency"]
            iptw_cache_size = self._config_json["iptw_caches-size"]
            dptw_cache_size = self._config_json["dptw_caches-size"]

        else:
            l1d_resp_latency = 10
            l2_data_latency = 20
            iptw_cache_size = "4KiB"
            dptw_cache_size = "4KiB"

        self.l1dcaches = [
            L1DCache(
                size=self._l1d_size,
                assoc=self._l1d_assoc,
                response_latency=l1d_resp_latency,
            )
            for i in range(board.get_processor().get_num_cores())
        ]

        self.l2bus = L2XBar()

        self.l2cache = L2Cache(
            size=self._l2_size,
            assoc=self._l2_assoc,
            data_latency=l2_data_latency,
        )

        # ITLB Page walk caches
        self.iptw_caches = [
            MMUCache(size=iptw_cache_size)
            for _ in range(board.get_processor().get_num_cores())
        ]
        # DTLB Page walk caches
        self.dptw_caches = [
            MMUCache(size=dptw_cache_size)
            for _ in range(board.get_processor().get_num_cores())
        ]

        if board.has_coherent_io():
            self._setup_io_cache(board, self._config_json)

        for i, cpu in enumerate(board.get_processor().get_cores()):

            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)

            self.l1icaches[i].mem_side = self.l2bus.cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2bus.cpu_side_ports
            self.iptw_caches[i].mem_side = self.l2bus.cpu_side_ports
            self.dptw_caches[i].mem_side = self.l2bus.cpu_side_ports

            cpu.connect_walker_ports(
                self.iptw_caches[i].cpu_side, self.dptw_caches[i].cpu_side
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        self.l2bus.mem_side_ports = self.l2cache.cpu_side
        self.membus.cpu_side_ports = self.l2cache.mem_side

    def _setup_io_cache(self, board: AbstractBoard) -> None:

        """Create a cache for coherent I/O connections"""
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1kB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()
