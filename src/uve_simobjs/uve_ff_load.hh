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
    bool commit_ready;
    Tick start_time;

   public:
    FifoEntry(uint8_t width, uint16_t _cfg_sz, int sid, SubStreamID ssid)
        : CoreContainer(),
          size(0),
          csize(0),
          config_size(_cfg_sz / 8),
          commit_ready(false),
          start_time(curTick()) {  // Config size to be used in bytes
        this->zero();
        rstate = States::NotComplete;
        cstate = States::Clean;
        set_width(width);
        set_streaming(true);
        set_sid(sid);
        set_ssid(ssid);
    }
    FifoEntry()
        : CoreContainer(),
          size(0),
          csize(0),
          config_size(0),
          commit_ready(false),
          start_time(0) {  // Config size to be used in bytes
        this->zero();
        rstate = States::NotComplete;
        cstate = States::Clean;
        set_width(0);
        set_streaming(false);
        set_sid(0);
        set_ssid(0);
    }
    ~FifoEntry() {}

    bool complete() const { return rstate == States::Complete; }
    bool ready() const { return cstate == States::Complete; }

    void merge_data(uint8_t *data, uint16_t offset, uint16_t size);
    uint16_t getSize() const { return size; }
    bool reserve(uint16_t *_size, bool * end_status, bool dim_end);
    void set_ready_to_commit() { commit_ready = true; }
    bool is_ready_to_commit() const { return commit_ready; }
    Tick time() { return start_time; }
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
        uint64_t id;
        uint16_t size;
        uint16_t offset;
        bool used;
        bool split;
        uint64_t id2;
        uint16_t size2;
        uint16_t offset2;
    } MapStruct;

    // Simple MapStruct
    MapStruct create_MS(uint64_t id, uint16_t size, uint16_t offset) {
        MapStruct new_ms;
        new_ms.id = id;
        new_ms.size = size;
        new_ms.offset = offset;
        new_ms.used = false;
        new_ms.split = false;
        return new_ms;
    }
    // Split MapStruct (For vector alignement)
    MapStruct create_MS(uint64_t id1, uint64_t id2, uint16_t size1,
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
    uint8_t total_size;
    uint32_t max_request_size;
    uint16_t config_size;
    SEIterationStatus status;
    using MapVector = std::vector<MapStruct>;
    MapVector map;

    SpeculativeIter speculationPointer;
    int my_id;
    bool load_nstore;

   public:
    StreamFifo(uint16_t _cfg_sz, uint8_t depth, uint32_t _max_request_size,
               int id, bool is_load=false)
        : max_size(depth),
          total_size(depth + 4),
          max_request_size(_max_request_size),
          config_size(_cfg_sz),
          status(SEIterationStatus::Clean),
          map(),
          my_id(id),
          load_nstore(is_load) {
        fifo_container = new FifoContainer();
        speculationPointer = SpeculativeIter(fifo_container);
    }

    void insert(uint16_t size, SubStreamID ssid, uint8_t width,  bool dim_end,
                bool * end_status);
    SmartReturn merge_data(SubStreamID ssid, uint8_t *data);
    SmartReturn merge_data_store(SubStreamID ssid, uint8_t *data,
                                 uint16_t valid);
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

    SmartReturn storeReady();
    SmartReturn storeSquash(SubStreamID ssid);
    SmartReturn storeCommit(SubStreamID ssid);
    SmartReturn storeDiscard(SubStreamID ssid);
    FifoEntry storeGet();

    uint16_t availableSpace() {
        uint16_t space = max_size * config_size;
        for (auto it = fifo_container->begin(); it != fifo_container->end();
             it++) {
            space -= it->getSize() * 8;
        }
        return space > max_request_size ? max_request_size : space;
    }

    uint16_t entries() {
        uint16_t _entries = 0;
        for (auto it = fifo_container->begin(); it != fifo_container->end();
             it++) {
            if (it->ready()) _entries++;
        }
        return _entries;
    }

   private:
    uint16_t real_size();

    std::string print_dimension_hop(uint64_t container){
            std::stringstream ostr;
            ostr << "[";
            for(int i=0; i<DimensionHop::dh_size; i++){
                if(((container & (1 << i)) > 0) && i == (uint64_t) DimensionHop::last){
                    ostr << "L";
                    break; 
                }
                if((container & (1 << i)) > 0) ostr << (int) i << ":";
            }
            ostr << "]";
            return ostr.str();
}

    const char *print_fifo() {
        std::stringstream sout;
        sout << "fifo(" << my_id << ") iter*(" << speculationPointer.getID()
             << ") [";
        auto iter = fifo_container->cbegin();
        while (iter != fifo_container->cend()) {
            if (iter != fifo_container->cbegin()) sout << ", ";
            sout << iter->get_ssid() << " ";
            sout << print_dimension_hop(iter->get_last());
            if (iter->get_ssid() == speculationPointer->get_ssid())
                sout << "*";
            if (iter->is_ready_to_commit()) sout << "C";
            if (iter->ready())
                sout << "F";  // F means full, C suits commit best
            if (iter->complete()) sout << "R";  // R means reserved
            iter++;
        }
        sout << "]";
        return sout.str().c_str();
    }
};

// This is the load fifo object that contains one fifo per stream
class UVELoadFifo : public SimObject {
   private:
    std::vector<StreamFifo *> fifos;

   public:
    uint16_t cacheLineSize;
    UVEStreamingEngine *engine;
    UVEStreamingEngineParams *confParams;
    unsigned fifo_depth;

   public:
    UVELoadFifo(UVEStreamingEngineParams *params);

    bool tick(CallbackInfo *res);
    SmartReturn getData(StreamID sid);
    void init();
    SmartReturn insert(StreamID sid, SubStreamID ssid, CoreContainer data);
    void reserve(StreamID sid, SubStreamID ssid, uint8_t size, uint8_t width,
                 bool dim_end, bool * end_status);
    SmartReturn full(StreamID sid);
    SmartReturn ready(StreamID sid);
    SmartReturn squash(StreamID sid);
    SmartReturn shouldSquash(StreamID sid);
    SmartReturn commit(StreamID sid);
    void synchronizeLists(StreamID sid);
    SmartReturn clear(StreamID sid);
    SmartReturn isFinished(StreamID sid);
    uint16_t getAvailableSpace(StreamID sid);

   protected:
    // Cycles at 0, 25, 50, 75, 90, 100. %
    Stats::VectorDistribution fifo_occupancy;
    // Cycles per entry (avg, max, min)
    Stats::VectorDistribution entry_time;

   public:
    void regStats() override;
};

// This is the load fifo object that contains one fifo per stream
class UVEStoreFifo : public SimObject {
   private:
    std::vector<StreamFifo *> fifos;

   public:
    uint16_t cacheLineSize;
    UVEStreamingEngine *engine;
    UVEStreamingEngineParams *confParams;
    unsigned fifo_depth;
    std::vector<SubStreamID> reservation_ssid;

   public:
    UVEStoreFifo(UVEStreamingEngineParams *params);

    bool tick(CallbackInfo *res);
    SmartReturn getData(StreamID sid);
    void init();
    SmartReturn insert_data(StreamID sid, SubStreamID ssid,
                            CoreContainer data);
    SmartReturn reserve(StreamID sid, SubStreamID *ssid);
    SmartReturn full(StreamID sid);
    SmartReturn ready(StreamID sid);
    SmartReturn ready();
    SmartReturn squash(StreamID sid, SubStreamID ssid);
    SmartReturn shouldSquash(StreamID sid);
    SmartReturn commit(StreamID sid, SubStreamID ssid);
    void synchronizeLists(StreamID sid);
    SmartReturn clear(StreamID sid);
    SmartReturn isFinished(StreamID sid);
    uint16_t getAvailableSpace(StreamID sid);

   protected:
    // Cycles at 0, 25, 50, 75, 90, 100. %
    Stats::VectorDistribution fifo_occupancy;
    // Cycles per entry (avg, max, min)
    Stats::VectorDistribution entry_time;

   public:
    void regStats() override;
};

#endif  //__UVE_SIMOBJS_FIFO_LOAD_HH__