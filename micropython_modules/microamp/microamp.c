// Include MicroPython API.
#include "py/runtime.h"
#include <brisc_thread.h>

// This is the function which will be called from Python as microamp.open(a).
STATIC mp_obj_t microamp_get_ticks() {

    // Get a new channel handle.
    return mp_obj_new_int((int)b_thread_systick());
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_0(microamp_get_ticks_obj, microamp_get_ticks);


// This is the function which will be called from Python as microamp.add_ints(a, b).
STATIC mp_obj_t microamp_add_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
    // Extract the ints from the micropython input objects.
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    // Calculate the addition and convert to MicroPython object.
    return mp_obj_new_int(a + b);
}
// Define a Python reference to the function above.
STATIC MP_DEFINE_CONST_FUN_OBJ_2(microamp_add_ints_obj, microamp_add_ints);


// Define all properties of the module.
// Table entries are key/value pairs of the attribute name (a string)
// and the MicroPython object reference.
// All identifiers and strings are written as MP_QSTR_xxx and will be
// optimized to word-sized integers by the build system (interned strings).
STATIC const mp_rom_map_elem_t microamp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_microamp) },
    { MP_ROM_QSTR(MP_QSTR_get_ticks), MP_ROM_PTR(&microamp_get_ticks_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&microamp_add_ints_obj) },
};
STATIC MP_DEFINE_CONST_DICT(microamp_module_globals, microamp_module_globals_table);

// Define module object.
const mp_obj_module_t microamp_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&microamp_module_globals,
};

// Register the module to make it available in Python.
// Note: the "1" in the third argument means this module is always enabled.
// This "1" can be optionally replaced with a macro like MODULE_microamp_ENABLED
// which can then be used to conditionally enable this module.
MP_REGISTER_MODULE(MP_QSTR_microamp, microamp_cmodule, 1);
