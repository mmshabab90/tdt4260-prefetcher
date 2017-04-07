// CZone Delta Correlation Prefetcher
// Computer Acvhitecture NTNU 2017
// Anders Liland and Adrian Ribe

#include <iostream>
#include <algorithm>
#include <iterator>
#include <list>
#include <map>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <complex>


#include "interface.hh"

#define BLOCK_SIZE 64 // Size of cache blocks (cache lines) in bytes.
#define MAX_QUEUE_SIZE 100 // Maximum number of pending prefetch requests.
#define MAX_PHYS_MEM_ADDR ((uint64_t)(256*1024*1024) - 1) // 268 435 455
// mem_addr is 28 bit

using namespace std;

//const int DELTA_CORRELATION_SEQUENCE_LENGTH = 2;
//const int CZONE_SIZE = 64; // number of bytes in CZone
const int PREFETCH_DEGREE = 4;
const int GHB_LENGTH_MAX = 100;
const int CZoneMask = 3; // number of digits to mask away

static int Timestep = 0;

enum State{CZone,pushGHB, traverse, prefetch };

struct GHBEntry{
    GHBEntry();
    GHBEntry(Addr mem_addr, int delta);
    Addr mem_addr, pc;
    int delta, CZoneTag;
    GHBEntry *next, *prev; // pointers used to make a linked list inside each CZone
};
GHBEntry::GHBEntry() : mem_addr(0), pc(0), delta(0), next(NULL), prev(NULL){}
GHBEntry::GHBEntry(Addr mem_addr, int delta) : mem_addr(mem_addr), pc(0),delta(delta), next(NULL), prev(NULL){}


class GHBTable{
    public:
        State state;
        std::vector<int> calculatePrefetchAddr(Addr mem_addr);
        Addr maskCZoneAddr(Addr mem_addr);
        void printDeltaBuffer(vector<int> s);
        void printGHB(int CZoneTag);
        void printIndexTable();
    private:
        static int GHBNumberOfEntries;
        std::map<Addr, GHBEntry*> indexTable;
        std::map<Addr, GHBEntry*>::iterator indexTableIterator;
        list<GHBEntry> ghb_list;
        int key_register[2];
        int compare_register[2];
        vector<int> delta_buffer;
};
int GHBTable::GHBNumberOfEntries = 0;
static GHBTable * table;

// Mask the 'CZoneMask' MSB of mem_addr
Addr GHBTable::maskCZoneAddr(Addr mem_addr){
    return mem_addr/pow(10,CZoneMask);
}

void GHBTable::printGHB(int CZoneTag){
    int j = 0;
    cout << "GHB:";
    for (std::list<GHBEntry>::const_iterator it = ghb_list.begin(), end = ghb_list.end(); it != end; ++it) {
        if (it->CZoneTag == CZoneTag){
            cout << "\t[" << j << "]\tMem_addr: " << it->mem_addr << "\tDelta: " << it->delta << "\tCZoneTag: " << it->CZoneTag << endl;
            j++;
        }
    }
}

void GHBTable::printDeltaBuffer(vector<int> s){
    int j = 0;
    cout << "DB:";
    for(vector<int>::iterator it = s.begin(); it != s.end(); it++){
        cout << "\t[" << j << "]\tDeltaBuffer: " << *it << endl;
        j++;
    }
}

std::vector<int> GHBTable::calculatePrefetchAddr(Addr mem_addr){
    Addr CZoneTag;
    GHBEntry *CZoneHead = NULL;
    GHBEntry *entry = new GHBEntry();
    delta_buffer.clear();
    key_register[0] = -1;
    compare_register[0] = -1;

        // Check IndexTable for ZCone tag, and add to list if not pressent.
            Timestep++;
            cout << "\n\n" << endl;
            cout << "TIMESTEP\t" << Timestep << endl;

            CZoneTag = maskCZoneAddr(mem_addr); // mask CZone tag
            entry->CZoneTag = CZoneTag;

            indexTableIterator = indexTable.find(CZoneTag);
            if (indexTableIterator != indexTable.end()){ // tag found
                    CZoneHead = indexTableIterator->second; // ptr to newest element in same CZone in GHB
            }else{ // tag not found
                CZoneHead = entry;
                CZoneHead->mem_addr = mem_addr;
                indexTable.insert(std::pair<Addr, GHBEntry*>(CZoneTag, CZoneHead));
            }

            entry->mem_addr = mem_addr;
            int temp_delta = mem_addr - CZoneHead->mem_addr;
            entry->delta = abs(temp_delta); //TODO: can delta be negative?
            cout << "Addr: " <<  entry->mem_addr << " Delta: " << entry->delta<< endl;

            // add deltas to key_register
            key_register[1] = CZoneHead->delta;
            key_register[0] = entry->delta;
            cout << "key_register[0] = " << key_register[0] << endl;
            cout << "key_register[1] = " << key_register[1] << endl;

            //update value in Index prt Table
            CZoneHead = entry; // make new entry top of its CZone
            indexTableIterator->second = entry; // update ptr in map to point to newest emelent in CZone

            indexTable.insert(std::pair<Addr, GHBEntry*>(CZoneTag, CZoneHead));
            ghb_list.push_front(*entry);
            GHBNumberOfEntries++;

            if (GHBNumberOfEntries > GHB_LENGTH_MAX){ // ghb_list is a FIFO. Pop end when list is too long.
                cout << "pop ghb_list " << endl;
            }

            printGHB(CZoneTag);

            // TODO: do not include oldest delta/first delta when traversing. This delta will always be 0, since it is first in the list (head).
            for(std::list<GHBEntry>::iterator it = ghb_list.begin(); it != ghb_list.end(); it++){
                //if (it->CZoneTag == CZoneTag){ // only travers miss addresses in same CZone
                    compare_register[1] = compare_register[0];
                    compare_register[0] = it->delta;
                    cout << "compare_register[0] = " << compare_register[0];
                    cout << "  compare_register[1] = " << compare_register[1] << endl;

                    delta_buffer.insert(delta_buffer.begin(), compare_register[0]); // Second cycle: shiften into delta_buffer at start of compare_register, according to algorithm
                        if(compare_register[0] == key_register[0] && compare_register[1] == key_register[1] && it->mem_addr != mem_addr ){ //correlation hits
                            cout << "correltaion hit" << endl;
                            delta_buffer.pop_back();
                            return delta_buffer;
                        }
                //    }
            }

            // No correlation found.
            delta_buffer.clear();
            return delta_buffer; // return vector without any elements
}

// --------- PREFETCH SIMULATED FUNCTIONS ------------------------
void prefetch_init(void){
    //TODO:DPRINTF(HWPrefetch, "HWPrefetch\tInitializing prefetcher\n");
    std::cout << "prefetch_init" << std::endl;
    table = new GHBTable;

}

void prefetch_access(AccessStat stat){

    Addr pf_addr = 0, mem_addr = stat.mem_addr;
    std::vector<int> temp_delta_buffer;
    if(stat.miss){ //calculate prefetch address only on miss

        temp_delta_buffer = table->calculatePrefetchAddr(stat.mem_addr);
        if ( temp_delta_buffer.empty() ) {
            cout << "No delta correlation found for miss addr: " << stat.mem_addr << endl;
        } else {
            int delta_buffer_size = temp_delta_buffer.size();
            table->printDeltaBuffer(temp_delta_buffer);

            int j = 0;
            for(int i = 0; i < PREFETCH_DEGREE; i++){
                if (j >= delta_buffer_size){ // reset delta buffer to start, if prefetch dergee is bigger than buffer size
                    j = 0;
                }
                pf_addr = mem_addr + temp_delta_buffer[j];
                if(pf_addr < MAX_PHYS_MEM_ADDR){
                    cout << "Issue prefetch for address: " << pf_addr << endl;
                    //TODO:DPRINTF(HWPrefetch, "HWPrefetch\tAddress %#x was accessed. Prefetched %#x \n", stat.mem_addr, pf_addr);
                    if(!in_cache(pf_addr) && !in_mshr_queue(pf_addr)) //TODO: does this matter?
                        issue_prefetch(pf_addr);
                }else{
                    cout << "Prefetch address out of bounds " << pf_addr << endl;
                }
                mem_addr = pf_addr; // The prefetch addresses are cumulative
                j++;
            }
        }

    }
}

void prefetch_complete(Addr addr){
    //TODO:DPRINTF(HWPrefetch,"HWPrefetch\tprefetch_complete\n");
    cout << "prefetch_complete" << endl;
}

// BEFORE EACH RUN
// Decomment issue_prefetch()
// Change CZoneSize
// Uncommen DPRINTF
// Uncomment
