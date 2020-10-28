#include "utils.hh"

SEIter::SEIter(SEStack* cmds, Tick _start_tick)
    : SEList(),
      elem_counter(0),
      sequence_number(-1),
      end_ssid(-1),
      start_tick(_start_tick) {
    // JMTODO: Create iterator from cmds
    // Validate all commands
    // Create iteration tree
    // Empty Stack
    status = SEIterationStatus::Configured;
    SECommand cmd = cmds->front().cmd;
    width = cmd.get_width();
    head_stride = cmd.get_dimension()->get_stride();
    tc = cmd.get_tc();
    sid = cmd.getStreamID();
    mode = cmd.isLoad() ? StreamMode::load : StreamMode::store;

    insert_dim(new DimensionObject(cmd.get_dimension(), width, true));
    DPRINTF(JMDEVEL, PR_INFO("Inserting first command: %s\n"),
            cmd.to_string());

    cmds->pop_front();

    DimensionObject* dimobj;
    while (!cmds->empty()) {
        cmd = cmds->front().cmd;
        if( cmd.isConfig() ){
            config_dim();
            cmds->pop_front();
            DPRINTF(JMDEVEL, PR_INFO("CfgVec command: %s\n"), cmd.to_string());
            continue;
        }

        if (cmds->size() == 1) {
            dimobj =
                new DimensionObject(cmd.get_dimension(), width, false, true);
        } else {
            dimobj = new DimensionObject(cmd.get_dimension(), width);
        }


        DPRINTF(JMDEVEL, PR_INFO("Inserting command: %s\n"), cmd.to_string());

        if (cmd.isDimension())
            insert_dim(dimobj);
        else
            insert_mod(dimobj);
        cmds->pop_front();
    }
    DPRINTF(JMDEVEL, PR_ANN("Tree SID(%d): \n%s\n"), this->sid,
                                             this->to_string());
    initial_offset = initial_offset_calculation(true);
    current_dim = get_end();
}

SEIter::SEIter() {
    status = SEIterationStatus::Clean;
    pageFunction = nullptr;
    sid = 0;
    initial_offset = 0;
    width = 0;
    current_nd = nullptr;
    current_dim = nullptr;
    elem_counter = 0;
    sequence_number = 0;
    end_ssid = 0;
    start_tick = 0;
    head_stride = 0;
    cur_request = SERequestInfo();
    last_request = SERequestInfo();
    tc = nullptr;
    page_jump = false;
    page_jump_vaddr = 0;
    mode = StreamMode::load;
}
SEIter::~SEIter() {}

DimensionOffset
SEIter::initial_offset_calculation(tnode* cursor, DimensionOffset offset,
                                   bool zero) {
    if (cursor->next != nullptr) {
        offset = initial_offset_calculation(cursor->next, offset, zero);
    }
    return offset + cursor->content->get_initial_offset(zero);
}

DimensionOffset
SEIter::initial_offset_calculation(bool zero) {
    tnode* cursor = get_head();
    DimensionOffset offset = 0;
    if (cursor->next != nullptr)
        offset = initial_offset_calculation(cursor->next, offset, zero);
    offset += cursor->content->get_initial_offset(zero);
    return offset;
}

DimensionOffset
SEIter::offset_calculation(tnode* cursor, DimensionOffset offset) {
    if (cursor->next != nullptr) {
        offset = offset_calculation(cursor->next, offset);
    }
    cursor->content->set_offset();
    return offset + cursor->content->get_cur_offset();
}

DimensionOffset
SEIter::offset_calculation() {
    tnode* cursor = get_head();
    DimensionOffset offset = 0;
    if (cursor->next != nullptr)
        offset = offset_calculation(cursor->next, offset);
    cursor->content->set_offset();
    offset += cursor->content->get_cur_offset();
    return offset;
}

void 
SEIter::get_ended_dimensions(bool * result, tnode* cursor){
    if(cursor->content->has_ended()){
        (*result) = true;
    }
    if (cursor->next != nullptr){
        get_ended_dimensions(result++, cursor->next);
    }
    return;
}
void 
SEIter::get_ended_dimensions(bool * result){
    tnode* cursor = get_head();
    tnode* last_cursor = get_head();
    int iter = 0;
    while(cursor != nullptr)
    {
        if(cursor->content->has_ended()){
            result[iter] = cursor->content->is_vector();
        }
        iter++;
        last_cursor = cursor;
        cursor = cursor->next;
    }
    if(last_cursor->content->has_ended()){
        result[DimensionHop::last] = true;
    }
    return;
}

void SEIter::stats(SERequestInfo result, StopReason sres){
    // Create results with this
};

SERequestInfo
SEIter::advance(uint16_t max_size) {
    // Iterate until request is generated
    assert(status != SEIterationStatus::Ended &&
           status != SEIterationStatus::Clean &&
           status != SEIterationStatus::Stalled);

    SERequestInfo request;
    bool result = false;
    status = SEIterationStatus::Running;
    StopReason sres = StopReason::BufferFull;

    elem_counter = 0;
    while (elem_counter < max_size / (width * 8)) {
        assert(current_dim != nullptr);

        result = current_dim->content->advance();
        if (current_dim != get_head() && current_dim->sibling != nullptr){
            current_dim->sibling->content->advance();
        }
        if (result) {  // Dimension Change -> Issue request
            sres = StopReason::DimensionSwap;
            if (current_dim->next == nullptr) {
                status = SEIterationStatus::Ended;
                sres = StopReason::End;
                elem_counter++;
                break;
            }
            current_dim = current_dim->next;
            current_dim->content->lower_ended();

            // Do a peek to assure it is not the last iteration
            auto aux_dim = current_dim;
            while (aux_dim != nullptr) {
                if (aux_dim->content->peek()) {
                    aux_dim->content->advance();
                    if (aux_dim != get_head() && aux_dim->sibling != nullptr){
            aux_dim->sibling->content->advance();}
                    aux_dim = aux_dim->next;
                    // current_dim->content->advance();
                    // current_dim = aux_dim;
                } else {
                    if (aux_dim != current_dim){
                        aux_dim->content->advance();
                        if (aux_dim != get_head() &&
                            aux_dim->sibling != nullptr){
                            aux_dim->sibling->content->advance();}
                    }
                    break;
                }
            };
            if (aux_dim == nullptr) {
                status = SEIterationStatus::Ended;
                sres = StopReason::End;
                elem_counter++;
                break;
            }
            if (elem_counter == 0) {
                // Here we find a dimension swap that has not
                // calculated anything. E.g. Dim2->Dim3
                if (current_dim == get_head()) break;
                if (current_dim->prev == get_head()){
                    elem_counter++;
                    break;
                } 
                current_dim->content->advance();
                current_dim->content->set_offset();
                elem_counter++;
                current_dim = current_dim->prev;
                while (current_dim->content->has_ended() &&
                       current_dim != get_head()) {
                    current_dim->content->set_offset();
                    current_dim = current_dim->prev;
                }
                continue;
            }
            elem_counter++;
            break;
        } else {
            if (current_dim != get_head()) {
                // We are not in the lowest dimension
                // Need to go back to the lower dimension
                current_dim = current_dim->prev;
                continue;
            } else if (head_stride != 1) {
                sres = StopReason::NonCoallescing;
                elem_counter++;
                break;
            } else
                elem_counter++;
        }
    }
    request.mode = mode;
    request.initial_offset = initial_offset_calculation();
    request.final_offset = request.initial_offset + ( width * elem_counter );
    offset_calculation();
    get_ended_dimensions(request.dimensions_ended);
    request.iterations = elem_counter;
    request.status = status;
    request.width = width;
    request.tc = tc;
    request.sid = sid;
    request.stop_reason = sres;
    request.sequence_number = ++sequence_number;
    if (status == SEIterationStatus::Ended) {
        end_ssid = sequence_number - 1;
    }
    initial_offset = request.final_offset;
    last_request = cur_request;
    cur_request = request;
    stats(request, sres);
    return request;
}

bool
SEIter::empty() {
    return (status == SEIterationStatus::Clean ||
            status == SEIterationStatus::Ended)
               ? true
               : false;
}

uint8_t
SEIter::getWidth() {
    return width;
}

uint8_t
SEIter::getCompressedWidth() {
    uint8_t _width = 0;
    switch (width) {
        case 1:
            _width = 0;
            break;
        case 2:
            _width = 1;
            break;
        case 4:
            _width = 2;
            break;
        case 8:
            _width = 3;
            break;
    }
    return _width;
}

// returns true if it could not translate
// paddr is passed by referenced, contains the translation result
// if it succeds (return is true)
bool
SEIter::translatePaddr(Addr* paddr) {
    int64_t aux = 0;
    // First request must always be translated by TLB
    if (sequence_number == 0) {
        last_request = cur_request;
        return false;
    }

    // Check if the page is the same between last and current request
    if (pageFunction(cur_request.initial_offset, cur_request.final_offset) &&
        pageFunction(last_request.initial_offset,
                     cur_request.initial_offset) &&
        pageFunction(last_request.initial_offset, last_request.final_offset)) {
        aux = (int64_t)last_request.initial_paddr +
                 ((int64_t)cur_request.initial_offset -
                  (int64_t)last_request.initial_offset);
        if(aux < 0) return false;
        *paddr = (Addr) aux;
        return true;

        // If the page is not mantained see if the current one is and
        // check that the last translation targets a new page
    } else if (pageFunction(cur_request.initial_offset,
                            cur_request.final_offset) &&
               page_jump) {
        aux = ((int64_t) last_request.initial_paddr +
                 ((int64_t)cur_request.initial_offset -
                  (int64_t) page_jump_vaddr));
        if (aux < 0) return false;
        *paddr = (Addr) aux;
        return true;
    } else {
        // In this case it is not possible to infer the paddr
        // Data is in new page
        return false;
    }
}

void
SEIter::setPaddr(Addr paddr, bool new_page, Addr vaddr) {
    cur_request.initial_paddr = paddr;
    page_jump_vaddr = vaddr;
    page_jump = new_page;
}

SmartReturn
SEIter::ended() {
    return SmartReturn::compare(status == SEIterationStatus::Ended);
}
int64_t
SEIter::get_end_ssid() {
    return end_ssid;
}

void
SEIter::stall() {
    status = SEIterationStatus::Stalled;
}
void
SEIter::resume() {
    status = SEIterationStatus::Running;
}
bool
SEIter::stalled() {
    return status == SEIterationStatus::Stalled;
}
bool
SEIter::is_load() {
    return mode == StreamMode::load;
}

Tick
SEIter::time() {
    return start_tick;
}

const char*
SEIter::print_state() {
    std::stringstream sout;
    sout << "[";
    auto aux_node = current_dim;
    while (aux_node->prev != nullptr) {
        aux_node = aux_node->prev;
    }
    sout << "<dim>";
    while (aux_node != nullptr) {
        if (aux_node == current_dim){
            sout << "●";
        }
        sout << "{";
        sout << "c(" << aux_node->content->get_counter() << ")";
        sout << "s(" << aux_node->content->get_size() << ")";
        sout << "end(" << (aux_node->content->has_ended() ? "T" : "F") << ")";
        sout << "}  ";
        if (aux_node->sibling != nullptr){
            aux_node = aux_node->sibling;
            sout << "<mod>";
            continue;
        }
        if (aux_node->next != nullptr) sout << "<dim>";
        aux_node = aux_node->next;
    }
    sout << "]";
    return sout.str().c_str();
}
