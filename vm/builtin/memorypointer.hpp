#ifndef RBX_BUILTIN_MEMORYPOINTER_HPP
#define RBX_BUILTIN_MEMORYPOINTER_HPP

#include "builtin/object.hpp"
#include "type_info.hpp"

namespace rubinius {
  class MemoryPointer : public Object {
    public:
    const static size_t fields = 0;
    const static object_type type = MemoryPointerType;

    void* pointer;
    bool autorelease;

    static void init(STATE);

    static MemoryPointer* create(STATE, void* ptr);

    // Ruby.primitive :memorypointer_address
    Integer* get_address(STATE);

    // Ruby.primitive :memorypointer_set_address
    Integer* set_address(STATE, Integer* ptr);

    // Ruby.primitive :memorypointer_set_autorelease
    Object* set_autorelease(STATE, Object* val);

    // Ruby.primitive :memorypointer_add
    MemoryPointer* add(STATE, Integer* amount);

    // Ruby.primitive :memorypointer_write_int
    Integer* write_int(STATE, Integer* val);

    // Ruby.primitive :memorypointer_read_int
    Integer* read_int(STATE);

    // Ruby.primitive :memorypointer_write_long
    Integer* write_long(STATE, Integer* val);

    // Ruby.primitive :memorypointer_read_long
    Integer* read_long(STATE);

    // Ruby.primitive :memorypointer_write_float
    Float* write_float(STATE, Float* val);

    // Ruby.primitive :memorypointer_read_float
    Float* read_float(STATE);

    // Ruby.primitive :memorypointer_read_pointer
    MemoryPointer* read_pointer(STATE);

    // Ruby.primitive :memorypointer_read_string
    String* read_string(STATE, Fixnum* len);

    // Ruby.primitive :memorypointer_read_string_to_null
    String* read_string_to_null(STATE);

    // Ruby.primitive :memorypointer_write_string
    MemoryPointer* write_string(STATE, String* str, Fixnum* len);

    Object* get_field(STATE, int offset, int type);

    // Ruby.primitive :memorypointer_get_at_offset
    Object* get_at_offset(STATE, Fixnum* offset, Fixnum* type);

    void   set_field(STATE, int offset, int type, Object* val);

    // Ruby.primitive :memorypointer_set_at_offset
    Object* set_at_offset(STATE, Fixnum* offset, Fixnum* type, Object* val);


    /** TODO: Need cleanup here? */
    class Info : public TypeInfo {
    public:
      Info(object_type type, bool cleanup = false) : TypeInfo(type, cleanup) { }
      virtual void mark(Object* t, ObjectMark& mark);
    };
  };
}

#endif
