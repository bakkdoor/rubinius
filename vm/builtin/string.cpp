#include "builtin/string.hpp"
#include "builtin/bytearray.hpp"
#include "builtin/class.hpp"
#include "builtin/exception.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/symbol.hpp"
#include "builtin/float.hpp"
#include "builtin/integer.hpp"

#include "vm.hpp"
#include "vm/object_utils.hpp"
#include "primitives.hpp"
#include "objectmemory.hpp"

#include <unistd.h>
#include <iostream>

#define HashPrime 16777619
#define MASK_28 (((unsigned int)1<<28)-1)

namespace rubinius {

  void String::init(STATE) {
    GO(string).set(state->new_class("String", G(object), String::fields));
    G(string)->set_object_type(state, StringType);
  }

  /* String::size returns the number of actual bytes in the string NOT
   * including the trailing NULL byte.
   *
   * String::create(state, "foo")->size() is 3.
   */
  size_t String::size() {
    return num_bytes_->to_native();
  }

  /* Creates a String instance with +num_bytes+ == +size+ and
   * having a ByteArray with at least (size + 1) bytes.
   */
  String* String::create(STATE, Fixnum* size) {
    String *so;

    so = (String*)state->om->new_object(G(string), String::fields);

    so->num_bytes(state, size);
    so->characters(state, size);
    so->encoding(state, Qnil);
    so->hash_value(state, (Integer*)Qnil);

    size_t bytes = size->to_native() + 1;
    ByteArray* ba = ByteArray::create(state, bytes);
    ba->bytes[bytes-1] = 0;

    so->data(state, ba);

    return so;
  }

  /* +bytes+ should NOT attempt to take the trailing null into account
   * +bytes+ is the number of 'real' characters in the string
   */
  String* String::create(STATE, const char* str, size_t bytes) {
    String *so;

    if(bytes == 0 && str) bytes = strlen(str);

    so = (String*)state->om->new_object(G(string), String::fields);

    so->num_bytes(state, Fixnum::from(bytes));
    so->characters(state, so->num_bytes());
    so->encoding(state, Qnil);
    so->hash_value(state, (Integer*)Qnil);

    ByteArray* ba = ByteArray::create(state, bytes + 1);
    if(str) std::memcpy(ba->bytes, str, bytes);
    ba->bytes[bytes] = 0;

    so->data(state, ba);

    return so;
  }

  String* String::from_bytearray(STATE, ByteArray* ba, Integer* start, Integer* count) {
    String* s = (String*)state->om->new_object(G(string), String::fields);

    s->num_bytes(state, count);
    s->characters(state, count);
    s->encoding(state, Qnil);
    s->hash_value(state, (Integer*)Qnil);

    // fetch_bytes NULL terminates
    s->data(state, ba->fetch_bytes(state, start, count));

    return s;
  }

  hashval String::hash_string(STATE) {
    unsigned char *bp;

    if(!hash_value_->nil_p()) {
      return (hashval)as<Integer>(hash_value_)->to_native();
    }
    bp = (unsigned char*)(data_->bytes);
    size_t sz = size();

    hashval h = hash_str(bp, sz);
    hash_value(state, Integer::from(state, h));

    return h;
  }

  hashval String::hash_str(const unsigned char *bp, unsigned int sz) {
    unsigned char *be;
    unsigned int hv;

    be = (unsigned char*)bp + sz;

    hv = 0;

    while(bp < be) {
      hv *= HashPrime;
      hv ^= *bp++;
    }
    hv = (hv>>28) ^ (hv & MASK_28);

    return hv;
  }

  Symbol* String::to_sym(STATE) {
    return state->symbol(this);
  }

  char* String::byte_address() {
    return (char*)data_->bytes;
  }

  char* String::c_str() {
    sassert(size() < data_->size());

    char* c_string = (char*)data_->bytes;
    if(c_string[size()] != 0) {
      c_string[size()] = 0;
    }

    return c_string;
  }

  bool String::string_equal_p(STATE, Object* a, Object* b) {
    String* self = as<String>(a);
    String* other = as<String>(b);

    if(self->num_bytes() != other->num_bytes()) return false;
    if(strncmp(self->byte_address(), other->byte_address(), self->num_bytes()->to_native())) {
      return false;
    }

    return true;
  }

  Object* String::equal(STATE, String* other) {
    bool ret = String::string_equal_p(state, this, other);
    return ret ? Qtrue : Qfalse;
  }

  String* String::string_dup(STATE) {
    String* ns;

    ns = as<String>(dup(state));
    ns->shared(state, Qtrue);
    shared(state, Qtrue);

    state->om->set_class(ns, class_object(state));

    return ns;
  }

  void String::unshare(STATE) {
    data(state, as<ByteArray>(data_->dup(state)));
    shared(state, Qfalse);
  }

  /* This is not the same as String::append below that
   * takes a const char* other. In C/C++, strings are
   * terminated with NULL. That's not true in Ruby, where
   * strings may contain embedded NULL characters. We must
   * use the size of the string in Ruby to know the limits.
   */
  String* String::append(STATE, String* other) {
    // No need to call unshare and duplicate a ByteArray
    // just to throw it away.
    if(shared_) shared(state, Qfalse);

    size_t new_size = size() + other->size();

    ByteArray *ba = ByteArray::create(state, new_size + 1);
    std::memcpy(ba->bytes, data_->bytes, size());
    std::memcpy(ba->bytes + size(), other->data()->bytes, other->size());

    ba->bytes[new_size] = 0;

    num_bytes(state, Integer::from(state, new_size));
    data(state, ba);
    hash_value(state, (Integer*)Qnil);

    return this;
  }

  /* Since we're passed a C/C++ string, which is NULL
   * terminated, we use strlen to determine the size of
   * +other+. This is NOT the same as String::append
   * above that takes a Ruby String*.
   */
  String* String::append(STATE, const char* other) {
    if(shared_) unshare(state);

    size_t len = strlen(other);
    size_t new_size = size() + len;

    // Leave one extra byte of room for the trailing null
    ByteArray *d2 = ByteArray::create(state, new_size + 1);
    std::memcpy(d2->bytes, data_->bytes, size());

    // Append on top of the null byte at the end of s1, not after it
    std::memcpy(d2->bytes + size(), other, len);

    // The 0-based index of the last character is new_size - 1
    d2->bytes[new_size] = 0;

    num_bytes(state, Integer::from(state, new_size));
    data(state, d2);
    hash_value(state, (Integer*)Qnil);

    return this;
  }

  String* String::add(STATE, String* other) {
    return string_dup(state)->append(state, other);
  }

  String* String::add(STATE, const char* other) {
    return string_dup(state)->append(state, other);
  }

  Float* String::to_f(STATE) {
    return Float::create(state, this->to_double(state));
  }

  double String::to_double(STATE) {
    double value;
    char *ba = data_->to_chars(state);
    char *p, *n, *rest;
    int e_seen = 0;

    p = ba;
    while (ISSPACE(*p)) p++;
    n = p;

    while (*p) {
      if (*p == '_') {
        p++;
      } else {
        if(*p == 'e' || *p == 'E') {
          if(e_seen) {
            *n = 0;
            break;
          }
          e_seen = 1;
        } else if(!(ISDIGIT(*p) || *p == '.' || *p == '-' || *p == '+')) {
          *n = 0;
          break;
        }

        *n++ = *p++;
      }
    }
    *n = 0;

    /* Some implementations of strtod() don't guarantee to
     * set errno, so we need to reset it ourselves.
     */
    errno = 0;

    value = strtod(ba, &rest);
    if (errno == ERANGE) {
      printf("Float %s out of range\n", ba);
    }

    free(ba);


    return value;
  }

  // Character-wise logical AND of two strings. Modifies the receiver.
  String* String::apply_and(STATE, String* other) {
    native_int count, i;
    ByteArray* s = data_;
    ByteArray* o = other->data();

    if(num_bytes_ > other->num_bytes()) {
      count = other->num_bytes()->to_native();
    } else {
      count = num_bytes_->to_native();
    }

    // Use && not & to keep 1's in the table.
    for(i = 0; i < count; i++) {
      s->bytes[i] = s->bytes[i] && o->bytes[i];
    }

    return this;
  }

  struct tr_data {
    char tr[256];
    native_int set[256];
    native_int steps;
    native_int last;
    native_int limit;

    bool assign(native_int chr) {
      int j, i = this->set[chr];

      if(this->limit >= 0 && this->steps >= this->limit) return true;

      if(i < 0) {
        this->tr[this->last] = chr;
      } else {
        this->last--;
        for(j = i + 1; j <= this->last; j++) {
          this->set[(native_int)this->tr[j]]--;
          this->tr[j-1] = this->tr[j];
        }
        this->tr[this->last] = chr;
      }
      this->set[chr] = this->last++;
      this->steps++;

      return false;
    }
  };

  Fixnum* String::tr_expand(STATE, Object* limit) {
    struct tr_data data;
    native_int seq;
    native_int max;
    native_int chr;

    data.last = 0;
    data.steps = 0;

    if(Integer* lim = try_as<Integer>(limit)) {
      data.limit = lim->to_native();
    } else {
      data.limit = -1;
    }

    unsigned char* str = data_->bytes;
    native_int bytes = (native_int)this->size();
    native_int start = bytes > 1 && str[0] == '^' ? 1 : 0;
    std::memset(data.set, -1, sizeof(native_int) * 256);

    for(native_int i = start; i < bytes;) {
      chr = str[i];
      seq = ++i < bytes ? str[i] : -1;

      if(seq == '-') {
        max = ++i < bytes ? str[i] : -1;
        if(max >= 0) {
          while(chr <= max) {
            if(data.assign(chr)) return tr_replace(state, &data);
            chr++;
          }
          i++;
        } else {
          if(data.assign(chr)) return tr_replace(state, &data);
          if(data.assign(seq)) return tr_replace(state, &data);
        }
      } else if(chr == '\\' && seq >= 0) {
        continue;
      } else {
        if(data.assign(chr)) return tr_replace(state, &data);
      }
    }

    return tr_replace(state, &data);
  }

  Fixnum* String::tr_replace(STATE, struct tr_data* data) {
    if(data->last > (native_int)this->size() || shared_->true_p()) {
      ByteArray* ba = ByteArray::create(state, this->size() + 1);

      this->data(state, ba);
      this->shared(state, Qfalse);
    }

    std::memcpy(data_->bytes, data->tr, data->last);
    data_->bytes[data->last] = 0;

    num_bytes(state, Fixnum::from(data->last));
    characters(state, num_bytes_);

    return Fixnum::from(data->steps);
  }

  String* String::copy_from(STATE, String* other, Fixnum* start, Fixnum* size, Fixnum* dest) {
    native_int src = start->to_native();
    native_int dst = dest->to_native();
    native_int cnt = size->to_native();

    native_int osz = other->size();
    if(src >= osz) return this;
    if(src < 0) src = 0;
    if(cnt > osz - src) cnt = osz - src;

    native_int sz = this->size();
    if(dst >= sz) return this;
    if(dst < 0) dst = 0;
    if(cnt > sz - dst) cnt = sz - dst;

    std::memcpy(data_->bytes + dst, other->data()->bytes + src, cnt);

    return this;
  }

  Fixnum* String::compare_substring(STATE, String* other, Fixnum* start, Fixnum* size) {
    native_int src = start->to_native();
    native_int cnt = size->to_native();
    native_int sz = (native_int)this->size();
    native_int osz = (native_int)other->size();

    if(src < 0) src = osz + src;
    if(src >= osz) {
      Exception::object_bounds_exceeded_error(state, "start exceeds size of other");
    } else if(src < 0) {
      Exception::object_bounds_exceeded_error(state, "start less than zero");
    }
    if(src + cnt > osz) cnt = osz - src;

    if(cnt > sz) cnt = sz;

    native_int cmp = std::memcmp(data_->bytes, other->data()->bytes + src, cnt);

    if(cmp < 0) {
      return Fixnum::from(-1);
    } else if(cmp > 0) {
      return Fixnum::from(1);
    } else {
      return Fixnum::from(0);
    }
  }

  String* String::pattern(STATE, Object* self, Fixnum* size, Object* pattern) {
    String* s = String::create(state, size);
    s->klass(state, (Class*)self);
    s->IsTainted = self->IsTainted;

    native_int cnt = size->to_native();

    if(Fixnum* chr = try_as<Fixnum>(pattern)) {
      std::memset(s->data()->bytes, (int)chr->to_native(), cnt);
    } else if(String* pat = try_as<String>(pattern)) {
      s->IsTainted |= pat->IsTainted;

      native_int psz = pat->size();
      if(psz == 1) {
        std::memset(s->data()->bytes, pat->data()->bytes[0], cnt);
      } else if(psz > 1) {
        native_int i, j, n;

        native_int sz = cnt / psz;
        for(n = i = 0; i < sz; i++) {
          for(j = 0; j < psz; j++, n++) {
            s->data()->bytes[n] = pat->data()->bytes[j];
          }
        }
        for(i = n, j = 0; i < cnt; i++, j++) {
          s->data()->bytes[i] = pat->data()->bytes[j];
        }
      }
    } else {
      Exception::argument_error(state, "pattern must be Fixnum of String");
    }

    return s;
  }

  String* String::crypt(STATE, String* salt) {
    const char* s = ::crypt(this->c_str(), salt->c_str());
    return String::create(state, s);
  }

  Integer* String::to_i(STATE, Fixnum* fix_base, Object* strict) {
    char* str = c_str();
    int base = fix_base->to_native();
    bool negative = false;
    Integer* value = Fixnum::from(0);

    if(base < 0 || base > 36) return (Integer*)Qnil;

    // Move past any spaces
    while(*str == ' ') str++;

    if(*str == '-') {
      str++;
      negative = true;
    }

    char chr;
    int detected_base = 0;
    char* str_start = str;

    // Try and detect a base prefix on the front. We have to do this
    // even though we might have been told the base, because we have
    // to know if we should discard the bytes that make up the prefix
    // if it's redundent with passed in base.
    //
    // For example, if base == 16 and str == "0xa", we return
    // to return 10. But if base == 10 and str == "0xa", we fail
    // because we rewind and try to process 0x as part of the
    // base 10 string.
    //
    if(*str == '0') {
      str++;
      switch(chr = *str++) {
      case 'b':
        detected_base = 2;
        break;
      case 'o':
        detected_base = 8;
        break;
      case 'd': // does this really exist?
        detected_base = 10;
        break;
      case 'x':
        detected_base = 16;
        break;
      default:
        // Ok, it's the weird octal 0 start thing. If the next
        // char is a valid octal character, then we switch to base
        // 8. Otherwise it's an invalid base prefix and we bail.
        if(chr >= '0' && chr <= '7') {
          detected_base = 8;
          str--;
        // If there is more and we're strict, bail.
        } else if(chr && strict == Qtrue) {
          return (Integer*)Qnil;
        } else {
          return value;
        }

        break;
      }

      // If 0 was passed in as the base, we use the detected base.
      if(base == 0) {
        base = detected_base;

      // If the passed in base and the detected base contradict
      // eachother, then rewind and process the whole string as
      // digits of the passed in base.
      } else if(base != detected_base) {
        // rewind the stream, and try and consume the prefix as
        // digits in the number.
        str = str_start;
      }
    }

    // Default to 10 if there is no input and no detected base.
    if(detected_base == 0 && base == 0) {
      base = 10;
    }

    // A stupid boundry case. A _ is ok at the beginning of the beginning
    // if we're not strict.
    if(*str == '_') {
      if(strict == Qtrue) {
        return (Integer*)Qnil;
      } else {
        str++;
      }
    }

    bool underscore = false;

    while(*str) {
      chr = *str++;

      // If we see space characters
      if(chr == ' ' || chr == '\t' || chr == '\n') {

        // Eat them all
        while(chr == ' ' || chr == '\t' || chr == '\n') {
          chr = *str++;
        }

        // If there is more stuff after the spaces, get out of dodge.
        if(chr) {
          if(strict == Qtrue) {
            return (Integer*)Qnil;
          } else {
            goto return_value;
          }
        }

        break;
      }

      // If it's an underscore, remember that. An underscore is valid iff
      // it followed by a valid character for this base.
      if(chr == '_') {
        underscore = true;
        continue;
      } else {
        underscore = false;
      }

      // We use A-Z (and a-z) here so we support up to base 36.
      if(chr >= '0' && chr <= '9') {
        chr -= '0';
      } else if(chr >= 'A' && chr <= 'Z') {
        chr -= ('A' - 10);
      } else if(chr >= 'a' && chr <= 'z') {
        chr -= ('a' - 10);
      }

      // Bail if the current chr is greater or equal to the base,
      // mean it's invalid.
      if(chr >= base) {
        if(strict == Qtrue) {
          return (Integer*)Qnil;
        } else {
          goto return_value;
        }
      }

      if(value != Fixnum::from(0)) {
        if(Fixnum *fix = try_as<Fixnum>(value)) {
          value = fix->mul(state, Fixnum::from(base));
        } else {
          value = as<Bignum>(value)->mul(state, Fixnum::from(base));
        }
      }

      if(Fixnum *fix = try_as<Fixnum>(value)) {
        value = fix->add(state, Fixnum::from(chr));
      } else {
        value = as<Bignum>(value)->add(state, Fixnum::from(chr));
      }
    }

    // If we last saw an underscore and we're strict, bail.
    if(underscore && strict == Qtrue) {
      return (Integer*)Qnil;
    }

return_value:
    if(negative) {
      if(Fixnum* fix = try_as<Fixnum>(value)) {
        value = fix->neg(state);
      } else {
        value = as<Bignum>(value)->neg(state);
      }
    }

    return value;
  }

  Integer* String::to_inum_prim(STATE, Fixnum* base, Object* strict) {
    Integer* val = to_i(state, base, strict);
    if(val->nil_p()) return (Integer*)Primitives::failure();

    return val;
  }

  void String::Info::show(STATE, Object* self, int level) {
    String* str = as<String>(self);
    std::cout << "\"" << str->c_str() << "\"" << std::endl;
  }

  void String::Info::show_simple(STATE, Object* self, int level) {
    show(state, self, level);
  }
}
