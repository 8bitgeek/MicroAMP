/** *************************************************************************   
 _____ _             _____ _____ _____ 
|     |_|___ ___ ___|  _  |     |  _  |
| | | | |  _|  _| . |     | | | |   __|
|_|_|_|_|___|_| |___|__|__|_|_|_|__|                       

MIT License

Copyright (c) 2021 Mike Sharkey

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

****************************************************************************/
#include "microamp_c.h"
#include <stdlib.h>
#include <string.h>

extern cpu_reg_t    __microamp_pages__;
extern cpu_reg_t    __microamp_page_size__;
extern cpu_reg_t    __microamp_shared_ram__;
extern cpu_reg_t    __microamp_shared_size__;

#define microamp_malloc         m_malloc
#define microamp_realloc        m_realloc
#define microamp_free           m_free

#define microamp_shmem_base()   ((void*)&__microamp_shared_ram__)
#define microamp_shmem_size()   ((size_t)&__microamp_shared_size__)
#define microamp_shmem_pages()  ((size_t)&__microamp_pages__)
#define microamp_shmem_pagesz() ((size_t)&__microamp_page_size__)
#define microamp_shmem_page(n)  ((size_t)microamp_shmem_base()+(microamp_shmem_pagesz()*(n)))

static microamp_endpoint_t* microamp_new_endpoint(microamp_state_t* microamp_state);
static int microamp_get_empty_handle(microamp_state_t* microamp_state);
static int microamp_lookup(microamp_state_t* microamp_state,const char* name);
static int microamp_ring_put(size_t head, size_t tail, uint8_t* buf, size_t size, uint8_t ch);
static int microamp_ring_get(size_t head, size_t tail, const uint8_t* buf, size_t size, uint8_t* ch);

/** *************************************************************************  
 * \note \ref g_microamp_state is Kind of a dirty hack for now to provide a 
 * global interface to the microamp_state to the python interface.
****************************************************************************/
microamp_state_t* g_microamp_state=NULL;


/** *************************************************************************  
*************************** Poll For I/O Events ***************************** 
****************************************************************************/
void microamp_poll_hook(void)
{

    for(int nenadpoint=0; nenadpoint < g_microamp_state->endpointcnt; nenadpoint++)
    {
        volatile microamp_endpoint_t* endpoint = &g_microamp_state->endpoint[nenadpoint];
        
        // /** Handle the Python-side events */
        // if ( endpoint->dataready_event.py_fn || endpoint->dataempty_event.py_fn )
        // {
        //     size_t avail;
            
        //     b_mutex_lock((brisc_mutex_t*)&endpoint->mutex);
        //     avail = microamp_ring_avail(endpoint->head,endpoint->tail,endpoint->shmemsz);
        //     endpoint->dataempty = !avail;
        //     b_mutex_unlock((brisc_mutex_t*)&endpoint->mutex);

        //     if ( avail && endpoint->dataready_event.py_fn )
        //     {
        //         mp_call_function_1(endpoint->dataready_event.py_fn,endpoint->dataready_event.py_arg);
        //     }

        //     if ( !avail && endpoint->dataempty && endpoint->dataempty_event.py_fn )
        //     {
        //         mp_call_function_1(endpoint->dataempty_event.py_fn,endpoint->dataempty_event.py_arg);
        //     }
        // }

        /** Handle the 'C' side events */
        if ( (endpoint->dataready_event.c_fn || endpoint->dataempty_event.c_fn) )
        {
            size_t avail;
            
            b_mutex_lock((brisc_mutex_t*)&endpoint->mutex);
            avail = microamp_ring_avail(endpoint->head,endpoint->tail,endpoint->shmemsz);
            endpoint->dataempty = !avail;
            b_mutex_unlock((brisc_mutex_t*)&endpoint->mutex);

            if ( avail && endpoint->dataready_event.c_fn )
            {
                endpoint->dataready_event.c_fn(endpoint->dataready_event.c_arg);
            }

            if ( !avail && endpoint->dataempty && endpoint->dataempty_event.c_fn )
            {
                endpoint->dataempty_event.c_fn(endpoint->dataempty_event.c_arg);
            }
        }


    }
}


/** *************************************************************************  
*************************** 'C' Public Interface ****************************
****************************************************************************/

void microamp_init(microamp_state_t* microamp_state)
{
    memset(microamp_state,0,sizeof(microamp_state_t));
    g_microamp_state=microamp_state;
}

int microamp_indexof(microamp_state_t* microamp_state,const char* name)
{
    b_mutex_lock(&microamp_state->mutex);
    int index = microamp_lookup(microamp_state,name);
    b_mutex_unlock(&microamp_state->mutex);
    return index;
}

int microamp_count(microamp_state_t* microamp_state)
{
    int cnt;
    b_mutex_lock(&microamp_state->mutex);
    cnt = microamp_state->endpointcnt;
    b_mutex_unlock(&microamp_state->mutex);
    return cnt;
}

const char* microamp_at(microamp_state_t* microamp_state,int index)
{
    if ( index < microamp_state->endpointcnt )
    {
        microamp_endpoint_t* endpoint = &microamp_state->endpoint[index];
        return (const char*)endpoint->name;
    }
    return NULL;
}

int microamp_create(microamp_state_t* microamp_state,const char* name,size_t size)
{
    if ( size <= microamp_shmem_pagesz() )
    {
        b_mutex_lock(&microamp_state->mutex);
        if ( microamp_lookup(microamp_state,name) == MICROAMP_ERR_NONE )
        {
            microamp_endpoint_t* endpoint = microamp_new_endpoint(microamp_state);
            if ( endpoint != NULL )
            {
                int index = microamp_state->endpointcnt-1;
                strncpy(endpoint->name,name,MICROAMP_MAX_NAME);
                endpoint->shmembase = microamp_shmem_page(index);
                endpoint->shmemsz = size;
                b_mutex_unlock(&microamp_state->mutex);
                return index;
            }
            b_mutex_unlock(&microamp_state->mutex);
            return MICROAMP_ERR_RES;
        }
        b_mutex_unlock(&microamp_state->mutex);
        return MICROAMP_ERR_DUP;
    }
    return MICROAMP_ERR_RES;
}

int microamp_open(microamp_state_t* microamp_state,const char* name)
{
    b_mutex_lock(&microamp_state->mutex);
    int nendpoint = microamp_lookup(microamp_state,name);
    if ( nendpoint >= 0 )
    {
        int nhandle = microamp_get_empty_handle(microamp_state);
        if ( nhandle >= 0 )
        {
            microamp_handle_t* handle = &microamp_state->handle[nhandle];
            handle->endpoint = &microamp_state->endpoint[nendpoint];
            handle->endpoint->nrefs++;
            b_mutex_unlock(&microamp_state->mutex);
            return nhandle;
        }
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

int microamp_close(microamp_state_t* microamp_state,int nhandle)
{
    b_mutex_lock(&microamp_state->mutex);
    if ( nhandle >= 0 && nhandle < MICROAMP_MAX_HANDLE )
    {
        microamp_handle_t* handle = &microamp_state->handle[nhandle];
        if ( handle->endpoint != NULL && handle->endpoint->nrefs > 0 )
        {
            --handle->endpoint->nrefs;
            memset(handle,0,sizeof(microamp_handle_t));
            b_mutex_unlock(&microamp_state->mutex);
            return 0;
        }
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

extern int microamp_lock(microamp_state_t* microamp_state,int nhandle)
{
    microamp_handle_t* handle = &microamp_state->handle[nhandle];
    if ( handle->endpoint != NULL )
    {
        b_mutex_lock(&handle->endpoint->mutex);
        return 0;
    }
    return MICROAMP_ERR_NONE;
}

extern int microamp_unlock(microamp_state_t* microamp_state,int nhandle)
{
    microamp_handle_t* handle = &microamp_state->handle[nhandle];
    if ( handle->endpoint != NULL )
    {
        b_mutex_unlock(&handle->endpoint->mutex);
        return 0;
    }
    return MICROAMP_ERR_NONE;
}

extern int microamp_trylock(microamp_state_t* microamp_state,int nhandle)
{
    microamp_handle_t* handle = &microamp_state->handle[nhandle];
    if ( handle->endpoint != NULL )
    {
        return b_mutex_try_lock(&handle->endpoint->mutex) ? MICROAMP_ERR_BLOCK : 0;
    }
    return MICROAMP_ERR_NONE;
}

extern int microamp_read(microamp_state_t* microamp_state,int nhandle,void* buf,size_t size)
{
    microamp_handle_t* handle;
    b_mutex_lock(&microamp_state->mutex);
    if ( nhandle >= 0 && nhandle < MICROAMP_MAX_HANDLE )
    {
        handle = &microamp_state->handle[nhandle];
        if ( handle->endpoint )
        {
            uint8_t* p = (uint8_t*)buf;
            for(int n=0; n < size; n++)
            {
                int t;
                if ( (t=microamp_ring_get( handle->endpoint->head,
                                        handle->endpoint->tail,
                                        (void*)handle->endpoint->shmembase,
                                        handle->endpoint->shmemsz,&p[n]
                                        )) < 0 )
                {
                    b_mutex_unlock(&microamp_state->mutex);
                    return MICROAMP_ERR_UNDFL;
                }
                handle->endpoint->tail = t;
                if ( microamp_ring_avail( handle->endpoint->head, handle->endpoint->tail, handle->endpoint->shmemsz ) == 0 )
                    handle->endpoint->dataempty = true;
            }
            b_mutex_unlock(&microamp_state->mutex);
            return size;
        }
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

extern int microamp_write(microamp_state_t* microamp_state,int nhandle,const void* buf,size_t size)
{
    microamp_handle_t* handle;
    b_mutex_lock(&microamp_state->mutex);
    if ( nhandle >= 0 && nhandle < MICROAMP_MAX_HANDLE )
    {
        handle = &microamp_state->handle[nhandle];
        if ( handle->endpoint )
        {
            uint8_t* p = (uint8_t*)buf;
            for(int n=0; n < size; n++)
            {
                int h;
                if ( (h=microamp_ring_put( handle->endpoint->head,
                                        handle->endpoint->tail,
                                        (void*)handle->endpoint->shmembase,
                                        handle->endpoint->shmemsz,p[n]
                                        )) < 0 )
                {
                    b_mutex_unlock(&microamp_state->mutex);
                    return MICROAMP_ERR_OVRFL;
                }
                handle->endpoint->head = h;
                if ( microamp_ring_avail( handle->endpoint->head, handle->endpoint->tail, handle->endpoint->shmemsz ) > 0 )
                    handle->endpoint->dataempty = false;
                handle->endpoint->dataempty_event.writer_thread = b_thread_current();
            }
            b_mutex_unlock(&microamp_state->mutex);
            return size;
        }
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

extern int microamp_avail(microamp_state_t* microamp_state,int nhandle)
{
    microamp_handle_t* handle;
    b_mutex_lock(&microamp_state->mutex);
    if ( nhandle >= 0 && nhandle < MICROAMP_MAX_HANDLE )
    {
        handle = &microamp_state->handle[nhandle];
        if ( handle->endpoint )
        {
            size_t size = microamp_ring_avail( handle->endpoint->head,
                                        handle->endpoint->tail,
                                        handle->endpoint->shmemsz);
            b_mutex_unlock(&microamp_state->mutex);
            return size;
        }
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

extern int microamp_dataready_handler(microamp_state_t* microamp_state,int nhandle,void(*fn)(void*),void* arg)
{
    microamp_handle_t* handle;
    b_mutex_lock(&microamp_state->mutex);
    handle = &microamp_state->handle[nhandle];
    if ( handle->endpoint )
    {
        handle->endpoint->dataready_event.c_fn = fn;
        handle->endpoint->dataready_event.c_arg = arg;
        b_mutex_unlock(&microamp_state->mutex);
        return 0;
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}

extern int microamp_dataempty_handler(microamp_state_t* microamp_state,int nhandle,void(*fn)(void*),void* arg)
{
    microamp_handle_t* handle;
    b_mutex_lock(&microamp_state->mutex);
    handle = &microamp_state->handle[nhandle];
    if ( handle->endpoint )
    {
        handle->endpoint->dataempty_event.c_fn = fn;
        handle->endpoint->dataempty_event.c_arg = arg;
        b_mutex_unlock(&microamp_state->mutex);
        return 0;
    }
    b_mutex_unlock(&microamp_state->mutex);
    return MICROAMP_ERR_NONE;
}


/** *************************************************************************  
*************************** 'C' Static Interface ****************************
****************************************************************************/

/** *************************************************************************  
 * \brief Instantiate a new endpoint and insert into the endpoint list.
 * \param microamp_state Pointer to starage for MicroAMP state.
 * \return pointer to new endpoint or NULL on failed
****************************************************************************/
static microamp_endpoint_t* microamp_new_endpoint(microamp_state_t* microamp_state)
{
    if ( microamp_state->endpointcnt < MICROAMP_MAX_ENDPOINT )
    {
        microamp_endpoint_t* endpoint = &microamp_state->endpoint[microamp_state->endpointcnt++];
        memset(endpoint,0,sizeof(microamp_endpoint_t));
        return endpoint;
    }
    return NULL;
}

/** *************************************************************************  
 * \return an empty handle.
****************************************************************************/
static int microamp_get_empty_handle(microamp_state_t* microamp_state)
{
    microamp_handle_t empty_handle;
    memset(&empty_handle,0,sizeof(microamp_handle_t));
    for(int nhandle=0; nhandle < MICROAMP_MAX_HANDLE; nhandle++)
    {
        if ( memcmp( &microamp_state->handle[nhandle], &empty_handle, sizeof(microamp_handle_t) ) == 0 )
        {
            return nhandle;
        }
    }
    return MICROAMP_ERR_NONE;
}

/** *************************************************************************  
 * \return the index of and endpoint with @ref name, or < 0 on fail.
 * \param name the name of the endpoint to locate. 
****************************************************************************/
static int microamp_lookup(microamp_state_t* microamp_state,const char* name)
{
    for( int index=0; index < microamp_state->endpointcnt; index++ )
    {
        microamp_endpoint_t* endpoint = &microamp_state->endpoint[index];
        if ( strcmp(endpoint->name,name) == 0 )
        {
            return index;
        }
    }
    return MICROAMP_ERR_NONE;
}

/** *************************************************************************  
 * \brief Calculate the head pointer for a ring buffer 'put' operation.
 * \param head The current head pointer
 * \param tail The current tail pointer
 * \param buf the buffer 
 * \param size the size of the buffer. 
 * \return The updated head pointer of < 0 on overflow
****************************************************************************/
static int microamp_ring_put(size_t head, size_t tail, uint8_t* buf, size_t size, uint8_t ch)
{
    buf[head++] = ch;
    if ( head >= size )
        head=0;
    if ( head==tail )
        return MICROAMP_ERR_OVRFL;
    return (int)head;
}

/** *************************************************************************  
 * \brief Calculate the tail pointer for a ring buffer 'get' operation.
 * \param head The current head pointer
 * \param tail The current tail pointer
 * \param buf the buffer 
 * \param size the size of the buffer. 
 * \return The updated tail pointer of < 0 on underflow
****************************************************************************/
static int microamp_ring_get(size_t head, size_t tail, const uint8_t* buf, size_t size, uint8_t* ch)
{
    if ( head==tail )
        return MICROAMP_ERR_UNDFL;
    *ch = buf[tail++];
    if ( tail >= size )
        tail = 0;
    return (int)tail;
}

extern int microamp_ring_avail(size_t head, size_t tail, size_t size)
{
    if ( head!=tail )
    {
        if ( head > tail )
            return head-tail;
        
        return (size-tail) + head;
    }
    return 0;
}

