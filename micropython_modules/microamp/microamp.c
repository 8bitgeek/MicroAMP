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
#include "microamp.h"
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
static int microamp_ring_avail(size_t head, size_t tail, size_t size);

/** *************************************************************************  
 * \note \ref g_microamp_state is Kind of a dirty hack for now to provide a 
 * global interface to the microamp_state to the python interface.
****************************************************************************/
static microamp_state_t* g_microamp_state=NULL;


/** *************************************************************************  
*************************** Poll For I/O Events ***************************** 
****************************************************************************/
void microamp_poll_hook(void)
{

    for(int nenadpoint=0; nenadpoint < g_microamp_state->endpointcnt; nenadpoint++)
    {
        volatile microamp_endpoint_t* endpoint = &g_microamp_state->endpoint[nenadpoint];
        
        /** Handle the Python-side events */
        if ( endpoint->dataready_event.py_fn || endpoint->dataempty_event.py_fn )
        {
            size_t avail;
            
            b_mutex_lock((brisc_mutex_t*)&endpoint->mutex);
            avail = microamp_ring_avail(endpoint->head,endpoint->tail,endpoint->shmemsz);
            endpoint->dataempty = !avail;
            b_mutex_unlock((brisc_mutex_t*)&endpoint->mutex);

            if ( avail && endpoint->dataready_event.py_fn )
            {
                mp_call_function_1(endpoint->dataready_event.py_fn,endpoint->dataready_event.py_arg);
            }

            if ( !avail && endpoint->dataempty && endpoint->dataempty_event.py_fn )
            {
                mp_call_function_1(endpoint->dataempty_event.py_fn,endpoint->dataempty_event.py_arg);
            }
        }

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

/** *************************************************************************  
 * \brief Calculate the tail pointer for a ring buffer 'get' operation.
 * \param head The current head pointer
 * \param tail The current tail pointer
 * \param buf the buffer 
 * \param size the size of the buffer. 
 * \return The updated tail pointer of < 0 on underflow
****************************************************************************/
static int microamp_ring_avail(size_t head, size_t tail, size_t size)
{
    if ( head!=tail )
    {
        if ( head > tail )
            return head-tail;
        
        return (size-tail) + head;
    }
    return 0;
}


/** *************************************************************************  
*************************** Python Static Interface *************************
****************************************************************************/


/** *************************************************************************   
 * \brief Create a new endpoint using @name, and a shared buffer 
 *        of @ref size bytes.
 * \param name The ascii name of the endpoint.
 * \param size The size of the shared memory buffer to allocate
 * \return 0 upon success, or < 0 indicates an error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_create(mp_obj_t name_obj, mp_obj_t size_obj) 
{
    if ( mp_obj_is_str(name_obj) && mp_obj_is_int(size_obj) )
    {
        const char* name = mp_obj_str_get_str(name_obj);
        size_t size = mp_obj_get_int(size_obj);

        return mp_obj_new_int( microamp_create( g_microamp_state,name,size) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(microamp_py_create_obj, microamp_py_create);


/** *************************************************************************   
 * \brief Test if an endpoint exists by @name
 * \param name The ascii name of the endpoint.
 * \return A index to the endpoint, or < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_indexof(mp_obj_t name_obj) 
{
    if ( mp_obj_is_str(name_obj) )
    {
        const char* name = mp_obj_str_get_str(name_obj);
        return mp_obj_new_int( microamp_indexof( g_microamp_state,name) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_indexof_obj, microamp_py_indexof);


/** *************************************************************************   
 * \return The number of endpoints.
****************************************************************************/
STATIC mp_obj_t microamp_py_count() 
{
    return mp_obj_new_int( microamp_count( g_microamp_state) );
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(microamp_py_count_obj, microamp_py_count);


/** *************************************************************************   
 * \param index the index of the endpoint to query.
 * \return the endpoint at \ref index.
****************************************************************************/
STATIC mp_obj_t microamp_py_at(mp_obj_t index_obj) 
{
    const char* str = microamp_at(g_microamp_state,mp_obj_get_int(index_obj));
    if ( str )
        return mp_obj_new_str(str,strlen(str));
    return mp_obj_new_str("",0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_at_obj, microamp_py_at);


/** *************************************************************************   
 * \brief Open an endpoint by @name
 * \param name The ascii name of the endpoint.
 * \return A handle to the endpoint, or < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_open(mp_obj_t name_obj) 
{
    if ( mp_obj_is_str(name_obj) )
    {
        const char* name = mp_obj_str_get_str(name_obj);
        return mp_obj_new_int( microamp_open( g_microamp_state,name) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_open_obj, microamp_py_open);


/** *************************************************************************   
 * \brief Close an endpoint by @handle
 * \param handle The ascii name of the endpoint.
 * \return 0 or  < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_close(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int handle = mp_obj_get_int(handle_obj);
        return mp_obj_new_int( microamp_close(g_microamp_state,handle) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_close_obj, microamp_py_close);


/** *************************************************************************   
 * \brief Blocking, lock the buffer semaphore.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_lock(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int handle = mp_obj_get_int(handle_obj);
        return mp_obj_new_int( microamp_lock(g_microamp_state,handle) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_lock_obj, microamp_py_lock);


/** *************************************************************************   
 * \brief unlock the buffer semaphore.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_unlock(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int handle = mp_obj_get_int(handle_obj);
        return mp_obj_new_int( microamp_unlock(g_microamp_state,handle) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_unlock_obj, microamp_py_unlock);


/** *************************************************************************   
 * \brief Non-blocking, lock the buffer semaphore.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
STATIC mp_obj_t microamp_py_trylock(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int handle = mp_obj_get_int(handle_obj);
        return mp_obj_new_int( microamp_trylock(g_microamp_state,handle) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_trylock_obj, microamp_py_trylock);


/** *************************************************************************   
 * \brief Read bytes from the endpoint associated with \ref nhandle.
 * \param nhandle The handle of the endpoint.
 * \param buffer A pointer to the read storage buffer area.
 * \param size The maximum size to read.
 * \return the number of bytes read, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_read(mp_obj_t handle_obj,mp_obj_t buffer_obj,mp_obj_t size_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        size_t buffer_len;
        int handle = mp_obj_get_int(handle_obj);
        uint8_t* buffer = (uint8_t*)mp_obj_str_get_data(buffer_obj,&buffer_len);
        size_t size = mp_obj_get_int(size_obj);
        return mp_obj_new_int( microamp_read(g_microamp_state,handle,buffer,size) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(microamp_py_read_obj, microamp_py_read);


/** *************************************************************************   
 * \brief Write bytes to the endpoint associated with \ref nhandle.
 * \param nhandle The handle of the endpoint.
 * \param buffer A pointer to the write storage buffer area.
 * \param size The maximum size to write.
 * \return the number of bytes written, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_write(mp_obj_t handle_obj,mp_obj_t buffer_obj,mp_obj_t size_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        size_t buffer_len;
        int nhandle = mp_obj_get_int(handle_obj);
        const uint8_t* buffer = (const uint8_t*)mp_obj_str_get_data(buffer_obj,&buffer_len);
        size_t size = mp_obj_get_int(size_obj);
        microamp_handle_t* handle = &g_microamp_state->handle[nhandle];
        handle->endpoint->dataempty_event.writer_thread = b_thread_current();
        return mp_obj_new_int( microamp_write(g_microamp_state,nhandle,buffer,size) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(microamp_py_write_obj, microamp_py_write);


/** *************************************************************************   
 * \brief Read bytes from the endpoint associated with \ref nhandle.
 * \param nhandle The handle of the endpoint
 * \return the number of bytes read, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_get(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int nhandle = mp_obj_get_int(handle_obj);
        size_t bytes_len = microamp_avail(g_microamp_state,nhandle);
        byte* bytes_ptr = m_new(byte, bytes_len + 1);
        int bytes_got = microamp_read(g_microamp_state,nhandle,bytes_ptr,bytes_len);
        if ( bytes_got >= 0 )
        {
            mp_obj_t result = mp_obj_new_bytes(bytes_ptr,bytes_got);
            m_free(bytes_ptr);
            return result;
        }
    }
    return mp_obj_new_bytes((const byte*)"",0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_get_obj, microamp_py_get);


/** *************************************************************************   
 * \brief Write bytes to the endpoint associated with \ref nhandle.
 * \param nhandle The handle of the endpoint.
 * \param buffer A pointer to the write storage buffer area.
 * \return the number of bytes written, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_put(mp_obj_t handle_obj,mp_obj_t buffer_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int nhandle = mp_obj_get_int(handle_obj);
        if ( nhandle >= 0 && nhandle < MICROAMP_MAX_HANDLE)
        {
            if ( /* mp_obj_is_str_or_bytes(buffer_obj) */ 1 )
            {
                size_t bytes_got;
                microamp_handle_t* handle = &g_microamp_state->handle[nhandle]; 
                const uint8_t* bytes_ptr = (const uint8_t*)mp_obj_str_get_data(buffer_obj,&bytes_got);
                handle->endpoint->dataempty_event.writer_thread = b_thread_current();
                if ( microamp_write(g_microamp_state,nhandle,bytes_ptr,bytes_got) >= 0 )
                    return  mp_obj_new_bytes(bytes_ptr,bytes_got);
            }
        }
    }
    return mp_obj_new_bytes((const byte*)"",0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(microamp_py_put_obj, microamp_py_put);


/** *************************************************************************   
 * \brief Number of bytes available bytes to the endpoint associated 
 *        with \ref nhandle.
 * \param nhandle The handle of the endpoint.
 * \return the number of bytes available, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_avail(mp_obj_t handle_obj) 
{
    if ( mp_obj_is_int(handle_obj) )
    {
        int handle = mp_obj_get_int(handle_obj);
        return mp_obj_new_int( microamp_avail(g_microamp_state,handle) );
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(microamp_py_avail_obj, microamp_py_avail);

/** *************************************************************************   
 * \brief Add a dataready event callback
 * \param callback A function pointer.
 * \param arg The arg to pass to the callback.
 * \return the number of bytes available, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_dataready_handler(mp_obj_t handle_obj,mp_obj_t callback_obj,mp_obj_t arg_obj) 
{
    if ( mp_obj_is_int(handle_obj) && mp_obj_is_callable(callback_obj) )
    {
        int nhandle = mp_obj_get_int(handle_obj);
        microamp_handle_t* handle = &g_microamp_state->handle[nhandle];
        handle->endpoint->dataready_event.py_fn = callback_obj;
        handle->endpoint->dataready_event.py_arg = arg_obj;
        return callback_obj;
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(microamp_py_dataready_handler_obj, microamp_py_dataready_handler);


/** *************************************************************************   
 * \brief Add a dataempty event callback
 * \param callback A function pointer.
 * \param arg The arg to pass to the callback.
 * \return the number of bytes available, or < 0 on error.
****************************************************************************/
STATIC mp_obj_t microamp_py_dataempty_handler(mp_obj_t handle_obj,mp_obj_t callback_obj,mp_obj_t arg_obj) 
{
    if ( mp_obj_is_int(handle_obj) && mp_obj_is_callable(callback_obj) )
    {
        int nhandle = mp_obj_get_int(handle_obj);
        microamp_handle_t* handle = &g_microamp_state->handle[nhandle];
        handle->endpoint->dataempty_event.py_fn = callback_obj;
        handle->endpoint->dataempty_event.py_arg = arg_obj;
        return callback_obj;
    }
    return mp_obj_new_int(MICROAMP_ERR_INVAL);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(microamp_py_dataempty_handler_obj, microamp_py_dataempty_handler);


/** *************************************************************************   
 * Define all properties of the module.
 * Table entries are key/value pairs of the attribute name (a string)
 * and the MicroPython object reference.
 * All identifiers and strings are written as MP_QSTR_xxx and will be
 * optimized to word-sized integers by the build system (interned strings).
****************************************************************************/
STATIC const mp_rom_map_elem_t microamp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_microamp) },
    { MP_ROM_QSTR(MP_QSTR_endpoint_create), MP_ROM_PTR(&microamp_py_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_endpoint_indexof), MP_ROM_PTR(&microamp_py_indexof_obj) },
    { MP_ROM_QSTR(MP_QSTR_endpoint_count), MP_ROM_PTR(&microamp_py_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_endpoint_at), MP_ROM_PTR(&microamp_py_at_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_open), MP_ROM_PTR(&microamp_py_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_close), MP_ROM_PTR(&microamp_py_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_lock), MP_ROM_PTR(&microamp_py_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_unlock), MP_ROM_PTR(&microamp_py_unlock_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_trylock), MP_ROM_PTR(&microamp_py_trylock_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_read), MP_ROM_PTR(&microamp_py_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_write), MP_ROM_PTR(&microamp_py_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_get), MP_ROM_PTR(&microamp_py_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_put), MP_ROM_PTR(&microamp_py_put_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_avail), MP_ROM_PTR(&microamp_py_avail_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_dataready_handler), MP_ROM_PTR(&microamp_py_dataready_handler_obj) },
    { MP_ROM_QSTR(MP_QSTR_channel_dataempty_handler), MP_ROM_PTR(&microamp_py_dataempty_handler_obj) },
};
STATIC MP_DEFINE_CONST_DICT(microamp_module_globals, microamp_module_globals_table);


/** *************************************************************************  
*************************** Python Public Interface *************************
****************************************************************************/
const mp_obj_module_t microamp_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&microamp_module_globals,
};

/** *************************************************************************   
 * Register the module to make it available in Python.
 * Note: the "1" in the third argument means this module is always enabled.
 * This "1" can be optionally replaced with a macro like MODULE_microamp_ENABLED
 * which can then be used to conditionally enable this module.
****************************************************************************/
MP_REGISTER_MODULE(MP_QSTR_microamp, microamp_cmodule, 1);
