# Copyright (c) 2021 The Regents of the University of California
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

from abc import ABC, abstractmethod

from ..motherboards.abstract_motherboard import AbstractMotherboard

from m5.objects import  BaseCPU, BaseXBar
from m5.params import Port

from typing import Tuple

class AbstractCacheHierarchy(ABC):

    """
    A Cache Hierarchy incorporates any system components which manages
    communicaton between the processor and memory. E.g., Caches, the MemBus,
    MMU, and the MMU Cache.

    TODO: "CacheHierarchy" isn't the best of names for this. This could be
    improved.
    """

    @abstractmethod
    def incorporate_cache(self, motherboard: AbstractMotherboard):
        """
        Incorporates the caches into a board.

        Each specific hierarchy needs to implement this function and will be
        unique for each setup.

        TODO: This should probably be renamed. Perhaps "incorporate(...)" (?)

        :param motherboard: The motherboard in which the cache heirarchy is to
        be incorporated.

        :type motherboard: AbstractMotherboard
        """

        raise NotImplementedError

    @abstractmethod
    def get_interrupt_ports(self, cpu: BaseCPU) -> Tuple[Port, Port]:
        """
        Obtain the interupt ports.

        :param cpu: The CPU in which the interupt ports belong.

        :type cpu: BaseCPU

        :returns: A 2-dimensional tuple of the request and the responce port.

        :rtype: Tuple[Port, Port]
        """
        raise NotImplementedError

    @abstractmethod
    def get_membus(self) -> BaseXBar:
        """
        Obtain the Memory Bus.

        :returns: The memory bus.

        :rtype: BaseXBar
        """
        raise NotImplementedError