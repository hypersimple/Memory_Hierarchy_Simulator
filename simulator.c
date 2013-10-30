/* By Yue Chen, for architecture homework1 */
/* Compile: gcc hw1.c -o hw1 */
/* The cache is PIPT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ 0
#define WRITE 1

// to determine which one is displayed on screen
unsigned int original_address;

// to indicate if there is a hit
int is_dc_hit = 0;
int is_tlb_hit = 0;

// the variable for initial value, self-explained
int tlb_set_number, tlb_set_size,
    virtual_page_number, physical_page_number, page_size,
    dc_set_number, dc_set_size, dc_line_size, dc_wt,
    l2_set_number, l2_set_size, l2_line_size, l2_wt,
    virtual_address_on, tlb_on, l2_cache_on;

int dc_write_through = 0, l2_write_through = 0, virtual_enable = 0,
    tlb_enable = 0, l2_enable = 0, has_memory_access = 0;

unsigned int tlb_index_bit, virtual_page_index_bit, page_offset_bit,
             dc_index_bit, dc_offset_bit, l2_index_bit, l2_offset_bit;

//the following variables starting with "d_" are for determining display on the screen
unsigned int d_address, d_virt_page, d_page_off, d_tlb_tag, d_tlb_ind,
             d_phys_pg, d_dc_tag, d_dc_ind, d_l2_tag, d_l2_ind;

// for the "hit" and "miss"
char d_tlb_res[5], d_pt_res[5], d_dc_res[5], d_l2_res[5];

// the following for counting
int dtlb_hits = 0, dtlb_misses = 0,
    pt_hits = 0, pt_faults = 0,
    dc_hits = 0, dc_misses = 0,
    l2_hits = 0, l2_misses = 0,
    total_reads = 0, total_writes = 0,
    main_memory_refs = 0, page_table_refs = 0,
    disk_refs = 0;
    
float dtlb_hit_ratio = 0.0, pt_hit_ratio = 0.0, dc_hit_ratio = 0.0,
      l2_hit_ratio = 0.0, ratio_of_reads = 0.0;

// switch debug mode
int debug = 0;

FILE* fp;

// physical memory struct
typedef struct p_mem_struct
{
    int empty;
    unsigned long lru;
    int dirty;
}p_mem_struct;

// cache struct
typedef struct cache_struct
{   
    unsigned int tag;
    //unsigned int index;
    int valid;
    unsigned long lru;
    int dirty;
    unsigned int page;
    unsigned int related_pa;
    // for dtlb
    unsigned int vp;
    unsigned int pp;
}cache_struct;

// for getting cache content
typedef struct cache_content_struct
{
    unsigned int tag;
    unsigned int index;
}cache_content_struct;

// the mapping from virtual page to physical page
typedef struct mapping
{
    unsigned int vp; // virtual page
    unsigned int pp; // physical page
    int valid;
    struct mapping *next;
}mapping;

// the pointer for physical memory, L1 data cache, l2 cache, TLB and the virtual to physical memory mapping
p_mem_struct *pm;
cache_struct *dc;
cache_struct *l2;
cache_struct *tlb;
mapping *map;

// write and read to/from physical memory
void write_to_pm(unsigned int);
void read_from_pm(unsigned int);


// to get a physical page from the physical memory
unsigned int get_a_pp(mapping *map)
{   
    int i;
    
    // to see of there is empty space in pm
    for (i = 0; i < physical_page_number; i++) {
        if (pm[i].empty) {
            pm[i].empty = 0;
            return i;
        }
    }
    
    // use the lru to replace the element in the physical memory
    unsigned long min = pm[0].lru;
    int min_unit = 0;
    for (i = 1; i < physical_page_number; i++) {
        if (pm[i].lru < min) {
            min = pm[i].lru;
            min_unit = i;
        }
    }
    
    //invalidate the corresponding TLB
    for (i = 0; i < tlb_set_size * tlb_set_number; i++) {
        if (tlb[i].pp == min_unit && tlb[i].valid) {
            tlb[i].valid = 0;
            fprintf(fp,"invalidating dtlb entry %d since phys page %d is being replaced\n",
                    i/tlb_set_size, min_unit);
        }
    }
    
    // if dirty, write back the page to disk
    if (pm[min_unit].dirty) {
        
        if (debug)
            printf("writing back to disk\n");
        disk_refs++;
    }
    
    while (map->next) {
        map = map->next;
        if (map->valid && (map->pp == min_unit)) {
            map->valid = 0;  //mark it invalid
        }
    }
    
    // mark the dc cache related to this physical page invalid
    for (i = 0; i < dc_set_size * dc_set_number; i++) {
        if (dc[i].page == min_unit) {
            if (dc[i].dirty) {
                unsigned int old_pa 
                           = dc[i].tag<<(dc_offset_bit+dc_index_bit)|
                             ((i / dc_set_size)<<dc_offset_bit);
                if (l2_enable) {
                    access_l2(WRITE, old_pa, 1);  //write the LINE back to l2
                    if (debug)
                        printf("writing back DC line with tag %x and index %x "
                           "to L2 cache\n",
                           dc[i].tag, i / dc_set_size);
                    fprintf(fp,"writing back DC line with tag %x and index %x "
                           "to L2 cache\n",
                           dc[i].tag, i / dc_set_size);
                } else {
                    if (debug)
                        printf("writing back DC line with tag %x and index %x "
                            "to memory\n",
                            dc[i].tag, i / dc_set_size);
                            
                    fprintf(fp, "writing back DC line with tag %x and index %x "
                            "to memory\n",
                            dc[i].tag, i / dc_set_size);
                     write_to_pm(old_pa);
                }
                dc[i].dirty = 0;
            }
            
            if (dc[i].valid) {
                if (debug)
                    printf("invalidating DC line with tag %x and index %x since phys page %x is being replaced\n", dc[i].tag, i / dc_set_size, min_unit);
                fprintf(fp, "invalidating DC line with tag %x and index %x since phys page %x is being replaced\n", dc[i].tag, i / dc_set_size, min_unit);
                dc[i].valid = 0;
            }
        } 
    }
    
    // mark the l2 cache related to this physical page invalid
    if (l2_enable) {
        for (i = 0; i < l2_set_size * l2_set_number; i++) {
            if (l2[i].page == min_unit) {
                if (l2[i].dirty) {
                    unsigned int old_pa 
                               = l2[i].tag<<(l2_offset_bit + l2_index_bit)|
                                 ((i / l2_set_size)<<l2_offset_bit);
                    if (debug)
                        printf("writing back L2 line with tag %x and index %x "
                           "to memory\n",
                           l2[i].tag, i / l2_set_size);
                           
                   fprintf(fp, "writing back L2 line with tag %x and index %x "
                           "to memory\n",
                           l2[i].tag, i / l2_set_size);
                    
                    write_to_pm(old_pa);
                    l2[i].dirty = 0;
                }
                
                if (l2[i].valid) {
                    if (debug)
                        printf("invalidating L2 line with tag %x and index %x since phys page %x is being replaced\n", l2[i].tag, i / l2_set_size, min_unit);
                        
                    fprintf(fp, "invalidating L2 line with tag %x and index %x since phys page %x is being replaced\n", l2[i].tag, i / l2_set_size, min_unit);
                    
                    l2[i].valid = 0;
                }
            }
        }
    }
    // set the new page not dirty
    pm[min_unit].dirty = 0;
    
    return min_unit;
}   


// look for the mapping of existing virtual page to physical page, if there exists,
// return the physical page; if not, get a new one by calling get_a_pp(mapping *map)
unsigned int lookup_or_add(mapping *map, unsigned int vp)
{
    page_table_refs++;
    d_virt_page = vp;
    
    if (!map->next) {
        strcpy(d_pt_res, "miss");
        pt_faults++;
        disk_refs++;
        mapping *map1 = (mapping*)malloc(sizeof(mapping));
        map->next = map1;
        map1->vp = vp;
        map1->valid = 1;
        map1->pp = get_a_pp(map); // get a physical page
        map1->next = NULL;
        return map1->pp;
    } else {
        map = map->next;
        while(map) {
            if (map->valid && map->vp == vp) {
                strcpy(d_pt_res, "hit");
                pt_hits++;
                return map->pp;
            } else
                if (map->next)
                    map = map->next;
                else
                    break;
        }
        disk_refs++;
        mapping *map1 = (mapping*)malloc(sizeof(mapping));
        map->next = map1;
        map1->vp = vp;
        map1->valid = 1;
        map1->pp = get_a_pp(map); // get a physical page
        map1->next = NULL;
        
        strcpy(d_pt_res, "miss");
        pt_faults++;
        
        return map1->pp;
    }
}


// to calculate log2(unsigned int)
unsigned int log2_my(unsigned int n)
{
    int targetlevel = 0;
    while (n >>= 1)
        targetlevel++;
    return targetlevel;
}


// the declaration of aceess_tlb, for accessing TLB
unsigned int access_tlb(unsigned int, mapping*);


// translate from virtual address to physical address
unsigned int v2p(unsigned int va, mapping *map)
{
    //printf("va: %x\n", va);
    unsigned int vp = va / page_size;
    unsigned int offset = va % page_size;
    unsigned int pp;
    
    if (tlb_enable)
        pp = access_tlb(vp, map);
    else
        pp = lookup_or_add(map, vp);
    
    unsigned int pa = (pp << log2_my(page_size)) + offset;
    //printf("pa: %x\n",pa);
    return pa;
}


// check if the number is power of 2
int power_of_2_check(unsigned int num)  
{  
    if((num != 1) && (num & (num - 1)))
        return 0;
    else
        return 1;
}


// convert from physical address to physical page number
unsigned int pa_to_ppn(unsigned int pa)
{   
    return pa / page_size;
}


// given the physical address, return the cache information
cache_content_struct pa_to_cache(unsigned int pa, unsigned int dc_set_number,
                   unsigned int dc_set_size, unsigned int dc_line_size)
{
    cache_content_struct cache;
    cache.index = (pa / dc_line_size) % dc_set_number;
    cache.tag = (pa / dc_line_size) / dc_set_number;
    return cache;
}



char *readFromIn(char *buffer)
{
    char *result = fgets(buffer, 4096, stdin);
    int len;

    // fgets returns NULL on error of end of input,
    // in which case buffer contents will be undefined
    if (result == NULL) {
        return NULL;
    }

    len = strlen (buffer);
    if (len == 0) {
        return NULL;
    }

    if (buffer[len - 1] == '\n')
        buffer[len - 1] = 0;

    return buffer;
}

// read from files
char *readFromFile(char *buffer, FILE *fr)
{
    char *result = fgets(buffer, 4096, fr);
    int len;

    // fgets returns NULL on error of end of input,
    // in which case buffer contents will be undefined
    if (result == NULL) {
        return NULL;
    }

    len = strlen (buffer);
    if (len == 0) {
        return NULL;
    }

    if (buffer[len - 1] == '\n')
        buffer[len - 1] = 0;

    return buffer;
}


// get the last word split by space from a line
char *last_word(char *string)
{
    char *p = strrchr(string, ' ');
    if (p && *(p + 1))
        //printf("%s\n", p + 1);
        return p + 1;
}


// simulate read from physical memory
void read_from_pm(unsigned int pa)
{
    main_memory_refs++;
    if (debug)
        printf("mem_READ: %x\n",pa);
}


// simulate write to physical memory
void write_to_pm(unsigned int pa)
{   
    main_memory_refs++;
    if (debug)
        printf("mem_WRITE: %x\n",pa);
    unsigned int page_number = pa_to_ppn(pa);
    pm[page_number].dirty = 1;
}


// the function to access data cache, r_w means read or write, pa means physical address
access_dc(int r_w, unsigned int pa)
{
    int i,j,k;
    cache_content_struct dcc;
    dcc = pa_to_cache(pa, dc_set_number, dc_set_size, dc_line_size);
    unsigned int page = pa_to_ppn(pa);
    
    if (virtual_enable) {
        for (i = 0; i < physical_page_number; i++)
            pm[i].lru >>= 1;
        pm[page].lru |= 1 << 31;
    }
    
    if (debug) {
        if(r_w == WRITE) printf("W ");
        else printf("R ");
        printf("%08x\n",pa);
    }
    
    d_dc_tag = dcc.tag;
    d_dc_ind = dcc.index;
    
    int hit = 0;
    for (i = 0; i < dc_set_size; i++) {

        // the hit situation
        if (dc[dcc.index * dc_set_size + i].valid && dc[dcc.index * dc_set_size + i].tag == dcc.tag) {
            hit = 1;
            
            is_dc_hit = 1;
            
            if (original_address == pa)
                strcpy(d_dc_res, "hit");
            dc_hits++;
            
            for (j = 0; j < dc_set_size; j++)
                dc[dcc.index * dc_set_size + j].lru >>= 1;
            dc[dcc.index * dc_set_size + i].lru |= 1 << 31;
                
            if (r_w == WRITE && dc_write_through) {
                if (l2_enable)
                    access_l2(WRITE, pa, 1);
                else
                    write_to_pm(pa);
            } else if (r_w == WRITE && (!dc_write_through)) {
                dc[dcc.index * dc_set_size + i].dirty = 1;
            }
                
            for (j = 0; j < dc_set_size; j++) {
                dc[dcc.index * dc_set_size + j].lru >>= 1;
            }
            break;
        }
    }
    
    // the miss situation
    if (!hit) {
        if (original_address == pa)
            strcpy(d_dc_res, "miss");
        dc_misses++;
        
        // if the access is WRITE and the policy is write through, directly 
        // write to the next level cache
        if (r_w == WRITE && dc_write_through)
            if (l2_enable) {
                access_l2(WRITE, pa, 1);
                return;
            }
            else {
                write_to_pm(pa);
                return;
            }
        else {
            // if the index has a empty slot, put the element in it
            int there_is_empty = 0;
            for (i = 0; i < dc_set_size; i++) {
                if (dc[dcc.index * dc_set_size + i].valid == 0) {
                    dc[dcc.index * dc_set_size + i].tag = dcc.tag;
                    dc[dcc.index * dc_set_size + i].page = pa_to_ppn(pa);
                    dc[dcc.index * dc_set_size + i].related_pa = pa;
                    
                    there_is_empty = 1;
                    // do the LRU work
                    for (j = 0; j < dc_set_size; j++)
                        dc[dcc.index * dc_set_size + j].lru >>= 1;
                    dc[dcc.index * dc_set_size + i].lru |= 1 << 31;
                    
                    if (r_w == WRITE && !dc_write_through) {
                        if (l2_enable)
                            access_l2(READ, pa, 1);
                        else
                            read_from_pm(pa);
                        
                        dc[dcc.index * dc_set_size + i].dirty = 1;
                    }
                    // read miss, get data from the next level cache
                    if (r_w == READ) {
                        if (l2_enable)
                            access_l2(READ, pa, 1);
                        else
                            read_from_pm(pa);
                    }
                    dc[dcc.index * dc_set_size + i].valid = 1;
                    return;
                }
            }

            // if there is no empty slot, replace the least lru element
            if (!there_is_empty) {
                // get the least recently used one
                unsigned long min = dc[dcc.index * dc_set_size].lru;
                int min_unit = dcc.index * dc_set_size;
                for (i = 1; i < dc_set_size; i++) {
                    if (dc[dcc.index * dc_set_size + i].lru < min) {
                        min = dc[dcc.index * dc_set_size + i].lru;
                        min_unit = dcc.index * dc_set_size + i;
                    }
                }
                // the the DC unit is dirty, write back DC line
                if (dc[min_unit].dirty) {
                   unsigned int old_pa 
                                = dc[min_unit].related_pa;
                                
                   if (l2_enable) {
                       access_l2(WRITE, old_pa, 1);  //write the LINE back to l2
                       if (debug)
                           printf("writing back DC line with tag %x and index %x "
                           "to L2 cache\n",
                           dc[min_unit].tag, dcc.index);
                       fprintf(fp, "writing back DC line with tag %x and index %x "
                           "to L2 cache\n",
                           dc[min_unit].tag, dcc.index);
                   } else {
                       if (debug)
                           printf("writing back DC line with tag %x and index %x "
                              "to memory\n",
                              dc[min_unit].tag, dcc.index);
                       // the example program does not count this into the main_memory_refs
                       fprintf(fp, "writing back DC line with tag %x and index %x "
                           "to memory\n",
                           dc[min_unit].tag, dcc.index);
                       
                       write_to_pm(old_pa);
                   }
                   dc[min_unit].dirty = 0;
                }
                
                // assign the new tag and page value
                dc[min_unit].tag = dcc.tag;
                dc[min_unit].page = pa_to_ppn(pa);
                dc[min_unit].related_pa = pa;
                
                // do the LRU job
                for (i = 0; i < dc_set_size; i++)
                    dc[dcc.index * dc_set_size + i].lru >>= 1;
                dc[min_unit].lru |= 1 << 31;
                
                // if READ, get the line from the next mem hier
                if (r_w == READ) {
                    if (l2_enable)
                        access_l2(READ, pa, 1);
                    else
                        read_from_pm(pa);
                    return;
                }
                
                // if it is a WRITE access and the policy is write back,
                // set the new line dirty
                if (r_w == WRITE && !dc_write_through) {
                    if (l2_enable)
                        access_l2(READ, pa, 1);
                    else
                        read_from_pm(pa);
                    dc[min_unit].dirty = 1;
                    return;
                }
            }
        }
    }           
}


// the function to access L2 cache, the display variable control if the content should be printed to the stdout
access_l2(int r_w, unsigned int pa, int display)  
{
    int i,j,k;
    cache_content_struct l2c;
    l2c = pa_to_cache(pa, l2_set_number, l2_set_size, l2_line_size);
    
    if (display) {
        if (debug) {
            //printf("%d\n",r_w);
            if(r_w == WRITE) printf("L2 W ");
            else printf("L2 R ");
            printf("%08x\n", pa);
        }
    }

    int hit = 0;
    for (i = 0; i < l2_set_size; i++) {
        // the hit situation
        if (l2[l2c.index * l2_set_size + i].valid && l2[l2c.index * l2_set_size + i].tag == l2c.tag) {
            hit = 1;
            
            if (display) {
                if (debug)
                    printf("l2_hit!\n");
                if (original_address == pa)
                    strcpy(d_l2_res, "hit");
                l2_hits++;
            }
                
            for (j = 0; j < l2_set_size; j++)
                l2[l2c.index * l2_set_size + j].lru >>= 1;
            l2[l2c.index * l2_set_size + i].lru |= 1 << 31;
            
            // write to pm if write through
            if (r_w == WRITE && l2_write_through) {
                if (display)
                    write_to_pm(pa);
            // set dirty if write back
            } else if (r_w == WRITE && (!l2_write_through)) {
                l2[l2c.index * l2_set_size + i].dirty = 1;
            }
            
            // do the LRU job
            for (j = 0; j < l2_set_size; j++) {
                l2[l2c.index * l2_set_size + j].lru >>= 1;
            }
            return;
        }
    }
    
    // the not hit situation
    if (!hit) {
        if (display) {
            if (debug)
                printf("l2_miss!\n");
            if (original_address == pa)
                strcpy(d_l2_res, "miss");
            l2_misses++;
        }
        if (r_w == WRITE && l2_write_through) {
            if (display)
                write_to_pm(pa);
            return;
        }
        else {
            // if the index has a empty slot, put the element in it
            int there_is_empty = 0;
            for (i = 0; i < l2_set_size; i++) {
                if (l2[l2c.index * l2_set_size + i].valid == 0) {
                    
                    l2[l2c.index * l2_set_size + i].tag = l2c.tag;
                    l2[l2c.index * l2_set_size + i].page = pa_to_ppn(pa);
                    l2[l2c.index * l2_set_size + i].related_pa = pa;
                    
                    there_is_empty = 1;
                    
                    for (j = 0; j < l2_set_size; j++)
                        l2[l2c.index * l2_set_size + j].lru >>= 1;
                    l2[l2c.index * l2_set_size + i].lru |= 1 << 31;
                    
                    if (r_w == READ) {
                        read_from_pm(pa);
                    }
                    
                    if (r_w == WRITE && !l2_write_through) {
                        read_from_pm(pa);
                        l2[l2c.index * l2_set_size + i].dirty = 1;
                    }
                    
                    l2[l2c.index * l2_set_size + i].valid = 1;
                    return;
                }
            }

            // if there is no empty slot, replace the least lru element
            if (!there_is_empty) {
                unsigned long min = l2[l2c.index * l2_set_size].lru;
                int min_unit = l2c.index * l2_set_size;
                for (i = 1; i < l2_set_size; i++) {
                    if (l2[l2c.index * l2_set_size + i].lru < min) {
                        min = l2[l2c.index * l2_set_size + i].lru;
                        min_unit = l2c.index * l2_set_size + i;
                    }
                }
                
                //old physical address in L2
                unsigned int old_pa = l2[min_unit].related_pa;
                                
                //old physical address in DC
                cache_content_struct dcc;
                dcc = pa_to_cache(old_pa, dc_set_number, dc_set_size, dc_line_size);
                
                int there_is_valid = 0;
                for (i = 0; i < dc_set_size; i++) {
                    if (dc[dcc.index * dc_set_size + i].tag == dcc.tag &&
                        dc[dcc.index * dc_set_size + i].valid) {
                        there_is_valid = 1;
                        
                        if (dc[dcc.index * dc_set_size + i].dirty == 1) {
                            unsigned int old_pa 
                                = dc[dcc.index * dc_set_size + i].related_pa;
                                
                            access_l2(WRITE, old_pa, 1);
                            if (debug)
                                printf("writing back DC line with tag %x and index %x "
                                   "to L2 cache\n",
                                   dcc.tag, dcc.index);
                            fprintf(fp,"writing back DC line with tag %x and index %x "
                                   "to L2 cache\n",
                                   dcc.tag, dcc.index);
                            
                        }
                        dc[dcc.index * dc_set_size + i].valid = 0;
                    }
                }
                  
                // is there is any valid DC line
                if (there_is_valid) {
                    if (debug)
                        printf("invalidating DC line with tag %x and index %x since "
                               "L2 line with tag %x and index %x is being replaced\n",
                               dcc.tag, dcc.index, l2[min_unit].tag, l2c.index);
                               
                    fprintf(fp, "invalidating DC line with tag %x and index %x since "
                               "L2 line with tag %x and index %x is being replaced\n",
                               dcc.tag, dcc.index, l2[min_unit].tag, l2c.index);
                }

                // if the old L2 line is dirty
                if (l2[min_unit].dirty) {
                    if (debug)
                        printf("writing back L2 line with tag %x and index %x "
                           "to memory\n",
                           l2[min_unit].tag, l2c.index);
                    
                    fprintf(fp, "writing back L2 line with tag %x and index %x "
                           "to memory\n",
                           l2[min_unit].tag, l2c.index);
                    
                    if (display)
                        write_to_pm(old_pa);  //write the LINE back to L2

                    l2[min_unit].dirty = 0;
                }
                
                // assign the new tag and page value
                l2[min_unit].tag = l2c.tag;
                l2[min_unit].page = pa_to_ppn(pa);
                l2[min_unit].related_pa = pa;
                
                for (i = 0; i < l2_set_size; i++)
                    l2[l2c.index * l2_set_size + i].lru >>= 1;
                l2[min_unit].lru |= 1 << 31;
                
                if (r_w == READ) {
                    read_from_pm(pa);
                    return;
                }
                
                // if write back, set dirty to 1
                if (r_w == WRITE && !l2_write_through) {
                    read_from_pm(pa);
                    l2[min_unit].dirty = 1;
                    return;
                }
            }
        }
    }
}


// print the mapping linked list for debugging
void printlist(mapping *map)
{
    if (!(map->next))
        return;
    map = map->next;
    
    printf("vp: %x, pp: %x\n", map->vp, map->pp);
    
    while (map->next) {
        map = map->next;
        printf("vp: %x, pp: %x\n", map->vp, map->pp);
    }
}


// the function to access TLB
unsigned int access_tlb(unsigned int vp, mapping *map)
{   
    d_virt_page = vp;
    int i,j,k;
    cache_content_struct tlbc;
    // get the tlb tag and index
    tlbc = pa_to_cache(vp, tlb_set_number, tlb_set_size, 1);
    //printf("tlb index and tag: %x %x\n", tlbc.index, tlbc.tag);
    d_tlb_tag = tlbc.tag;
    d_tlb_ind = tlbc.index;
    
    int hit = 0;
    for (i = 0; i < tlb_set_size; i++) {
        //printf("%d\n",tlbc.index * tlb_set_size + i);
        // the hit situation
        if (tlb[tlbc.index * tlb_set_size + i].valid && tlb[tlbc.index * tlb_set_size + i].tag == tlbc.tag) {
            hit = 1;
            strcpy(d_tlb_res, "hit");
            dtlb_hits++;
            is_tlb_hit = 1;
            
            for (j = 0; j < tlb_set_size; j++)
                tlb[tlbc.index * tlb_set_size + j].lru >>= 1;
            tlb[tlbc.index * tlb_set_size + i].lru |= 1 << 31;
                
            for (j = 0; j < tlb_set_size; j++) {
                tlb[tlbc.index * tlb_set_size + j].lru >>= 1;
            }
            
            return tlb[tlbc.index * tlb_set_size + i].pp;
        }
    }
    
    // the not hit situation
    if (!hit) {
        strcpy(d_tlb_res, "miss");
        dtlb_misses++;
        is_tlb_hit = 0;
        
        // if the index has a empty slot, put the element in it
        int there_is_empty = 0;
        for (i = 0; i < tlb_set_size; i++) {
            if (tlb[tlbc.index * tlb_set_size + i].valid == 0) {
                
                tlb[tlbc.index * tlb_set_size + i].tag = tlbc.tag;
                tlb[tlbc.index * tlb_set_size + i].vp = vp;
                
                there_is_empty = 1;
                
                for (j = 0; j < tlb_set_size; j++)
                    tlb[tlbc.index * tlb_set_size + j].lru >>= 1;
                tlb[tlbc.index * tlb_set_size + i].lru |= 1 << 31;
                
                tlb[tlbc.index * tlb_set_size + i].pp = lookup_or_add(map, vp);
                tlb[tlbc.index * tlb_set_size + i].valid = 1;
                
                return tlb[tlbc.index * tlb_set_size + i].pp;
            }
        }

        // if there is no empty slot, replace the least lru element
        if (!there_is_empty) {
            unsigned long min = tlb[tlbc.index * tlb_set_size].lru;
            int min_unit = tlbc.index * tlb_set_size;
            for (i = 1; i < tlb_set_size; i++) {
                if (tlb[tlbc.index * tlb_set_size + i].lru < min) {
                    min = tlb[tlbc.index * tlb_set_size + i].lru;
                    min_unit = tlbc.index * tlb_set_size + i;
                }
            }
            
            // assign the new tag and page value
            tlb[min_unit].tag = tlbc.tag;
            tlb[min_unit].vp = vp;
            
            for (i = 0; i < tlb_set_size; i++)
                tlb[tlbc.index * tlb_set_size + i].lru >>= 1;
            tlb[min_unit].lru |= 1 << 31;
            
            tlb[min_unit].pp = lookup_or_add(map, vp);
            return tlb[min_unit].pp;
        }
    }
}


int main()
{	
    int i,j,k;
    char buf[50][50];
    
    i = 0;
    
    // read the configuration file
    FILE *fr = fopen ("trace.config", "rt");
    while (readFromFile(buf[i], fr))
        i++;

    // flush the log file
    fp = fopen("trace.log", "w");
    fclose(fp);
    
    // open the log file and append
    fp = fopen("trace.log", "a");
    
    // parse the config file
    tlb_set_number = atoi(last_word(buf[1]));
    tlb_set_size = atoi(last_word(buf[2]));

    virtual_page_number = atoi(last_word(buf[5]));
    physical_page_number = atoi(last_word(buf[6]));
    page_size = atoi(last_word(buf[7]));

    dc_set_number = atoi(last_word(buf[10]));
    dc_set_size = atoi(last_word(buf[11]));
    dc_line_size = atoi(last_word(buf[12]));

    if (!strcmp(last_word(buf[13]), "y"))
        dc_write_through = 1;

    l2_set_number = atoi(last_word(buf[16]));
    l2_set_size = atoi(last_word(buf[17]));
    l2_line_size = atoi(last_word(buf[18]));

    if (!strcmp(last_word(buf[19]), "y"))
        l2_write_through = 1;

    // calculate the bit
    tlb_index_bit = log2_my(tlb_set_number);

    dc_index_bit = log2_my(dc_set_number);
    dc_offset_bit = log2_my(dc_line_size);
    l2_index_bit = log2_my(l2_set_number);
    l2_offset_bit = log2_my(l2_line_size);

    if (!strcmp(last_word(buf[21]), "y"))
        virtual_enable = 1;
    if (!strcmp(last_word(buf[22]), "y"))
        tlb_enable = 1;
    if (!strcmp(last_word(buf[23]), "y"))
        l2_enable = 1;

    printf("Data TLB contains %d sets.\n", tlb_set_number);
    printf("Each set contains %d entries.\n", tlb_set_size);
    printf("Number of bits used for the index is %d.\n\n", tlb_index_bit);

    virtual_page_index_bit = log2_my(virtual_page_number);
    page_offset_bit = log2_my(page_size);

    printf("Number of virtual pages is %d.\n", virtual_page_number);
    printf("Number of physical pages is %d.\n", physical_page_number);
    printf("Each page contains %d bytes.\n", page_size);
    printf("Number of bits used for the page table index is %u.\n", 
            virtual_page_index_bit);
    printf("Number of bits used for the page offset is %u.\n\n", page_offset_bit);

    printf("D-cache contains %d sets.\n", dc_set_number);
    printf("Each set contains %d entries.\n", dc_set_size);
    printf("Each line is %d bytes.\n", dc_line_size);
    if (dc_write_through)
        printf("The cache uses a no write-allocate and write-through policy.\n");
    else
        printf("The cache uses a write-allocate and write-back policy.\n");
    printf("Number of bits used for the index is %u.\n", dc_index_bit);
    printf("Number of bits used for the offset is %u.\n\n", dc_offset_bit);

    printf("L2-cache contains %d sets.\n", l2_set_number);
    printf("Each set contains %d entries.\n", l2_set_size);
    printf("Each line is %d bytes.\n", l2_line_size);
    if (l2_write_through)
        printf("The cache uses a no write-allocate and write-through policy.\n");
    else
        printf("The cache uses a write-allocate and write-back policy.\n");
    printf("Number of bits used for the index is %u.\n", l2_index_bit);
    printf("Number of bits used for the offset is %u.\n\n", l2_offset_bit);

    if (virtual_enable)
        printf("The addresses read in are virtual addresses.\n");
    else
        printf("The addresses read in are physical addresses.\n");
    if (!tlb_enable)
        printf("TLB is disabled in this configuration.\n");
    if (!l2_enable)
        printf("L2 cache is disabled in this configuration.\n");
    printf("\n");
    
    if(virtual_enable)
        printf("Virtual  Virt.  Page TLB    TLB TLB  PT   Phys        DC  DC          L2  L2\n");
    else
        printf("Physical Virt.  Page TLB    TLB TLB  PT   Phys        DC  DC          L2  L2\n");
        
    printf("Address  Page # Off  Tag    Ind Res. Res. Pg # DC Tag Ind Res. L2 Tag Ind Res.\n");
    printf("-------- ------ ---- ------ --- ---- ---- ---- ------ --- ---- ------ --- ----\n");
    
    // malloc spaces for the DC, L2, TLB, physical memory and the mapping
    dc = malloc(dc_set_size * dc_set_number * sizeof(cache_struct));
    l2 = malloc(l2_set_size * l2_set_number * sizeof(cache_struct));
    tlb = malloc(tlb_set_size * tlb_set_number * sizeof(cache_struct));
    pm = malloc(physical_page_number * sizeof(p_mem_struct));
    
    map = (mapping*)malloc(sizeof(mapping));
    map->next = NULL;
    map->valid = 0;
    
    // initialization
    for (i = 0; i < dc_set_size * dc_set_number; i++) {
        dc[i].valid = 0;
        dc[i].lru = 0;
        dc[i].tag = 0;
        dc[i].dirty = 0;
    }
    
    for (i = 0; i < l2_set_size * l2_set_number; i++) {
        l2[i].valid = 0;
        l2[i].lru = 0;
        l2[i].tag = 0;
        l2[i].dirty = 0;
    }
    
    for (i = 0; i < tlb_set_size * tlb_set_number; i++) {
        tlb[i].valid = 0;
        tlb[i].lru = 0;
        tlb[i].tag = 0;
    }
    
    for (i = 0; i < physical_page_number; i++) {
        pm[i].empty = 1;
        pm[i].lru = 0;
        pm[i].dirty = 0;
    }

    
    char buf_input[1000][50];
    
    int line_count = 0;
    while (readFromIn(buf_input[line_count])) {
        char *d = strtok(buf_input[line_count], ":");
        int r_w;
        unsigned int address;
        int dis = 0;
        while (d != NULL) {
            if (dis == 0) {
                // count WRITE and READ
                if(!strcmp(d, "W")) {
                    r_w = WRITE;
                    total_writes++;
                } else {
                    r_w = READ;
                    total_reads++;
                }
                dis++;
            } else {
                address = (unsigned int)strtol(d, NULL, 16);
            }
            d = strtok(NULL, ":");
        }
        
        d_page_off = address % page_size;
        
        if (r_w == READ)
            fprintf(fp, "read at %08x\n", address);
        else
            fprintf(fp, "write at %08x\n", address);
        
        is_dc_hit = 0;
        is_tlb_hit = 0;
        
        // calculate the page number, cache tag, cache index, and access the DC
        // using physical address if virtual memory is not enabled
        if (*buf_input[line_count])
            if (!virtual_enable) {
                d_phys_pg = address / page_size;
                original_address = address;
                
                access_dc(r_w, address);
                cache_content_struct l2c = pa_to_cache(address, l2_set_number, l2_set_size, l2_line_size);
                d_l2_tag = l2c.tag;
                d_l2_ind = l2c.index;
            }
            // if it is virtual address, translate it from virtual to physical
            else {
                unsigned int pa = v2p(address, map);
                //printf("physical address %x\n",pa);
                
                unsigned int page = pa_to_ppn(pa);
                original_address = pa;
                d_phys_pg = page;
                
                access_dc(r_w, pa);
                
                cache_content_struct l2c = pa_to_cache(pa, l2_set_number, l2_set_size, l2_line_size);
                d_l2_tag = l2c.tag;
                d_l2_ind = l2c.index;
                
                // mark the page as dirty if WRITE access
                if (r_w == WRITE) {
                    pm[page].dirty = 1;
                }
            }


        if (debug)
            printlist(map);

        // print out the result       
        printf("%08x ", address);
        
        if (virtual_enable)
            printf("%6x ", d_virt_page);
        else
            printf("       ");
            
        printf("%4x ", d_page_off);
        
        if (tlb_enable)
            printf("%6x %3x %-4s ", d_tlb_tag, d_tlb_ind, d_tlb_res);
        else
            printf("                ");
            
        if (virtual_enable && !is_tlb_hit)
            printf("%-4s ", d_pt_res);
        else
            printf("     ");
        
        printf("%4x %6x %3x %-4s ", d_phys_pg, d_dc_tag, d_dc_ind, d_dc_res);
        
        if (l2_enable && (!is_dc_hit || 
           (r_w == WRITE && is_dc_hit && dc_write_through)))
            printf("%6x %3x %-4s", d_l2_tag, d_l2_ind, d_l2_res);

               
        if (debug)
            printf("\n==========================================================="
                   "==========================");
      
        printf("\n");
        fprintf(fp, "\n");
        
        line_count++;
    }

    // print out the statistical data
    printf("\nSimulation statistics\n\n");
    printf("dtlb hits        : %d\n", dtlb_hits);
    printf("dtlb misses      : %d\n", dtlb_misses);
    dtlb_hit_ratio = (float)dtlb_hits / (float)(dtlb_hits + dtlb_misses);
    if (tlb_enable)
        printf("dtlb hit ratio   : %f\n\n", dtlb_hit_ratio);
    else
        printf("dtlb hit ratio   : N/A\n\n");

    printf("pt hits          : %d\n", pt_hits);
    printf("pt faults        : %d\n", pt_faults);
    pt_hit_ratio = (float)pt_hits / (float)(pt_hits + pt_faults);
    if (virtual_enable)
        printf("pt hit ratio     : %f\n\n", pt_hit_ratio);
    else
        printf("pt hit ratio     : N/A\n\n", pt_hit_ratio);

    printf("dc hits          : %d\n", dc_hits);
    printf("dc misses        : %d\n", dc_misses);
    dc_hit_ratio = (float)dc_hits / (float)(dc_hits + dc_misses);
    printf("dc hit ratio     : %f\n\n", dc_hit_ratio);

    printf("L2 hits          : %d\n", l2_hits);
    printf("L2 misses        : %d\n", l2_misses);
    l2_hit_ratio = (float)l2_hits / (float)(l2_hits + l2_misses);
    if (l2_enable)
        printf("L2 hit ratio     : %f\n\n", l2_hit_ratio);
    else
        printf("L2 hit ratio     : N/A\n\n", l2_hit_ratio);
    
    printf("Total reads      : %d\n", total_reads);
    printf("Total writes     : %d\n", total_writes);
    ratio_of_reads = (float)total_reads / (float)(total_reads + total_writes);
    printf("Ratio of reads   : %f\n\n", ratio_of_reads);
    
    printf("main memory refs : %d\n", main_memory_refs);
    printf("page table refs  : %d\n", page_table_refs);
    printf("disk refs        : %d\n", disk_refs);
    
    fclose(fp);
    
    return 0;
}
