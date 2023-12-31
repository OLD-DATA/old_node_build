// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_GLOBALS_H_
#define V8_GLOBALS_H_

// -----------------------------------------------------------------------------
// Types
// Visual Studio C++ is missing the stdint.h header file. Instead we define
// standard integer types for Windows here.

#ifdef _MSC_VER
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;  // NOLINT
typedef unsigned short uint16_t;  // NOLINT
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else  // _MSC_VER
#include <stdint.h>  // for intptr_t
#endif  // _MSC_VER


namespace v8 { namespace internal {

// Support for alternative bool type. This is only enabled if the code is
// compiled with USE_MYBOOL defined. This catches some nasty type bugs.
// For instance, 'bool b = "false";' results in b == true! This is a hidden
// source of bugs.
// However, redefining the bool type does have some negative impact on some
// platforms. It gives rise to compiler warnings (i.e. with
// MSVC) in the API header files when mixing code that uses the standard
// bool with code that uses the redefined version.
// This does not actually belong in the platform code, but needs to be
// defined here because the platform code uses bool, and platform.h is
// include very early in the main include file.

#ifdef USE_MYBOOL
typedef unsigned int __my_bool__;
#define bool __my_bool__  // use 'indirection' to avoid name clashes
#endif

typedef uint8_t byte;
typedef byte* Address;

// Code-point values in Unicode 4.0 are 21 bits wide.
typedef uint16_t uc16;
typedef signed int uc32;

#ifndef ARM
#define CAN_READ_UNALIGNED 1
#endif

// -----------------------------------------------------------------------------
// Constants

#ifdef DEBUG
const bool kDebug = true;
#else
const bool kDebug = false;
#endif  // DEBUG

const int KB = 1024;
const int MB = KB * KB;
const int GB = KB * KB * KB;
const int kMaxInt = 0x7FFFFFFF;
const int kMinInt = -kMaxInt - 1;

const int kCharSize     = sizeof(char);    // NOLINT
const int kShortSize    = sizeof(short);   // NOLINT
const int kIntSize      = sizeof(int);     // NOLINT
const int kDoubleSize   = sizeof(double);  // NOLINT
const int kPointerSize  = sizeof(void*);   // NOLINT

const int kPointerSizeLog2 = 2;

const int kObjectAlignmentBits = 2;
const int kObjectAlignmentMask = (1 << kObjectAlignmentBits) - 1;
const int kObjectAlignment = 1 << kObjectAlignmentBits;


// Tag information for HeapObject.
const int kHeapObjectTag = 1;
const int kHeapObjectTagSize = 2;
const int kHeapObjectTagMask = (1 << kHeapObjectTagSize) - 1;


// Tag information for Smi.
const int kSmiTag = 0;
const int kSmiTagSize = 1;
const int kSmiTagMask = (1 << kSmiTagSize) - 1;


// Tag information for Failure.
const int kFailureTag = 3;
const int kFailureTagSize = 2;
const int kFailureTagMask = (1 << kFailureTagSize) - 1;


const int kBitsPerByte = 8;
const int kBitsPerByteLog2 = 3;
const int kBitsPerPointer = kPointerSize * kBitsPerByte;
const int kBitsPerInt = kIntSize * kBitsPerByte;


// Zap-value: The value used for zapping dead objects. Should be a recognizable
// illegal heap object pointer.
const Address kZapValue = reinterpret_cast<Address>(0xdeadbeed);
const Address kHandleZapValue = reinterpret_cast<Address>(0xbaddead);
const Address kFromSpaceZapValue = reinterpret_cast<Address>(0xbeefdad);

// -----------------------------------------------------------------------------
// Forward declarations for frequently used classes
// (sorted alphabetically)

class AccessorInfo;
class Allocation;
class Arguments;
class Assembler;
class BreakableStatement;
class Code;
class CodeGenerator;
class CodeRegion;
class CodeStub;
class Context;
class Debug;
class Debugger;
class DebugInfo;
class Descriptor;
class DescriptorArray;
class Expression;
class ExternalReference;
class FixedArray;
class FunctionEntry;
class FunctionLiteral;
class FunctionTemplateInfo;
class Dictionary;
class FreeStoreAllocationPolicy;
template <typename T> class Handle;
class Heap;
class HeapObject;
class IC;
class InterceptorInfo;
class IterationStatement;
class JSArray;
class JSFunction;
class JSObject;
class LargeObjectSpace;
template <typename T, class P = FreeStoreAllocationPolicy> class List;
class LookupResult;
class MacroAssembler;
class Map;
class MapSpace;
class MarkCompactCollector;
class NewSpace;
class NodeVisitor;
class Object;
class OldSpace;
class Property;
class Proxy;
class RegExpNode;
struct RegExpCompileData;
class RegExpTree;
class RegExpCompiler;
class RegExpVisitor;
class Scope;
template<class Allocator = FreeStoreAllocationPolicy> class ScopeInfo;
class Script;
class Slot;
class Smi;
class Statement;
class String;
class Struct;
class SwitchStatement;
class AstVisitor;
class Variable;
class VariableProxy;
class RelocInfo;
class Deserializer;
class MessageLocation;
class ObjectGroup;
class TickSample;
class VirtualMemory;
class Mutex;

typedef bool (*WeakSlotCallback)(Object** pointer);

// -----------------------------------------------------------------------------
// Miscellaneous

// NOTE: SpaceIterator depends on AllocationSpace enumeration values being
// consecutive.
enum AllocationSpace {
  NEW_SPACE,          // Semispaces collected with copying collector.
  OLD_POINTER_SPACE,  // Must be first of the paged spaces - see PagedSpaces.
  OLD_DATA_SPACE,     // May not have pointers to new space.
  CODE_SPACE,         // Also one of the old spaces.  Marked executable.
  MAP_SPACE,          // Only map objects.
  LO_SPACE,           // Large objects.
  FIRST_SPACE = NEW_SPACE,
  LAST_SPACE = LO_SPACE  // <= 5 (see kSpaceBits and kLOSpacePointer)
};
const int kSpaceTagSize = 3;
const int kSpaceTagMask = (1 << kSpaceTagSize) - 1;


// A flag that indicates whether objects should be pretenured when
// allocated (allocated directly into the old generation) or not
// (allocated in the young generation if the object size and type
// allows).
enum PretenureFlag { NOT_TENURED, TENURED };

enum GarbageCollector { SCAVENGER, MARK_COMPACTOR };

enum Executability { NOT_EXECUTABLE, EXECUTABLE };


// A CodeDesc describes a buffer holding instructions and relocation
// information. The instructions start at the beginning of the buffer
// and grow forward, the relocation information starts at the end of
// the buffer and grows backward.
//
//  |<--------------- buffer_size ---------------->|
//  |<-- instr_size -->|        |<-- reloc_size -->|
//  +==================+========+==================+
//  |   instructions   |  free  |    reloc info    |
//  +==================+========+==================+
//  ^
//  |
//  buffer

struct CodeDesc {
  byte* buffer;
  int buffer_size;
  int instr_size;
  int reloc_size;
  Assembler* origin;
};


// Callback function on object slots, used for iterating heap object slots in
// HeapObjects, global pointers to heap objects, etc. The callback allows the
// callback function to change the value of the slot.
typedef void (*ObjectSlotCallback)(HeapObject** pointer);


// Callback function used for iterating objects in heap spaces,
// for example, scanning heap objects.
typedef int (*HeapObjectCallback)(HeapObject* obj);


// Callback function used for checking constraints when copying/relocating
// objects. Returns true if an object can be copied/relocated from its
// old_addr to a new_addr.
typedef bool (*ConstraintCallback)(Address new_addr, Address old_addr);


// Callback function on inline caches, used for iterating over inline caches
// in compiled code.
typedef void (*InlineCacheCallback)(Code* code, Address ic);


// State for inline cache call sites. Aliased as IC::State.
enum InlineCacheState {
  // Has never been executed.
  UNINITIALIZED,
  // Has never been executed, but is in a loop.
  UNINITIALIZED_IN_LOOP,
  // Has been executed but monomorhic state has been delayed.
  PREMONOMORPHIC,
  // Has been executed and only one receiver type has been seen.
  MONOMORPHIC,
  // Like MONOMORPHIC but check failed due to prototype.
  MONOMORPHIC_PROTOTYPE_FAILURE,
  // Multiple receiver types have been seen.
  MEGAMORPHIC,
  // Special states for debug break or step in prepare stubs.
  DEBUG_BREAK,
  DEBUG_PREPARE_STEP_IN
};


// Type of properties.
// Order of properties is significant.
// Must fit in the BitField PropertyDetails::TypeField.
// A copy of this is in mirror-delay.js.
enum PropertyType {
  NORMAL              = 0,  // only in slow mode
  FIELD               = 1,  // only in fast mode
  CONSTANT_FUNCTION   = 2,  // only in fast mode
  CALLBACKS           = 3,
  INTERCEPTOR         = 4,  // only in lookup results, not in descriptors.
  MAP_TRANSITION      = 5,  // only in fast mode
  CONSTANT_TRANSITION = 6,  // only in fast mode
  NULL_DESCRIPTOR     = 7,  // only in fast mode
  // All properties before MAP_TRANSITION are real.
  FIRST_PHANTOM_PROPERTY_TYPE = MAP_TRANSITION
};


// Whether to remove map transitions and constant transitions from a
// DescriptorArray.
enum TransitionFlag {
  REMOVE_TRANSITIONS,
  KEEP_TRANSITIONS
};


// Union used for fast testing of specific double values.
union DoubleRepresentation {
  double  value;
  int64_t bits;
  DoubleRepresentation(double x) { value = x; }
};


// AccessorCallback
struct AccessorDescriptor {
  Object* (*getter)(Object* object, void* data);
  Object* (*setter)(JSObject* object, Object* value, void* data);
  void* data;
};


// Logging and profiling.
// A StateTag represents a possible state of the VM.  When compiled with
// ENABLE_LOGGING_AND_PROFILING, the logger maintains a stack of these.
// Creating a VMState object enters a state by pushing on the stack, and
// destroying a VMState object leaves a state by popping the current state
// from the stack.

#define STATE_TAG_LIST(V) \
  V(JS)                   \
  V(GC)                   \
  V(COMPILER)             \
  V(OTHER)                \
  V(EXTERNAL)

enum StateTag {
#define DEF_STATE_TAG(name) name,
  STATE_TAG_LIST(DEF_STATE_TAG)
#undef DEF_STATE_TAG
  // Pseudo-types.
  state_tag_count
};


// -----------------------------------------------------------------------------
// Macros

// Testers for test.

#define HAS_SMI_TAG(value) \
  ((reinterpret_cast<int>(value) & kSmiTagMask) == kSmiTag)

#define HAS_FAILURE_TAG(value) \
  ((reinterpret_cast<int>(value) & kFailureTagMask) == kFailureTag)

#define HAS_HEAP_OBJECT_TAG(value) \
  ((reinterpret_cast<int>(value) & kHeapObjectTagMask) == kHeapObjectTag)

// OBJECT_SIZE_ALIGN returns the value aligned HeapObject size
#define OBJECT_SIZE_ALIGN(value)                                \
  ((value + kObjectAlignmentMask) & ~kObjectAlignmentMask)

// The expression OFFSET_OF(type, field) computes the byte-offset
// of the specified field relative to the containing type. This
// corresponds to 'offsetof' (in stddef.h), except that it doesn't
// use 0 or NULL, which causes a problem with the compiler warnings
// we have enabled (which is also why 'offsetof' doesn't seem to work).
// Here we simply use the non-zero value 4, which seems to work.
#define OFFSET_OF(type, field)                                          \
  (reinterpret_cast<intptr_t>(&(reinterpret_cast<type*>(4)->field)) - 4)


// The expression ARRAY_SIZE(a) is a compile-time constant of type
// size_t which represents the number of elements of the given
// array. You should only use ARRAY_SIZE on statically allocated
// arrays.
#define ARRAY_SIZE(a)                                   \
  ((sizeof(a) / sizeof(*(a))) /                         \
  static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))


// The USE(x) template is used to silence C++ compiler warnings
// issued for (yet) unused variables (typically parameters).
template <typename T>
static inline void USE(T) { }


// FUNCTION_ADDR(f) gets the address of a C function f.
#define FUNCTION_ADDR(f)                                        \
  (reinterpret_cast<v8::internal::Address>(reinterpret_cast<intptr_t>(f)))


// FUNCTION_CAST<F>(addr) casts an address into a function
// of type F. Used to invoke generated code from within C.
template <typename F>
F FUNCTION_CAST(Address addr) {
  return reinterpret_cast<F>(reinterpret_cast<intptr_t>(addr));
}


// A macro to disallow the evil copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName)      \
  TypeName(const TypeName&);                    \
  void operator=(const TypeName&)


// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName();                                    \
  DISALLOW_COPY_AND_ASSIGN(TypeName)


// Support for tracking C++ memory allocation.  Insert TRACK_MEMORY("Fisk")
// inside a C++ class and new and delete will be overloaded so logging is
// performed.
// This file (globals.h) is included before log.h, so we use direct calls to
// the Logger rather than the LOG macro.
#ifdef DEBUG
#define TRACK_MEMORY(name) \
  void* operator new(size_t size) { \
    void* result = ::operator new(size); \
    Logger::NewEvent(name, result, size); \
    return result; \
  } \
  void operator delete(void* object) { \
    Logger::DeleteEvent(name, object); \
    ::operator delete(object); \
  }
#else
#define TRACK_MEMORY(name)
#endif

// define used for helping GCC to make better inlining. Don't bother for debug
// builds. On GCC 3.4.5 using __attribute__((always_inline)) causes compilation
// errors in debug build.
#if defined(__GNUC__) && !defined(DEBUG)
#if (__GNUC__ >= 4)
#define INLINE(header) inline header  __attribute__((always_inline))
#else
#define INLINE(header) inline __attribute__((always_inline)) header
#endif
#else
#define INLINE(header) inline header
#endif

// The type-based aliasing rule allows the compiler to assume that pointers of
// different types (for some definition of different) never alias each other.
// Thus the following code does not work:
//
// float f = foo();
// int fbits = *(int*)(&f);
//
// The compiler 'knows' that the int pointer can't refer to f since the types
// don't match, so the compiler may cache f in a register, leaving random data
// in fbits.  Using C++ style casts makes no difference, however a pointer to
// char data is assumed to alias any other pointer.  This is the 'memcpy
// exception'.
//
// Bit_cast uses the memcpy exception to move the bits from a variable of one
// type o a variable of another type.  Of course the end result is likely to
// be implementation dependent.  Most compilers (gcc-4.2 and MSVC 2005)
// will completely optimize bit_cast away.
//
// There is an additional use for bit_cast.
// Recent gccs will warn when they see casts that may result in breakage due to
// the type-based aliasing rule.  If you have checked that there is no breakage
// you can use bit_cast to cast one pointer type to another.  This confuses gcc
// enough that it can no longer see that you have cast one pointer type to
// another thus avoiding the warning.
template <class Dest, class Source>
inline Dest bit_cast(const Source& source) {
  // Compile time assertion: sizeof(Dest) == sizeof(Source)
  // A compile error here means your Dest and Source have different sizes.
  typedef char VerifySizesAreEqual[sizeof(Dest) == sizeof(Source) ? 1 : -1];

  Dest dest;
  memcpy(&dest, &source, sizeof(dest));
  return dest;
}


} }  // namespace v8::internal

#endif  // V8_GLOBALS_H_
