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
#include <microamp_c.h>
#include <brisc_thread.h>
#include <brisc_mutex.h>

#ifdef __cplusplus
extern "C"
{
#endif


/** *************************************************************************  
 * \brief maintains the state of an endpoint callback.
****************************************************************************/
typedef struct _microamp_callback_
{
    mp_obj_t                    py_fn;
    mp_obj_t                    py_arg;
    // void                        (*c_fn)(void*);
    // void*                       c_arg;
    // volatile brisc_thread_t*    writer_thread;
} py_microamp_callback_t;

#ifdef __cplusplus
}
#endif

#endif
