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

/** *************************************************************************  
 * \note \ref g_microamp_state is Kind of a dirty hack for now to provide a 
 * global interface to the microamp_state to the python interface.
****************************************************************************/
extern microamp_state_t* g_microamp_state;


/** *************************************************************************  
*************************** Poll For I/O Events ***************************** 
****************************************************************************/
void py_microamp_poll_hook(void)
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
    }
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
        if ( bytes_ptr )
        {
            int bytes_got = microamp_read(g_microamp_state,nhandle,bytes_ptr,bytes_len);
            if ( bytes_got >= 0 )
            {
                mp_obj_t result = mp_obj_new_bytes(bytes_ptr,bytes_got);
                m_free(bytes_ptr);
                return result;
            }
            m_free(bytes_ptr);
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
                const uint8_t* bytes_ptr = (const uint8_t*)mp_obj_str_get_data(buffer_obj,&bytes_got);
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
