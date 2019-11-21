#ifndef __UVE_SIMOBJS_FIFO_LOAD_HH__
#define __UVE_SIMOBJS_FIFO_LOAD_HH__

#include "arch/riscv/registers.hh"
#include "base/circlebuf.hh"
#include "debug/JMDEVEL.hh"
#include "debug/UVEFifo.hh"
#include "params/UVEStreamingEngine.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"
#include "uve_simobjs/fifo_utils.hh"
#include "uve_simobjs/utils.hh"

using CoreContainer = RiscvISA::VecRegContainer;
using ViewContainer = RiscvISA::VecReg;

class UVELoadFifo;

// Each fifo entry is a VecRegContainer extended with valid bits and
// a size counter.. this is to enable the ordered fill of each container
class FifoEntry : public CoreContainer {
   private:
    typedef enum { Complete, NotComplete, Clean } States;
    States rstate, cstate;
    uint16_t size, csize;
    uint16_t config_size;

   public:
    FifoEntry(uint8_t width, uint16_t _cfg_sz, int sid, int64_t ssid)
        : CoreContainer(),
          size(0),
          csize(0),
          config_size(_cfg_sz / 8) {  // Config size to be used in bytes
        this->zero();
        rstate = States::NotComplete;
        cstate = States::Clean;
        set_width(width);
        set_streaming(true);
        set_sid(sid);
        set_ssid(ssid);
    }
    bool complete() { return rstate == States::Complete; }
    bool ready() { return cstate == States::Complete; }

    void merge_data(uint8_t *data, uint16_t offset, uint16_t size);
    uint16_t getSize() { return size; }
    bool reserve(uint16_t *_size, bool last);
};

// Each fifo is composed of FifoEntry objects, which themselfs insert the data
// and verify themselfs as destination.
// If the data is not for the current entries, create another entry.
// StreamFifo only redirects the data, and gives statistics on how full it is
class StreamFifo {
   private:
    using FifoContainer = std::list<FifoEntry>;
    using SpeculativeIter = DumbIterator<FifoContainer>;
    typedef struct mapEntry {
        uint16_t id;
        uint16_t size;
        uint16_t offset;
        bool used;
        bool split;
        uint16_t id2;
        uint16_t size2;
        uint16_t offset2;
    } MapStruct;

    // Simple MapStruct
    MapStruct create_MS(uint16_t id, uint16_t size, uint16_t offset) {
        MapStruct new_ms;
        new_ms.id = id;
        new_ms.size = size;
        new_ms.offset = offset;
        new_ms.used = false;
        new_ms.split = false;
        return new_ms;
    }
    // Split MapStruct (For vector alignement)
    MapStruct create_MS(uint16_t id1, uint16_t id2, uint16_t size1,
                        uint16_t size2, uint16_t offset1, uint16_t offset2) {
        MapStruct new_ms;
        new_ms.split = true;

        new_ms.id = id1;
        new_ms.size = size1;
        new_ms.offset = offset1;
        new_ms.used = false;
        new_ms.id2 = id2;
        new_ms.size2 = size2;
        new_ms.offset2 = offset2;
        return new_ms;
    }

    FifoContainer *fifo_container;
    uint8_t max_size;
    uint32_t max_request_size;
    UVELoadFifo *owner;
    uint16_t config_size;
    SEIterationStatus status;
    using MapVector = std::vector<MapStruct>;
    MapVector map;

    SpeculativeIter speculationPointer;
    int my_id;

   public:
    StreamFifo(uint16_t _cfg_sz, uint8_t depth, uint32_t _max_request_size,
               int id)
        : max_size(depth),
          max_request_size(_max_request_size),
          owner(nullptr),
          config_size(_cfg_sz),
          status(SEIterationStatus::Clean),
          map(),
          my_id(id) {
        fifo_container = new FifoContainer();
        speculationPointer = SpeculativeIter(fifo_container);
    }

    void set_owner(UVELoadFifo *own) { owner = own; }
    void insert(uint16_t size, uint16_t ssid, uint8_t width, bool last);
    SmartReturn merge_data(uint16_t ssid, uint8_t *data);
    FifoEntry get();
    SmartReturn full();
    SmartReturn ready();
    SmartReturn squash();
    SmartReturn shouldSquash();
    void synchronizeLists();
    SmartReturn commit();
    SmartReturn empty();
    SmartReturn complete() {
        return SmartReturn::compare(status == SEIterationStatus::Ended);
    }

    uint16_t availableSpace() {
        uint16_t space = max_size * config_size;
        for (auto it = fifo_container->begin(); it != fifo_container->end();
             it++) {
            space -= it->getSize() * 8;
        }
        return space > max_request_size ? max_request_size : space;
    }

   private:
    uint16_t real_size();
};

// This is the load fifo object that contains one fifo per stream
class UVELoadFifo : public SimObject {
   private:
    std::vector<StreamFifo *> fifos;

   public:
    uint16_t cacheLineSize;
    UVEStreamingEngine *engine;
    UVEStreamingEngineParams *confParams;

   public:
    UVELoadFifo(UVEStreamingEngineParams *params);

    bool tick(CallbackInfo *res);
    SmartReturn getData(int sid);
    void init();
    SmartReturn insert(StreamID sid, uint32_t ssid, CoreContainer data);
    void reserve(StreamID sid, uint32_t ssid, uint8_t size, uint8_t width,
                 bool last);
    SmartReturn fetch(StreamID sid, CoreContainer **cnt);
    SmartReturn full(StreamID sid);
    SmartReturn ready(StreamID sid);
    SmartReturn squash(StreamID sid);
    SmartReturn shouldSquash(StreamID sid);
    SmartReturn commit(StreamID sid);
    void synchronizeLists(StreamID sid);
    SmartReturn clear(StreamID sid);
    SmartReturn isFinished(StreamID sid);
    uint16_t getAvailableSpace(StreamID sid);
};

#endif  //__UVE_SIMOBJS_FIFO_LOAD_HH__