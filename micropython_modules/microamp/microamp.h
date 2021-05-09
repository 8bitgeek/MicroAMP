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
#ifndef __MICROAMP_H__
#define __MICROAMP_H__

#include <py/runtime.h>
#include <brisc_thread.h>
#include <brisc_mutex.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if !defined(MICROAMP_MAX_ENDPOINT)
#define MICROAMP_MAX_ENDPOINT 16  /**< maximum number of endpoint handles */
#endif

#if !defined(MICROAMP_MAX_HANDLE)
#define MICROAMP_MAX_HANDLE (MICROAMP_MAX_ENDPOINT*2)  
                                /**< maximum number of endpoint handles */
#endif

#if !defined(MICROAMP_MAX_NAME)
#define MICROAMP_MAX_NAME   10  /**< Maximum endpoint-name string length */
#endif

#define MICROAMP_ERR_DUP    -1  /**< Duplicate (endpoint name) */
#define MICROAMP_ERR_RES    -2  /**< No resource availabel to meet request */
#define MICROAMP_ERR_PROT   -3  /**< A protection violation */
#define MICROAMP_ERR_NONE   -4  /**< None, not-found */
#define MICROAMP_ERR_BLOCK  -5  /**< Would block */
#define MICROAMP_ERR_OVRFL  -6  /**< Overflow */
#define MICROAMP_ERR_UNDFL  -7  /**< Underflow */
#define MICROAMP_ERR_INVAL  -8  /**< Invalid Input */

/** *************************************************************************  
 * \brief maintains the state of an endpoint callback.
****************************************************************************/
typedef struct _microamp_callback_
{
    mp_obj_t                    py_fn;
    mp_obj_t                    py_arg;
    void                        (*c_fn)(void);
    void*                       c_arg;
    volatile brisc_thread_t*    thread;

} microamp_callback_t;

/** *************************************************************************  
 * \brief maintains the state of an endpoint.
****************************************************************************/
typedef struct _microamp_endpoint_
{
    char            name[MICROAMP_MAX_NAME+1];
    size_t          shmembase;
    size_t          shmemsz;
    brisc_mutex_t   mutex;
    size_t          nrefs;
    size_t          head;
    size_t          tail;
    microamp_callback_t     dataready_event;
    microamp_callback_t     dataempty_event;
} microamp_endpoint_t;

/** *************************************************************************  
 * \brief maintains the state of an endpoint handle.
****************************************************************************/
typedef struct _microamp_handle_
{
    microamp_endpoint_t*    endpoint;
} microamp_handle_t;

/** *************************************************************************  
 * \brief maintains the state of an openamp insatnce
****************************************************************************/
typedef struct _microamp_state_
{
    microamp_endpoint_t     endpoint[MICROAMP_MAX_ENDPOINT];
    size_t                  endpointcnt;
    brisc_mutex_t           mutex;
    microamp_handle_t       handle[MICROAMP_MAX_HANDLE];
} microamp_state_t;

/** *************************************************************************  
*************************** Poll For I/O Events ***************************** 
****************************************************************************/
extern void microamp_poll_hook(void);

/** *************************************************************************  
 * \brief Initialize MicroAMP state
 * \param microamp_state Pointer to starage for MicroAMP state.
****************************************************************************/
extern void microamp_init(microamp_state_t* microamp_state);

/** *************************************************************************   
 * \brief Create a new endpoint using @name, and a shared buffer 
 *        of @ref size bytes.
 * \param microamp_state A pointer to the microamp state.
 * \param name The ascii name of the endpoint.
 * \param size The size of the shared memory buffer to allocate
 * \return 0 upon success, or < 0 indicates an error condition.
****************************************************************************/
extern int microamp_create(microamp_state_t* microamp_state,
                            const char* name,
                            size_t size);

/** *************************************************************************   
 * \brief Test if an endpoint exists by @name
 * \param microamp_state A pointer to the microamp state.
 * \param name The ascii name of the endpoint.
 * \return A index to the endpoint, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_indexof(microamp_state_t* microamp_state,
                            const char* name);

/** *************************************************************************   
 * \return Number of endpoints, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_count(microamp_state_t* microamp_state);

/** *************************************************************************   
 * \param index of the endpoint to query name
 * \return Number naem of the endpoint at index
****************************************************************************/
extern const char* microamp_at(microamp_state_t* microamp_state,int index);

/** *************************************************************************   
 * \brief Open an endpoint by @name
 * \param microamp_state A pointer to the microamp state.
 * \param name The ascii name of the endpoint.
 * \return A handle to the endpoint, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_open(microamp_state_t* microamp_state,const char* name);

/** *************************************************************************   
 * \brief Close an endpoint
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_close(microamp_state_t* microamp_state,int nhandle);

/** *************************************************************************   
 * \brief Blocking, lock the buffer semaphore.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_lock(microamp_state_t* microamp_state,int nhandle);

/** *************************************************************************   
 * \brief unlock the buffer semaphore.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_unlock(microamp_state_t* microamp_state,int nhandle);

/** *************************************************************************   
 * \brief Non-blocking, lock the buffer semaphore.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \return 0 upon success, or < 0 indicates and error condition.
****************************************************************************/
extern int microamp_trylock(microamp_state_t* microamp_state,int nhandle);

/** *************************************************************************   
 * \brief Read bytes from the endpoint associated with \ref nhandle.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \param buffer A pointer to the read storage buffer area.
 * \param size The maximum size to read.
 * \return the number of bytes read, or < 0 on error.
****************************************************************************/
extern int microamp_read(microamp_state_t* microamp_state,int nhandle,void* buf,size_t size);

/** *************************************************************************   
 * \brief Write bytes to the endpoint associated with \ref nhandle.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \param buffer A pointer to the write storage buffer area.
 * \param size The maximum size to write.
 * \return the number of bytes written, or < 0 on error.
****************************************************************************/
extern int microamp_write(microamp_state_t* microamp_state,int nhandle,const void* buf,size_t size);

/** *************************************************************************   
 * \brief Number of bytes available bytes to the endpoint associated 
 *        with \ref nhandle.
 * \param microamp_state A pointer to the microamp state.
 * \param nhandle The handle of the endpoint.
 * \return the number of bytes available, or < 0 on error.
****************************************************************************/
extern int microamp_avail(microamp_state_t* microamp_state,int nhandle);

#ifdef __cplusplus
}
#endif

#endif
