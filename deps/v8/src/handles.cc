// Copyright 2009 the V8 project authors. All rights reserved.
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

#include "v8.h"

#include "accessors.h"
#include "api.h"
#include "bootstrapper.h"
#include "compiler.h"
#include "debug.h"
#include "execution.h"
#include "global-handles.h"
#include "natives.h"
#include "runtime.h"

namespace v8 { namespace internal {


v8::ImplementationUtilities::HandleScopeData HandleScope::current_ =
    { -1, NULL, NULL };


int HandleScope::NumberOfHandles() {
  int n = HandleScopeImplementer::instance()->Blocks()->length();
  if (n == 0) return 0;
  return ((n - 1) * kHandleBlockSize) +
      (current_.next - HandleScopeImplementer::instance()->Blocks()->last());
}


void** HandleScope::Extend() {
  void** result = current_.next;

  ASSERT(result == current_.limit);
  // Make sure there's at least one scope on the stack and that the
  // top of the scope stack isn't a barrier.
  if (current_.extensions < 0) {
    Utils::ReportApiFailure("v8::HandleScope::CreateHandle()",
                            "Cannot create a handle without a HandleScope");
    return NULL;
  }
  HandleScopeImplementer* impl = HandleScopeImplementer::instance();
  // If there's more room in the last block, we use that. This is used
  // for fast creation of scopes after scope barriers.
  if (!impl->Blocks()->is_empty()) {
    void** limit = &impl->Blocks()->last()[kHandleBlockSize];
    if (current_.limit != limit) {
      current_.limit = limit;
    }
  }

  // If we still haven't found a slot for the handle, we extend the
  // current handle scope by allocating a new handle block.
  if (result == current_.limit) {
    // If there's a spare block, use it for growing the current scope.
    result = impl->GetSpareOrNewBlock();
    // Add the extension to the global list of blocks, but count the
    // extension as part of the current scope.
    impl->Blocks()->Add(result);
    current_.extensions++;
    current_.limit = &result[kHandleBlockSize];
  }

  return result;
}


void HandleScope::DeleteExtensions() {
  ASSERT(current_.extensions != 0);
  HandleScopeImplementer::instance()->DeleteExtensions(current_.extensions);
}


void HandleScope::ZapRange(void** start, void** end) {
  if (start == NULL) return;
  for (void** p = start; p < end; p++) {
    *p = reinterpret_cast<void*>(v8::internal::kHandleZapValue);
  }
}


Handle<FixedArray> AddKeysFromJSArray(Handle<FixedArray> content,
                                      Handle<JSArray> array) {
  CALL_HEAP_FUNCTION(content->AddKeysFromJSArray(*array), FixedArray);
}


Handle<FixedArray> UnionOfKeys(Handle<FixedArray> first,
                               Handle<FixedArray> second) {
  CALL_HEAP_FUNCTION(first->UnionOfKeys(*second), FixedArray);
}


Handle<JSGlobalProxy> ReinitializeJSGlobalProxy(
    Handle<JSFunction> constructor,
    Handle<JSGlobalProxy> global) {
  CALL_HEAP_FUNCTION(Heap::ReinitializeJSGlobalProxy(*constructor, *global),
                     JSGlobalProxy);
}


void SetExpectedNofProperties(Handle<JSFunction> func, int nof) {
  func->shared()->set_expected_nof_properties(nof);
  if (func->has_initial_map()) {
    Handle<Map> new_initial_map =
        Factory::CopyMapDropTransitions(Handle<Map>(func->initial_map()));
    new_initial_map->set_unused_property_fields(nof);
    func->set_initial_map(*new_initial_map);
  }
}


void SetPrototypeProperty(Handle<JSFunction> func, Handle<JSObject> value) {
  CALL_HEAP_FUNCTION_VOID(func->SetPrototype(*value));
}


static int ExpectedNofPropertiesFromEstimate(int estimate) {
  // TODO(1231235): We need dynamic feedback to estimate the number
  // of expected properties in an object. The static hack below
  // is barely a solution.
  if (estimate == 0) return 4;
  return estimate + 2;
}


void SetExpectedNofPropertiesFromEstimate(Handle<SharedFunctionInfo> shared,
                                          int estimate) {
  shared->set_expected_nof_properties(
      ExpectedNofPropertiesFromEstimate(estimate));
}


void SetExpectedNofPropertiesFromEstimate(Handle<JSFunction> func,
                                          int estimate) {
  SetExpectedNofProperties(
      func, ExpectedNofPropertiesFromEstimate(estimate));
}


void NormalizeProperties(Handle<JSObject> object,
                         PropertyNormalizationMode mode) {
  CALL_HEAP_FUNCTION_VOID(object->NormalizeProperties(mode));
}


void NormalizeElements(Handle<JSObject> object) {
  CALL_HEAP_FUNCTION_VOID(object->NormalizeElements());
}


void TransformToFastProperties(Handle<JSObject> object,
                               int unused_property_fields) {
  CALL_HEAP_FUNCTION_VOID(
      object->TransformToFastProperties(unused_property_fields));
}


void FlattenString(Handle<String> string) {
  CALL_HEAP_FUNCTION_VOID(string->TryFlattenIfNotFlat());
  ASSERT(string->IsFlat());
}


Handle<Object> SetPrototype(Handle<JSFunction> function,
                            Handle<Object> prototype) {
  CALL_HEAP_FUNCTION(Accessors::FunctionSetPrototype(*function,
                                                     *prototype,
                                                     NULL),
                     Object);
}


Handle<Object> SetProperty(Handle<JSObject> object,
                           Handle<String> key,
                           Handle<Object> value,
                           PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(object->SetProperty(*key, *value, attributes), Object);
}


Handle<Object> SetProperty(Handle<Object> object,
                           Handle<Object> key,
                           Handle<Object> value,
                           PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(
      Runtime::SetObjectProperty(object, key, value, attributes), Object);
}


Handle<Object> IgnoreAttributesAndSetLocalProperty(Handle<JSObject> object,
                           Handle<String> key,
                           Handle<Object> value,
                           PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(object->
      IgnoreAttributesAndSetLocalProperty(*key, *value, attributes), Object);
}

Handle<Object> SetPropertyWithInterceptor(Handle<JSObject> object,
                                          Handle<String> key,
                                          Handle<Object> value,
                                          PropertyAttributes attributes) {
  CALL_HEAP_FUNCTION(object->SetPropertyWithInterceptor(*key,
                                                        *value,
                                                        attributes),
                     Object);
}


Handle<Object> GetProperty(Handle<JSObject> obj,
                           const char* name) {
  Handle<String> str = Factory::LookupAsciiSymbol(name);
  CALL_HEAP_FUNCTION(obj->GetProperty(*str), Object);
}


Handle<Object> GetProperty(Handle<Object> obj,
                           Handle<Object> key) {
  CALL_HEAP_FUNCTION(Runtime::GetObjectProperty(obj, key), Object);
}


Handle<Object> GetPropertyWithInterceptor(Handle<JSObject> receiver,
                                          Handle<JSObject> holder,
                                          Handle<String> name,
                                          PropertyAttributes* attributes) {
  CALL_HEAP_FUNCTION(holder->GetPropertyWithInterceptor(*receiver,
                                                        *name,
                                                        attributes),
                     Object);
}


Handle<Object> GetPrototype(Handle<Object> obj) {
  Handle<Object> result(obj->GetPrototype());
  return result;
}


Handle<Object> GetHiddenProperties(Handle<JSObject> obj,
                                   bool create_if_needed) {
  CALL_HEAP_FUNCTION(obj->GetHiddenProperties(create_if_needed), Object);
}


Handle<Object> DeleteElement(Handle<JSObject> obj,
                             uint32_t index) {
  CALL_HEAP_FUNCTION(obj->DeleteElement(index), Object);
}


Handle<Object> DeleteProperty(Handle<JSObject> obj,
                              Handle<String> prop) {
  CALL_HEAP_FUNCTION(obj->DeleteProperty(*prop), Object);
}


Handle<Object> LookupSingleCharacterStringFromCode(uint32_t index) {
  CALL_HEAP_FUNCTION(Heap::LookupSingleCharacterStringFromCode(index), Object);
}


Handle<String> SubString(Handle<String> str, int start, int end) {
  CALL_HEAP_FUNCTION(str->Slice(start, end), String);
}


Handle<Object> SetElement(Handle<JSObject> object,
                          uint32_t index,
                          Handle<Object> value) {
  CALL_HEAP_FUNCTION(object->SetElement(index, *value), Object);
}


Handle<JSObject> Copy(Handle<JSObject> obj) {
  CALL_HEAP_FUNCTION(Heap::CopyJSObject(*obj), JSObject);
}


// Wrappers for scripts are kept alive and cached in weak global
// handles referred from proxy objects held by the scripts as long as
// they are used. When they are not used anymore, the garbage
// collector will call the weak callback on the global handle
// associated with the wrapper and get rid of both the wrapper and the
// handle.
static void ClearWrapperCache(Persistent<v8::Value> handle, void*) {
#ifdef ENABLE_HEAP_PROTECTION
  // Weak reference callbacks are called as if from outside V8.  We
  // need to reeenter to unprotect the heap.
  VMState state(OTHER);
#endif
  Handle<Object> cache = Utils::OpenHandle(*handle);
  JSValue* wrapper = JSValue::cast(*cache);
  Proxy* proxy = Script::cast(wrapper->value())->wrapper();
  ASSERT(proxy->proxy() == reinterpret_cast<Address>(cache.location()));
  proxy->set_proxy(0);
  GlobalHandles::Destroy(cache.location());
  Counters::script_wrappers.Decrement();
}


Handle<JSValue> GetScriptWrapper(Handle<Script> script) {
  Handle<Object> cache(reinterpret_cast<Object**>(script->wrapper()->proxy()));
  if (!cache.is_null()) {
    // Return the script wrapper directly from the cache.
    return Handle<JSValue>(JSValue::cast(*cache));
  }

  // Construct a new script wrapper.
  Counters::script_wrappers.Increment();
  Handle<JSFunction> constructor = Top::script_function();
  Handle<JSValue> result =
      Handle<JSValue>::cast(Factory::NewJSObject(constructor));
  result->set_value(*script);

  // Create a new weak global handle and use it to cache the wrapper
  // for future use. The cache will automatically be cleared by the
  // garbage collector when it is not used anymore.
  Handle<Object> handle = GlobalHandles::Create(*result);
  GlobalHandles::MakeWeak(handle.location(), NULL, &ClearWrapperCache);
  script->wrapper()->set_proxy(reinterpret_cast<Address>(handle.location()));
  return result;
}


// Init line_ends array with code positions of line ends inside script
// source.
void InitScriptLineEnds(Handle<Script> script) {
  if (!script->line_ends()->IsUndefined()) return;

  if (!script->source()->IsString()) {
    ASSERT(script->source()->IsUndefined());
    script->set_line_ends(*(Factory::NewJSArray(0)));
    ASSERT(script->line_ends()->IsJSArray());
    return;
  }

  Handle<String> src(String::cast(script->source()));
  const int src_len = src->length();
  Handle<String> new_line = Factory::NewStringFromAscii(CStrVector("\n"));

  // Pass 1: Identify line count.
  int line_count = 0;
  int position = 0;
  while (position != -1 && position < src_len) {
    position = Runtime::StringMatch(src, new_line, position);
    if (position != -1) {
      position++;
    }
    // Even if the last line misses a line end, it is counted.
    line_count++;
  }

  // Pass 2: Fill in line ends positions
  Handle<FixedArray> array = Factory::NewFixedArray(line_count);
  int array_index = 0;
  position = 0;
  while (position != -1 && position < src_len) {
    position = Runtime::StringMatch(src, new_line, position);
    // If the script does not end with a line ending add the final end
    // position as just past the last line ending.
    array->set(array_index++,
               Smi::FromInt(position != -1 ? position++ : src_len));
  }
  ASSERT(array_index == line_count);

  Handle<JSArray> object = Factory::NewJSArrayWithElements(array);
  script->set_line_ends(*object);
  ASSERT(script->line_ends()->IsJSArray());
}


// Convert code position into line number.
int GetScriptLineNumber(Handle<Script> script, int code_pos) {
  InitScriptLineEnds(script);
  AssertNoAllocation no_allocation;
  JSArray* line_ends_array = JSArray::cast(script->line_ends());
  const int line_ends_len = (Smi::cast(line_ends_array->length()))->value();

  int line = -1;
  if (line_ends_len > 0 &&
      code_pos <= (Smi::cast(line_ends_array->GetElement(0)))->value()) {
    line = 0;
  } else {
    for (int i = 1; i < line_ends_len; ++i) {
      if ((Smi::cast(line_ends_array->GetElement(i - 1)))->value() < code_pos &&
          code_pos <= (Smi::cast(line_ends_array->GetElement(i)))->value()) {
        line = i;
        break;
      }
    }
  }

  return line != -1 ? line + script->line_offset()->value() : line;
}


// Compute the property keys from the interceptor.
v8::Handle<v8::Array> GetKeysForNamedInterceptor(Handle<JSObject> receiver,
                                                 Handle<JSObject> object) {
  Handle<InterceptorInfo> interceptor(object->GetNamedInterceptor());
  Handle<Object> data(interceptor->data());
  v8::AccessorInfo info(
    v8::Utils::ToLocal(receiver),
    v8::Utils::ToLocal(data),
    v8::Utils::ToLocal(object));
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::NamedPropertyEnumerator enum_fun =
        v8::ToCData<v8::NamedPropertyEnumerator>(interceptor->enumerator());
    LOG(ApiObjectAccess("interceptor-named-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
      result = enum_fun(info);
    }
  }
  return result;
}


// Compute the element keys from the interceptor.
v8::Handle<v8::Array> GetKeysForIndexedInterceptor(Handle<JSObject> receiver,
                                                   Handle<JSObject> object) {
  Handle<InterceptorInfo> interceptor(object->GetIndexedInterceptor());
  Handle<Object> data(interceptor->data());
  v8::AccessorInfo info(
    v8::Utils::ToLocal(receiver),
    v8::Utils::ToLocal(data),
    v8::Utils::ToLocal(object));
  v8::Handle<v8::Array> result;
  if (!interceptor->enumerator()->IsUndefined()) {
    v8::IndexedPropertyEnumerator enum_fun =
        v8::ToCData<v8::IndexedPropertyEnumerator>(interceptor->enumerator());
    LOG(ApiObjectAccess("interceptor-indexed-enum", *object));
    {
      // Leaving JavaScript.
      VMState state(EXTERNAL);
      result = enum_fun(info);
    }
  }
  return result;
}


Handle<FixedArray> GetKeysInFixedArrayFor(Handle<JSObject> object) {
  Handle<FixedArray> content = Factory::empty_fixed_array();

  JSObject* arguments_boilerplate =
      Top::context()->global_context()->arguments_boilerplate();
  JSFunction* arguments_function =
      JSFunction::cast(arguments_boilerplate->map()->constructor());
  bool allow_enumeration = (object->map()->constructor() != arguments_function);

  // Only collect keys if access is permitted.
  if (allow_enumeration) {
    for (Handle<Object> p = object;
         *p != Heap::null_value();
         p = Handle<Object>(p->GetPrototype())) {
      Handle<JSObject> current(JSObject::cast(*p));

      // Check access rights if required.
      if (current->IsAccessCheckNeeded() &&
        !Top::MayNamedAccess(*current, Heap::undefined_value(),
                             v8::ACCESS_KEYS)) {
        Top::ReportFailedAccessCheck(*current, v8::ACCESS_KEYS);
        break;
      }

      // Compute the property keys.
      content = UnionOfKeys(content, GetEnumPropertyKeys(current));

      // Add the property keys from the interceptor.
      if (current->HasNamedInterceptor()) {
        v8::Handle<v8::Array> result =
            GetKeysForNamedInterceptor(object, current);
        if (!result.IsEmpty())
          content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      }

      // Compute the element keys.
      Handle<FixedArray> element_keys =
          Factory::NewFixedArray(current->NumberOfEnumElements());
      current->GetEnumElementKeys(*element_keys);
      content = UnionOfKeys(content, element_keys);

      // Add the element keys from the interceptor.
      if (current->HasIndexedInterceptor()) {
        v8::Handle<v8::Array> result =
            GetKeysForIndexedInterceptor(object, current);
        if (!result.IsEmpty())
          content = AddKeysFromJSArray(content, v8::Utils::OpenHandle(*result));
      }
    }
  }
  return content;
}


Handle<JSArray> GetKeysFor(Handle<JSObject> object) {
  Counters::for_in.Increment();
  Handle<FixedArray> elements = GetKeysInFixedArrayFor(object);
  return Factory::NewJSArrayWithElements(elements);
}


Handle<FixedArray> GetEnumPropertyKeys(Handle<JSObject> object) {
  int index = 0;
  if (object->HasFastProperties()) {
    if (object->map()->instance_descriptors()->HasEnumCache()) {
      Counters::enum_cache_hits.Increment();
      DescriptorArray* desc = object->map()->instance_descriptors();
      return Handle<FixedArray>(FixedArray::cast(desc->GetEnumCache()));
    }
    Counters::enum_cache_misses.Increment();
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = Factory::NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = Factory::NewFixedArray(num_enum);
    for (DescriptorReader r(object->map()->instance_descriptors());
         !r.eos();
         r.advance()) {
      if (r.IsProperty() && !r.IsDontEnum()) {
        (*storage)->set(index, r.GetKey());
        (*sort_array)->set(index, Smi::FromInt(r.GetDetails().index()));
        index++;
      }
    }
    (*storage)->SortPairs(*sort_array);
    Handle<FixedArray> bridge_storage =
        Factory::NewFixedArray(DescriptorArray::kEnumCacheBridgeLength);
    DescriptorArray* desc = object->map()->instance_descriptors();
    desc->SetEnumCache(*bridge_storage, *storage);
    ASSERT(storage->length() == index);
    return storage;
  } else {
    int num_enum = object->NumberOfEnumProperties();
    Handle<FixedArray> storage = Factory::NewFixedArray(num_enum);
    Handle<FixedArray> sort_array = Factory::NewFixedArray(num_enum);
    object->property_dictionary()->CopyEnumKeysTo(*storage, *sort_array);
    return storage;
  }
}


bool CompileLazyShared(Handle<SharedFunctionInfo> shared,
                       ClearExceptionFlag flag,
                       int loop_nesting) {
  // Compile the source information to a code object.
  ASSERT(!shared->is_compiled());
  bool result = Compiler::CompileLazy(shared, loop_nesting);
  ASSERT(result != Top::has_pending_exception());
  if (!result && flag == CLEAR_EXCEPTION) Top::clear_pending_exception();
  return result;
}


bool CompileLazy(Handle<JSFunction> function, ClearExceptionFlag flag) {
  // Compile the source information to a code object.
  Handle<SharedFunctionInfo> shared(function->shared());
  return CompileLazyShared(shared, flag, 0);
}


bool CompileLazyInLoop(Handle<JSFunction> function, ClearExceptionFlag flag) {
  // Compile the source information to a code object.
  Handle<SharedFunctionInfo> shared(function->shared());
  return CompileLazyShared(shared, flag, 1);
}

OptimizedObjectForAddingMultipleProperties::
OptimizedObjectForAddingMultipleProperties(Handle<JSObject> object,
                                           bool condition) {
  object_ = object;
  if (condition && object_->HasFastProperties()) {
    // Normalize the properties of object to avoid n^2 behavior
    // when extending the object multiple properties.
    unused_property_fields_ = object->map()->unused_property_fields();
    NormalizeProperties(object_, KEEP_INOBJECT_PROPERTIES);
    has_been_transformed_ = true;

  } else {
    has_been_transformed_ = false;
  }
}


OptimizedObjectForAddingMultipleProperties::
~OptimizedObjectForAddingMultipleProperties() {
  // Reoptimize the object to allow fast property access.
  if (has_been_transformed_) {
    TransformToFastProperties(object_, unused_property_fields_);
  }
}


void LoadLazy(Handle<JSFunction> fun, bool* pending_exception) {
  HandleScope scope;
  Handle<FixedArray> info(FixedArray::cast(fun->shared()->lazy_load_data()));
  int index = Smi::cast(info->get(0))->value();
  ASSERT(index >= 0);
  Handle<Context> compile_context(Context::cast(info->get(1)));
  Handle<Context> function_context(Context::cast(info->get(2)));
  Handle<Object> receiver(compile_context->global()->builtins());

  Vector<const char> name = Natives::GetScriptName(index);

  Handle<JSFunction> boilerplate;

  if (!Bootstrapper::NativesCacheLookup(name, &boilerplate)) {
    Handle<String> source_code = Bootstrapper::NativesSourceLookup(index);
    Handle<String> script_name = Factory::NewStringFromAscii(name);
    bool allow_natives_syntax = FLAG_allow_natives_syntax;
    FLAG_allow_natives_syntax = true;
    boilerplate = Compiler::Compile(source_code, script_name, 0, 0, NULL, NULL);
    FLAG_allow_natives_syntax = allow_natives_syntax;
    // If the compilation failed (possibly due to stack overflows), we
    // should never enter the result in the natives cache. Instead we
    // return from the function without marking the function as having
    // been lazily loaded.
    if (boilerplate.is_null()) {
      *pending_exception = true;
      return;
    }
    Bootstrapper::NativesCacheAdd(name, boilerplate);
  }

  // We shouldn't get here if compiling the script failed.
  ASSERT(!boilerplate.is_null());

  // When the debugger running in its own context touches lazy loaded
  // functions loading can be triggered. In that case ensure that the
  // execution of the boilerplate is in the correct context.
  SaveContext save;
  if (!Debug::debug_context().is_null() &&
      Top::context() == *Debug::debug_context()) {
    Top::set_context(*compile_context);
  }

  // Reset the lazy load data before running the script to make sure
  // not to get recursive lazy loading.
  fun->shared()->set_lazy_load_data(Heap::undefined_value());

  // Run the script.
  Handle<JSFunction> script_fun(
      Factory::NewFunctionFromBoilerplate(boilerplate, function_context));
  Execution::Call(script_fun, receiver, 0, NULL, pending_exception);

  // If lazy loading failed, restore the unloaded state of fun.
  if (*pending_exception) fun->shared()->set_lazy_load_data(*info);
}


void SetupLazy(Handle<JSFunction> fun,
               int index,
               Handle<Context> compile_context,
               Handle<Context> function_context) {
  Handle<FixedArray> arr = Factory::NewFixedArray(3);
  arr->set(0, Smi::FromInt(index));
  arr->set(1, *compile_context);  // Compile in this context
  arr->set(2, *function_context);  // Set function context to this
  fun->shared()->set_lazy_load_data(*arr);
}

} }  // namespace v8::internal
