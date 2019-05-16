/*
 * Copyright (c) 2013 ARM Limited
 * Copyright (c) 2014 Sven Karlsson
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2017 The University of Virginia
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
 * Authors: Andreas Hansson
 *          Sven Karlsson
 *          Alec Roelke
 */

#ifndef __ARCH_RISCV_TYPES_HH__
#define __ARCH_RISCV_TYPES_HH__

#include "arch/generic/types.hh"

namespace RiscvISA
{

typedef uint32_t MachInst;
typedef uint64_t ExtMachInst;

class PCState : public GenericISA::UPCState<MachInst>
{
  private:
    bool _compressed;
    bool _rv32;

  public:
    PCState() : UPCState() { _compressed = false; _rv32 = false; }
    PCState(Addr val) : UPCState(val) { _compressed = false; _rv32 = false; }

    void compressed(bool c) { _compressed = c; }
    bool compressed() { return _compressed; }

    void rv32(bool val) { _rv32 = val; }
    bool rv32() const { return _rv32; }

    bool
    branching() const
    {
        if (_compressed) {
            return npc() != pc() + sizeof(MachInst)/2 ||
                    nupc() != upc() + 1;
        } else {
            return npc() != pc() + sizeof(MachInst) ||
                    nupc() != upc() + 1;
        }
    }
  };

  // JMFIXME: Change in the future
  // JMNOTE: vec_reg.hh (L.159) sets the limit to 2048 (256 bytes).
  constexpr unsigned MaxUveVecLenInBits = 2048;
  static_assert(MaxUveVecLenInBits >= 64 &&
                  MaxUveVecLenInBits <= 8192 &&
                  MaxUveVecLenInBits % 64 == 0,
                  "Unsupported max. UVE vector length");
  constexpr unsigned MaxUveVecLenInBytes  = MaxUveVecLenInBits >> 3;
  constexpr unsigned MaxUveVecLenInWords  = MaxUveVecLenInBits >> 5;
  constexpr unsigned MaxUveVecLenInDWords = MaxUveVecLenInBits >> 6;

  constexpr unsigned VecRegSizeBytes = MaxUveVecLenInBytes;
  constexpr unsigned VecPredRegSizeBits = MaxUveVecLenInBytes;
  constexpr unsigned VecPredRegHasPackedRepr = false;
}

#endif // __ARCH_RISCV_TYPES_HH__
