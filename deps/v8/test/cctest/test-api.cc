// Copyright 2007-2008 the V8 project authors. All rights reserved.
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

#include <stdlib.h>

#include <map>
#include <string>

#include "v8.h"

#include "api.h"
#include "snapshot.h"
#include "platform.h"
#include "top.h"
#include "cctest.h"

static bool IsNaN(double x) {
#ifdef WIN32
  return _isnan(x);
#else
  return isnan(x);
#endif
}

using ::v8::ObjectTemplate;
using ::v8::Value;
using ::v8::Context;
using ::v8::Local;
using ::v8::String;
using ::v8::Script;
using ::v8::Function;
using ::v8::AccessorInfo;
using ::v8::Extension;

namespace i = ::v8::internal;

static Local<Value> v8_num(double x) {
  return v8::Number::New(x);
}


static Local<String> v8_str(const char* x) {
  return String::New(x);
}


static Local<Script> v8_compile(const char* x) {
  return Script::Compile(v8_str(x));
}


// A LocalContext holds a reference to a v8::Context.
class LocalContext {
 public:
  LocalContext(v8::ExtensionConfiguration* extensions = 0,
               v8::Handle<ObjectTemplate> global_template =
                   v8::Handle<ObjectTemplate>(),
               v8::Handle<Value> global_object = v8::Handle<Value>())
    : context_(Context::New(extensions, global_template, global_object)) {
    context_->Enter();
  }

  virtual ~LocalContext() {
    context_->Exit();
    context_.Dispose();
  }

  Context* operator->() { return *context_; }
  Context* operator*() { return *context_; }
  Local<Context> local() { return Local<Context>::New(context_); }
  bool IsReady() { return !context_.IsEmpty(); }

 private:
  v8::Persistent<Context> context_;
};


// Switches between all the Api tests using the threading support.
// In order to get a surprising but repeatable pattern of thread
// switching it has extra semaphores to control the order in which
// the tests alternate, not relying solely on the big V8 lock.
//
// A test is augmented with calls to ApiTestFuzzer::Fuzz() in its
// callbacks.  This will have no effect when we are not running the
// thread fuzzing test.  In the thread fuzzing test it will
// pseudorandomly select a successor thread and switch execution
// to that thread, suspending the current test.
class ApiTestFuzzer: public v8::internal::Thread {
 public:
  void CallTest();
  explicit ApiTestFuzzer(int num)
      : test_number_(num),
        gate_(v8::internal::OS::CreateSemaphore(0)),
        active_(true) {
  }
  ~ApiTestFuzzer() { delete gate_; }

  // The ApiTestFuzzer is also a Thread, so it has a Run method.
  virtual void Run();

  enum PartOfTest { FIRST_PART, SECOND_PART };

  static void Setup(PartOfTest part);
  static void RunAllTests();
  static void TearDown();
  // This method switches threads if we are running the Threading test.
  // Otherwise it does nothing.
  static void Fuzz();
 private:
  static bool fuzzing_;
  static int tests_being_run_;
  static int current_;
  static int active_tests_;
  static bool NextThread();
  int test_number_;
  v8::internal::Semaphore* gate_;
  bool active_;
  void ContextSwitch();
  static int GetNextTestNumber();
  static v8::internal::Semaphore* all_tests_done_;
};


#define THREADED_TEST(Name)                                          \
  static void Test##Name();                                          \
  RegisterThreadedTest register_##Name(Test##Name);                  \
  /* */ TEST(Name)


class RegisterThreadedTest {
 public:
  explicit RegisterThreadedTest(CcTest::TestFunction* callback)
      : fuzzer_(NULL), callback_(callback) {
    prev_ = first_;
    first_ = this;
    count_++;
  }
  static int count() { return count_; }
  static RegisterThreadedTest* nth(int i) {
    ASSERT(i < count());
    RegisterThreadedTest* current = first_;
    while (i > 0) {
      i--;
      current = current->prev_;
    }
    return current;
  }
  CcTest::TestFunction* callback() { return callback_; }
  ApiTestFuzzer* fuzzer_;

 private:
  static RegisterThreadedTest* first_;
  static int count_;
  CcTest::TestFunction* callback_;
  RegisterThreadedTest* prev_;
};


RegisterThreadedTest *RegisterThreadedTest::first_ = NULL;
int RegisterThreadedTest::count_ = 0;


static int signature_callback_count;
static v8::Handle<Value> IncrementingSignatureCallback(
    const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  signature_callback_count++;
  v8::Handle<v8::Array> result = v8::Array::New(args.Length());
  for (int i = 0; i < args.Length(); i++)
    result->Set(v8::Integer::New(i), args[i]);
  return result;
}


static v8::Handle<Value> SignatureCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  v8::Handle<v8::Array> result = v8::Array::New(args.Length());
  for (int i = 0; i < args.Length(); i++) {
    result->Set(v8::Integer::New(i), args[i]);
  }
  return result;
}


THREADED_TEST(Handles) {
  v8::HandleScope scope;
  Local<Context> local_env;
  {
    LocalContext env;
    local_env = env.local();
  }

  // Local context should still be live.
  CHECK(!local_env.IsEmpty());
  local_env->Enter();

  v8::Handle<v8::Primitive> undef = v8::Undefined();
  CHECK(!undef.IsEmpty());
  CHECK(undef->IsUndefined());

  const char* c_source = "1 + 2 + 3";
  Local<String> source = String::New(c_source);
  Local<Script> script = Script::Compile(source);
  CHECK_EQ(6, script->Run()->Int32Value());

  local_env->Exit();
}


// Helper function that compiles and runs the source.
static Local<Value> CompileRun(const char* source) {
  return Script::Compile(String::New(source))->Run();
}

THREADED_TEST(ReceiverSignature) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<v8::FunctionTemplate> fun = v8::FunctionTemplate::New();
  v8::Handle<v8::Signature> sig = v8::Signature::New(fun);
  fun->PrototypeTemplate()->Set(
      v8_str("m"),
      v8::FunctionTemplate::New(IncrementingSignatureCallback,
                                v8::Handle<Value>(),
                                sig));
  env->Global()->Set(v8_str("Fun"), fun->GetFunction());
  signature_callback_count = 0;
  CompileRun(
      "var o = new Fun();"
      "o.m();");
  CHECK_EQ(1, signature_callback_count);
  v8::Handle<v8::FunctionTemplate> sub_fun = v8::FunctionTemplate::New();
  sub_fun->Inherit(fun);
  env->Global()->Set(v8_str("SubFun"), sub_fun->GetFunction());
  CompileRun(
      "var o = new SubFun();"
      "o.m();");
  CHECK_EQ(2, signature_callback_count);

  v8::TryCatch try_catch;
  CompileRun(
      "var o = { };"
      "o.m = Fun.prototype.m;"
      "o.m();");
  CHECK_EQ(2, signature_callback_count);
  CHECK(try_catch.HasCaught());
  try_catch.Reset();
  v8::Handle<v8::FunctionTemplate> unrel_fun = v8::FunctionTemplate::New();
  sub_fun->Inherit(fun);
  env->Global()->Set(v8_str("UnrelFun"), unrel_fun->GetFunction());
  CompileRun(
      "var o = new UnrelFun();"
      "o.m = Fun.prototype.m;"
      "o.m();");
  CHECK_EQ(2, signature_callback_count);
  CHECK(try_catch.HasCaught());
}




THREADED_TEST(ArgumentSignature) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<v8::FunctionTemplate> cons = v8::FunctionTemplate::New();
  cons->SetClassName(v8_str("Cons"));
  v8::Handle<v8::Signature> sig =
      v8::Signature::New(v8::Handle<v8::FunctionTemplate>(), 1, &cons);
  v8::Handle<v8::FunctionTemplate> fun =
      v8::FunctionTemplate::New(SignatureCallback, v8::Handle<Value>(), sig);
  env->Global()->Set(v8_str("Cons"), cons->GetFunction());
  env->Global()->Set(v8_str("Fun1"), fun->GetFunction());

  v8::Handle<Value> value1 = CompileRun("Fun1(4) == '';");
  ASSERT(value1->IsTrue());

  v8::Handle<Value> value2 = CompileRun("Fun1(new Cons()) == '[object Cons]';");
  ASSERT(value2->IsTrue());

  v8::Handle<Value> value3 = CompileRun("Fun1() == '';");
  ASSERT(value3->IsTrue());

  v8::Handle<v8::FunctionTemplate> cons1 = v8::FunctionTemplate::New();
  cons1->SetClassName(v8_str("Cons1"));
  v8::Handle<v8::FunctionTemplate> cons2 = v8::FunctionTemplate::New();
  cons2->SetClassName(v8_str("Cons2"));
  v8::Handle<v8::FunctionTemplate> cons3 = v8::FunctionTemplate::New();
  cons3->SetClassName(v8_str("Cons3"));

  v8::Handle<v8::FunctionTemplate> args[3] = { cons1, cons2, cons3 };
  v8::Handle<v8::Signature> wsig =
      v8::Signature::New(v8::Handle<v8::FunctionTemplate>(), 3, args);
  v8::Handle<v8::FunctionTemplate> fun2 =
      v8::FunctionTemplate::New(SignatureCallback, v8::Handle<Value>(), wsig);

  env->Global()->Set(v8_str("Cons1"), cons1->GetFunction());
  env->Global()->Set(v8_str("Cons2"), cons2->GetFunction());
  env->Global()->Set(v8_str("Cons3"), cons3->GetFunction());
  env->Global()->Set(v8_str("Fun2"), fun2->GetFunction());
  v8::Handle<Value> value4 = CompileRun(
      "Fun2(new Cons1(), new Cons2(), new Cons3()) =="
      "'[object Cons1],[object Cons2],[object Cons3]'");
  ASSERT(value4->IsTrue());

  v8::Handle<Value> value5 = CompileRun(
      "Fun2(new Cons1(), new Cons2(), 5) == '[object Cons1],[object Cons2],'");
  ASSERT(value5->IsTrue());

  v8::Handle<Value> value6 = CompileRun(
      "Fun2(new Cons3(), new Cons2(), new Cons1()) == ',[object Cons2],'");
  ASSERT(value6->IsTrue());

  v8::Handle<Value> value7 = CompileRun(
      "Fun2(new Cons1(), new Cons2(), new Cons3(), 'd') == "
      "'[object Cons1],[object Cons2],[object Cons3],d';");
  ASSERT(value7->IsTrue());

  v8::Handle<Value> value8 = CompileRun(
      "Fun2(new Cons1(), new Cons2()) == '[object Cons1],[object Cons2]'");
  ASSERT(value8->IsTrue());
}


THREADED_TEST(HulIgennem) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<v8::Primitive> undef = v8::Undefined();
  Local<String> undef_str = undef->ToString();
  char* value = i::NewArray<char>(undef_str->Length() + 1);
  undef_str->WriteAscii(value);
  CHECK_EQ(0, strcmp(value, "undefined"));
  i::DeleteArray(value);
}


THREADED_TEST(Access) {
  v8::HandleScope scope;
  LocalContext env;
  Local<v8::Object> obj = v8::Object::New();
  Local<Value> foo_before = obj->Get(v8_str("foo"));
  CHECK(foo_before->IsUndefined());
  Local<String> bar_str = v8_str("bar");
  obj->Set(v8_str("foo"), bar_str);
  Local<Value> foo_after = obj->Get(v8_str("foo"));
  CHECK(!foo_after->IsUndefined());
  CHECK(foo_after->IsString());
  CHECK_EQ(bar_str, foo_after);
}


THREADED_TEST(Script) {
  v8::HandleScope scope;
  LocalContext env;
  const char* c_source = "1 + 2 + 3";
  Local<String> source = String::New(c_source);
  Local<Script> script = Script::Compile(source);
  CHECK_EQ(6, script->Run()->Int32Value());
}


static uint16_t* AsciiToTwoByteString(const char* source) {
  size_t array_length = strlen(source) + 1;
  uint16_t* converted = i::NewArray<uint16_t>(array_length);
  for (size_t i = 0; i < array_length; i++) converted[i] = source[i];
  return converted;
}


class TestResource: public String::ExternalStringResource {
 public:
  static int dispose_count;

  explicit TestResource(uint16_t* data)
      : data_(data), length_(0) {
    while (data[length_]) ++length_;
  }

  ~TestResource() {
    i::DeleteArray(data_);
    ++dispose_count;
  }

  const uint16_t* data() const {
    return data_;
  }

  size_t length() const {
    return length_;
  }
 private:
  uint16_t* data_;
  size_t length_;
};


int TestResource::dispose_count = 0;


class TestAsciiResource: public String::ExternalAsciiStringResource {
 public:
  static int dispose_count;

  explicit TestAsciiResource(char* data)
      : data_(data),
        length_(strlen(data)) { }

  ~TestAsciiResource() {
    i::DeleteArray(data_);
    ++dispose_count;
  }

  const char* data() const {
    return data_;
  }

  size_t length() const {
    return length_;
  }
 private:
  char* data_;
  size_t length_;
};


int TestAsciiResource::dispose_count = 0;


THREADED_TEST(ScriptUsingStringResource) {
  TestResource::dispose_count = 0;
  const char* c_source = "1 + 2 * 3";
  uint16_t* two_byte_source = AsciiToTwoByteString(c_source);
  {
    v8::HandleScope scope;
    LocalContext env;
    TestResource* resource = new TestResource(two_byte_source);
    Local<String> source = String::NewExternal(resource);
    Local<Script> script = Script::Compile(source);
    Local<Value> value = script->Run();
    CHECK(value->IsNumber());
    CHECK_EQ(7, value->Int32Value());
    CHECK(source->IsExternal());
    CHECK_EQ(resource,
             static_cast<TestResource*>(source->GetExternalStringResource()));
    v8::internal::Heap::CollectAllGarbage();
    CHECK_EQ(0, TestResource::dispose_count);
  }
  v8::internal::Heap::CollectAllGarbage();
  CHECK_EQ(1, TestResource::dispose_count);
}


THREADED_TEST(ScriptUsingAsciiStringResource) {
  TestAsciiResource::dispose_count = 0;
  const char* c_source = "1 + 2 * 3";
  {
    v8::HandleScope scope;
    LocalContext env;
    Local<String> source =
        String::NewExternal(new TestAsciiResource(i::StrDup(c_source)));
    Local<Script> script = Script::Compile(source);
    Local<Value> value = script->Run();
    CHECK(value->IsNumber());
    CHECK_EQ(7, value->Int32Value());
    v8::internal::Heap::CollectAllGarbage();
    CHECK_EQ(0, TestAsciiResource::dispose_count);
  }
  v8::internal::Heap::CollectAllGarbage();
  CHECK_EQ(1, TestAsciiResource::dispose_count);
}


THREADED_TEST(ScriptMakingExternalString) {
  TestResource::dispose_count = 0;
  uint16_t* two_byte_source = AsciiToTwoByteString("1 + 2 * 3");
  {
    v8::HandleScope scope;
    LocalContext env;
    Local<String> source = String::New(two_byte_source);
    bool success = source->MakeExternal(new TestResource(two_byte_source));
    CHECK(success);
    Local<Script> script = Script::Compile(source);
    Local<Value> value = script->Run();
    CHECK(value->IsNumber());
    CHECK_EQ(7, value->Int32Value());
    v8::internal::Heap::CollectAllGarbage();
    CHECK_EQ(0, TestResource::dispose_count);
  }
  v8::internal::Heap::CollectAllGarbage();
  CHECK_EQ(1, TestResource::dispose_count);
}


THREADED_TEST(ScriptMakingExternalAsciiString) {
  TestAsciiResource::dispose_count = 0;
  const char* c_source = "1 + 2 * 3";
  {
    v8::HandleScope scope;
    LocalContext env;
    Local<String> source = v8_str(c_source);
    bool success = source->MakeExternal(
        new TestAsciiResource(i::StrDup(c_source)));
    CHECK(success);
    Local<Script> script = Script::Compile(source);
    Local<Value> value = script->Run();
    CHECK(value->IsNumber());
    CHECK_EQ(7, value->Int32Value());
    v8::internal::Heap::CollectAllGarbage();
    CHECK_EQ(0, TestAsciiResource::dispose_count);
  }
  v8::internal::Heap::CollectAllGarbage();
  CHECK_EQ(1, TestAsciiResource::dispose_count);
}


THREADED_TEST(UsingExternalString) {
  v8::HandleScope scope;
  uint16_t* two_byte_string = AsciiToTwoByteString("test string");
  Local<String> string = String::NewExternal(new TestResource(two_byte_string));
  i::Handle<i::String> istring = v8::Utils::OpenHandle(*string);
  // Trigger GCs so that the newly allocated string moves to old gen.
  i::Heap::CollectGarbage(0, i::NEW_SPACE);  // in survivor space now
  i::Heap::CollectGarbage(0, i::NEW_SPACE);  // in old gen now
  i::Handle<i::String> isymbol = i::Factory::SymbolFromString(istring);
  CHECK(isymbol->IsSymbol());
}


THREADED_TEST(UsingExternalAsciiString) {
  v8::HandleScope scope;
  const char* one_byte_string = "test string";
  Local<String> string = String::NewExternal(
      new TestAsciiResource(i::StrDup(one_byte_string)));
  i::Handle<i::String> istring = v8::Utils::OpenHandle(*string);
  // Trigger GCs so that the newly allocated string moves to old gen.
  i::Heap::CollectGarbage(0, i::NEW_SPACE);  // in survivor space now
  i::Heap::CollectGarbage(0, i::NEW_SPACE);  // in old gen now
  i::Handle<i::String> isymbol = i::Factory::SymbolFromString(istring);
  CHECK(isymbol->IsSymbol());
}


THREADED_TEST(GlobalProperties) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<v8::Object> global = env->Global();
  global->Set(v8_str("pi"), v8_num(3.1415926));
  Local<Value> pi = global->Get(v8_str("pi"));
  CHECK_EQ(3.1415926, pi->NumberValue());
}


static v8::Handle<Value> handle_call(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(102);
}


static v8::Handle<Value> construct_call(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  args.This()->Set(v8_str("x"), v8_num(1));
  args.This()->Set(v8_str("y"), v8_num(2));
  return args.This();
}

THREADED_TEST(FunctionTemplate) {
  v8::HandleScope scope;
  LocalContext env;
  {
    Local<v8::FunctionTemplate> fun_templ =
        v8::FunctionTemplate::New(handle_call);
    Local<Function> fun = fun_templ->GetFunction();
    env->Global()->Set(v8_str("obj"), fun);
    Local<Script> script = v8_compile("obj()");
    CHECK_EQ(102, script->Run()->Int32Value());
  }
  // Use SetCallHandler to initialize a function template, should work like the
  // previous one.
  {
    Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New();
    fun_templ->SetCallHandler(handle_call);
    Local<Function> fun = fun_templ->GetFunction();
    env->Global()->Set(v8_str("obj"), fun);
    Local<Script> script = v8_compile("obj()");
    CHECK_EQ(102, script->Run()->Int32Value());
  }
  // Test constructor calls.
  {
    Local<v8::FunctionTemplate> fun_templ =
        v8::FunctionTemplate::New(construct_call);
    fun_templ->SetClassName(v8_str("funky"));
    Local<Function> fun = fun_templ->GetFunction();
    env->Global()->Set(v8_str("obj"), fun);
    Local<Script> script = v8_compile("var s = new obj(); s.x");
    CHECK_EQ(1, script->Run()->Int32Value());

    Local<Value> result = v8_compile("(new obj()).toString()")->Run();
    CHECK_EQ(v8_str("[object funky]"), result);
  }
}


static v8::Handle<Value> handle_property(Local<String> name,
                                         const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8_num(900);
}


THREADED_TEST(PropertyHandler) {
  v8::HandleScope scope;
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New();
  fun_templ->InstanceTemplate()->SetAccessor(v8_str("foo"), handle_property);
  LocalContext env;
  Local<Function> fun = fun_templ->GetFunction();
  env->Global()->Set(v8_str("Fun"), fun);
  Local<Script> getter = v8_compile("var obj = new Fun(); obj.foo;");
  CHECK_EQ(900, getter->Run()->Int32Value());
  Local<Script> setter = v8_compile("obj.foo = 901;");
  CHECK_EQ(901, setter->Run()->Int32Value());
}


THREADED_TEST(Number) {
  v8::HandleScope scope;
  LocalContext env;
  double PI = 3.1415926;
  Local<v8::Number> pi_obj = v8::Number::New(PI);
  CHECK_EQ(PI, pi_obj->NumberValue());
}


THREADED_TEST(ToNumber) {
  v8::HandleScope scope;
  LocalContext env;
  Local<String> str = v8_str("3.1415926");
  CHECK_EQ(3.1415926, str->NumberValue());
  v8::Handle<v8::Boolean> t = v8::True();
  CHECK_EQ(1.0, t->NumberValue());
  v8::Handle<v8::Boolean> f = v8::False();
  CHECK_EQ(0.0, f->NumberValue());
}


THREADED_TEST(Date) {
  v8::HandleScope scope;
  LocalContext env;
  double PI = 3.1415926;
  Local<Value> date_obj = v8::Date::New(PI);
  CHECK_EQ(3.0, date_obj->NumberValue());
}


THREADED_TEST(Boolean) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<v8::Boolean> t = v8::True();
  CHECK(t->Value());
  v8::Handle<v8::Boolean> f = v8::False();
  CHECK(!f->Value());
  v8::Handle<v8::Primitive> u = v8::Undefined();
  CHECK(!u->BooleanValue());
  v8::Handle<v8::Primitive> n = v8::Null();
  CHECK(!n->BooleanValue());
  v8::Handle<String> str1 = v8_str("");
  CHECK(!str1->BooleanValue());
  v8::Handle<String> str2 = v8_str("x");
  CHECK(str2->BooleanValue());
  CHECK(!v8::Number::New(0)->BooleanValue());
  CHECK(v8::Number::New(-1)->BooleanValue());
  CHECK(v8::Number::New(1)->BooleanValue());
  CHECK(v8::Number::New(42)->BooleanValue());
  CHECK(!v8_compile("NaN")->Run()->BooleanValue());
}


static v8::Handle<Value> DummyCallHandler(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(13.4);
}


static v8::Handle<Value> GetM(Local<String> name, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8_num(876);
}


THREADED_TEST(GlobalPrototype) {
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> func_templ = v8::FunctionTemplate::New();
  func_templ->PrototypeTemplate()->Set(
      "dummy",
      v8::FunctionTemplate::New(DummyCallHandler));
  v8::Handle<ObjectTemplate> templ = func_templ->InstanceTemplate();
  templ->Set("x", v8_num(200));
  templ->SetAccessor(v8_str("m"), GetM);
  LocalContext env(0, templ);
  v8::Handle<v8::Object> obj = env->Global();
  v8::Handle<Script> script = v8_compile("dummy()");
  v8::Handle<Value> result = script->Run();
  CHECK_EQ(13.4, result->NumberValue());
  CHECK_EQ(200, v8_compile("x")->Run()->Int32Value());
  CHECK_EQ(876, v8_compile("m")->Run()->Int32Value());
}


static v8::Handle<Value> GetIntValue(Local<String> property,
                                     const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  int* value =
      static_cast<int*>(v8::Handle<v8::External>::Cast(info.Data())->Value());
  return v8_num(*value);
}

static void SetIntValue(Local<String> property,
                        Local<Value> value,
                        const AccessorInfo& info) {
  int* field =
      static_cast<int*>(v8::Handle<v8::External>::Cast(info.Data())->Value());
  *field = value->Int32Value();
}

int foo, bar, baz;

THREADED_TEST(GlobalVariableAccess) {
  foo = 0;
  bar = -4;
  baz = 10;
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->InstanceTemplate()->SetAccessor(v8_str("foo"),
                                         GetIntValue,
                                         SetIntValue,
                                         v8::External::New(&foo));
  templ->InstanceTemplate()->SetAccessor(v8_str("bar"),
                                         GetIntValue,
                                         SetIntValue,
                                         v8::External::New(&bar));
  templ->InstanceTemplate()->SetAccessor(v8_str("baz"),
                                         GetIntValue,
                                         SetIntValue,
                                         v8::External::New(&baz));
  LocalContext env(0, templ->InstanceTemplate());
  v8_compile("foo = (++bar) + baz")->Run();
  CHECK_EQ(bar, -3);
  CHECK_EQ(foo, 7);
}


THREADED_TEST(ObjectTemplate) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ1 = ObjectTemplate::New();
  templ1->Set("x", v8_num(10));
  templ1->Set("y", v8_num(13));
  LocalContext env;
  Local<v8::Object> instance1 = templ1->NewInstance();
  env->Global()->Set(v8_str("p"), instance1);
  CHECK(v8_compile("(p.x == 10)")->Run()->BooleanValue());
  CHECK(v8_compile("(p.y == 13)")->Run()->BooleanValue());
  Local<v8::FunctionTemplate> fun = v8::FunctionTemplate::New();
  fun->PrototypeTemplate()->Set("nirk", v8_num(123));
  Local<ObjectTemplate> templ2 = fun->InstanceTemplate();
  templ2->Set("a", v8_num(12));
  templ2->Set("b", templ1);
  Local<v8::Object> instance2 = templ2->NewInstance();
  env->Global()->Set(v8_str("q"), instance2);
  CHECK(v8_compile("(q.nirk == 123)")->Run()->BooleanValue());
  CHECK(v8_compile("(q.a == 12)")->Run()->BooleanValue());
  CHECK(v8_compile("(q.b.x == 10)")->Run()->BooleanValue());
  CHECK(v8_compile("(q.b.y == 13)")->Run()->BooleanValue());
}


static v8::Handle<Value> GetFlabby(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(17.2);
}


static v8::Handle<Value> GetKnurd(Local<String> property, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8_num(15.2);
}


THREADED_TEST(DescriptorInheritance) {
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> super = v8::FunctionTemplate::New();
  super->PrototypeTemplate()->Set("flabby",
                                  v8::FunctionTemplate::New(GetFlabby));
  super->PrototypeTemplate()->Set("PI", v8_num(3.14));

  super->InstanceTemplate()->SetAccessor(v8_str("knurd"), GetKnurd);

  v8::Handle<v8::FunctionTemplate> base1 = v8::FunctionTemplate::New();
  base1->Inherit(super);
  base1->PrototypeTemplate()->Set("v1", v8_num(20.1));

  v8::Handle<v8::FunctionTemplate> base2 = v8::FunctionTemplate::New();
  base2->Inherit(super);
  base2->PrototypeTemplate()->Set("v2", v8_num(10.1));

  LocalContext env;

  env->Global()->Set(v8_str("s"), super->GetFunction());
  env->Global()->Set(v8_str("base1"), base1->GetFunction());
  env->Global()->Set(v8_str("base2"), base2->GetFunction());

  // Checks right __proto__ chain.
  CHECK(CompileRun("base1.prototype.__proto__ == s.prototype")->BooleanValue());
  CHECK(CompileRun("base2.prototype.__proto__ == s.prototype")->BooleanValue());

  CHECK(v8_compile("s.prototype.PI == 3.14")->Run()->BooleanValue());

  // Instance accessor should not be visible on function object or its prototype
  CHECK(CompileRun("s.knurd == undefined")->BooleanValue());
  CHECK(CompileRun("s.prototype.knurd == undefined")->BooleanValue());
  CHECK(CompileRun("base1.prototype.knurd == undefined")->BooleanValue());

  env->Global()->Set(v8_str("obj"),
                     base1->GetFunction()->NewInstance());
  CHECK_EQ(17.2, v8_compile("obj.flabby()")->Run()->NumberValue());
  CHECK(v8_compile("'flabby' in obj")->Run()->BooleanValue());
  CHECK_EQ(15.2, v8_compile("obj.knurd")->Run()->NumberValue());
  CHECK(v8_compile("'knurd' in obj")->Run()->BooleanValue());
  CHECK_EQ(20.1, v8_compile("obj.v1")->Run()->NumberValue());

  env->Global()->Set(v8_str("obj2"),
                     base2->GetFunction()->NewInstance());
  CHECK_EQ(17.2, v8_compile("obj2.flabby()")->Run()->NumberValue());
  CHECK(v8_compile("'flabby' in obj2")->Run()->BooleanValue());
  CHECK_EQ(15.2, v8_compile("obj2.knurd")->Run()->NumberValue());
  CHECK(v8_compile("'knurd' in obj2")->Run()->BooleanValue());
  CHECK_EQ(10.1, v8_compile("obj2.v2")->Run()->NumberValue());

  // base1 and base2 cannot cross reference to each's prototype
  CHECK(v8_compile("obj.v2")->Run()->IsUndefined());
  CHECK(v8_compile("obj2.v1")->Run()->IsUndefined());
}


int echo_named_call_count;


static v8::Handle<Value> EchoNamedProperty(Local<String> name,
                                           const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(v8_str("data"), info.Data());
  echo_named_call_count++;
  return name;
}


THREADED_TEST(NamedPropertyHandlerGetter) {
  echo_named_call_count = 0;
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->InstanceTemplate()->SetNamedPropertyHandler(EchoNamedProperty,
                                                     0, 0, 0, 0,
                                                     v8_str("data"));
  LocalContext env;
  env->Global()->Set(v8_str("obj"),
                     templ->GetFunction()->NewInstance());
  CHECK_EQ(echo_named_call_count, 0);
  v8_compile("obj.x")->Run();
  CHECK_EQ(echo_named_call_count, 1);
  const char* code = "var str = 'oddle'; obj[str] + obj.poddle;";
  v8::Handle<Value> str = CompileRun(code);
  String::AsciiValue value(str);
  CHECK_EQ(*value, "oddlepoddle");
  // Check default behavior
  CHECK_EQ(v8_compile("obj.flob = 10;")->Run()->Int32Value(), 10);
  CHECK(v8_compile("'myProperty' in obj")->Run()->BooleanValue());
  CHECK(v8_compile("delete obj.myProperty")->Run()->BooleanValue());
}


int echo_indexed_call_count = 0;


static v8::Handle<Value> EchoIndexedProperty(uint32_t index,
                                             const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(v8_num(637), info.Data());
  echo_indexed_call_count++;
  return v8_num(index);
}


THREADED_TEST(IndexedPropertyHandlerGetter) {
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->InstanceTemplate()->SetIndexedPropertyHandler(EchoIndexedProperty,
                                                       0, 0, 0, 0,
                                                       v8_num(637));
  LocalContext env;
  env->Global()->Set(v8_str("obj"),
                     templ->GetFunction()->NewInstance());
  Local<Script> script = v8_compile("obj[900]");
  CHECK_EQ(script->Run()->Int32Value(), 900);
}


v8::Handle<v8::Object> bottom;

static v8::Handle<Value> CheckThisIndexedPropertyHandler(
    uint32_t index,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<Value>();
}

static v8::Handle<Value> CheckThisNamedPropertyHandler(
    Local<String> name,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<Value>();
}


v8::Handle<Value> CheckThisIndexedPropertySetter(uint32_t index,
                                                 Local<Value> value,
                                                 const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<Value>();
}


v8::Handle<Value> CheckThisNamedPropertySetter(Local<String> property,
                                               Local<Value> value,
                                               const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<Value>();
}

v8::Handle<v8::Boolean> CheckThisIndexedPropertyQuery(
    uint32_t index,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Boolean>();
}


v8::Handle<v8::Boolean> CheckThisNamedPropertyQuery(Local<String> property,
                                                    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Boolean>();
}


v8::Handle<v8::Boolean> CheckThisIndexedPropertyDeleter(
    uint32_t index,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Boolean>();
}


v8::Handle<v8::Boolean> CheckThisNamedPropertyDeleter(
    Local<String> property,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Boolean>();
}


v8::Handle<v8::Array> CheckThisIndexedPropertyEnumerator(
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Array>();
}


v8::Handle<v8::Array> CheckThisNamedPropertyEnumerator(
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.This()->Equals(bottom));
  return v8::Handle<v8::Array>();
}


THREADED_TEST(PropertyHandlerInPrototype) {
  v8::HandleScope scope;
  LocalContext env;

  // Set up a prototype chain with three interceptors.
  v8::Handle<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->InstanceTemplate()->SetIndexedPropertyHandler(
      CheckThisIndexedPropertyHandler,
      CheckThisIndexedPropertySetter,
      CheckThisIndexedPropertyQuery,
      CheckThisIndexedPropertyDeleter,
      CheckThisIndexedPropertyEnumerator);

  templ->InstanceTemplate()->SetNamedPropertyHandler(
      CheckThisNamedPropertyHandler,
      CheckThisNamedPropertySetter,
      CheckThisNamedPropertyQuery,
      CheckThisNamedPropertyDeleter,
      CheckThisNamedPropertyEnumerator);

  bottom = templ->GetFunction()->NewInstance();
  Local<v8::Object> top = templ->GetFunction()->NewInstance();
  Local<v8::Object> middle = templ->GetFunction()->NewInstance();

  bottom->Set(v8_str("__proto__"), middle);
  middle->Set(v8_str("__proto__"), top);
  env->Global()->Set(v8_str("obj"), bottom);

  // Indexed and named get.
  Script::Compile(v8_str("obj[0]"))->Run();
  Script::Compile(v8_str("obj.x"))->Run();

  // Indexed and named set.
  Script::Compile(v8_str("obj[1] = 42"))->Run();
  Script::Compile(v8_str("obj.y = 42"))->Run();

  // Indexed and named query.
  Script::Compile(v8_str("0 in obj"))->Run();
  Script::Compile(v8_str("'x' in obj"))->Run();

  // Indexed and named deleter.
  Script::Compile(v8_str("delete obj[0]"))->Run();
  Script::Compile(v8_str("delete obj.x"))->Run();

  // Enumerators.
  Script::Compile(v8_str("for (var p in obj) ;"))->Run();
}


static v8::Handle<Value> PrePropertyHandlerGet(Local<String> key,
                                               const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  if (v8_str("pre")->Equals(key)) {
    return v8_str("PrePropertyHandler: pre");
  }
  return v8::Handle<String>();
}


static v8::Handle<v8::Boolean> PrePropertyHandlerHas(Local<String> key,
                                                     const AccessorInfo&) {
  if (v8_str("pre")->Equals(key)) {
    return v8::True();
  }

  return v8::Handle<v8::Boolean>();  // do not intercept the call
}


THREADED_TEST(PrePropertyHandler) {
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> desc = v8::FunctionTemplate::New();
  desc->InstanceTemplate()->SetNamedPropertyHandler(PrePropertyHandlerGet,
                                                    0,
                                                    PrePropertyHandlerHas);
  LocalContext env(NULL, desc->InstanceTemplate());
  Script::Compile(v8_str(
      "var pre = 'Object: pre'; var on = 'Object: on';"))->Run();
  v8::Handle<Value> result_pre = Script::Compile(v8_str("pre"))->Run();
  CHECK_EQ(v8_str("PrePropertyHandler: pre"), result_pre);
  v8::Handle<Value> result_on = Script::Compile(v8_str("on"))->Run();
  CHECK_EQ(v8_str("Object: on"), result_on);
  v8::Handle<Value> result_post = Script::Compile(v8_str("post"))->Run();
  CHECK(result_post.IsEmpty());
}


THREADED_TEST(UndefinedIsNotEnumerable) {
  v8::HandleScope scope;
  LocalContext env;
  v8::Handle<Value> result = Script::Compile(v8_str(
      "this.propertyIsEnumerable(undefined)"))->Run();
  CHECK(result->IsFalse());
}


v8::Handle<Script> call_recursively_script;
static const int kTargetRecursionDepth = 300;  // near maximum


static v8::Handle<Value> CallScriptRecursivelyCall(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  int depth = args.This()->Get(v8_str("depth"))->Int32Value();
  if (depth == kTargetRecursionDepth) return v8::Undefined();
  args.This()->Set(v8_str("depth"), v8::Integer::New(depth + 1));
  return call_recursively_script->Run();
}


static v8::Handle<Value> CallFunctionRecursivelyCall(
    const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  int depth = args.This()->Get(v8_str("depth"))->Int32Value();
  if (depth == kTargetRecursionDepth) {
    printf("[depth = %d]\n", depth);
    return v8::Undefined();
  }
  args.This()->Set(v8_str("depth"), v8::Integer::New(depth + 1));
  v8::Handle<Value> function =
      args.This()->Get(v8_str("callFunctionRecursively"));
  return v8::Handle<Function>::Cast(function)->Call(args.This(), 0, NULL);
}


THREADED_TEST(DeepCrossLanguageRecursion) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> global = ObjectTemplate::New();
  global->Set(v8_str("callScriptRecursively"),
              v8::FunctionTemplate::New(CallScriptRecursivelyCall));
  global->Set(v8_str("callFunctionRecursively"),
              v8::FunctionTemplate::New(CallFunctionRecursivelyCall));
  LocalContext env(NULL, global);

  env->Global()->Set(v8_str("depth"), v8::Integer::New(0));
  call_recursively_script = v8_compile("callScriptRecursively()");
  v8::Handle<Value> result = call_recursively_script->Run();
  call_recursively_script = v8::Handle<Script>();

  env->Global()->Set(v8_str("depth"), v8::Integer::New(0));
  Script::Compile(v8_str("callFunctionRecursively()"))->Run();
}


static v8::Handle<Value>
    ThrowingPropertyHandlerGet(Local<String> key, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8::ThrowException(key);
}


static v8::Handle<Value> ThrowingPropertyHandlerSet(Local<String> key,
                                                    Local<Value>,
                                                    const AccessorInfo&) {
  v8::ThrowException(key);
  return v8::Undefined();  // not the same as v8::Handle<v8::Value>()
}


THREADED_TEST(CallbackExceptionRegression) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetNamedPropertyHandler(ThrowingPropertyHandlerGet,
                               ThrowingPropertyHandlerSet);
  LocalContext env;
  env->Global()->Set(v8_str("obj"), obj->NewInstance());
  v8::Handle<Value> otto = Script::Compile(v8_str(
      "try { with (obj) { otto; } } catch (e) { e; }"))->Run();
  CHECK_EQ(v8_str("otto"), otto);
  v8::Handle<Value> netto = Script::Compile(v8_str(
      "try { with (obj) { netto = 4; } } catch (e) { e; }"))->Run();
  CHECK_EQ(v8_str("netto"), netto);
}


static v8::Handle<Value> ThrowingGetAccessor(Local<String> name,
                                             const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8::ThrowException(v8_str("g"));
}


static void ThrowingSetAccessor(Local<String> name,
                                Local<Value> value,
                                const AccessorInfo& info) {
  v8::ThrowException(value);
}


THREADED_TEST(Regress1054726) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetAccessor(v8_str("x"),
                   ThrowingGetAccessor,
                   ThrowingSetAccessor,
                   Local<Value>());

  LocalContext env;
  env->Global()->Set(v8_str("obj"), obj->NewInstance());

  // Use the throwing property setter/getter in a loop to force
  // the accessor ICs to be initialized.
  v8::Handle<Value> result;
  result = Script::Compile(v8_str(
      "var result = '';"
      "for (var i = 0; i < 5; i++) {"
      "  try { obj.x; } catch (e) { result += e; }"
      "}; result"))->Run();
  CHECK_EQ(v8_str("ggggg"), result);

  result = Script::Compile(String::New(
      "var result = '';"
      "for (var i = 0; i < 5; i++) {"
      "  try { obj.x = i; } catch (e) { result += e; }"
      "}; result"))->Run();
  CHECK_EQ(v8_str("01234"), result);
}


THREADED_TEST(FunctionPrototype) {
  v8::HandleScope scope;
  Local<v8::FunctionTemplate> Foo = v8::FunctionTemplate::New();
  Foo->PrototypeTemplate()->Set(v8_str("plak"), v8_num(321));
  LocalContext env;
  env->Global()->Set(v8_str("Foo"), Foo->GetFunction());
  Local<Script> script = Script::Compile(v8_str("Foo.prototype.plak"));
  CHECK_EQ(script->Run()->Int32Value(), 321);
}


THREADED_TEST(InternalFields) {
  v8::HandleScope scope;
  LocalContext env;

  Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  Local<v8::ObjectTemplate> instance_templ = templ->InstanceTemplate();
  instance_templ->SetInternalFieldCount(1);
  Local<v8::Object> obj = templ->GetFunction()->NewInstance();
  CHECK_EQ(1, obj->InternalFieldCount());
  CHECK(obj->GetInternalField(0)->IsUndefined());
  obj->SetInternalField(0, v8_num(17));
  CHECK_EQ(17, obj->GetInternalField(0)->Int32Value());
}


THREADED_TEST(IdentityHash) {
  v8::HandleScope scope;
  LocalContext env;

  // Ensure that the test starts with an fresh heap to test whether the hash
  // code is based on the address.
  i::Heap::CollectAllGarbage();
  Local<v8::Object> obj = v8::Object::New();
  int hash = obj->GetIdentityHash();
  int hash1 = obj->GetIdentityHash();
  CHECK_EQ(hash, hash1);
  int hash2 = v8::Object::New()->GetIdentityHash();
  // Since the identity hash is essentially a random number two consecutive
  // objects should not be assigned the same hash code. If the test below fails
  // the random number generator should be evaluated.
  CHECK_NE(hash, hash2);
  i::Heap::CollectAllGarbage();
  int hash3 = v8::Object::New()->GetIdentityHash();
  // Make sure that the identity hash is not based on the initial address of
  // the object alone. If the test below fails the random number generator
  // should be evaluated.
  CHECK_NE(hash, hash3);
  int hash4 = obj->GetIdentityHash();
  CHECK_EQ(hash, hash4);
}


THREADED_TEST(HiddenProperties) {
  v8::HandleScope scope;
  LocalContext env;

  v8::Local<v8::Object> obj = v8::Object::New();
  v8::Local<v8::String> key = v8_str("api-test::hidden-key");
  v8::Local<v8::String> empty = v8_str("");
  v8::Local<v8::String> prop_name = v8_str("prop_name");

  i::Heap::CollectAllGarbage();

  // Make sure delete of a non-existent hidden value works
  CHECK(obj->DeleteHiddenValue(key));

  CHECK(obj->SetHiddenValue(key, v8::Integer::New(1503)));
  CHECK_EQ(1503, obj->GetHiddenValue(key)->Int32Value());
  CHECK(obj->SetHiddenValue(key, v8::Integer::New(2002)));
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());

  i::Heap::CollectAllGarbage();

  // Make sure we do not find the hidden property.
  CHECK(!obj->Has(empty));
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());
  CHECK(obj->Get(empty)->IsUndefined());
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());
  CHECK(obj->Set(empty, v8::Integer::New(2003)));
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());
  CHECK_EQ(2003, obj->Get(empty)->Int32Value());

  i::Heap::CollectAllGarbage();

  // Add another property and delete it afterwards to force the object in
  // slow case.
  CHECK(obj->Set(prop_name, v8::Integer::New(2008)));
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());
  CHECK_EQ(2008, obj->Get(prop_name)->Int32Value());
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());
  CHECK(obj->Delete(prop_name));
  CHECK_EQ(2002, obj->GetHiddenValue(key)->Int32Value());

  i::Heap::CollectAllGarbage();

  CHECK(obj->DeleteHiddenValue(key));
  CHECK(obj->GetHiddenValue(key).IsEmpty());
}


THREADED_TEST(External) {
  v8::HandleScope scope;
  int x = 3;
  Local<v8::External> ext = v8::External::New(&x);
  LocalContext env;
  env->Global()->Set(v8_str("ext"), ext);
  Local<Value> reext_obj = Script::Compile(v8_str("this.ext"))->Run();
  v8::Handle<v8::External> reext = v8::Handle<v8::External>::Cast(reext_obj);
  int* ptr = static_cast<int*>(reext->Value());
  CHECK_EQ(x, 3);
  *ptr = 10;
  CHECK_EQ(x, 10);

  // Make sure unaligned pointers are wrapped properly.
  char* data = i::StrDup("0123456789");
  Local<v8::Value> zero = v8::External::Wrap(&data[0]);
  Local<v8::Value> one = v8::External::Wrap(&data[1]);
  Local<v8::Value> two = v8::External::Wrap(&data[2]);
  Local<v8::Value> three = v8::External::Wrap(&data[3]);

  char* char_ptr = reinterpret_cast<char*>(v8::External::Unwrap(zero));
  CHECK_EQ('0', *char_ptr);
  char_ptr = reinterpret_cast<char*>(v8::External::Unwrap(one));
  CHECK_EQ('1', *char_ptr);
  char_ptr = reinterpret_cast<char*>(v8::External::Unwrap(two));
  CHECK_EQ('2', *char_ptr);
  char_ptr = reinterpret_cast<char*>(v8::External::Unwrap(three));
  CHECK_EQ('3', *char_ptr);
  i::DeleteArray(data);
}


THREADED_TEST(GlobalHandle) {
  v8::Persistent<String> global;
  {
    v8::HandleScope scope;
    Local<String> str = v8_str("str");
    global = v8::Persistent<String>::New(str);
  }
  CHECK_EQ(global->Length(), 3);
  global.Dispose();
}


THREADED_TEST(ScriptException) {
  v8::HandleScope scope;
  LocalContext env;
  Local<Script> script = Script::Compile(v8_str("throw 'panama!';"));
  v8::TryCatch try_catch;
  Local<Value> result = script->Run();
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  String::AsciiValue exception_value(try_catch.Exception());
  CHECK_EQ(*exception_value, "panama!");
}


bool message_received;


static void check_message(v8::Handle<v8::Message> message,
                          v8::Handle<Value> data) {
  CHECK_EQ(5.76, data->NumberValue());
  CHECK_EQ(6.75, message->GetScriptResourceName()->NumberValue());
  message_received = true;
}


THREADED_TEST(MessageHandlerData) {
  message_received = false;
  v8::HandleScope scope;
  CHECK(!message_received);
  v8::V8::AddMessageListener(check_message, v8_num(5.76));
  LocalContext context;
  v8::ScriptOrigin origin =
      v8::ScriptOrigin(v8_str("6.75"));
  Script::Compile(v8_str("throw 'error'"), &origin)->Run();
  CHECK(message_received);
  // clear out the message listener
  v8::V8::RemoveMessageListeners(check_message);
}


THREADED_TEST(GetSetProperty) {
  v8::HandleScope scope;
  LocalContext context;
  context->Global()->Set(v8_str("foo"), v8_num(14));
  context->Global()->Set(v8_str("12"), v8_num(92));
  context->Global()->Set(v8::Integer::New(16), v8_num(32));
  context->Global()->Set(v8_num(13), v8_num(56));
  Local<Value> foo = Script::Compile(v8_str("this.foo"))->Run();
  CHECK_EQ(14, foo->Int32Value());
  Local<Value> twelve = Script::Compile(v8_str("this[12]"))->Run();
  CHECK_EQ(92, twelve->Int32Value());
  Local<Value> sixteen = Script::Compile(v8_str("this[16]"))->Run();
  CHECK_EQ(32, sixteen->Int32Value());
  Local<Value> thirteen = Script::Compile(v8_str("this[13]"))->Run();
  CHECK_EQ(56, thirteen->Int32Value());
  CHECK_EQ(92, context->Global()->Get(v8::Integer::New(12))->Int32Value());
  CHECK_EQ(92, context->Global()->Get(v8_str("12"))->Int32Value());
  CHECK_EQ(92, context->Global()->Get(v8_num(12))->Int32Value());
  CHECK_EQ(32, context->Global()->Get(v8::Integer::New(16))->Int32Value());
  CHECK_EQ(32, context->Global()->Get(v8_str("16"))->Int32Value());
  CHECK_EQ(32, context->Global()->Get(v8_num(16))->Int32Value());
  CHECK_EQ(56, context->Global()->Get(v8::Integer::New(13))->Int32Value());
  CHECK_EQ(56, context->Global()->Get(v8_str("13"))->Int32Value());
  CHECK_EQ(56, context->Global()->Get(v8_num(13))->Int32Value());
}


THREADED_TEST(PropertyAttributes) {
  v8::HandleScope scope;
  LocalContext context;
  // read-only
  Local<String> prop = v8_str("read_only");
  context->Global()->Set(prop, v8_num(7), v8::ReadOnly);
  CHECK_EQ(7, context->Global()->Get(prop)->Int32Value());
  Script::Compile(v8_str("read_only = 9"))->Run();
  CHECK_EQ(7, context->Global()->Get(prop)->Int32Value());
  context->Global()->Set(prop, v8_num(10));
  CHECK_EQ(7, context->Global()->Get(prop)->Int32Value());
  // dont-delete
  prop = v8_str("dont_delete");
  context->Global()->Set(prop, v8_num(13), v8::DontDelete);
  CHECK_EQ(13, context->Global()->Get(prop)->Int32Value());
  Script::Compile(v8_str("delete dont_delete"))->Run();
  CHECK_EQ(13, context->Global()->Get(prop)->Int32Value());
}


THREADED_TEST(Array) {
  v8::HandleScope scope;
  LocalContext context;
  Local<v8::Array> array = v8::Array::New();
  CHECK_EQ(0, array->Length());
  CHECK(array->Get(v8::Integer::New(0))->IsUndefined());
  CHECK(!array->Has(0));
  CHECK(array->Get(v8::Integer::New(100))->IsUndefined());
  CHECK(!array->Has(100));
  array->Set(v8::Integer::New(2), v8_num(7));
  CHECK_EQ(3, array->Length());
  CHECK(!array->Has(0));
  CHECK(!array->Has(1));
  CHECK(array->Has(2));
  CHECK_EQ(7, array->Get(v8::Integer::New(2))->Int32Value());
  Local<Value> obj = Script::Compile(v8_str("[1, 2, 3]"))->Run();
  Local<v8::Array> arr = Local<v8::Array>::Cast(obj);
  CHECK_EQ(3, arr->Length());
  CHECK_EQ(1, arr->Get(v8::Integer::New(0))->Int32Value());
  CHECK_EQ(2, arr->Get(v8::Integer::New(1))->Int32Value());
  CHECK_EQ(3, arr->Get(v8::Integer::New(2))->Int32Value());
}


v8::Handle<Value> HandleF(const v8::Arguments& args) {
  v8::HandleScope scope;
  ApiTestFuzzer::Fuzz();
  Local<v8::Array> result = v8::Array::New(args.Length());
  for (int i = 0; i < args.Length(); i++)
    result->Set(v8::Integer::New(i), args[i]);
  return scope.Close(result);
}


THREADED_TEST(Vector) {
  v8::HandleScope scope;
  Local<ObjectTemplate> global = ObjectTemplate::New();
  global->Set(v8_str("f"), v8::FunctionTemplate::New(HandleF));
  LocalContext context(0, global);

  const char* fun = "f()";
  Local<v8::Array> a0 =
      Local<v8::Array>::Cast(Script::Compile(String::New(fun))->Run());
  CHECK_EQ(0, a0->Length());

  const char* fun2 = "f(11)";
  Local<v8::Array> a1 =
      Local<v8::Array>::Cast(Script::Compile(String::New(fun2))->Run());
  CHECK_EQ(1, a1->Length());
  CHECK_EQ(11, a1->Get(v8::Integer::New(0))->Int32Value());

  const char* fun3 = "f(12, 13)";
  Local<v8::Array> a2 =
      Local<v8::Array>::Cast(Script::Compile(String::New(fun3))->Run());
  CHECK_EQ(2, a2->Length());
  CHECK_EQ(12, a2->Get(v8::Integer::New(0))->Int32Value());
  CHECK_EQ(13, a2->Get(v8::Integer::New(1))->Int32Value());

  const char* fun4 = "f(14, 15, 16)";
  Local<v8::Array> a3 =
      Local<v8::Array>::Cast(Script::Compile(String::New(fun4))->Run());
  CHECK_EQ(3, a3->Length());
  CHECK_EQ(14, a3->Get(v8::Integer::New(0))->Int32Value());
  CHECK_EQ(15, a3->Get(v8::Integer::New(1))->Int32Value());
  CHECK_EQ(16, a3->Get(v8::Integer::New(2))->Int32Value());

  const char* fun5 = "f(17, 18, 19, 20)";
  Local<v8::Array> a4 =
      Local<v8::Array>::Cast(Script::Compile(String::New(fun5))->Run());
  CHECK_EQ(4, a4->Length());
  CHECK_EQ(17, a4->Get(v8::Integer::New(0))->Int32Value());
  CHECK_EQ(18, a4->Get(v8::Integer::New(1))->Int32Value());
  CHECK_EQ(19, a4->Get(v8::Integer::New(2))->Int32Value());
  CHECK_EQ(20, a4->Get(v8::Integer::New(3))->Int32Value());
}


THREADED_TEST(FunctionCall) {
  v8::HandleScope scope;
  LocalContext context;
  CompileRun(
    "function Foo() {"
    "  var result = [];"
    "  for (var i = 0; i < arguments.length; i++) {"
    "    result.push(arguments[i]);"
    "  }"
    "  return result;"
    "}");
  Local<Function> Foo =
      Local<Function>::Cast(context->Global()->Get(v8_str("Foo")));

  v8::Handle<Value>* args0 = NULL;
  Local<v8::Array> a0 = Local<v8::Array>::Cast(Foo->Call(Foo, 0, args0));
  CHECK_EQ(0, a0->Length());

  v8::Handle<Value> args1[] = { v8_num(1.1) };
  Local<v8::Array> a1 = Local<v8::Array>::Cast(Foo->Call(Foo, 1, args1));
  CHECK_EQ(1, a1->Length());
  CHECK_EQ(1.1, a1->Get(v8::Integer::New(0))->NumberValue());

  v8::Handle<Value> args2[] = { v8_num(2.2),
                                v8_num(3.3) };
  Local<v8::Array> a2 = Local<v8::Array>::Cast(Foo->Call(Foo, 2, args2));
  CHECK_EQ(2, a2->Length());
  CHECK_EQ(2.2, a2->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(3.3, a2->Get(v8::Integer::New(1))->NumberValue());

  v8::Handle<Value> args3[] = { v8_num(4.4),
                                v8_num(5.5),
                                v8_num(6.6) };
  Local<v8::Array> a3 = Local<v8::Array>::Cast(Foo->Call(Foo, 3, args3));
  CHECK_EQ(3, a3->Length());
  CHECK_EQ(4.4, a3->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(5.5, a3->Get(v8::Integer::New(1))->NumberValue());
  CHECK_EQ(6.6, a3->Get(v8::Integer::New(2))->NumberValue());

  v8::Handle<Value> args4[] = { v8_num(7.7),
                                v8_num(8.8),
                                v8_num(9.9),
                                v8_num(10.11) };
  Local<v8::Array> a4 = Local<v8::Array>::Cast(Foo->Call(Foo, 4, args4));
  CHECK_EQ(4, a4->Length());
  CHECK_EQ(7.7, a4->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(8.8, a4->Get(v8::Integer::New(1))->NumberValue());
  CHECK_EQ(9.9, a4->Get(v8::Integer::New(2))->NumberValue());
  CHECK_EQ(10.11, a4->Get(v8::Integer::New(3))->NumberValue());
}


static const char* js_code_causing_out_of_memory =
    "var a = new Array(); while(true) a.push(a);";


// These tests run for a long time and prevent us from running tests
// that come after them so they cannot run in parallel.
TEST(OutOfMemory) {
  // It's not possible to read a snapshot into a heap with different dimensions.
  if (v8::internal::Snapshot::IsEnabled()) return;
  // Set heap limits.
  static const int K = 1024;
  v8::ResourceConstraints constraints;
  constraints.set_max_young_space_size(256 * K);
  constraints.set_max_old_space_size(4 * K * K);
  v8::SetResourceConstraints(&constraints);

  // Execute a script that causes out of memory.
  v8::HandleScope scope;
  LocalContext context;
  v8::V8::IgnoreOutOfMemoryException();
  Local<Script> script =
      Script::Compile(String::New(js_code_causing_out_of_memory));
  Local<Value> result = script->Run();

  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


v8::Handle<Value> ProvokeOutOfMemory(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();

  v8::HandleScope scope;
  LocalContext context;
  Local<Script> script =
      Script::Compile(String::New(js_code_causing_out_of_memory));
  Local<Value> result = script->Run();

  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());

  return result;
}


TEST(OutOfMemoryNested) {
  // It's not possible to read a snapshot into a heap with different dimensions.
  if (v8::internal::Snapshot::IsEnabled()) return;
  // Set heap limits.
  static const int K = 1024;
  v8::ResourceConstraints constraints;
  constraints.set_max_young_space_size(256 * K);
  constraints.set_max_old_space_size(4 * K * K);
  v8::SetResourceConstraints(&constraints);

  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ProvokeOutOfMemory"),
             v8::FunctionTemplate::New(ProvokeOutOfMemory));
  LocalContext context(0, templ);
  v8::V8::IgnoreOutOfMemoryException();
  Local<Value> result = CompileRun(
    "var thrown = false;"
    "try {"
    "  ProvokeOutOfMemory();"
    "} catch (e) {"
    "  thrown = true;"
    "}");
  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


TEST(HugeConsStringOutOfMemory) {
  // It's not possible to read a snapshot into a heap with different dimensions.
  if (v8::internal::Snapshot::IsEnabled()) return;
  v8::HandleScope scope;
  LocalContext context;
  // Set heap limits.
  static const int K = 1024;
  v8::ResourceConstraints constraints;
  constraints.set_max_young_space_size(256 * K);
  constraints.set_max_old_space_size(2 * K * K);
  v8::SetResourceConstraints(&constraints);

  // Execute a script that causes out of memory.
  v8::V8::IgnoreOutOfMemoryException();

  // Build huge string. This should fail with out of memory exception.
  Local<Value> result = CompileRun(
    "var str = Array.prototype.join.call({length: 513}, \"A\").toUpperCase();"
    "for (var i = 0; i < 21; i++) { str = str + str; }");

  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


THREADED_TEST(ConstructCall) {
  v8::HandleScope scope;
  LocalContext context;
  CompileRun(
    "function Foo() {"
    "  var result = [];"
    "  for (var i = 0; i < arguments.length; i++) {"
    "    result.push(arguments[i]);"
    "  }"
    "  return result;"
    "}");
  Local<Function> Foo =
      Local<Function>::Cast(context->Global()->Get(v8_str("Foo")));

  v8::Handle<Value>* args0 = NULL;
  Local<v8::Array> a0 = Local<v8::Array>::Cast(Foo->NewInstance(0, args0));
  CHECK_EQ(0, a0->Length());

  v8::Handle<Value> args1[] = { v8_num(1.1) };
  Local<v8::Array> a1 = Local<v8::Array>::Cast(Foo->NewInstance(1, args1));
  CHECK_EQ(1, a1->Length());
  CHECK_EQ(1.1, a1->Get(v8::Integer::New(0))->NumberValue());

  v8::Handle<Value> args2[] = { v8_num(2.2),
                                v8_num(3.3) };
  Local<v8::Array> a2 = Local<v8::Array>::Cast(Foo->NewInstance(2, args2));
  CHECK_EQ(2, a2->Length());
  CHECK_EQ(2.2, a2->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(3.3, a2->Get(v8::Integer::New(1))->NumberValue());

  v8::Handle<Value> args3[] = { v8_num(4.4),
                                v8_num(5.5),
                                v8_num(6.6) };
  Local<v8::Array> a3 = Local<v8::Array>::Cast(Foo->NewInstance(3, args3));
  CHECK_EQ(3, a3->Length());
  CHECK_EQ(4.4, a3->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(5.5, a3->Get(v8::Integer::New(1))->NumberValue());
  CHECK_EQ(6.6, a3->Get(v8::Integer::New(2))->NumberValue());

  v8::Handle<Value> args4[] = { v8_num(7.7),
                                v8_num(8.8),
                                v8_num(9.9),
                                v8_num(10.11) };
  Local<v8::Array> a4 = Local<v8::Array>::Cast(Foo->NewInstance(4, args4));
  CHECK_EQ(4, a4->Length());
  CHECK_EQ(7.7, a4->Get(v8::Integer::New(0))->NumberValue());
  CHECK_EQ(8.8, a4->Get(v8::Integer::New(1))->NumberValue());
  CHECK_EQ(9.9, a4->Get(v8::Integer::New(2))->NumberValue());
  CHECK_EQ(10.11, a4->Get(v8::Integer::New(3))->NumberValue());
}


static void CheckUncle(v8::TryCatch* try_catch) {
  CHECK(try_catch->HasCaught());
  String::AsciiValue str_value(try_catch->Exception());
  CHECK_EQ(*str_value, "uncle?");
  try_catch->Reset();
}


THREADED_TEST(ConversionException) {
  v8::HandleScope scope;
  LocalContext env;
  CompileRun(
    "function TestClass() { };"
    "TestClass.prototype.toString = function () { throw 'uncle?'; };"
    "var obj = new TestClass();");
  Local<Value> obj = env->Global()->Get(v8_str("obj"));

  v8::TryCatch try_catch;

  Local<Value> to_string_result = obj->ToString();
  CHECK(to_string_result.IsEmpty());
  CheckUncle(&try_catch);

  Local<Value> to_number_result = obj->ToNumber();
  CHECK(to_number_result.IsEmpty());
  CheckUncle(&try_catch);

  Local<Value> to_integer_result = obj->ToInteger();
  CHECK(to_integer_result.IsEmpty());
  CheckUncle(&try_catch);

  Local<Value> to_uint32_result = obj->ToUint32();
  CHECK(to_uint32_result.IsEmpty());
  CheckUncle(&try_catch);

  Local<Value> to_int32_result = obj->ToInt32();
  CHECK(to_int32_result.IsEmpty());
  CheckUncle(&try_catch);

  Local<Value> to_object_result = v8::Undefined()->ToObject();
  CHECK(to_object_result.IsEmpty());
  CHECK(try_catch.HasCaught());
  try_catch.Reset();

  int32_t int32_value = obj->Int32Value();
  CHECK_EQ(0, int32_value);
  CheckUncle(&try_catch);

  uint32_t uint32_value = obj->Uint32Value();
  CHECK_EQ(0, uint32_value);
  CheckUncle(&try_catch);

  double number_value = obj->NumberValue();
  CHECK_NE(0, IsNaN(number_value));
  CheckUncle(&try_catch);

  int64_t integer_value = obj->IntegerValue();
  CHECK_EQ(0.0, static_cast<double>(integer_value));
  CheckUncle(&try_catch);
}


v8::Handle<Value> ThrowFromC(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8::ThrowException(v8_str("konto"));
}


v8::Handle<Value> CCatcher(const v8::Arguments& args) {
  if (args.Length() < 1) return v8::Boolean::New(false);
  v8::HandleScope scope;
  v8::TryCatch try_catch;
  Local<Value> result = v8::Script::Compile(args[0]->ToString())->Run();
  CHECK(!try_catch.HasCaught() || result.IsEmpty());
  return v8::Boolean::New(try_catch.HasCaught());
}


THREADED_TEST(APICatch) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ThrowFromC"),
             v8::FunctionTemplate::New(ThrowFromC));
  LocalContext context(0, templ);
  CompileRun(
    "var thrown = false;"
    "try {"
    "  ThrowFromC();"
    "} catch (e) {"
    "  thrown = true;"
    "}");
  Local<Value> thrown = context->Global()->Get(v8_str("thrown"));
  CHECK(thrown->BooleanValue());
}


THREADED_TEST(APIThrowTryCatch) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ThrowFromC"),
             v8::FunctionTemplate::New(ThrowFromC));
  LocalContext context(0, templ);
  v8::TryCatch try_catch;
  CompileRun("ThrowFromC();");
  CHECK(try_catch.HasCaught());
}


// Test that a try-finally block doesn't shadow a try-catch block
// when setting up an external handler.
//
// BUG(271): Some of the exception propagation does not work on the
// ARM simulator because the simulator separates the C++ stack and the
// JS stack.  This test therefore fails on the simulator.  The test is
// not threaded to allow the threading tests to run on the simulator.
TEST(TryCatchInTryFinally) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("CCatcher"),
             v8::FunctionTemplate::New(CCatcher));
  LocalContext context(0, templ);
  Local<Value> result = CompileRun("try {"
                                   "  try {"
                                   "    CCatcher('throw 7;');"
                                   "  } finally {"
                                   "  }"
                                   "} catch (e) {"
                                   "}");
  CHECK(result->IsTrue());
}


static void receive_message(v8::Handle<v8::Message> message,
                            v8::Handle<v8::Value> data) {
  message->Get();
  message_received = true;
}


TEST(APIThrowMessage) {
  message_received = false;
  v8::HandleScope scope;
  v8::V8::AddMessageListener(receive_message);
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ThrowFromC"),
             v8::FunctionTemplate::New(ThrowFromC));
  LocalContext context(0, templ);
  CompileRun("ThrowFromC();");
  CHECK(message_received);
  v8::V8::RemoveMessageListeners(check_message);
}


TEST(APIThrowMessageAndVerboseTryCatch) {
  message_received = false;
  v8::HandleScope scope;
  v8::V8::AddMessageListener(receive_message);
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ThrowFromC"),
             v8::FunctionTemplate::New(ThrowFromC));
  LocalContext context(0, templ);
  v8::TryCatch try_catch;
  try_catch.SetVerbose(true);
  Local<Value> result = CompileRun("ThrowFromC();");
  CHECK(try_catch.HasCaught());
  CHECK(result.IsEmpty());
  CHECK(message_received);
  v8::V8::RemoveMessageListeners(check_message);
}


THREADED_TEST(ExternalScriptException) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("ThrowFromC"),
             v8::FunctionTemplate::New(ThrowFromC));
  LocalContext context(0, templ);

  v8::TryCatch try_catch;
  Local<Script> script
      = Script::Compile(v8_str("ThrowFromC(); throw 'panama';"));
  Local<Value> result = script->Run();
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  String::AsciiValue exception_value(try_catch.Exception());
  CHECK_EQ("konto", *exception_value);
}



v8::Handle<Value> CThrowCountDown(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(4, args.Length());
  int count = args[0]->Int32Value();
  int cInterval = args[2]->Int32Value();
  if (count == 0) {
    return v8::ThrowException(v8_str("FromC"));
  } else {
    Local<v8::Object> global = Context::GetCurrent()->Global();
    Local<Value> fun = global->Get(v8_str("JSThrowCountDown"));
    v8::Handle<Value> argv[] = { v8_num(count - 1),
                                 args[1],
                                 args[2],
                                 args[3] };
    if (count % cInterval == 0) {
      v8::TryCatch try_catch;
      Local<Value> result =
          v8::Handle<Function>::Cast(fun)->Call(global, 4, argv);
      int expected = args[3]->Int32Value();
      if (try_catch.HasCaught()) {
        CHECK_EQ(expected, count);
        CHECK(result.IsEmpty());
        CHECK(!i::Top::has_scheduled_exception());
      } else {
        CHECK_NE(expected, count);
      }
      return result;
    } else {
      return v8::Handle<Function>::Cast(fun)->Call(global, 4, argv);
    }
  }
}


v8::Handle<Value> JSCheck(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(3, args.Length());
  bool equality = args[0]->BooleanValue();
  int count = args[1]->Int32Value();
  int expected = args[2]->Int32Value();
  if (equality) {
    CHECK_EQ(count, expected);
  } else {
    CHECK_NE(count, expected);
  }
  return v8::Undefined();
}


THREADED_TEST(EvalInTryFinally) {
  v8::HandleScope scope;
  LocalContext context;
  v8::TryCatch try_catch;
  CompileRun("(function() {"
             "  try {"
             "    eval('asldkf (*&^&*^');"
             "  } finally {"
             "    return;"
             "  }"
             "})()");
  CHECK(!try_catch.HasCaught());
}


// This test works by making a stack of alternating JavaScript and C
// activations.  These activations set up exception handlers with regular
// intervals, one interval for C activations and another for JavaScript
// activations.  When enough activations have been created an exception is
// thrown and we check that the right activation catches the exception and that
// no other activations do.  The right activation is always the topmost one with
// a handler, regardless of whether it is in JavaScript or C.
//
// The notation used to describe a test case looks like this:
//
//    *JS[4] *C[3] @JS[2] C[1] JS[0]
//
// Each entry is an activation, either JS or C.  The index is the count at that
// level.  Stars identify activations with exception handlers, the @ identifies
// the exception handler that should catch the exception.
//
// BUG(271): Some of the exception propagation does not work on the
// ARM simulator because the simulator separates the C++ stack and the
// JS stack.  This test therefore fails on the simulator.  The test is
// not threaded to allow the threading tests to run on the simulator.
TEST(ExceptionOrder) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("check"), v8::FunctionTemplate::New(JSCheck));
  templ->Set(v8_str("CThrowCountDown"),
             v8::FunctionTemplate::New(CThrowCountDown));
  LocalContext context(0, templ);
  CompileRun(
    "function JSThrowCountDown(count, jsInterval, cInterval, expected) {"
    "  if (count == 0) throw 'FromJS';"
    "  if (count % jsInterval == 0) {"
    "    try {"
    "      var value = CThrowCountDown(count - 1,"
    "                                  jsInterval,"
    "                                  cInterval,"
    "                                  expected);"
    "      check(false, count, expected);"
    "      return value;"
    "    } catch (e) {"
    "      check(true, count, expected);"
    "    }"
    "  } else {"
    "    return CThrowCountDown(count - 1, jsInterval, cInterval, expected);"
    "  }"
    "}");
  Local<Function> fun =
      Local<Function>::Cast(context->Global()->Get(v8_str("JSThrowCountDown")));

  const int argc = 4;
  //                             count      jsInterval cInterval  expected

  // *JS[4] *C[3] @JS[2] C[1] JS[0]
  v8::Handle<Value> a0[argc] = { v8_num(4), v8_num(2), v8_num(3), v8_num(2) };
  fun->Call(fun, argc, a0);

  // JS[5] *C[4] JS[3] @C[2] JS[1] C[0]
  v8::Handle<Value> a1[argc] = { v8_num(5), v8_num(6), v8_num(1), v8_num(2) };
  fun->Call(fun, argc, a1);

  // JS[6] @C[5] JS[4] C[3] JS[2] C[1] JS[0]
  v8::Handle<Value> a2[argc] = { v8_num(6), v8_num(7), v8_num(5), v8_num(5) };
  fun->Call(fun, argc, a2);

  // @JS[6] C[5] JS[4] C[3] JS[2] C[1] JS[0]
  v8::Handle<Value> a3[argc] = { v8_num(6), v8_num(6), v8_num(7), v8_num(6) };
  fun->Call(fun, argc, a3);

  // JS[6] *C[5] @JS[4] C[3] JS[2] C[1] JS[0]
  v8::Handle<Value> a4[argc] = { v8_num(6), v8_num(4), v8_num(5), v8_num(4) };
  fun->Call(fun, argc, a4);

  // JS[6] C[5] *JS[4] @C[3] JS[2] C[1] JS[0]
  v8::Handle<Value> a5[argc] = { v8_num(6), v8_num(4), v8_num(3), v8_num(3) };
  fun->Call(fun, argc, a5);
}


v8::Handle<Value> ThrowValue(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(1, args.Length());
  return v8::ThrowException(args[0]);
}


THREADED_TEST(ThrowValues) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("Throw"), v8::FunctionTemplate::New(ThrowValue));
  LocalContext context(0, templ);
  v8::Handle<v8::Array> result = v8::Handle<v8::Array>::Cast(CompileRun(
    "function Run(obj) {"
    "  try {"
    "    Throw(obj);"
    "  } catch (e) {"
    "    return e;"
    "  }"
    "  return 'no exception';"
    "}"
    "[Run('str'), Run(1), Run(0), Run(null), Run(void 0)];"));
  CHECK_EQ(5, result->Length());
  CHECK(result->Get(v8::Integer::New(0))->IsString());
  CHECK(result->Get(v8::Integer::New(1))->IsNumber());
  CHECK_EQ(1, result->Get(v8::Integer::New(1))->Int32Value());
  CHECK(result->Get(v8::Integer::New(2))->IsNumber());
  CHECK_EQ(0, result->Get(v8::Integer::New(2))->Int32Value());
  CHECK(result->Get(v8::Integer::New(3))->IsNull());
  CHECK(result->Get(v8::Integer::New(4))->IsUndefined());
}


THREADED_TEST(CatchZero) {
  v8::HandleScope scope;
  LocalContext context;
  v8::TryCatch try_catch;
  CHECK(!try_catch.HasCaught());
  Script::Compile(v8_str("throw 10"))->Run();
  CHECK(try_catch.HasCaught());
  CHECK_EQ(10, try_catch.Exception()->Int32Value());
  try_catch.Reset();
  CHECK(!try_catch.HasCaught());
  Script::Compile(v8_str("throw 0"))->Run();
  CHECK(try_catch.HasCaught());
  CHECK_EQ(0, try_catch.Exception()->Int32Value());
}


THREADED_TEST(CatchExceptionFromWith) {
  v8::HandleScope scope;
  LocalContext context;
  v8::TryCatch try_catch;
  CHECK(!try_catch.HasCaught());
  Script::Compile(v8_str("var o = {}; with (o) { throw 42; }"))->Run();
  CHECK(try_catch.HasCaught());
}


THREADED_TEST(Equality) {
  v8::HandleScope scope;
  LocalContext context;
  // Check that equality works at all before relying on CHECK_EQ
  CHECK(v8_str("a")->Equals(v8_str("a")));
  CHECK(!v8_str("a")->Equals(v8_str("b")));

  CHECK_EQ(v8_str("a"), v8_str("a"));
  CHECK_NE(v8_str("a"), v8_str("b"));
  CHECK_EQ(v8_num(1), v8_num(1));
  CHECK_EQ(v8_num(1.00), v8_num(1));
  CHECK_NE(v8_num(1), v8_num(2));

  // Assume String is not symbol.
  CHECK(v8_str("a")->StrictEquals(v8_str("a")));
  CHECK(!v8_str("a")->StrictEquals(v8_str("b")));
  CHECK(!v8_str("5")->StrictEquals(v8_num(5)));
  CHECK(v8_num(1)->StrictEquals(v8_num(1)));
  CHECK(!v8_num(1)->StrictEquals(v8_num(2)));
  CHECK(v8_num(0)->StrictEquals(v8_num(-0)));
  Local<Value> not_a_number = v8_num(i::OS::nan_value());
  CHECK(!not_a_number->StrictEquals(not_a_number));
  CHECK(v8::False()->StrictEquals(v8::False()));
  CHECK(!v8::False()->StrictEquals(v8::Undefined()));

  v8::Handle<v8::Object> obj = v8::Object::New();
  v8::Persistent<v8::Object> alias = v8::Persistent<v8::Object>::New(obj);
  CHECK(alias->StrictEquals(obj));
  alias.Dispose();
}


THREADED_TEST(MultiRun) {
  v8::HandleScope scope;
  LocalContext context;
  Local<Script> script = Script::Compile(v8_str("x"));
  for (int i = 0; i < 10; i++)
    script->Run();
}


static v8::Handle<Value> GetXValue(Local<String> name,
                                   const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(info.Data(), v8_str("donut"));
  CHECK_EQ(name, v8_str("x"));
  return name;
}


THREADED_TEST(SimplePropertyRead) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessor(v8_str("x"), GetXValue, NULL, v8_str("donut"));
  LocalContext context;
  context->Global()->Set(v8_str("obj"), templ->NewInstance());
  Local<Script> script = Script::Compile(v8_str("obj.x"));
  for (int i = 0; i < 10; i++) {
    Local<Value> result = script->Run();
    CHECK_EQ(result, v8_str("x"));
  }
}


v8::Persistent<Value> xValue;


static void SetXValue(Local<String> name,
                      Local<Value> value,
                      const AccessorInfo& info) {
  CHECK_EQ(value, v8_num(4));
  CHECK_EQ(info.Data(), v8_str("donut"));
  CHECK_EQ(name, v8_str("x"));
  CHECK(xValue.IsEmpty());
  xValue = v8::Persistent<Value>::New(value);
}


THREADED_TEST(SimplePropertyWrite) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessor(v8_str("x"), GetXValue, SetXValue, v8_str("donut"));
  LocalContext context;
  context->Global()->Set(v8_str("obj"), templ->NewInstance());
  Local<Script> script = Script::Compile(v8_str("obj.x = 4"));
  for (int i = 0; i < 10; i++) {
    CHECK(xValue.IsEmpty());
    script->Run();
    CHECK_EQ(v8_num(4), xValue);
    xValue.Dispose();
    xValue = v8::Persistent<Value>();
  }
}


static v8::Handle<Value> XPropertyGetter(Local<String> property,
                                         const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(info.Data()->IsUndefined());
  return property;
}


THREADED_TEST(NamedInterceporPropertyRead) {
  v8::HandleScope scope;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(XPropertyGetter);
  LocalContext context;
  context->Global()->Set(v8_str("obj"), templ->NewInstance());
  Local<Script> script = Script::Compile(v8_str("obj.x"));
  for (int i = 0; i < 10; i++) {
    Local<Value> result = script->Run();
    CHECK_EQ(result, v8_str("x"));
  }
}

THREADED_TEST(MultiContexts) {
  v8::HandleScope scope;
  v8::Handle<ObjectTemplate> templ = ObjectTemplate::New();
  templ->Set(v8_str("dummy"), v8::FunctionTemplate::New(DummyCallHandler));

  Local<String> password = v8_str("Password");

  // Create an environment
  LocalContext context0(0, templ);
  context0->SetSecurityToken(password);
  v8::Handle<v8::Object> global0 = context0->Global();
  global0->Set(v8_str("custom"), v8_num(1234));
  CHECK_EQ(1234, global0->Get(v8_str("custom"))->Int32Value());

  // Create an independent environment
  LocalContext context1(0, templ);
  context1->SetSecurityToken(password);
  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("custom"), v8_num(1234));
  CHECK_NE(global0, global1);
  CHECK_EQ(1234, global0->Get(v8_str("custom"))->Int32Value());
  CHECK_EQ(1234, global1->Get(v8_str("custom"))->Int32Value());

  // Now create a new context with the old global
  LocalContext context2(0, templ, global1);
  context2->SetSecurityToken(password);
  v8::Handle<v8::Object> global2 = context2->Global();
  CHECK_EQ(global1, global2);
  CHECK_EQ(0, global1->Get(v8_str("custom"))->Int32Value());
  CHECK_EQ(0, global2->Get(v8_str("custom"))->Int32Value());
}


THREADED_TEST(FunctionPrototypeAcrossContexts) {
  // Make sure that functions created by cloning boilerplates cannot
  // communicate through their __proto__ field.

  v8::HandleScope scope;

  LocalContext env0;
  v8::Handle<v8::Object> global0 =
      env0->Global();
  v8::Handle<v8::Object> object0 =
      v8::Handle<v8::Object>::Cast(global0->Get(v8_str("Object")));
  v8::Handle<v8::Object> tostring0 =
      v8::Handle<v8::Object>::Cast(object0->Get(v8_str("toString")));
  v8::Handle<v8::Object> proto0 =
      v8::Handle<v8::Object>::Cast(tostring0->Get(v8_str("__proto__")));
  proto0->Set(v8_str("custom"), v8_num(1234));

  LocalContext env1;
  v8::Handle<v8::Object> global1 =
      env1->Global();
  v8::Handle<v8::Object> object1 =
      v8::Handle<v8::Object>::Cast(global1->Get(v8_str("Object")));
  v8::Handle<v8::Object> tostring1 =
      v8::Handle<v8::Object>::Cast(object1->Get(v8_str("toString")));
  v8::Handle<v8::Object> proto1 =
      v8::Handle<v8::Object>::Cast(tostring1->Get(v8_str("__proto__")));
  CHECK(!proto1->Has(v8_str("custom")));
}


THREADED_TEST(Regress892105) {
  // Make sure that object and array literals created by cloning
  // boilerplates cannot communicate through their __proto__
  // field. This is rather difficult to check, but we try to add stuff
  // to Object.prototype and Array.prototype and create a new
  // environment. This should succeed.

  v8::HandleScope scope;

  Local<String> source = v8_str("Object.prototype.obj = 1234;"
                                "Array.prototype.arr = 4567;"
                                "8901");

  LocalContext env0;
  Local<Script> script0 = Script::Compile(source);
  CHECK_EQ(8901.0, script0->Run()->NumberValue());

  LocalContext env1;
  Local<Script> script1 = Script::Compile(source);
  CHECK_EQ(8901.0, script1->Run()->NumberValue());
}


static void ExpectString(const char* code, const char* expected) {
  Local<Value> result = CompileRun(code);
  CHECK(result->IsString());
  String::AsciiValue ascii(result);
  CHECK_EQ(0, strcmp(*ascii, expected));
}


static void ExpectBoolean(const char* code, bool expected) {
  Local<Value> result = CompileRun(code);
  CHECK(result->IsBoolean());
  CHECK_EQ(expected, result->BooleanValue());
}


static void ExpectObject(const char* code, Local<Value> expected) {
  Local<Value> result = CompileRun(code);
  CHECK(result->Equals(expected));
}


THREADED_TEST(UndetectableObject) {
  v8::HandleScope scope;
  LocalContext env;

  Local<v8::FunctionTemplate> desc =
      v8::FunctionTemplate::New(0, v8::Handle<Value>());
  desc->InstanceTemplate()->MarkAsUndetectable();  // undetectable

  Local<v8::Object> obj = desc->GetFunction()->NewInstance();
  env->Global()->Set(v8_str("undetectable"), obj);

  ExpectString("undetectable.toString()", "[object Object]");
  ExpectString("typeof undetectable", "undefined");
  ExpectString("typeof(undetectable)", "undefined");
  ExpectBoolean("typeof undetectable == 'undefined'", true);
  ExpectBoolean("typeof undetectable == 'object'", false);
  ExpectBoolean("if (undetectable) { true; } else { false; }", false);
  ExpectBoolean("!undetectable", true);

  ExpectObject("true&&undetectable", obj);
  ExpectBoolean("false&&undetectable", false);
  ExpectBoolean("true||undetectable", true);
  ExpectObject("false||undetectable", obj);

  ExpectObject("undetectable&&true", obj);
  ExpectObject("undetectable&&false", obj);
  ExpectBoolean("undetectable||true", true);
  ExpectBoolean("undetectable||false", false);

  ExpectBoolean("undetectable==null", true);
  ExpectBoolean("null==undetectable", true);
  ExpectBoolean("undetectable==undefined", true);
  ExpectBoolean("undefined==undetectable", true);
  ExpectBoolean("undetectable==undetectable", true);


  ExpectBoolean("undetectable===null", false);
  ExpectBoolean("null===undetectable", false);
  ExpectBoolean("undetectable===undefined", false);
  ExpectBoolean("undefined===undetectable", false);
  ExpectBoolean("undetectable===undetectable", true);
}


THREADED_TEST(UndetectableString) {
  v8::HandleScope scope;
  LocalContext env;

  Local<String> obj = String::NewUndetectable("foo");
  env->Global()->Set(v8_str("undetectable"), obj);

  ExpectString("undetectable", "foo");
  ExpectString("typeof undetectable", "undefined");
  ExpectString("typeof(undetectable)", "undefined");
  ExpectBoolean("typeof undetectable == 'undefined'", true);
  ExpectBoolean("typeof undetectable == 'string'", false);
  ExpectBoolean("if (undetectable) { true; } else { false; }", false);
  ExpectBoolean("!undetectable", true);

  ExpectObject("true&&undetectable", obj);
  ExpectBoolean("false&&undetectable", false);
  ExpectBoolean("true||undetectable", true);
  ExpectObject("false||undetectable", obj);

  ExpectObject("undetectable&&true", obj);
  ExpectObject("undetectable&&false", obj);
  ExpectBoolean("undetectable||true", true);
  ExpectBoolean("undetectable||false", false);

  ExpectBoolean("undetectable==null", true);
  ExpectBoolean("null==undetectable", true);
  ExpectBoolean("undetectable==undefined", true);
  ExpectBoolean("undefined==undetectable", true);
  ExpectBoolean("undetectable==undetectable", true);


  ExpectBoolean("undetectable===null", false);
  ExpectBoolean("null===undetectable", false);
  ExpectBoolean("undetectable===undefined", false);
  ExpectBoolean("undefined===undetectable", false);
  ExpectBoolean("undetectable===undetectable", true);
}


template <typename T> static void USE(T) { }


// This test is not intended to be run, just type checked.
static void PersistentHandles() {
  USE(PersistentHandles);
  Local<String> str = v8_str("foo");
  v8::Persistent<String> p_str = v8::Persistent<String>::New(str);
  USE(p_str);
  Local<Script> scr = Script::Compile(v8_str(""));
  v8::Persistent<Script> p_scr = v8::Persistent<Script>::New(scr);
  USE(p_scr);
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  v8::Persistent<ObjectTemplate> p_templ =
    v8::Persistent<ObjectTemplate>::New(templ);
  USE(p_templ);
}


static v8::Handle<Value> HandleLogDelegator(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8::Undefined();
}


THREADED_TEST(GlobalObjectTemplate) {
  v8::HandleScope handle_scope;
  Local<ObjectTemplate> global_template = ObjectTemplate::New();
  global_template->Set(v8_str("JSNI_Log"),
                       v8::FunctionTemplate::New(HandleLogDelegator));
  v8::Persistent<Context> context = Context::New(0, global_template);
  Context::Scope context_scope(context);
  Script::Compile(v8_str("JSNI_Log('LOG')"))->Run();
  context.Dispose();
}


static const char* kSimpleExtensionSource =
  "function Foo() {"
  "  return 4;"
  "}";


THREADED_TEST(SimpleExtensions) {
  v8::HandleScope handle_scope;
  v8::RegisterExtension(new Extension("simpletest", kSimpleExtensionSource));
  const char* extension_names[] = { "simpletest" };
  v8::ExtensionConfiguration extensions(1, extension_names);
  v8::Handle<Context> context = Context::New(&extensions);
  Context::Scope lock(context);
  v8::Handle<Value> result = Script::Compile(v8_str("Foo()"))->Run();
  CHECK_EQ(result, v8::Integer::New(4));
}


static const char* kEvalExtensionSource =
  "function UseEval() {"
  "  var x = 42;"
  "  return eval('x');"
  "}";


THREADED_TEST(UseEvalFromExtension) {
  v8::HandleScope handle_scope;
  v8::RegisterExtension(new Extension("evaltest", kEvalExtensionSource));
  const char* extension_names[] = { "evaltest" };
  v8::ExtensionConfiguration extensions(1, extension_names);
  v8::Handle<Context> context = Context::New(&extensions);
  Context::Scope lock(context);
  v8::Handle<Value> result = Script::Compile(v8_str("UseEval()"))->Run();
  CHECK_EQ(result, v8::Integer::New(42));
}


static const char* kWithExtensionSource =
  "function UseWith() {"
  "  var x = 42;"
  "  with({x:87}) { return x; }"
  "}";


THREADED_TEST(UseWithFromExtension) {
  v8::HandleScope handle_scope;
  v8::RegisterExtension(new Extension("withtest", kWithExtensionSource));
  const char* extension_names[] = { "withtest" };
  v8::ExtensionConfiguration extensions(1, extension_names);
  v8::Handle<Context> context = Context::New(&extensions);
  Context::Scope lock(context);
  v8::Handle<Value> result = Script::Compile(v8_str("UseWith()"))->Run();
  CHECK_EQ(result, v8::Integer::New(87));
}


THREADED_TEST(AutoExtensions) {
  v8::HandleScope handle_scope;
  Extension* extension = new Extension("autotest", kSimpleExtensionSource);
  extension->set_auto_enable(true);
  v8::RegisterExtension(extension);
  v8::Handle<Context> context = Context::New();
  Context::Scope lock(context);
  v8::Handle<Value> result = Script::Compile(v8_str("Foo()"))->Run();
  CHECK_EQ(result, v8::Integer::New(4));
}


static void CheckDependencies(const char* name, const char* expected) {
  v8::HandleScope handle_scope;
  v8::ExtensionConfiguration config(1, &name);
  LocalContext context(&config);
  CHECK_EQ(String::New(expected), context->Global()->Get(v8_str("loaded")));
}


/*
 * Configuration:
 *
 *     /-- B <--\
 * A <-          -- D <-- E
 *     \-- C <--/
 */
THREADED_TEST(ExtensionDependency) {
  static const char* kEDeps[] = { "D" };
  v8::RegisterExtension(new Extension("E", "this.loaded += 'E';", 1, kEDeps));
  static const char* kDDeps[] = { "B", "C" };
  v8::RegisterExtension(new Extension("D", "this.loaded += 'D';", 2, kDDeps));
  static const char* kBCDeps[] = { "A" };
  v8::RegisterExtension(new Extension("B", "this.loaded += 'B';", 1, kBCDeps));
  v8::RegisterExtension(new Extension("C", "this.loaded += 'C';", 1, kBCDeps));
  v8::RegisterExtension(new Extension("A", "this.loaded += 'A';"));
  CheckDependencies("A", "undefinedA");
  CheckDependencies("B", "undefinedAB");
  CheckDependencies("C", "undefinedAC");
  CheckDependencies("D", "undefinedABCD");
  CheckDependencies("E", "undefinedABCDE");
  v8::HandleScope handle_scope;
  static const char* exts[2] = { "C", "E" };
  v8::ExtensionConfiguration config(2, exts);
  LocalContext context(&config);
  CHECK_EQ(v8_str("undefinedACBDE"), context->Global()->Get(v8_str("loaded")));
}


static const char* kExtensionTestScript =
  "native function A();"
  "native function B();"
  "native function C();"
  "function Foo(i) {"
  "  if (i == 0) return A();"
  "  if (i == 1) return B();"
  "  if (i == 2) return C();"
  "}";


static v8::Handle<Value> CallFun(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return args.Data();
}


class FunctionExtension : public Extension {
 public:
  FunctionExtension() : Extension("functiontest", kExtensionTestScript) { }
  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<String> name);
};


static int lookup_count = 0;
v8::Handle<v8::FunctionTemplate> FunctionExtension::GetNativeFunction(
      v8::Handle<String> name) {
  lookup_count++;
  if (name->Equals(v8_str("A"))) {
    return v8::FunctionTemplate::New(CallFun, v8::Integer::New(8));
  } else if (name->Equals(v8_str("B"))) {
    return v8::FunctionTemplate::New(CallFun, v8::Integer::New(7));
  } else if (name->Equals(v8_str("C"))) {
    return v8::FunctionTemplate::New(CallFun, v8::Integer::New(6));
  } else {
    return v8::Handle<v8::FunctionTemplate>();
  }
}


THREADED_TEST(FunctionLookup) {
  v8::RegisterExtension(new FunctionExtension());
  v8::HandleScope handle_scope;
  static const char* exts[1] = { "functiontest" };
  v8::ExtensionConfiguration config(1, exts);
  LocalContext context(&config);
  CHECK_EQ(3, lookup_count);
  CHECK_EQ(v8::Integer::New(8), Script::Compile(v8_str("Foo(0)"))->Run());
  CHECK_EQ(v8::Integer::New(7), Script::Compile(v8_str("Foo(1)"))->Run());
  CHECK_EQ(v8::Integer::New(6), Script::Compile(v8_str("Foo(2)"))->Run());
}


static const char* last_location;
static const char* last_message;
void StoringErrorCallback(const char* location, const char* message) {
  if (last_location == NULL) {
    last_location = location;
    last_message = message;
  }
}


// ErrorReporting creates a circular extensions configuration and
// tests that the fatal error handler gets called.  This renders V8
// unusable and therefore this test cannot be run in parallel.
TEST(ErrorReporting) {
  v8::V8::SetFatalErrorHandler(StoringErrorCallback);
  static const char* aDeps[] = { "B" };
  v8::RegisterExtension(new Extension("A", "", 1, aDeps));
  static const char* bDeps[] = { "A" };
  v8::RegisterExtension(new Extension("B", "", 1, bDeps));
  last_location = NULL;
  v8::ExtensionConfiguration config(1, bDeps);
  v8::Handle<Context> context = Context::New(&config);
  CHECK(context.IsEmpty());
  CHECK_NE(last_location, NULL);
}


static const char* js_code_causing_huge_string_flattening =
    "var str = 'X';"
    "for (var i = 0; i < 29; i++) {"
    "  str = str + str;"
    "}"
    "str.match(/X/);";


void OOMCallback(const char* location, const char* message) {
  exit(0);
}


TEST(RegexpOutOfMemory) {
  // Execute a script that causes out of memory when flattening a string.
  v8::HandleScope scope;
  v8::V8::SetFatalErrorHandler(OOMCallback);
  LocalContext context;
  Local<Script> script =
      Script::Compile(String::New(js_code_causing_huge_string_flattening));
  last_location = NULL;
  Local<Value> result = script->Run();

  CHECK(false);  // Should not return.
}


static void MissingScriptInfoMessageListener(v8::Handle<v8::Message> message,
                                             v8::Handle<Value> data) {
  CHECK_EQ(v8::Undefined(), data);
  CHECK(message->GetScriptResourceName()->IsUndefined());
  CHECK_EQ(v8::Undefined(), message->GetScriptResourceName());
  message->GetLineNumber();
  message->GetSourceLine();
}


THREADED_TEST(ErrorWithMissingScriptInfo) {
  v8::HandleScope scope;
  LocalContext context;
  v8::V8::AddMessageListener(MissingScriptInfoMessageListener);
  Script::Compile(v8_str("throw Error()"))->Run();
  v8::V8::RemoveMessageListeners(MissingScriptInfoMessageListener);
}


int global_index = 0;

class Snorkel {
 public:
  Snorkel() { index_ = global_index++; }
  int index_;
};

class Whammy {
 public:
  Whammy() {
    cursor_ = 0;
  }
  ~Whammy() {
    script_.Dispose();
  }
  v8::Handle<Script> getScript() {
    if (script_.IsEmpty())
      script_ = v8::Persistent<Script>::New(v8_compile("({}).blammo"));
    return Local<Script>(*script_);
  }

 public:
  static const int kObjectCount = 256;
  int cursor_;
  v8::Persistent<v8::Object> objects_[kObjectCount];
  v8::Persistent<Script> script_;
};

static void HandleWeakReference(v8::Persistent<v8::Value> obj, void* data) {
  Snorkel* snorkel = reinterpret_cast<Snorkel*>(data);
  delete snorkel;
  obj.ClearWeak();
}

v8::Handle<Value> WhammyPropertyGetter(Local<String> name,
                                       const AccessorInfo& info) {
  Whammy* whammy =
    static_cast<Whammy*>(v8::Handle<v8::External>::Cast(info.Data())->Value());

  v8::Persistent<v8::Object> prev = whammy->objects_[whammy->cursor_];

  v8::Handle<v8::Object> obj = v8::Object::New();
  v8::Persistent<v8::Object> global = v8::Persistent<v8::Object>::New(obj);
  if (!prev.IsEmpty()) {
    prev->Set(v8_str("next"), obj);
    prev.MakeWeak(new Snorkel(), &HandleWeakReference);
    whammy->objects_[whammy->cursor_].Clear();
  }
  whammy->objects_[whammy->cursor_] = global;
  whammy->cursor_ = (whammy->cursor_ + 1) % Whammy::kObjectCount;
  return whammy->getScript()->Run();
}

THREADED_TEST(WeakReference) {
  v8::HandleScope handle_scope;
  v8::Handle<v8::ObjectTemplate> templ= v8::ObjectTemplate::New();
  templ->SetNamedPropertyHandler(WhammyPropertyGetter,
                                 0, 0, 0, 0,
                                 v8::External::New(new Whammy()));
  const char* extension_list[] = { "v8/gc" };
  v8::ExtensionConfiguration extensions(1, extension_list);
  v8::Persistent<Context> context = Context::New(&extensions);
  Context::Scope context_scope(context);

  v8::Handle<v8::Object> interceptor = templ->NewInstance();
  context->Global()->Set(v8_str("whammy"), interceptor);
  const char* code =
      "var last;"
      "for (var i = 0; i < 10000; i++) {"
      "  var obj = whammy.length;"
      "  if (last) last.next = obj;"
      "  last = obj;"
      "}"
      "gc();"
      "4";
  v8::Handle<Value> result = CompileRun(code);
  CHECK_EQ(4.0, result->NumberValue());

  context.Dispose();
}


v8::Handle<Function> args_fun;


static v8::Handle<Value> ArgumentsTestCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(args_fun, args.Callee());
  CHECK_EQ(3, args.Length());
  CHECK_EQ(v8::Integer::New(1), args[0]);
  CHECK_EQ(v8::Integer::New(2), args[1]);
  CHECK_EQ(v8::Integer::New(3), args[2]);
  CHECK_EQ(v8::Undefined(), args[3]);
  v8::HandleScope scope;
  i::Heap::CollectAllGarbage();
  return v8::Undefined();
}


THREADED_TEST(Arguments) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> global = ObjectTemplate::New();
  global->Set(v8_str("f"), v8::FunctionTemplate::New(ArgumentsTestCallback));
  LocalContext context(NULL, global);
  args_fun = v8::Handle<Function>::Cast(context->Global()->Get(v8_str("f")));
  v8_compile("f(1, 2, 3)")->Run();
}


static int x_register = 0;
static v8::Handle<v8::Object> x_receiver;
static v8::Handle<v8::Object> x_holder;


static v8::Handle<Value> XGetter(Local<String> name, const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK_EQ(x_receiver, info.This());
  CHECK_EQ(x_holder, info.Holder());
  return v8_num(x_register);
}


static void XSetter(Local<String> name,
                    Local<Value> value,
                    const AccessorInfo& info) {
  CHECK_EQ(x_holder, info.This());
  CHECK_EQ(x_holder, info.Holder());
  x_register = value->Int32Value();
}


THREADED_TEST(AccessorIC) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetAccessor(v8_str("x"), XGetter, XSetter);
  LocalContext context;
  x_holder = obj->NewInstance();
  context->Global()->Set(v8_str("holder"), x_holder);
  x_receiver = v8::Object::New();
  context->Global()->Set(v8_str("obj"), x_receiver);
  v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(CompileRun(
    "obj.__proto__ = holder;"
    "var result = [];"
    "for (var i = 0; i < 10; i++) {"
    "  holder.x = i;"
    "  result.push(obj.x);"
    "}"
    "result"));
  CHECK_EQ(10, array->Length());
  for (int i = 0; i < 10; i++) {
    v8::Handle<Value> entry = array->Get(v8::Integer::New(i));
    CHECK_EQ(v8::Integer::New(i), entry);
  }
}


static v8::Handle<Value> NoBlockGetterX(Local<String> name,
                                        const AccessorInfo&) {
  return v8::Handle<Value>();
}


static v8::Handle<Value> NoBlockGetterI(uint32_t index,
                                        const AccessorInfo&) {
  return v8::Handle<Value>();
}


static v8::Handle<v8::Boolean> PDeleter(Local<String> name,
                                        const AccessorInfo&) {
  if (!name->Equals(v8_str("foo"))) {
    return v8::Handle<v8::Boolean>();  // not intercepted
  }

  return v8::False();  // intercepted, and don't delete the property
}


static v8::Handle<v8::Boolean> IDeleter(uint32_t index, const AccessorInfo&) {
  if (index != 2) {
    return v8::Handle<v8::Boolean>();  // not intercepted
  }

  return v8::False();  // intercepted, and don't delete the property
}


THREADED_TEST(Deleter) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetNamedPropertyHandler(NoBlockGetterX, NULL, NULL, PDeleter, NULL);
  obj->SetIndexedPropertyHandler(NoBlockGetterI, NULL, NULL, IDeleter, NULL);
  LocalContext context;
  context->Global()->Set(v8_str("k"), obj->NewInstance());
  CompileRun(
    "k.foo = 'foo';"
    "k.bar = 'bar';"
    "k[2] = 2;"
    "k[4] = 4;");
  CHECK(v8_compile("delete k.foo")->Run()->IsFalse());
  CHECK(v8_compile("delete k.bar")->Run()->IsTrue());

  CHECK_EQ(v8_compile("k.foo")->Run(), v8_str("foo"));
  CHECK(v8_compile("k.bar")->Run()->IsUndefined());

  CHECK(v8_compile("delete k[2]")->Run()->IsFalse());
  CHECK(v8_compile("delete k[4]")->Run()->IsTrue());

  CHECK_EQ(v8_compile("k[2]")->Run(), v8_num(2));
  CHECK(v8_compile("k[4]")->Run()->IsUndefined());
}


static v8::Handle<Value> GetK(Local<String> name, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8::Undefined();
}


static v8::Handle<v8::Array> NamedEnum(const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  v8::Handle<v8::Array> result = v8::Array::New(3);
  result->Set(v8::Integer::New(0), v8_str("foo"));
  result->Set(v8::Integer::New(1), v8_str("bar"));
  result->Set(v8::Integer::New(2), v8_str("baz"));
  return result;
}


static v8::Handle<v8::Array> IndexedEnum(const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  v8::Handle<v8::Array> result = v8::Array::New(2);
  result->Set(v8::Integer::New(0), v8_str("hat"));
  result->Set(v8::Integer::New(1), v8_str("gyt"));
  return result;
}


THREADED_TEST(Enumerators) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetNamedPropertyHandler(GetK, NULL, NULL, NULL, NamedEnum);
  obj->SetIndexedPropertyHandler(NULL, NULL, NULL, NULL, IndexedEnum);
  LocalContext context;
  context->Global()->Set(v8_str("k"), obj->NewInstance());
  v8::Handle<v8::Array> result = v8::Handle<v8::Array>::Cast(CompileRun(
    "var result = [];"
    "for (var prop in k) {"
    "  result.push(prop);"
    "}"
    "result"));
  CHECK_EQ(5, result->Length());
  CHECK_EQ(v8_str("foo"), result->Get(v8::Integer::New(0)));
  CHECK_EQ(v8_str("bar"), result->Get(v8::Integer::New(1)));
  CHECK_EQ(v8_str("baz"), result->Get(v8::Integer::New(2)));
  CHECK_EQ(v8_str("hat"), result->Get(v8::Integer::New(3)));
  CHECK_EQ(v8_str("gyt"), result->Get(v8::Integer::New(4)));
}


int p_getter_count;
int p_getter_count2;


static v8::Handle<Value> PGetter(Local<String> name, const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  p_getter_count++;
  v8::Handle<v8::Object> global = Context::GetCurrent()->Global();
  CHECK_EQ(info.Holder(), global->Get(v8_str("o1")));
  if (name->Equals(v8_str("p1"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o1")));
  } else if (name->Equals(v8_str("p2"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o2")));
  } else if (name->Equals(v8_str("p3"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o3")));
  } else if (name->Equals(v8_str("p4"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o4")));
  }
  return v8::Undefined();
}


static void RunHolderTest(v8::Handle<v8::ObjectTemplate> obj) {
  ApiTestFuzzer::Fuzz();
  LocalContext context;
  context->Global()->Set(v8_str("o1"), obj->NewInstance());
  CompileRun(
    "o1.__proto__ = { };"
    "var o2 = { __proto__: o1 };"
    "var o3 = { __proto__: o2 };"
    "var o4 = { __proto__: o3 };"
    "for (var i = 0; i < 10; i++) o4.p4;"
    "for (var i = 0; i < 10; i++) o3.p3;"
    "for (var i = 0; i < 10; i++) o2.p2;"
    "for (var i = 0; i < 10; i++) o1.p1;");
}


static v8::Handle<Value> PGetter2(Local<String> name,
                                  const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  p_getter_count2++;
  v8::Handle<v8::Object> global = Context::GetCurrent()->Global();
  CHECK_EQ(info.Holder(), global->Get(v8_str("o1")));
  if (name->Equals(v8_str("p1"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o1")));
  } else if (name->Equals(v8_str("p2"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o2")));
  } else if (name->Equals(v8_str("p3"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o3")));
  } else if (name->Equals(v8_str("p4"))) {
    CHECK_EQ(info.This(), global->Get(v8_str("o4")));
  }
  return v8::Undefined();
}


THREADED_TEST(GetterHolders) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetAccessor(v8_str("p1"), PGetter);
  obj->SetAccessor(v8_str("p2"), PGetter);
  obj->SetAccessor(v8_str("p3"), PGetter);
  obj->SetAccessor(v8_str("p4"), PGetter);
  p_getter_count = 0;
  RunHolderTest(obj);
  CHECK_EQ(40, p_getter_count);
}


THREADED_TEST(PreInterceptorHolders) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetNamedPropertyHandler(PGetter2);
  p_getter_count2 = 0;
  RunHolderTest(obj);
  CHECK_EQ(40, p_getter_count2);
}


THREADED_TEST(ObjectInstantiation) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessor(v8_str("t"), PGetter2);
  LocalContext context;
  context->Global()->Set(v8_str("o"), templ->NewInstance());
  for (int i = 0; i < 100; i++) {
    v8::HandleScope inner_scope;
    v8::Handle<v8::Object> obj = templ->NewInstance();
    CHECK_NE(obj, context->Global()->Get(v8_str("o")));
    context->Global()->Set(v8_str("o2"), obj);
    v8::Handle<Value> value =
        Script::Compile(v8_str("o.__proto__ === o2.__proto__"))->Run();
    CHECK_EQ(v8::True(), value);
    context->Global()->Set(v8_str("o"), obj);
  }
}


THREADED_TEST(StringWrite) {
  v8::HandleScope scope;
  v8::Handle<String> str = v8_str("abcde");

  char buf[100];
  int len;

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf);
  CHECK_EQ(len, 5);
  CHECK_EQ(strncmp("abcde\0", buf, 6), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 0, 4);
  CHECK_EQ(len, 4);
  CHECK_EQ(strncmp("abcd\1", buf, 5), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 0, 5);
  CHECK_EQ(len, 5);
  CHECK_EQ(strncmp("abcde\1", buf, 6), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 0, 6);
  CHECK_EQ(len, 5);
  CHECK_EQ(strncmp("abcde\0", buf, 6), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 4, -1);
  CHECK_EQ(len, 1);
  CHECK_EQ(strncmp("e\0", buf, 2), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 4, 6);
  CHECK_EQ(len, 1);
  CHECK_EQ(strncmp("e\0", buf, 2), 0);

  memset(buf, 0x1, sizeof(buf));
  len = str->WriteAscii(buf, 4, 1);
  CHECK_EQ(len, 1);
  CHECK_EQ(strncmp("e\1", buf, 2), 0);
}


THREADED_TEST(ToArrayIndex) {
  v8::HandleScope scope;
  LocalContext context;

  v8::Handle<String> str = v8_str("42");
  v8::Handle<v8::Uint32> index = str->ToArrayIndex();
  CHECK(!index.IsEmpty());
  CHECK_EQ(42.0, index->Uint32Value());
  str = v8_str("42asdf");
  index = str->ToArrayIndex();
  CHECK(index.IsEmpty());
  str = v8_str("-42");
  index = str->ToArrayIndex();
  CHECK(index.IsEmpty());
  str = v8_str("4294967295");
  index = str->ToArrayIndex();
  CHECK(!index.IsEmpty());
  CHECK_EQ(4294967295.0, index->Uint32Value());
  v8::Handle<v8::Number> num = v8::Number::New(1);
  index = num->ToArrayIndex();
  CHECK(!index.IsEmpty());
  CHECK_EQ(1.0, index->Uint32Value());
  num = v8::Number::New(-1);
  index = num->ToArrayIndex();
  CHECK(index.IsEmpty());
  v8::Handle<v8::Object> obj = v8::Object::New();
  index = obj->ToArrayIndex();
  CHECK(index.IsEmpty());
}


THREADED_TEST(ErrorConstruction) {
  v8::HandleScope scope;
  LocalContext context;

  v8::Handle<String> foo = v8_str("foo");
  v8::Handle<String> message = v8_str("message");
  v8::Handle<Value> range_error = v8::Exception::RangeError(foo);
  CHECK(range_error->IsObject());
  v8::Handle<v8::Object> range_obj(v8::Handle<v8::Object>::Cast(range_error));
  CHECK(v8::Handle<v8::Object>::Cast(range_error)->Get(message)->Equals(foo));
  v8::Handle<Value> reference_error = v8::Exception::ReferenceError(foo);
  CHECK(reference_error->IsObject());
  CHECK(
      v8::Handle<v8::Object>::Cast(reference_error)->Get(message)->Equals(foo));
  v8::Handle<Value> syntax_error = v8::Exception::SyntaxError(foo);
  CHECK(syntax_error->IsObject());
  CHECK(v8::Handle<v8::Object>::Cast(syntax_error)->Get(message)->Equals(foo));
  v8::Handle<Value> type_error = v8::Exception::TypeError(foo);
  CHECK(type_error->IsObject());
  CHECK(v8::Handle<v8::Object>::Cast(type_error)->Get(message)->Equals(foo));
  v8::Handle<Value> error = v8::Exception::Error(foo);
  CHECK(error->IsObject());
  CHECK(v8::Handle<v8::Object>::Cast(error)->Get(message)->Equals(foo));
}


static v8::Handle<Value> YGetter(Local<String> name, const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8_num(10);
}


static void YSetter(Local<String> name,
                    Local<Value> value,
                    const AccessorInfo& info) {
  if (info.This()->Has(name)) {
    info.This()->Delete(name);
  }
  info.This()->Set(name, value);
}


THREADED_TEST(DeleteAccessor) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> obj = ObjectTemplate::New();
  obj->SetAccessor(v8_str("y"), YGetter, YSetter);
  LocalContext context;
  v8::Handle<v8::Object> holder = obj->NewInstance();
  context->Global()->Set(v8_str("holder"), holder);
  v8::Handle<Value> result = CompileRun(
      "holder.y = 11; holder.y = 12; holder.y");
  CHECK_EQ(12, result->Uint32Value());
}


THREADED_TEST(TypeSwitch) {
  v8::HandleScope scope;
  v8::Handle<v8::FunctionTemplate> templ1 = v8::FunctionTemplate::New();
  v8::Handle<v8::FunctionTemplate> templ2 = v8::FunctionTemplate::New();
  v8::Handle<v8::FunctionTemplate> templ3 = v8::FunctionTemplate::New();
  v8::Handle<v8::FunctionTemplate> templs[3] = { templ1, templ2, templ3 };
  v8::Handle<v8::TypeSwitch> type_switch = v8::TypeSwitch::New(3, templs);
  LocalContext context;
  v8::Handle<v8::Object> obj0 = v8::Object::New();
  v8::Handle<v8::Object> obj1 = templ1->GetFunction()->NewInstance();
  v8::Handle<v8::Object> obj2 = templ2->GetFunction()->NewInstance();
  v8::Handle<v8::Object> obj3 = templ3->GetFunction()->NewInstance();
  for (int i = 0; i < 10; i++) {
    CHECK_EQ(0, type_switch->match(obj0));
    CHECK_EQ(1, type_switch->match(obj1));
    CHECK_EQ(2, type_switch->match(obj2));
    CHECK_EQ(3, type_switch->match(obj3));
    CHECK_EQ(3, type_switch->match(obj3));
    CHECK_EQ(2, type_switch->match(obj2));
    CHECK_EQ(1, type_switch->match(obj1));
    CHECK_EQ(0, type_switch->match(obj0));
  }
}


// For use within the TestSecurityHandler() test.
static bool g_security_callback_result = false;
static bool NamedSecurityTestCallback(Local<v8::Object> global,
                                      Local<Value> name,
                                      v8::AccessType type,
                                      Local<Value> data) {
  // Always allow read access.
  if (type == v8::ACCESS_GET)
    return true;

  // Sometimes allow other access.
  return g_security_callback_result;
}


static bool IndexedSecurityTestCallback(Local<v8::Object> global,
                                        uint32_t key,
                                        v8::AccessType type,
                                        Local<Value> data) {
  // Always allow read access.
  if (type == v8::ACCESS_GET)
    return true;

  // Sometimes allow other access.
  return g_security_callback_result;
}


static int trouble_nesting = 0;
static v8::Handle<Value> TroubleCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  trouble_nesting++;

  // Call a JS function that throws an uncaught exception.
  Local<v8::Object> arg_this = Context::GetCurrent()->Global();
  Local<Value> trouble_callee = (trouble_nesting == 3) ?
    arg_this->Get(v8_str("trouble_callee")) :
    arg_this->Get(v8_str("trouble_caller"));
  CHECK(trouble_callee->IsFunction());
  return Function::Cast(*trouble_callee)->Call(arg_this, 0, NULL);
}


static int report_count = 0;
static void ApiUncaughtExceptionTestListener(v8::Handle<v8::Message>,
                                             v8::Handle<Value>) {
  report_count++;
}


// Counts uncaught exceptions, but other tests running in parallel
// also have uncaught exceptions.
TEST(ApiUncaughtException) {
  report_count = 0;
  v8::HandleScope scope;
  LocalContext env;
  v8::V8::AddMessageListener(ApiUncaughtExceptionTestListener);

  Local<v8::FunctionTemplate> fun = v8::FunctionTemplate::New(TroubleCallback);
  v8::Local<v8::Object> global = env->Global();
  global->Set(v8_str("trouble"), fun->GetFunction());

  Script::Compile(v8_str("function trouble_callee() {"
                         "  var x = null;"
                         "  return x.foo;"
                         "};"
                         "function trouble_caller() {"
                         "  trouble();"
                         "};"))->Run();
  Local<Value> trouble = global->Get(v8_str("trouble"));
  CHECK(trouble->IsFunction());
  Local<Value> trouble_callee = global->Get(v8_str("trouble_callee"));
  CHECK(trouble_callee->IsFunction());
  Local<Value> trouble_caller = global->Get(v8_str("trouble_caller"));
  CHECK(trouble_caller->IsFunction());
  Function::Cast(*trouble_caller)->Call(global, 0, NULL);
  CHECK_EQ(1, report_count);
  v8::V8::RemoveMessageListeners(ApiUncaughtExceptionTestListener);
}


TEST(CompilationErrorUsingTryCatchHandler) {
  v8::HandleScope scope;
  LocalContext env;
  v8::TryCatch try_catch;
  Script::Compile(v8_str("This doesn't &*&@#$&*^ compile."));
  CHECK_NE(NULL, *try_catch.Exception());
  CHECK(try_catch.HasCaught());
}


TEST(TryCatchFinallyUsingTryCatchHandler) {
  v8::HandleScope scope;
  LocalContext env;
  v8::TryCatch try_catch;
  Script::Compile(v8_str("try { throw ''; } catch (e) {}"))->Run();
  CHECK(!try_catch.HasCaught());
  Script::Compile(v8_str("try { throw ''; } finally {}"))->Run();
  CHECK(try_catch.HasCaught());
  try_catch.Reset();
  Script::Compile(v8_str("(function() {"
                         "try { throw ''; } finally { return; }"
                         "})()"))->Run();
  CHECK(!try_catch.HasCaught());
  Script::Compile(v8_str("(function()"
                         "  { try { throw ''; } finally { throw 0; }"
                         "})()"))->Run();
  CHECK(try_catch.HasCaught());
}


// SecurityHandler can't be run twice
TEST(SecurityHandler) {
  v8::HandleScope scope0;
  v8::Handle<v8::ObjectTemplate> global_template = v8::ObjectTemplate::New();
  global_template->SetAccessCheckCallbacks(NamedSecurityTestCallback,
                                           IndexedSecurityTestCallback);
  // Create an environment
  v8::Persistent<Context> context0 =
    Context::New(NULL, global_template);
  context0->Enter();

  v8::Handle<v8::Object> global0 = context0->Global();
  v8::Handle<Script> script0 = v8_compile("foo = 111");
  script0->Run();
  global0->Set(v8_str("0"), v8_num(999));
  v8::Handle<Value> foo0 = global0->Get(v8_str("foo"));
  CHECK_EQ(111, foo0->Int32Value());
  v8::Handle<Value> z0 = global0->Get(v8_str("0"));
  CHECK_EQ(999, z0->Int32Value());

  // Create another environment, should fail security checks.
  v8::HandleScope scope1;

  v8::Persistent<Context> context1 =
    Context::New(NULL, global_template);
  context1->Enter();

  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("othercontext"), global0);
  // This set will fail the security check.
  v8::Handle<Script> script1 =
    v8_compile("othercontext.foo = 222; othercontext[0] = 888;");
  script1->Run();
  // This read will pass the security check.
  v8::Handle<Value> foo1 = global0->Get(v8_str("foo"));
  CHECK_EQ(111, foo1->Int32Value());
  // This read will pass the security check.
  v8::Handle<Value> z1 = global0->Get(v8_str("0"));
  CHECK_EQ(999, z1->Int32Value());

  // Create another environment, should pass security checks.
  { g_security_callback_result = true;  // allow security handler to pass.
    v8::HandleScope scope2;
    LocalContext context2;
    v8::Handle<v8::Object> global2 = context2->Global();
    global2->Set(v8_str("othercontext"), global0);
    v8::Handle<Script> script2 =
        v8_compile("othercontext.foo = 333; othercontext[0] = 888;");
    script2->Run();
    v8::Handle<Value> foo2 = global0->Get(v8_str("foo"));
    CHECK_EQ(333, foo2->Int32Value());
    v8::Handle<Value> z2 = global0->Get(v8_str("0"));
    CHECK_EQ(888, z2->Int32Value());
  }

  context1->Exit();
  context1.Dispose();

  context0->Exit();
  context0.Dispose();
}


THREADED_TEST(SecurityChecks) {
  v8::HandleScope handle_scope;
  LocalContext env1;
  v8::Persistent<Context> env2 = Context::New();

  Local<Value> foo = v8_str("foo");
  Local<Value> bar = v8_str("bar");

  // Set to the same domain.
  env1->SetSecurityToken(foo);

  // Create a function in env1.
  Script::Compile(v8_str("spy=function(){return spy;}"))->Run();
  Local<Value> spy = env1->Global()->Get(v8_str("spy"));
  CHECK(spy->IsFunction());

  // Create another function accessing global objects.
  Script::Compile(v8_str("spy2=function(){return new this.Array();}"))->Run();
  Local<Value> spy2 = env1->Global()->Get(v8_str("spy2"));
  CHECK(spy2->IsFunction());

  // Switch to env2 in the same domain and invoke spy on env2.
  {
    env2->SetSecurityToken(foo);
    // Enter env2
    Context::Scope scope_env2(env2);
    Local<Value> result = Function::Cast(*spy)->Call(env2->Global(), 0, NULL);
    CHECK(result->IsFunction());
  }

  {
    env2->SetSecurityToken(bar);
    Context::Scope scope_env2(env2);

    // Call cross_domain_call, it should throw an exception
    v8::TryCatch try_catch;
    Function::Cast(*spy2)->Call(env2->Global(), 0, NULL);
    CHECK(try_catch.HasCaught());
  }

  env2.Dispose();
}


// Regression test case for issue 1183439.
THREADED_TEST(SecurityChecksForPrototypeChain) {
  v8::HandleScope scope;
  LocalContext current;
  v8::Persistent<Context> other = Context::New();

  // Change context to be able to get to the Object function in the
  // other context without hitting the security checks.
  v8::Local<Value> other_object;
  { Context::Scope scope(other);
    other_object = other->Global()->Get(v8_str("Object"));
    other->Global()->Set(v8_num(42), v8_num(87));
  }

  current->Global()->Set(v8_str("other"), other->Global());
  CHECK(v8_compile("other")->Run()->Equals(other->Global()));

  // Make sure the security check fails here and we get an undefined
  // result instead of getting the Object function. Repeat in a loop
  // to make sure to exercise the IC code.
  v8::Local<Script> access_other0 = v8_compile("other.Object");
  v8::Local<Script> access_other1 = v8_compile("other[42]");
  for (int i = 0; i < 5; i++) {
    CHECK(!access_other0->Run()->Equals(other_object));
    CHECK(access_other0->Run()->IsUndefined());
    CHECK(!access_other1->Run()->Equals(v8_num(87)));
    CHECK(access_other1->Run()->IsUndefined());
  }

  // Create an object that has 'other' in its prototype chain and make
  // sure we cannot access the Object function indirectly through
  // that. Repeat in a loop to make sure to exercise the IC code.
  v8_compile("function F() { };"
             "F.prototype = other;"
             "var f = new F();")->Run();
  v8::Local<Script> access_f0 = v8_compile("f.Object");
  v8::Local<Script> access_f1 = v8_compile("f[42]");
  for (int j = 0; j < 5; j++) {
    CHECK(!access_f0->Run()->Equals(other_object));
    CHECK(access_f0->Run()->IsUndefined());
    CHECK(!access_f1->Run()->Equals(v8_num(87)));
    CHECK(access_f1->Run()->IsUndefined());
  }

  // Now it gets hairy: Set the prototype for the other global object
  // to be the current global object. The prototype chain for 'f' now
  // goes through 'other' but ends up in the current global object.
  { Context::Scope scope(other);
    other->Global()->Set(v8_str("__proto__"), current->Global());
  }
  // Set a named and an index property on the current global
  // object. To force the lookup to go through the other global object,
  // the properties must not exist in the other global object.
  current->Global()->Set(v8_str("foo"), v8_num(100));
  current->Global()->Set(v8_num(99), v8_num(101));
  // Try to read the properties from f and make sure that the access
  // gets stopped by the security checks on the other global object.
  Local<Script> access_f2 = v8_compile("f.foo");
  Local<Script> access_f3 = v8_compile("f[99]");
  for (int k = 0; k < 5; k++) {
    CHECK(!access_f2->Run()->Equals(v8_num(100)));
    CHECK(access_f2->Run()->IsUndefined());
    CHECK(!access_f3->Run()->Equals(v8_num(101)));
    CHECK(access_f3->Run()->IsUndefined());
  }
  other.Dispose();
}


THREADED_TEST(CrossDomainDelete) {
  v8::HandleScope handle_scope;
  LocalContext env1;
  v8::Persistent<Context> env2 = Context::New();

  Local<Value> foo = v8_str("foo");
  Local<Value> bar = v8_str("bar");

  // Set to the same domain.
  env1->SetSecurityToken(foo);
  env2->SetSecurityToken(foo);

  env1->Global()->Set(v8_str("prop"), v8_num(3));
  env2->Global()->Set(v8_str("env1"), env1->Global());

  // Change env2 to a different domain and delete env1.prop.
  env2->SetSecurityToken(bar);
  {
    Context::Scope scope_env2(env2);
    Local<Value> result =
        Script::Compile(v8_str("delete env1.prop"))->Run();
    CHECK(result->IsFalse());
  }

  // Check that env1.prop still exists.
  Local<Value> v = env1->Global()->Get(v8_str("prop"));
  CHECK(v->IsNumber());
  CHECK_EQ(3, v->Int32Value());

  env2.Dispose();
}


THREADED_TEST(CrossDomainIsPropertyEnumerable) {
  v8::HandleScope handle_scope;
  LocalContext env1;
  v8::Persistent<Context> env2 = Context::New();

  Local<Value> foo = v8_str("foo");
  Local<Value> bar = v8_str("bar");

  // Set to the same domain.
  env1->SetSecurityToken(foo);
  env2->SetSecurityToken(foo);

  env1->Global()->Set(v8_str("prop"), v8_num(3));
  env2->Global()->Set(v8_str("env1"), env1->Global());

  // env1.prop is enumerable in env2.
  Local<String> test = v8_str("propertyIsEnumerable.call(env1, 'prop')");
  {
    Context::Scope scope_env2(env2);
    Local<Value> result = Script::Compile(test)->Run();
    CHECK(result->IsTrue());
  }

  // Change env2 to a different domain and test again.
  env2->SetSecurityToken(bar);
  {
    Context::Scope scope_env2(env2);
    Local<Value> result = Script::Compile(test)->Run();
    CHECK(result->IsFalse());
  }

  env2.Dispose();
}


THREADED_TEST(CrossDomainForIn) {
  v8::HandleScope handle_scope;
  LocalContext env1;
  v8::Persistent<Context> env2 = Context::New();

  Local<Value> foo = v8_str("foo");
  Local<Value> bar = v8_str("bar");

  // Set to the same domain.
  env1->SetSecurityToken(foo);
  env2->SetSecurityToken(foo);

  env1->Global()->Set(v8_str("prop"), v8_num(3));
  env2->Global()->Set(v8_str("env1"), env1->Global());

  // Change env2 to a different domain and set env1's global object
  // as the __proto__ of an object in env2 and enumerate properties
  // in for-in. It shouldn't enumerate properties on env1's global
  // object.
  env2->SetSecurityToken(bar);
  {
    Context::Scope scope_env2(env2);
    Local<Value> result =
        CompileRun("(function(){var obj = {'__proto__':env1};"
                   "for (var p in obj)"
                   "   if (p == 'prop') return false;"
                   "return true;})()");
    CHECK(result->IsTrue());
  }
  env2.Dispose();
}


TEST(ContextDetachGlobal) {
  v8::HandleScope handle_scope;
  LocalContext env1;
  v8::Persistent<Context> env2 = Context::New();

  Local<v8::Object> global1 = env1->Global();

  Local<Value> foo = v8_str("foo");

  // Set to the same domain.
  env1->SetSecurityToken(foo);
  env2->SetSecurityToken(foo);

  // Enter env2
  env2->Enter();

  // Create a function in env1
  Local<v8::Object> global2 = env2->Global();
  global2->Set(v8_str("prop"), v8::Integer::New(1));
  CompileRun("function getProp() {return prop;}");

  env1->Global()->Set(v8_str("getProp"),
                      global2->Get(v8_str("getProp")));

  // Detach env1's global, and reuse the global object of env1
  env2->Exit();
  env2->DetachGlobal();
  // env2 has a new global object.
  CHECK(!env2->Global()->Equals(global2));

  v8::Persistent<Context> env3 =
      Context::New(0, v8::Handle<v8::ObjectTemplate>(), global2);
  env3->SetSecurityToken(v8_str("bar"));
  env3->Enter();

  Local<v8::Object> global3 = env3->Global();
  CHECK_EQ(global2, global3);
  CHECK(global3->Get(v8_str("prop"))->IsUndefined());
  CHECK(global3->Get(v8_str("getProp"))->IsUndefined());
  global3->Set(v8_str("prop"), v8::Integer::New(-1));
  global3->Set(v8_str("prop2"), v8::Integer::New(2));
  env3->Exit();

  // Call getProp in env1, and it should return the value 1
  {
    Local<Value> get_prop = global1->Get(v8_str("getProp"));
    CHECK(get_prop->IsFunction());
    v8::TryCatch try_catch;
    Local<Value> r = Function::Cast(*get_prop)->Call(global1, 0, NULL);
    CHECK(!try_catch.HasCaught());
    CHECK_EQ(1, r->Int32Value());
  }

  // Check that env3 is not accessible from env1
  {
    Local<Value> r = global3->Get(v8_str("prop2"));
    CHECK(r->IsUndefined());
  }

  env2.Dispose();
  env3.Dispose();
}


static bool NamedAccessBlocker(Local<v8::Object> global,
                               Local<Value> name,
                               v8::AccessType type,
                               Local<Value> data) {
  return Context::GetCurrent()->Global()->Equals(global);
}


static bool IndexedAccessBlocker(Local<v8::Object> global,
                                 uint32_t key,
                                 v8::AccessType type,
                                 Local<Value> data) {
  return Context::GetCurrent()->Global()->Equals(global);
}


static int g_echo_value = -1;
static v8::Handle<Value> EchoGetter(Local<String> name,
                                    const AccessorInfo& info) {
  return v8_num(g_echo_value);
}


static void EchoSetter(Local<String> name,
                       Local<Value> value,
                       const AccessorInfo&) {
  if (value->IsNumber())
    g_echo_value = value->Int32Value();
}


static v8::Handle<Value> UnreachableGetter(Local<String> name,
                                           const AccessorInfo& info) {
  CHECK(false);  // This function should not be called..
  return v8::Undefined();
}


static void UnreachableSetter(Local<String>, Local<Value>,
                              const AccessorInfo&) {
  CHECK(false);  // This function should nto be called.
}


THREADED_TEST(AccessControl) {
  v8::HandleScope handle_scope;
  v8::Handle<v8::ObjectTemplate> global_template = v8::ObjectTemplate::New();

  global_template->SetAccessCheckCallbacks(NamedAccessBlocker,
                                           IndexedAccessBlocker);

  // Add an accessor accessible by cross-domain JS code.
  global_template->SetAccessor(
      v8_str("accessible_prop"),
      EchoGetter, EchoSetter,
      v8::Handle<Value>(),
      v8::AccessControl(v8::ALL_CAN_READ | v8::ALL_CAN_WRITE));

  // Add an accessor that is not accessible by cross-domain JS code.
  global_template->SetAccessor(v8_str("blocked_prop"),
                               UnreachableGetter, UnreachableSetter,
                               v8::Handle<Value>(),
                               v8::DEFAULT);

  // Create an environment
  v8::Persistent<Context> context0 = Context::New(NULL, global_template);
  context0->Enter();

  v8::Handle<v8::Object> global0 = context0->Global();

  v8::HandleScope scope1;

  v8::Persistent<Context> context1 = Context::New();
  context1->Enter();

  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("other"), global0);

  v8::Handle<Value> value;

  // Access blocked property
  value = v8_compile("other.blocked_prop = 1")->Run();
  value = v8_compile("other.blocked_prop")->Run();
  CHECK(value->IsUndefined());

  value = v8_compile("propertyIsEnumerable.call(other, 'blocked_prop')")->Run();
  CHECK(value->IsFalse());

  // Access accessible property
  value = v8_compile("other.accessible_prop = 3")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(3, value->Int32Value());

  value = v8_compile("other.accessible_prop")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(3, value->Int32Value());

  value =
    v8_compile("propertyIsEnumerable.call(other, 'accessible_prop')")->Run();
  CHECK(value->IsTrue());

  // Enumeration doesn't enumerate accessors from inaccessible objects in
  // the prototype chain even if the accessors are in themselves accessible.
  Local<Value> result =
      CompileRun("(function(){var obj = {'__proto__':other};"
                 "for (var p in obj)"
                 "   if (p == 'accessible_prop' || p == 'blocked_prop') {"
                 "     return false;"
                 "   }"
                 "return true;})()");
  CHECK(result->IsTrue());

  context1->Exit();
  context0->Exit();
  context1.Dispose();
  context0.Dispose();
}


static v8::Handle<Value> ConstTenGetter(Local<String> name,
                                        const AccessorInfo& info) {
  return v8_num(10);
}


THREADED_TEST(CrossDomainAccessors) {
  v8::HandleScope handle_scope;

  v8::Handle<v8::FunctionTemplate> func_template = v8::FunctionTemplate::New();

  v8::Handle<v8::ObjectTemplate> global_template =
      func_template->InstanceTemplate();

  v8::Handle<v8::ObjectTemplate> proto_template =
      func_template->PrototypeTemplate();

  // Add an accessor to proto that's accessible by cross-domain JS code.
  proto_template->SetAccessor(v8_str("accessible"),
                              ConstTenGetter, 0,
                              v8::Handle<Value>(),
                              v8::ALL_CAN_READ);

  // Add an accessor that is not accessible by cross-domain JS code.
  global_template->SetAccessor(v8_str("unreachable"),
                               UnreachableGetter, 0,
                               v8::Handle<Value>(),
                               v8::DEFAULT);

  v8::Persistent<Context> context0 = Context::New(NULL, global_template);
  context0->Enter();

  Local<v8::Object> global = context0->Global();
  // Add a normal property that shadows 'accessible'
  global->Set(v8_str("accessible"), v8_num(11));

  // Enter a new context.
  v8::HandleScope scope1;
  v8::Persistent<Context> context1 = Context::New();
  context1->Enter();

  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("other"), global);

  // Should return 10, instead of 11
  v8::Handle<Value> value = v8_compile("other.accessible")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(10, value->Int32Value());

  value = v8_compile("other.unreachable")->Run();
  CHECK(value->IsUndefined());

  context1->Exit();
  context0->Exit();
  context1.Dispose();
  context0.Dispose();
}


static int named_access_count = 0;
static int indexed_access_count = 0;

static bool NamedAccessCounter(Local<v8::Object> global,
                               Local<Value> name,
                               v8::AccessType type,
                               Local<Value> data) {
  named_access_count++;
  return true;
}


static bool IndexedAccessCounter(Local<v8::Object> global,
                                 uint32_t key,
                                 v8::AccessType type,
                                 Local<Value> data) {
  indexed_access_count++;
  return true;
}


// This one is too easily disturbed by other tests.
TEST(AccessControlIC) {
  named_access_count = 0;
  indexed_access_count = 0;

  v8::HandleScope handle_scope;

  // Create an environment.
  v8::Persistent<Context> context0 = Context::New();
  context0->Enter();

  // Create an object that requires access-check functions to be
  // called for cross-domain access.
  v8::Handle<v8::ObjectTemplate> object_template = v8::ObjectTemplate::New();
  object_template->SetAccessCheckCallbacks(NamedAccessCounter,
                                           IndexedAccessCounter);
  Local<v8::Object> object = object_template->NewInstance();

  v8::HandleScope scope1;

  // Create another environment.
  v8::Persistent<Context> context1 = Context::New();
  context1->Enter();

  // Make easy access to the object from the other environment.
  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("obj"), object);

  v8::Handle<Value> value;

  // Check that the named access-control function is called every time.
  CompileRun("function testProp(obj) {"
             "  for (var i = 0; i < 10; i++) obj.prop = 1;"
             "  for (var j = 0; j < 10; j++) obj.prop;"
             "  return obj.prop"
             "}");
  value = CompileRun("testProp(obj)");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(21, named_access_count);

  // Check that the named access-control function is called every time.
  CompileRun("var p = 'prop';"
             "function testKeyed(obj) {"
             "  for (var i = 0; i < 10; i++) obj[p] = 1;"
             "  for (var j = 0; j < 10; j++) obj[p];"
             "  return obj[p];"
             "}");
  // Use obj which requires access checks.  No inline caching is used
  // in that case.
  value = CompileRun("testKeyed(obj)");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(42, named_access_count);
  // Force the inline caches into generic state and try again.
  CompileRun("testKeyed({ a: 0 })");
  CompileRun("testKeyed({ b: 0 })");
  value = CompileRun("testKeyed(obj)");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(63, named_access_count);

  // Check that the indexed access-control function is called every time.
  CompileRun("function testIndexed(obj) {"
             "  for (var i = 0; i < 10; i++) obj[0] = 1;"
             "  for (var j = 0; j < 10; j++) obj[0];"
             "  return obj[0]"
             "}");
  value = CompileRun("testIndexed(obj)");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(21, indexed_access_count);
  // Force the inline caches into generic state.
  CompileRun("testIndexed(new Array(1))");
  // Test that the indexed access check is called.
  value = CompileRun("testIndexed(obj)");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(42, indexed_access_count);

  // Check that the named access check is called when invoking
  // functions on an object that requires access checks.
  CompileRun("obj.f = function() {}");
  CompileRun("function testCallNormal(obj) {"
             "  for (var i = 0; i < 10; i++) obj.f();"
             "}");
  CompileRun("testCallNormal(obj)");
  CHECK_EQ(74, named_access_count);

  // Force obj into slow case.
  value = CompileRun("delete obj.prop");
  CHECK(value->BooleanValue());
  // Force inline caches into dictionary probing mode.
  CompileRun("var o = { x: 0 }; delete o.x; testProp(o);");
  // Test that the named access check is called.
  value = CompileRun("testProp(obj);");
  CHECK(value->IsNumber());
  CHECK_EQ(1, value->Int32Value());
  CHECK_EQ(96, named_access_count);

  // Force the call inline cache into dictionary probing mode.
  CompileRun("o.f = function() {}; testCallNormal(o)");
  // Test that the named access check is still called for each
  // invocation of the function.
  value = CompileRun("testCallNormal(obj)");
  CHECK_EQ(106, named_access_count);

  context1->Exit();
  context0->Exit();
  context1.Dispose();
  context0.Dispose();
}


static bool NamedAccessFlatten(Local<v8::Object> global,
                               Local<Value> name,
                               v8::AccessType type,
                               Local<Value> data) {
  char buf[100];
  int len;

  CHECK(name->IsString());

  memset(buf, 0x1, sizeof(buf));
  len = Local<String>::Cast(name)->WriteAscii(buf);
  CHECK_EQ(4, len);

  uint16_t buf2[100];

  memset(buf, 0x1, sizeof(buf));
  len = Local<String>::Cast(name)->Write(buf2);
  CHECK_EQ(4, len);

  return true;
}


static bool IndexedAccessFlatten(Local<v8::Object> global,
                                 uint32_t key,
                                 v8::AccessType type,
                                 Local<Value> data) {
  return true;
}


// Regression test.  In access checks, operations that may cause
// garbage collection are not allowed.  It used to be the case that
// using the Write operation on a string could cause a garbage
// collection due to flattening of the string.  This is no longer the
// case.
THREADED_TEST(AccessControlFlatten) {
  named_access_count = 0;
  indexed_access_count = 0;

  v8::HandleScope handle_scope;

  // Create an environment.
  v8::Persistent<Context> context0 = Context::New();
  context0->Enter();

  // Create an object that requires access-check functions to be
  // called for cross-domain access.
  v8::Handle<v8::ObjectTemplate> object_template = v8::ObjectTemplate::New();
  object_template->SetAccessCheckCallbacks(NamedAccessFlatten,
                                           IndexedAccessFlatten);
  Local<v8::Object> object = object_template->NewInstance();

  v8::HandleScope scope1;

  // Create another environment.
  v8::Persistent<Context> context1 = Context::New();
  context1->Enter();

  // Make easy access to the object from the other environment.
  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("obj"), object);

  v8::Handle<Value> value;

  value = v8_compile("var p = 'as' + 'df';")->Run();
  value = v8_compile("obj[p];")->Run();

  context1->Exit();
  context0->Exit();
  context1.Dispose();
  context0.Dispose();
}


static v8::Handle<Value> AccessControlNamedGetter(
    Local<String>, const AccessorInfo&) {
  return v8::Integer::New(42);
}


static v8::Handle<Value> AccessControlNamedSetter(
    Local<String>, Local<Value> value, const AccessorInfo&) {
  return value;
}


static v8::Handle<Value> AccessControlIndexedGetter(
      uint32_t index,
      const AccessorInfo& info) {
  return v8_num(42);
}


static v8::Handle<Value> AccessControlIndexedSetter(
    uint32_t, Local<Value> value, const AccessorInfo&) {
  return value;
}


THREADED_TEST(AccessControlInterceptorIC) {
  named_access_count = 0;
  indexed_access_count = 0;

  v8::HandleScope handle_scope;

  // Create an environment.
  v8::Persistent<Context> context0 = Context::New();
  context0->Enter();

  // Create an object that requires access-check functions to be
  // called for cross-domain access.  The object also has interceptors
  // interceptor.
  v8::Handle<v8::ObjectTemplate> object_template = v8::ObjectTemplate::New();
  object_template->SetAccessCheckCallbacks(NamedAccessCounter,
                                           IndexedAccessCounter);
  object_template->SetNamedPropertyHandler(AccessControlNamedGetter,
                                           AccessControlNamedSetter);
  object_template->SetIndexedPropertyHandler(AccessControlIndexedGetter,
                                             AccessControlIndexedSetter);
  Local<v8::Object> object = object_template->NewInstance();

  v8::HandleScope scope1;

  // Create another environment.
  v8::Persistent<Context> context1 = Context::New();
  context1->Enter();

  // Make easy access to the object from the other environment.
  v8::Handle<v8::Object> global1 = context1->Global();
  global1->Set(v8_str("obj"), object);

  v8::Handle<Value> value;

  // Check that the named access-control function is called every time
  // eventhough there is an interceptor on the object.
  value = v8_compile("for (var i = 0; i < 10; i++) obj.x = 1;")->Run();
  value = v8_compile("for (var i = 0; i < 10; i++) obj.x;"
                     "obj.x")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(42, value->Int32Value());
  CHECK_EQ(21, named_access_count);

  value = v8_compile("var p = 'x';")->Run();
  value = v8_compile("for (var i = 0; i < 10; i++) obj[p] = 1;")->Run();
  value = v8_compile("for (var i = 0; i < 10; i++) obj[p];"
                     "obj[p]")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(42, value->Int32Value());
  CHECK_EQ(42, named_access_count);

  // Check that the indexed access-control function is called every
  // time eventhough there is an interceptor on the object.
  value = v8_compile("for (var i = 0; i < 10; i++) obj[0] = 1;")->Run();
  value = v8_compile("for (var i = 0; i < 10; i++) obj[0];"
                     "obj[0]")->Run();
  CHECK(value->IsNumber());
  CHECK_EQ(42, value->Int32Value());
  CHECK_EQ(21, indexed_access_count);

  context1->Exit();
  context0->Exit();
  context1.Dispose();
  context0.Dispose();
}


THREADED_TEST(Version) {
  v8::V8::GetVersion();
}


static v8::Handle<Value> InstanceFunctionCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(12);
}


THREADED_TEST(InstanceProperties) {
  v8::HandleScope handle_scope;
  LocalContext context;

  Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
  Local<ObjectTemplate> instance = t->InstanceTemplate();

  instance->Set(v8_str("x"), v8_num(42));
  instance->Set(v8_str("f"),
                v8::FunctionTemplate::New(InstanceFunctionCallback));

  Local<Value> o = t->GetFunction()->NewInstance();

  context->Global()->Set(v8_str("i"), o);
  Local<Value> value = Script::Compile(v8_str("i.x"))->Run();
  CHECK_EQ(42, value->Int32Value());

  value = Script::Compile(v8_str("i.f()"))->Run();
  CHECK_EQ(12, value->Int32Value());
}


static v8::Handle<Value>
GlobalObjectInstancePropertiesGet(Local<String> key, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  return v8::Handle<Value>();
}


THREADED_TEST(GlobalObjectInstanceProperties) {
  v8::HandleScope handle_scope;

  Local<Value> global_object;

  Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
  t->InstanceTemplate()->SetNamedPropertyHandler(
      GlobalObjectInstancePropertiesGet);
  Local<ObjectTemplate> instance_template = t->InstanceTemplate();
  instance_template->Set(v8_str("x"), v8_num(42));
  instance_template->Set(v8_str("f"),
                         v8::FunctionTemplate::New(InstanceFunctionCallback));

  {
    LocalContext env(NULL, instance_template);
    // Hold on to the global object so it can be used again in another
    // environment initialization.
    global_object = env->Global();

    Local<Value> value = Script::Compile(v8_str("x"))->Run();
    CHECK_EQ(42, value->Int32Value());
    value = Script::Compile(v8_str("f()"))->Run();
    CHECK_EQ(12, value->Int32Value());
  }

  {
    // Create new environment reusing the global object.
    LocalContext env(NULL, instance_template, global_object);
    Local<Value> value = Script::Compile(v8_str("x"))->Run();
    CHECK_EQ(42, value->Int32Value());
    value = Script::Compile(v8_str("f()"))->Run();
    CHECK_EQ(12, value->Int32Value());
  }
}


static v8::Handle<Value> ShadowFunctionCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(42);
}


static int shadow_y;
static int shadow_y_setter_call_count;
static int shadow_y_getter_call_count;


static void ShadowYSetter(Local<String>, Local<Value>, const AccessorInfo&) {
  shadow_y_setter_call_count++;
  shadow_y = 42;
}


static v8::Handle<Value> ShadowYGetter(Local<String> name,
                                       const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  shadow_y_getter_call_count++;
  return v8_num(shadow_y);
}


static v8::Handle<Value> ShadowIndexedGet(uint32_t index,
                                          const AccessorInfo& info) {
  return v8::Handle<Value>();
}


static v8::Handle<Value> ShadowNamedGet(Local<String> key,
                                        const AccessorInfo&) {
  return v8::Handle<Value>();
}


THREADED_TEST(ShadowObject) {
  shadow_y = shadow_y_setter_call_count = shadow_y_getter_call_count = 0;
  v8::HandleScope handle_scope;

  Local<ObjectTemplate> global_template = v8::ObjectTemplate::New();
  LocalContext context(NULL, global_template);

  Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
  t->InstanceTemplate()->SetNamedPropertyHandler(ShadowNamedGet);
  t->InstanceTemplate()->SetIndexedPropertyHandler(ShadowIndexedGet);
  Local<ObjectTemplate> proto = t->PrototypeTemplate();
  Local<ObjectTemplate> instance = t->InstanceTemplate();

  // Only allow calls of f on instances of t.
  Local<v8::Signature> signature = v8::Signature::New(t);
  proto->Set(v8_str("f"),
             v8::FunctionTemplate::New(ShadowFunctionCallback,
                                       Local<Value>(),
                                       signature));
  proto->Set(v8_str("x"), v8_num(12));

  instance->SetAccessor(v8_str("y"), ShadowYGetter, ShadowYSetter);

  Local<Value> o = t->GetFunction()->NewInstance();
  context->Global()->Set(v8_str("__proto__"), o);

  Local<Value> value =
      Script::Compile(v8_str("propertyIsEnumerable(0)"))->Run();
  CHECK(value->IsBoolean());
  CHECK(!value->BooleanValue());

  value = Script::Compile(v8_str("x"))->Run();
  CHECK_EQ(12, value->Int32Value());

  value = Script::Compile(v8_str("f()"))->Run();
  CHECK_EQ(42, value->Int32Value());

  Script::Compile(v8_str("y = 42"))->Run();
  CHECK_EQ(1, shadow_y_setter_call_count);
  value = Script::Compile(v8_str("y"))->Run();
  CHECK_EQ(1, shadow_y_getter_call_count);
  CHECK_EQ(42, value->Int32Value());
}


THREADED_TEST(HiddenPrototype) {
  v8::HandleScope handle_scope;
  LocalContext context;

  Local<v8::FunctionTemplate> t0 = v8::FunctionTemplate::New();
  t0->InstanceTemplate()->Set(v8_str("x"), v8_num(0));
  Local<v8::FunctionTemplate> t1 = v8::FunctionTemplate::New();
  t1->SetHiddenPrototype(true);
  t1->InstanceTemplate()->Set(v8_str("y"), v8_num(1));
  Local<v8::FunctionTemplate> t2 = v8::FunctionTemplate::New();
  t2->SetHiddenPrototype(true);
  t2->InstanceTemplate()->Set(v8_str("z"), v8_num(2));
  Local<v8::FunctionTemplate> t3 = v8::FunctionTemplate::New();
  t3->InstanceTemplate()->Set(v8_str("u"), v8_num(3));

  Local<v8::Object> o0 = t0->GetFunction()->NewInstance();
  Local<v8::Object> o1 = t1->GetFunction()->NewInstance();
  Local<v8::Object> o2 = t2->GetFunction()->NewInstance();
  Local<v8::Object> o3 = t3->GetFunction()->NewInstance();

  // Setting the prototype on an object skips hidden prototypes.
  CHECK_EQ(0, o0->Get(v8_str("x"))->Int32Value());
  o0->Set(v8_str("__proto__"), o1);
  CHECK_EQ(0, o0->Get(v8_str("x"))->Int32Value());
  CHECK_EQ(1, o0->Get(v8_str("y"))->Int32Value());
  o0->Set(v8_str("__proto__"), o2);
  CHECK_EQ(0, o0->Get(v8_str("x"))->Int32Value());
  CHECK_EQ(1, o0->Get(v8_str("y"))->Int32Value());
  CHECK_EQ(2, o0->Get(v8_str("z"))->Int32Value());
  o0->Set(v8_str("__proto__"), o3);
  CHECK_EQ(0, o0->Get(v8_str("x"))->Int32Value());
  CHECK_EQ(1, o0->Get(v8_str("y"))->Int32Value());
  CHECK_EQ(2, o0->Get(v8_str("z"))->Int32Value());
  CHECK_EQ(3, o0->Get(v8_str("u"))->Int32Value());

  // Getting the prototype of o0 should get the first visible one
  // which is o3.  Therefore, z should not be defined on the prototype
  // object.
  Local<Value> proto = o0->Get(v8_str("__proto__"));
  CHECK(proto->IsObject());
  CHECK(Local<v8::Object>::Cast(proto)->Get(v8_str("z"))->IsUndefined());
}


THREADED_TEST(GetterSetterExceptions) {
  v8::HandleScope handle_scope;
  LocalContext context;
  CompileRun(
    "function Foo() { };"
    "function Throw() { throw 5; };"
    "var x = { };"
    "x.__defineSetter__('set', Throw);"
    "x.__defineGetter__('get', Throw);");
  Local<v8::Object> x =
      Local<v8::Object>::Cast(context->Global()->Get(v8_str("x")));
  v8::TryCatch try_catch;
  x->Set(v8_str("set"), v8::Integer::New(8));
  x->Get(v8_str("get"));
  x->Set(v8_str("set"), v8::Integer::New(8));
  x->Get(v8_str("get"));
  x->Set(v8_str("set"), v8::Integer::New(8));
  x->Get(v8_str("get"));
  x->Set(v8_str("set"), v8::Integer::New(8));
  x->Get(v8_str("get"));
}


THREADED_TEST(Constructor) {
  v8::HandleScope handle_scope;
  LocalContext context;
  Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->SetClassName(v8_str("Fun"));
  Local<Function> cons = templ->GetFunction();
  context->Global()->Set(v8_str("Fun"), cons);
  Local<v8::Object> inst = cons->NewInstance();
  i::Handle<i::JSObject> obj = v8::Utils::OpenHandle(*inst);
  Local<Value> value = CompileRun("(new Fun()).constructor === Fun");
  CHECK(value->BooleanValue());
}

THREADED_TEST(FunctionDescriptorException) {
  v8::HandleScope handle_scope;
  LocalContext context;
  Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->SetClassName(v8_str("Fun"));
  Local<Function> cons = templ->GetFunction();
  context->Global()->Set(v8_str("Fun"), cons);
  Local<Value> value = CompileRun(
    "function test() {"
    "  try {"
    "    (new Fun()).blah()"
    "  } catch (e) {"
    "    var str = String(e);"
    "    if (str.indexOf('TypeError') == -1) return 1;"
    "    if (str.indexOf('[object Fun]') != -1) return 2;"
    "    if (str.indexOf('#<a Fun>') == -1) return 3;"
    "    return 0;"
    "  }"
    "  return 4;"
    "}"
    "test();");
  CHECK_EQ(0, value->Int32Value());
}


THREADED_TEST(EvalAliasedDynamic) {
  v8::HandleScope scope;
  LocalContext current;

  // This sets 'global' to the real global object (as opposed to the
  // proxy). It is highly implementation dependent, so take care.
  current->Global()->Set(v8_str("global"), current->Global()->GetPrototype());

  // Tests where aliased eval can only be resolved dynamically.
  Local<Script> script =
      Script::Compile(v8_str("function f(x) { "
                             "  var foo = 2;"
                             "  with (x) { return eval('foo'); }"
                             "}"
                             "foo = 0;"
                             "result1 = f(new Object());"
                             "result2 = f(global);"
                             "var x = new Object();"
                             "x.eval = function(x) { return 1; };"
                             "result3 = f(x);"));
  script->Run();
  CHECK_EQ(2, current->Global()->Get(v8_str("result1"))->Int32Value());
  CHECK_EQ(0, current->Global()->Get(v8_str("result2"))->Int32Value());
  CHECK_EQ(1, current->Global()->Get(v8_str("result3"))->Int32Value());

  v8::TryCatch try_catch;
  script =
    Script::Compile(v8_str("function f(x) { "
                           "  var bar = 2;"
                           "  with (x) { return eval('bar'); }"
                           "}"
                           "f(global)"));
  script->Run();
  CHECK(try_catch.HasCaught());
  try_catch.Reset();
}


THREADED_TEST(CrossEval) {
  v8::HandleScope scope;
  LocalContext other;
  LocalContext current;

  Local<String> token = v8_str("<security token>");
  other->SetSecurityToken(token);
  current->SetSecurityToken(token);

  // Setup reference from current to other.
  current->Global()->Set(v8_str("other"), other->Global());

  // Check that new variables are introduced in other context.
  Local<Script> script =
      Script::Compile(v8_str("other.eval('var foo = 1234')"));
  script->Run();
  Local<Value> foo = other->Global()->Get(v8_str("foo"));
  CHECK_EQ(1234, foo->Int32Value());
  CHECK(!current->Global()->Has(v8_str("foo")));

  // Check that writing to non-existing properties introduces them in
  // the other context.
  script =
      Script::Compile(v8_str("other.eval('na = 1234')"));
  script->Run();
  CHECK_EQ(1234, other->Global()->Get(v8_str("na"))->Int32Value());
  CHECK(!current->Global()->Has(v8_str("na")));

  // Check that global variables in current context are not visible in other
  // context.
  v8::TryCatch try_catch;
  script =
      Script::Compile(v8_str("var bar = 42; other.eval('bar');"));
  Local<Value> result = script->Run();
  CHECK(try_catch.HasCaught());
  try_catch.Reset();

  // Check that local variables in current context are not visible in other
  // context.
  script =
      Script::Compile(v8_str("(function() { "
                             "  var baz = 87;"
                             "  return other.eval('baz');"
                             "})();"));
  result = script->Run();
  CHECK(try_catch.HasCaught());
  try_catch.Reset();

  // Check that global variables in the other environment are visible
  // when evaluting code.
  other->Global()->Set(v8_str("bis"), v8_num(1234));
  script = Script::Compile(v8_str("other.eval('bis')"));
  CHECK_EQ(1234, script->Run()->Int32Value());
  CHECK(!try_catch.HasCaught());

  // Check that the 'this' pointer points to the global object evaluating
  // code.
  other->Global()->Set(v8_str("t"), other->Global());
  script = Script::Compile(v8_str("other.eval('this == t')"));
  result = script->Run();
  CHECK(result->IsTrue());
  CHECK(!try_catch.HasCaught());

  // Check that variables introduced in with-statement are not visible in
  // other context.
  script =
      Script::Compile(v8_str("with({x:2}){other.eval('x')}"));
  result = script->Run();
  CHECK(try_catch.HasCaught());
  try_catch.Reset();

  // Check that you cannot use 'eval.call' with another object than the
  // current global object.
  script =
      Script::Compile(v8_str("other.y = 1; eval.call(other, 'y')"));
  result = script->Run();
  CHECK(try_catch.HasCaught());
}


THREADED_TEST(CrossLazyLoad) {
  v8::HandleScope scope;
  LocalContext other;
  LocalContext current;

  Local<String> token = v8_str("<security token>");
  other->SetSecurityToken(token);
  current->SetSecurityToken(token);

  // Setup reference from current to other.
  current->Global()->Set(v8_str("other"), other->Global());

  // Trigger lazy loading in other context.
  Local<Script> script =
      Script::Compile(v8_str("other.eval('new Date(42)')"));
  Local<Value> value = script->Run();
  CHECK_EQ(42.0, value->NumberValue());
}


static v8::Handle<Value> call_as_function(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return args[0];
}


// Test that a call handler can be set for objects which will allow
// non-function objects created through the API to be called as
// functions.
THREADED_TEST(CallAsFunction) {
  v8::HandleScope scope;
  LocalContext context;

  Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
  Local<ObjectTemplate> instance_template = t->InstanceTemplate();
  instance_template->SetCallAsFunctionHandler(call_as_function);
  Local<v8::Object> instance = t->GetFunction()->NewInstance();
  context->Global()->Set(v8_str("obj"), instance);
  v8::TryCatch try_catch;
  Local<Value> value;
  CHECK(!try_catch.HasCaught());

  value = Script::Compile(v8_str("obj(42)"))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(42, value->Int32Value());

  value = Script::Compile(v8_str("(function(o){return o(49)})(obj)"))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(49, value->Int32Value());

  // test special case of call as function
  value = Script::Compile(v8_str("[obj]['0'](45)"))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(45, value->Int32Value());

  value = Script::Compile(v8_str("obj.call = Function.prototype.call;"
                                 "obj.call(null, 87)"))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(87, value->Int32Value());

  // Regression tests for bug #1116356: Calling call through call/apply
  // must work for non-function receivers.
  const char* apply_99 = "Function.prototype.call.apply(obj, [this, 99])";
  value = Script::Compile(v8_str(apply_99))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(99, value->Int32Value());

  const char* call_17 = "Function.prototype.call.call(obj, this, 17)";
  value = Script::Compile(v8_str(call_17))->Run();
  CHECK(!try_catch.HasCaught());
  CHECK_EQ(17, value->Int32Value());

  // Try something that will cause an exception: Call the object as a
  // constructor. This should be the last test.
  value = Script::Compile(v8_str("new obj(42)"))->Run();
  CHECK(try_catch.HasCaught());
}


static int CountHandles() {
  return v8::HandleScope::NumberOfHandles();
}


static int Recurse(int depth, int iterations) {
  v8::HandleScope scope;
  if (depth == 0) return CountHandles();
  for (int i = 0; i < iterations; i++) {
    Local<v8::Number> n = v8::Integer::New(42);
  }
  return Recurse(depth - 1, iterations);
}


THREADED_TEST(HandleIteration) {
  static const int kIterations = 500;
  static const int kNesting = 200;
  CHECK_EQ(0, CountHandles());
  {
    v8::HandleScope scope1;
    CHECK_EQ(0, CountHandles());
    for (int i = 0; i < kIterations; i++) {
      Local<v8::Number> n = v8::Integer::New(42);
      CHECK_EQ(i + 1, CountHandles());
    }

    CHECK_EQ(kIterations, CountHandles());
    {
      v8::HandleScope scope2;
      for (int j = 0; j < kIterations; j++) {
        Local<v8::Number> n = v8::Integer::New(42);
        CHECK_EQ(j + 1 + kIterations, CountHandles());
      }
    }
    CHECK_EQ(kIterations, CountHandles());
  }
  CHECK_EQ(0, CountHandles());
  CHECK_EQ(kNesting * kIterations, Recurse(kNesting, kIterations));
}


static v8::Handle<Value> InterceptorHasOwnPropertyGetter(
    Local<String> name,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8::Handle<Value>();
}


THREADED_TEST(InterceptorHasOwnProperty) {
  v8::HandleScope scope;
  LocalContext context;
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New();
  Local<v8::ObjectTemplate> instance_templ = fun_templ->InstanceTemplate();
  instance_templ->SetNamedPropertyHandler(InterceptorHasOwnPropertyGetter);
  Local<Function> function = fun_templ->GetFunction();
  context->Global()->Set(v8_str("constructor"), function);
  v8::Handle<Value> value = CompileRun(
      "var o = new constructor();"
      "o.hasOwnProperty('ostehaps');");
  CHECK_EQ(false, value->BooleanValue());
  value = CompileRun(
      "o.ostehaps = 42;"
      "o.hasOwnProperty('ostehaps');");
  CHECK_EQ(true, value->BooleanValue());
  value = CompileRun(
      "var p = new constructor();"
      "p.hasOwnProperty('ostehaps');");
  CHECK_EQ(false, value->BooleanValue());
}


static v8::Handle<Value> InterceptorLoadICGetter(Local<String> name,
                                                 const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("x")->Equals(name));
  return v8::Integer::New(42);
}


// This test should hit the load IC for the interceptor case.
THREADED_TEST(InterceptorLoadIC) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(InterceptorLoadICGetter);
  LocalContext context;
  context->Global()->Set(v8_str("o"), templ->NewInstance());
  v8::Handle<Value> value = CompileRun(
    "var result = 0;"
    "for (var i = 0; i < 1000; i++) {"
    "  result = o.x;"
    "}");
  CHECK_EQ(42, value->Int32Value());
}


static v8::Handle<Value> InterceptorStoreICSetter(
    Local<String> key, Local<Value> value, const AccessorInfo&) {
  CHECK(v8_str("x")->Equals(key));
  CHECK_EQ(42, value->Int32Value());
  return value;
}


// This test should hit the store IC for the interceptor case.
THREADED_TEST(InterceptorStoreIC) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(InterceptorLoadICGetter,
                                 InterceptorStoreICSetter);
  LocalContext context;
  context->Global()->Set(v8_str("o"), templ->NewInstance());
  v8::Handle<Value> value = CompileRun(
    "for (var i = 0; i < 1000; i++) {"
    "  o.x = 42;"
    "}");
}



v8::Handle<Value> call_ic_function;
v8::Handle<Value> call_ic_function2;
v8::Handle<Value> call_ic_function3;

static v8::Handle<Value> InterceptorCallICGetter(Local<String> name,
                                                 const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  CHECK(v8_str("x")->Equals(name));
  return call_ic_function;
}


// This test should hit the call IC for the interceptor case.
THREADED_TEST(InterceptorCallIC) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(InterceptorCallICGetter);
  LocalContext context;
  context->Global()->Set(v8_str("o"), templ->NewInstance());
  call_ic_function =
      v8_compile("function f(x) { return x + 1; }; f")->Run();
  v8::Handle<Value> value = CompileRun(
    "var result = 0;"
    "for (var i = 0; i < 1000; i++) {"
    "  result = o.x(41);"
    "}");
  CHECK_EQ(42, value->Int32Value());
}

static int interceptor_call_count = 0;

static v8::Handle<Value> InterceptorICRefErrorGetter(Local<String> name,
                                                     const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  if (v8_str("x")->Equals(name) && interceptor_call_count++ < 20) {
    return call_ic_function2;
  }
  return v8::Handle<Value>();
}


// This test should hit load and call ICs for the interceptor case.
// Once in a while, the interceptor will reply that a property was not
// found in which case we should get a reference error.
THREADED_TEST(InterceptorICReferenceErrors) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(InterceptorICRefErrorGetter);
  LocalContext context(0, templ, v8::Handle<Value>());
  call_ic_function2 = v8_compile("function h(x) { return x; }; h")->Run();
  v8::Handle<Value> value = CompileRun(
    "function f() {"
    "  for (var i = 0; i < 1000; i++) {"
    "    try { x; } catch(e) { return true; }"
    "  }"
    "  return false;"
    "};"
    "f();");
  CHECK_EQ(true, value->BooleanValue());
  interceptor_call_count = 0;
  value = CompileRun(
    "function g() {"
    "  for (var i = 0; i < 1000; i++) {"
    "    try { x(42); } catch(e) { return true; }"
    "  }"
    "  return false;"
    "};"
    "g();");
  CHECK_EQ(true, value->BooleanValue());
}


static int interceptor_ic_exception_get_count = 0;

static v8::Handle<Value> InterceptorICExceptionGetter(
    Local<String> name,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  if (v8_str("x")->Equals(name) && ++interceptor_ic_exception_get_count < 20) {
    return call_ic_function3;
  }
  if (interceptor_ic_exception_get_count == 20) {
    return v8::ThrowException(v8_num(42));
  }
  // Do not handle get for properties other than x.
  return v8::Handle<Value>();
}

// Test interceptor load/call IC where the interceptor throws an
// exception once in a while.
THREADED_TEST(InterceptorICGetterExceptions) {
  interceptor_ic_exception_get_count = 0;
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(InterceptorICExceptionGetter);
  LocalContext context(0, templ, v8::Handle<Value>());
  call_ic_function3 = v8_compile("function h(x) { return x; }; h")->Run();
  v8::Handle<Value> value = CompileRun(
    "function f() {"
    "  for (var i = 0; i < 100; i++) {"
    "    try { x; } catch(e) { return true; }"
    "  }"
    "  return false;"
    "};"
    "f();");
  CHECK_EQ(true, value->BooleanValue());
  interceptor_ic_exception_get_count = 0;
  value = CompileRun(
    "function f() {"
    "  for (var i = 0; i < 100; i++) {"
    "    try { x(42); } catch(e) { return true; }"
    "  }"
    "  return false;"
    "};"
    "f();");
  CHECK_EQ(true, value->BooleanValue());
}


static int interceptor_ic_exception_set_count = 0;

static v8::Handle<Value> InterceptorICExceptionSetter(
      Local<String> key, Local<Value> value, const AccessorInfo&) {
  ApiTestFuzzer::Fuzz();
  if (++interceptor_ic_exception_set_count > 20) {
    return v8::ThrowException(v8_num(42));
  }
  // Do not actually handle setting.
  return v8::Handle<Value>();
}

// Test interceptor store IC where the interceptor throws an exception
// once in a while.
THREADED_TEST(InterceptorICSetterExceptions) {
  interceptor_ic_exception_set_count = 0;
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(0, InterceptorICExceptionSetter);
  LocalContext context(0, templ, v8::Handle<Value>());
  v8::Handle<Value> value = CompileRun(
    "function f() {"
    "  for (var i = 0; i < 100; i++) {"
    "    try { x = 42; } catch(e) { return true; }"
    "  }"
    "  return false;"
    "};"
    "f();");
  CHECK_EQ(true, value->BooleanValue());
}


// Test that we ignore null interceptors.
THREADED_TEST(NullNamedInterceptor) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetNamedPropertyHandler(0);
  LocalContext context;
  templ->Set("x", v8_num(42));
  v8::Handle<v8::Object> obj = templ->NewInstance();
  context->Global()->Set(v8_str("obj"), obj);
  v8::Handle<Value> value = CompileRun("obj.x");
  CHECK(value->IsInt32());
  CHECK_EQ(42, value->Int32Value());
}


// Test that we ignore null interceptors.
THREADED_TEST(NullIndexedInterceptor) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetIndexedPropertyHandler(0);
  LocalContext context;
  templ->Set("42", v8_num(42));
  v8::Handle<v8::Object> obj = templ->NewInstance();
  context->Global()->Set(v8_str("obj"), obj);
  v8::Handle<Value> value = CompileRun("obj[42]");
  CHECK(value->IsInt32());
  CHECK_EQ(42, value->Int32Value());
}


static v8::Handle<Value> ParentGetter(Local<String> name,
                                      const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8_num(1);
}


static v8::Handle<Value> ChildGetter(Local<String> name,
                                     const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8_num(42);
}


THREADED_TEST(Overriding) {
  v8::HandleScope scope;
  LocalContext context;

  // Parent template.
  Local<v8::FunctionTemplate> parent_templ = v8::FunctionTemplate::New();
  Local<ObjectTemplate> parent_instance_templ =
      parent_templ->InstanceTemplate();
  parent_instance_templ->SetAccessor(v8_str("f"), ParentGetter);

  // Template that inherits from the parent template.
  Local<v8::FunctionTemplate> child_templ = v8::FunctionTemplate::New();
  Local<ObjectTemplate> child_instance_templ =
      child_templ->InstanceTemplate();
  child_templ->Inherit(parent_templ);
  // Override 'f'.  The child version of 'f' should get called for child
  // instances.
  child_instance_templ->SetAccessor(v8_str("f"), ChildGetter);
  // Add 'g' twice.  The 'g' added last should get called for instances.
  child_instance_templ->SetAccessor(v8_str("g"), ParentGetter);
  child_instance_templ->SetAccessor(v8_str("g"), ChildGetter);

  // Add 'h' as an accessor to the proto template with ReadOnly attributes
  // so 'h' can be shadowed on the instance object.
  Local<ObjectTemplate> child_proto_templ = child_templ->PrototypeTemplate();
  child_proto_templ->SetAccessor(v8_str("h"), ParentGetter, 0,
      v8::Handle<Value>(), v8::DEFAULT, v8::ReadOnly);

  // Add 'i' as an accessor to the instance template with ReadOnly attributes
  // but the attribute does not have effect because it is duplicated with
  // NULL setter.
  child_instance_templ->SetAccessor(v8_str("i"), ChildGetter, 0,
      v8::Handle<Value>(), v8::DEFAULT, v8::ReadOnly);



  // Instantiate the child template.
  Local<v8::Object> instance = child_templ->GetFunction()->NewInstance();

  // Check that the child function overrides the parent one.
  context->Global()->Set(v8_str("o"), instance);
  Local<Value> value = v8_compile("o.f")->Run();
  // Check that the 'g' that was added last is hit.
  CHECK_EQ(42, value->Int32Value());
  value = v8_compile("o.g")->Run();
  CHECK_EQ(42, value->Int32Value());

  // Check 'h' can be shadowed.
  value = v8_compile("o.h = 3; o.h")->Run();
  CHECK_EQ(3, value->Int32Value());

  // Check 'i' is cannot be shadowed or changed.
  value = v8_compile("o.i = 3; o.i")->Run();
  CHECK_EQ(42, value->Int32Value());
}


static v8::Handle<Value> IsConstructHandler(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  if (args.IsConstructCall()) {
    return v8::Boolean::New(true);
  }
  return v8::Boolean::New(false);
}


THREADED_TEST(IsConstructCall) {
  v8::HandleScope scope;

  // Function template with call handler.
  Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->SetCallHandler(IsConstructHandler);

  LocalContext context;

  context->Global()->Set(v8_str("f"), templ->GetFunction());
  Local<Value> value = v8_compile("f()")->Run();
  CHECK(!value->BooleanValue());
  value = v8_compile("new f()")->Run();
  CHECK(value->BooleanValue());
}


THREADED_TEST(ObjectProtoToString) {
  v8::HandleScope scope;
  Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New();
  templ->SetClassName(v8_str("MyClass"));

  LocalContext context;

  Local<String> customized_tostring = v8_str("customized toString");

  // Replace Object.prototype.toString
  v8_compile("Object.prototype.toString = function() {"
                  "  return 'customized toString';"
                  "}")->Run();

  // Normal ToString call should call replaced Object.prototype.toString
  Local<v8::Object> instance = templ->GetFunction()->NewInstance();
  Local<String> value = instance->ToString();
  CHECK(value->IsString() && value->Equals(customized_tostring));

  // ObjectProtoToString should not call replace toString function.
  value = instance->ObjectProtoToString();
  CHECK(value->IsString() && value->Equals(v8_str("[object MyClass]")));

  // Check global
  value = context->Global()->ObjectProtoToString();
  CHECK(value->IsString() && value->Equals(v8_str("[object global]")));

  // Check ordinary object
  Local<Value> object = v8_compile("new Object()")->Run();
  value = Local<v8::Object>::Cast(object)->ObjectProtoToString();
  CHECK(value->IsString() && value->Equals(v8_str("[object Object]")));
}


bool ApiTestFuzzer::fuzzing_ = false;
v8::internal::Semaphore* ApiTestFuzzer::all_tests_done_=
  v8::internal::OS::CreateSemaphore(0);
int ApiTestFuzzer::active_tests_;
int ApiTestFuzzer::tests_being_run_;
int ApiTestFuzzer::current_;


// We are in a callback and want to switch to another thread (if we
// are currently running the thread fuzzing test).
void ApiTestFuzzer::Fuzz() {
  if (!fuzzing_) return;
  ApiTestFuzzer* test = RegisterThreadedTest::nth(current_)->fuzzer_;
  test->ContextSwitch();
}


// Let the next thread go.  Since it is also waiting on the V8 lock it may
// not start immediately.
bool ApiTestFuzzer::NextThread() {
  int test_position = GetNextTestNumber();
  int test_number = RegisterThreadedTest::nth(current_)->fuzzer_->test_number_;
  if (test_position == current_) {
    printf("Stay with %d\n", test_number);
    return false;
  }
  printf("Switch from %d to %d\n",
         current_ < 0 ? 0 : test_number, test_position < 0 ? 0 : test_number);
  current_ = test_position;
  RegisterThreadedTest::nth(current_)->fuzzer_->gate_->Signal();
  return true;
}


void ApiTestFuzzer::Run() {
  // When it is our turn...
  gate_->Wait();
  {
    // ... get the V8 lock and start running the test.
    v8::Locker locker;
    CallTest();
  }
  // This test finished.
  active_ = false;
  active_tests_--;
  // If it was the last then signal that fact.
  if (active_tests_ == 0) {
    all_tests_done_->Signal();
  } else {
    // Otherwise select a new test and start that.
    NextThread();
  }
}


static unsigned linear_congruential_generator;


void ApiTestFuzzer::Setup(PartOfTest part) {
  linear_congruential_generator = i::FLAG_testing_prng_seed;
  fuzzing_ = true;
  int start = (part == FIRST_PART) ? 0 : (RegisterThreadedTest::count() >> 1);
  int end = (part == FIRST_PART)
      ? (RegisterThreadedTest::count() >> 1)
      : RegisterThreadedTest::count();
  active_tests_ = tests_being_run_ = end - start;
  for (int i = 0; i < tests_being_run_; i++) {
    RegisterThreadedTest::nth(i)->fuzzer_ = new ApiTestFuzzer(i + start);
  }
  for (int i = 0; i < active_tests_; i++) {
    RegisterThreadedTest::nth(i)->fuzzer_->Start();
  }
}


static void CallTestNumber(int test_number) {
  (RegisterThreadedTest::nth(test_number)->callback())();
}


void ApiTestFuzzer::RunAllTests() {
  // Set off the first test.
  current_ = -1;
  NextThread();
  // Wait till they are all done.
  all_tests_done_->Wait();
}


int ApiTestFuzzer::GetNextTestNumber() {
  int next_test;
  do {
    next_test = (linear_congruential_generator >> 16) % tests_being_run_;
    linear_congruential_generator *= 1664525u;
    linear_congruential_generator += 1013904223u;
  } while (!RegisterThreadedTest::nth(next_test)->fuzzer_->active_);
  return next_test;
}


void ApiTestFuzzer::ContextSwitch() {
  // If the new thread is the same as the current thread there is nothing to do.
  if (NextThread()) {
    // Now it can start.
    v8::Unlocker unlocker;
    // Wait till someone starts us again.
    gate_->Wait();
    // And we're off.
  }
}


void ApiTestFuzzer::TearDown() {
  fuzzing_ = false;
  for (int i = 0; i < RegisterThreadedTest::count(); i++) {
    ApiTestFuzzer *fuzzer = RegisterThreadedTest::nth(i)->fuzzer_;
    if (fuzzer != NULL) fuzzer->Join();
  }
}


// Lets not be needlessly self-referential.
TEST(Threading) {
  ApiTestFuzzer::Setup(ApiTestFuzzer::FIRST_PART);
  ApiTestFuzzer::RunAllTests();
  ApiTestFuzzer::TearDown();
}

TEST(Threading2) {
  ApiTestFuzzer::Setup(ApiTestFuzzer::SECOND_PART);
  ApiTestFuzzer::RunAllTests();
  ApiTestFuzzer::TearDown();
}


void ApiTestFuzzer::CallTest() {
  printf("Start test %d\n", test_number_);
  CallTestNumber(test_number_);
  printf("End test %d\n", test_number_);
}


static v8::Handle<Value> ThrowInJS(const v8::Arguments& args) {
  CHECK(v8::Locker::IsLocked());
  ApiTestFuzzer::Fuzz();
  v8::Unlocker unlocker;
  const char* code = "throw 7;";
  {
    v8::Locker nested_locker;
    v8::HandleScope scope;
    v8::Handle<Value> exception;
    { v8::TryCatch try_catch;
      v8::Handle<Value> value = CompileRun(code);
      CHECK(value.IsEmpty());
      CHECK(try_catch.HasCaught());
      // Make sure to wrap the exception in a new handle because
      // the handle returned from the TryCatch is destroyed
      // when the TryCatch is destroyed.
      exception = Local<Value>::New(try_catch.Exception());
    }
    return v8::ThrowException(exception);
  }
}


static v8::Handle<Value> ThrowInJSNoCatch(const v8::Arguments& args) {
  CHECK(v8::Locker::IsLocked());
  ApiTestFuzzer::Fuzz();
  v8::Unlocker unlocker;
  const char* code = "throw 7;";
  {
    v8::Locker nested_locker;
    v8::HandleScope scope;
    v8::Handle<Value> value = CompileRun(code);
    CHECK(value.IsEmpty());
    return v8_str("foo");
  }
}


// These are locking tests that don't need to be run again
// as part of the locking aggregation tests.
TEST(NestedLockers) {
  v8::Locker locker;
  CHECK(v8::Locker::IsLocked());
  v8::HandleScope scope;
  LocalContext env;
  Local<v8::FunctionTemplate> fun_templ = v8::FunctionTemplate::New(ThrowInJS);
  Local<Function> fun = fun_templ->GetFunction();
  env->Global()->Set(v8_str("throw_in_js"), fun);
  Local<Script> script = v8_compile("(function () {"
                                    "  try {"
                                    "    throw_in_js();"
                                    "    return 42;"
                                    "  } catch (e) {"
                                    "    return e * 13;"
                                    "  }"
                                    "})();");
  CHECK_EQ(91, script->Run()->Int32Value());
}


// These are locking tests that don't need to be run again
// as part of the locking aggregation tests.
TEST(NestedLockersNoTryCatch) {
  v8::Locker locker;
  v8::HandleScope scope;
  LocalContext env;
  Local<v8::FunctionTemplate> fun_templ =
      v8::FunctionTemplate::New(ThrowInJSNoCatch);
  Local<Function> fun = fun_templ->GetFunction();
  env->Global()->Set(v8_str("throw_in_js"), fun);
  Local<Script> script = v8_compile("(function () {"
                                    "  try {"
                                    "    throw_in_js();"
                                    "    return 42;"
                                    "  } catch (e) {"
                                    "    return e * 13;"
                                    "  }"
                                    "})();");
  CHECK_EQ(91, script->Run()->Int32Value());
}


THREADED_TEST(RecursiveLocking) {
  v8::Locker locker;
  {
    v8::Locker locker2;
    CHECK(v8::Locker::IsLocked());
  }
}


static v8::Handle<Value> UnlockForAMoment(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  v8::Unlocker unlocker;
  return v8::Undefined();
}


THREADED_TEST(LockUnlockLock) {
  {
    v8::Locker locker;
    v8::HandleScope scope;
    LocalContext env;
    Local<v8::FunctionTemplate> fun_templ =
        v8::FunctionTemplate::New(UnlockForAMoment);
    Local<Function> fun = fun_templ->GetFunction();
    env->Global()->Set(v8_str("unlock_for_a_moment"), fun);
    Local<Script> script = v8_compile("(function () {"
                                      "  unlock_for_a_moment();"
                                      "  return 42;"
                                      "})();");
    CHECK_EQ(42, script->Run()->Int32Value());
  }
  {
    v8::Locker locker;
    v8::HandleScope scope;
    LocalContext env;
    Local<v8::FunctionTemplate> fun_templ =
        v8::FunctionTemplate::New(UnlockForAMoment);
    Local<Function> fun = fun_templ->GetFunction();
    env->Global()->Set(v8_str("unlock_for_a_moment"), fun);
    Local<Script> script = v8_compile("(function () {"
                                      "  unlock_for_a_moment();"
                                      "  return 42;"
                                      "})();");
    CHECK_EQ(42, script->Run()->Int32Value());
  }
}


static int GetSurvivingGlobalObjectsCount() {
  int count = 0;
  // We need to collect all garbage twice to be sure that everything
  // has been collected.  This is because inline caches are cleared in
  // the first garbage collection but some of the maps have already
  // been marked at that point.  Therefore some of the maps are not
  // collected until the second garbage collection.
  v8::internal::Heap::CollectAllGarbage();
  v8::internal::Heap::CollectAllGarbage();
  v8::internal::HeapIterator it;
  while (it.has_next()) {
    v8::internal::HeapObject* object = it.next();
    if (object->IsJSGlobalObject()) {
      count++;
    }
  }
#ifdef DEBUG
  if (count > 0) v8::internal::Heap::TracePathToGlobal();
#endif
  return count;
}


TEST(DontLeakGlobalObjects) {
  // Regression test for issues 1139850 and 1174891.

  v8::V8::Initialize();

  int count = GetSurvivingGlobalObjectsCount();

  for (int i = 0; i < 5; i++) {
    { v8::HandleScope scope;
      LocalContext context;
    }
    CHECK_EQ(count, GetSurvivingGlobalObjectsCount());

    { v8::HandleScope scope;
      LocalContext context;
      v8_compile("Date")->Run();
    }
    CHECK_EQ(count, GetSurvivingGlobalObjectsCount());

    { v8::HandleScope scope;
      LocalContext context;
      v8_compile("/aaa/")->Run();
    }
    CHECK_EQ(count, GetSurvivingGlobalObjectsCount());

    { v8::HandleScope scope;
      const char* extension_list[] = { "v8/gc" };
      v8::ExtensionConfiguration extensions(1, extension_list);
      LocalContext context(&extensions);
      v8_compile("gc();")->Run();
    }
    CHECK_EQ(count, GetSurvivingGlobalObjectsCount());
  }
}


THREADED_TEST(CheckForCrossContextObjectLiterals) {
  v8::V8::Initialize();

  const int nof = 2;
  const char* sources[nof] = {
    "try { [ 2, 3, 4 ].forEach(5); } catch(e) { e.toString(); }",
    "Object()"
  };

  for (int i = 0; i < nof; i++) {
    const char* source = sources[i];
    { v8::HandleScope scope;
      LocalContext context;
      CompileRun(source);
    }
    { v8::HandleScope scope;
      LocalContext context;
      CompileRun(source);
    }
  }
}


static v8::Handle<Value> NestedScope(v8::Persistent<Context> env) {
  v8::HandleScope inner;
  env->Enter();
  v8::Handle<Value> three = v8_num(3);
  v8::Handle<Value> value = inner.Close(three);
  env->Exit();
  return value;
}


THREADED_TEST(NestedHandleScopeAndContexts) {
  v8::HandleScope outer;
  v8::Persistent<Context> env = Context::New();
  env->Enter();
  v8::Handle<Value> value = NestedScope(env);
  v8::Handle<String> str = value->ToString();
  env->Exit();
  env.Dispose();
}


THREADED_TEST(ExternalAllocatedMemory) {
  v8::HandleScope outer;
  const int kSize = 1024*1024;
  CHECK_EQ(v8::V8::AdjustAmountOfExternalAllocatedMemory(kSize), kSize);
  CHECK_EQ(v8::V8::AdjustAmountOfExternalAllocatedMemory(-kSize), 0);
}


THREADED_TEST(DisposeEnteredContext) {
  v8::HandleScope scope;
  LocalContext outer;
  { v8::Persistent<v8::Context> inner = v8::Context::New();
    inner->Enter();
    inner.Dispose();
    inner.Clear();
    inner->Exit();
  }
}


// Regression test for issue 54, object templates with internal fields
// but no accessors or interceptors did not get their internal field
// count set on instances.
THREADED_TEST(Regress54) {
  v8::HandleScope outer;
  LocalContext context;
  static v8::Persistent<v8::ObjectTemplate> templ;
  if (templ.IsEmpty()) {
    v8::HandleScope inner;
    v8::Handle<v8::ObjectTemplate> local = v8::ObjectTemplate::New();
    local->SetInternalFieldCount(1);
    templ = v8::Persistent<v8::ObjectTemplate>::New(inner.Close(local));
  }
  v8::Handle<v8::Object> result = templ->NewInstance();
  CHECK_EQ(1, result->InternalFieldCount());
}


// If part of the threaded tests, this test makes ThreadingTest fail
// on mac.
TEST(CatchStackOverflow) {
  v8::HandleScope scope;
  LocalContext context;
  v8::TryCatch try_catch;
  v8::Handle<v8::Script> script = v8::Script::Compile(v8::String::New(
    "function f() {"
    "  return f();"
    "}"
    ""
    "f();"));
  v8::Handle<v8::Value> result = script->Run();
  CHECK(result.IsEmpty());
}


static void CheckTryCatchSourceInfo(v8::Handle<v8::Script> script,
                                    const char* resource_name,
                                    int line_offset) {
  v8::HandleScope scope;
  v8::TryCatch try_catch;
  v8::Handle<v8::Value> result = script->Run();
  CHECK(result.IsEmpty());
  CHECK(try_catch.HasCaught());
  v8::Handle<v8::Message> message = try_catch.Message();
  CHECK(!message.IsEmpty());
  CHECK_EQ(10 + line_offset, message->GetLineNumber());
  CHECK_EQ(91, message->GetStartPosition());
  CHECK_EQ(92, message->GetEndPosition());
  CHECK_EQ(2, message->GetStartColumn());
  CHECK_EQ(3, message->GetEndColumn());
  v8::String::AsciiValue line(message->GetSourceLine());
  CHECK_EQ("  throw 'nirk';", *line);
  v8::String::AsciiValue name(message->GetScriptResourceName());
  CHECK_EQ(resource_name, *name);
}


THREADED_TEST(TryCatchSourceInfo) {
  v8::HandleScope scope;
  LocalContext context;
  v8::Handle<v8::String> source = v8::String::New(
      "function Foo() {\n"
      "  return Bar();\n"
      "}\n"
      "\n"
      "function Bar() {\n"
      "  return Baz();\n"
      "}\n"
      "\n"
      "function Baz() {\n"
      "  throw 'nirk';\n"
      "}\n"
      "\n"
      "Foo();\n");

  const char* resource_name;
  v8::Handle<v8::Script> script;
  resource_name = "test.js";
  script = v8::Script::Compile(source, v8::String::New(resource_name));
  CheckTryCatchSourceInfo(script, resource_name, 0);

  resource_name = "test1.js";
  v8::ScriptOrigin origin1(v8::String::New(resource_name));
  script = v8::Script::Compile(source, &origin1);
  CheckTryCatchSourceInfo(script, resource_name, 0);

  resource_name = "test2.js";
  v8::ScriptOrigin origin2(v8::String::New(resource_name), v8::Integer::New(7));
  script = v8::Script::Compile(source, &origin2);
  CheckTryCatchSourceInfo(script, resource_name, 7);
}


THREADED_TEST(CompilationCache) {
  v8::HandleScope scope;
  LocalContext context;
  v8::Handle<v8::String> source0 = v8::String::New("1234");
  v8::Handle<v8::String> source1 = v8::String::New("1234");
  v8::Handle<v8::Script> script0 =
      v8::Script::Compile(source0, v8::String::New("test.js"));
  v8::Handle<v8::Script> script1 =
      v8::Script::Compile(source1, v8::String::New("test.js"));
  v8::Handle<v8::Script> script2 =
      v8::Script::Compile(source0);  // different origin
  CHECK_EQ(1234, script0->Run()->Int32Value());
  CHECK_EQ(1234, script1->Run()->Int32Value());
  CHECK_EQ(1234, script2->Run()->Int32Value());
}


static v8::Handle<Value> FunctionNameCallback(const v8::Arguments& args) {
  ApiTestFuzzer::Fuzz();
  return v8_num(42);
}


THREADED_TEST(CallbackFunctionName) {
  v8::HandleScope scope;
  LocalContext context;
  Local<ObjectTemplate> t = ObjectTemplate::New();
  t->Set(v8_str("asdf"), v8::FunctionTemplate::New(FunctionNameCallback));
  context->Global()->Set(v8_str("obj"), t->NewInstance());
  v8::Handle<v8::Value> value = CompileRun("obj.asdf.name");
  CHECK(value->IsString());
  v8::String::AsciiValue name(value);
  CHECK_EQ("asdf", *name);
}


THREADED_TEST(DateAccess) {
  v8::HandleScope scope;
  LocalContext context;
  v8::Handle<v8::Value> date = v8::Date::New(1224744689038.0);
  CHECK(date->IsDate());
  CHECK_EQ(1224744689038.0, v8::Handle<v8::Date>::Cast(date)->NumberValue());
}


void CheckProperties(v8::Handle<v8::Value> val, int elmc, const char* elmv[]) {
  v8::Handle<v8::Object> obj = v8::Handle<v8::Object>::Cast(val);
  v8::Handle<v8::Array> props = obj->GetPropertyNames();
  CHECK_EQ(elmc, props->Length());
  for (int i = 0; i < elmc; i++) {
    v8::String::Utf8Value elm(props->Get(v8::Integer::New(i)));
    CHECK_EQ(elmv[i], *elm);
  }
}


THREADED_TEST(PropertyEnumeration) {
  v8::HandleScope scope;
  LocalContext context;
  v8::Handle<v8::Value> obj = v8::Script::Compile(v8::String::New(
      "var result = [];"
      "result[0] = {};"
      "result[1] = {a: 1, b: 2};"
      "result[2] = [1, 2, 3];"
      "var proto = {x: 1, y: 2, z: 3};"
      "var x = { __proto__: proto, w: 0, z: 1 };"
      "result[3] = x;"
      "result;"))->Run();
  v8::Handle<v8::Array> elms = v8::Handle<v8::Array>::Cast(obj);
  CHECK_EQ(4, elms->Length());
  int elmc0 = 0;
  const char** elmv0 = NULL;
  CheckProperties(elms->Get(v8::Integer::New(0)), elmc0, elmv0);
  int elmc1 = 2;
  const char* elmv1[] = {"a", "b"};
  CheckProperties(elms->Get(v8::Integer::New(1)), elmc1, elmv1);
  int elmc2 = 3;
  const char* elmv2[] = {"0", "1", "2"};
  CheckProperties(elms->Get(v8::Integer::New(2)), elmc2, elmv2);
  int elmc3 = 4;
  const char* elmv3[] = {"w", "z", "x", "y"};
  CheckProperties(elms->Get(v8::Integer::New(3)), elmc3, elmv3);
}


static v8::Handle<Value> AccessorProhibitsOverwritingGetter(
    Local<String> name,
    const AccessorInfo& info) {
  ApiTestFuzzer::Fuzz();
  return v8::True();
}


THREADED_TEST(AccessorProhibitsOverwriting) {
  v8::HandleScope scope;
  LocalContext context;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessor(v8_str("x"),
                     AccessorProhibitsOverwritingGetter,
                     0,
                     v8::Handle<Value>(),
                     v8::PROHIBITS_OVERWRITING,
                     v8::ReadOnly);
  Local<v8::Object> instance = templ->NewInstance();
  context->Global()->Set(v8_str("obj"), instance);
  Local<Value> value = CompileRun(
      "obj.__defineGetter__('x', function() { return false; });"
      "obj.x");
  CHECK(value->BooleanValue());
  value = CompileRun(
      "var setter_called = false;"
      "obj.__defineSetter__('x', function() { setter_called = true; });"
      "obj.x = 42;"
      "setter_called");
  CHECK(!value->BooleanValue());
  value = CompileRun(
      "obj2 = {};"
      "obj2.__proto__ = obj;"
      "obj2.__defineGetter__('x', function() { return false; });"
      "obj2.x");
  CHECK(value->BooleanValue());
  value = CompileRun(
      "var setter_called = false;"
      "obj2 = {};"
      "obj2.__proto__ = obj;"
      "obj2.__defineSetter__('x', function() { setter_called = true; });"
      "obj2.x = 42;"
      "setter_called");
  CHECK(!value->BooleanValue());
}


static bool NamedSetAccessBlocker(Local<v8::Object> obj,
                                  Local<Value> name,
                                  v8::AccessType type,
                                  Local<Value> data) {
  return type != v8::ACCESS_SET;
}


static bool IndexedSetAccessBlocker(Local<v8::Object> obj,
                                    uint32_t key,
                                    v8::AccessType type,
                                    Local<Value> data) {
  return type != v8::ACCESS_SET;
}


THREADED_TEST(DisableAccessChecksWhileConfiguring) {
  v8::HandleScope scope;
  LocalContext context;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessCheckCallbacks(NamedSetAccessBlocker,
                                 IndexedSetAccessBlocker);
  templ->Set(v8_str("x"), v8::True());
  Local<v8::Object> instance = templ->NewInstance();
  context->Global()->Set(v8_str("obj"), instance);
  Local<Value> value = CompileRun("obj.x");
  CHECK(value->BooleanValue());
}

static bool NamedGetAccessBlocker(Local<v8::Object> obj,
                                  Local<Value> name,
                                  v8::AccessType type,
                                  Local<Value> data) {
  return false;
}


static bool IndexedGetAccessBlocker(Local<v8::Object> obj,
                                    uint32_t key,
                                    v8::AccessType type,
                                    Local<Value> data) {
  return false;
}



THREADED_TEST(AccessChecksReenabledCorrectly) {
  v8::HandleScope scope;
  LocalContext context;
  Local<ObjectTemplate> templ = ObjectTemplate::New();
  templ->SetAccessCheckCallbacks(NamedGetAccessBlocker,
                                 IndexedGetAccessBlocker);
  templ->Set(v8_str("a"), v8_str("a"));
  // Add more than 8 (see kMaxFastProperties) properties
  // so that the constructor will force copying map.
  // Cannot sprintf, gcc complains unsafety.
  char buf[4];
  for (char i = '0'; i <= '9' ; i++) {
    buf[0] = i;
    for (char j = '0'; j <= '9'; j++) {
      buf[1] = j;
      for (char k = '0'; k <= '9'; k++) {
        buf[2] = k;
        buf[3] = 0;
        templ->Set(v8_str(buf), v8::Number::New(k));
      }
    }
  }

  Local<v8::Object> instance_1 = templ->NewInstance();
  context->Global()->Set(v8_str("obj_1"), instance_1);

  Local<Value> value_1 = CompileRun("obj_1.a");
  CHECK(value_1->IsUndefined());

  Local<v8::Object> instance_2 = templ->NewInstance();
  context->Global()->Set(v8_str("obj_2"), instance_2);

  Local<Value> value_2 = CompileRun("obj_2.a");
  CHECK(value_2->IsUndefined());
}

// This tests that access check information remains on the global
// object template when creating contexts.
THREADED_TEST(AccessControlRepeatedContextCreation) {
  v8::HandleScope handle_scope;
  v8::Handle<v8::ObjectTemplate> global_template = v8::ObjectTemplate::New();
  global_template->SetAccessCheckCallbacks(NamedSetAccessBlocker,
                                           IndexedSetAccessBlocker);
  i::Handle<i::ObjectTemplateInfo> internal_template =
      v8::Utils::OpenHandle(*global_template);
  CHECK(!internal_template->constructor()->IsUndefined());
  i::Handle<i::FunctionTemplateInfo> constructor(
      i::FunctionTemplateInfo::cast(internal_template->constructor()));
  CHECK(!constructor->access_check_info()->IsUndefined());
  v8::Persistent<Context> context0 = Context::New(NULL, global_template);
  CHECK(!constructor->access_check_info()->IsUndefined());
}


// This test verifies that pre-compilation (aka preparsing) can be called
// without initializing the whole VM. Thus we cannot run this test in a
// multi-threaded setup.
TEST(PreCompile) {
  // TODO(155): This test would break without the initialization of V8. This is
  // a workaround for now to make this test not fail.
  v8::V8::Initialize();
  const char *script = "function foo(a) { return a+1; }";
  v8::ScriptData *sd = v8::ScriptData::PreCompile(script, strlen(script));
  CHECK_NE(sd->Length(), 0);
  CHECK_NE(sd->Data(), NULL);
  delete sd;
}


// This tests that we do not allow dictionary load/call inline caches
// to use functions that have not yet been compiled.  The potential
// problem of loading a function that has not yet been compiled can
// arise because we share code between contexts via the compilation
// cache.
THREADED_TEST(DictionaryICLoadedFunction) {
  v8::HandleScope scope;
  // Test LoadIC.
  for (int i = 0; i < 2; i++) {
    LocalContext context;
    context->Global()->Set(v8_str("tmp"), v8::True());
    context->Global()->Delete(v8_str("tmp"));
    CompileRun("for (var j = 0; j < 10; j++) new RegExp('');");
  }
  // Test CallIC.
  for (int i = 0; i < 2; i++) {
    LocalContext context;
    context->Global()->Set(v8_str("tmp"), v8::True());
    context->Global()->Delete(v8_str("tmp"));
    CompileRun("for (var j = 0; j < 10; j++) RegExp('')");
  }
}


// Test that cross-context new calls use the context of the callee to
// create the new JavaScript object.
THREADED_TEST(CrossContextNew) {
  v8::HandleScope scope;
  v8::Persistent<Context> context0 = Context::New();
  v8::Persistent<Context> context1 = Context::New();

  // Allow cross-domain access.
  Local<String> token = v8_str("<security token>");
  context0->SetSecurityToken(token);
  context1->SetSecurityToken(token);

  // Set an 'x' property on the Object prototype and define a
  // constructor function in context0.
  context0->Enter();
  CompileRun("Object.prototype.x = 42; function C() {};");
  context0->Exit();

  // Call the constructor function from context0 and check that the
  // result has the 'x' property.
  context1->Enter();
  context1->Global()->Set(v8_str("other"), context0->Global());
  Local<Value> value = CompileRun("var instance = new other.C(); instance.x");
  CHECK(value->IsInt32());
  CHECK_EQ(42, value->Int32Value());
  context1->Exit();

  // Dispose the contexts to allow them to be garbage collected.
  context0.Dispose();
  context1.Dispose();
}


class RegExpInterruptTest {
 public:
  RegExpInterruptTest() : block_(NULL) {}
  ~RegExpInterruptTest() { delete block_; }
  void RunTest() {
    block_ = i::OS::CreateSemaphore(0);
    gc_count_ = 0;
    gc_during_regexp_ = 0;
    regexp_success_ = false;
    gc_success_ = false;
    GCThread gc_thread(this);
    gc_thread.Start();
    v8::Locker::StartPreemption(1);

    LongRunningRegExp();
    {
      v8::Unlocker unlock;
      gc_thread.Join();
    }
    v8::Locker::StopPreemption();
    CHECK(regexp_success_);
    CHECK(gc_success_);
  }
 private:
  // Number of garbage collections required.
  static const int kRequiredGCs = 5;

  class GCThread : public i::Thread {
   public:
    explicit GCThread(RegExpInterruptTest* test)
        : test_(test) {}
    virtual void Run() {
      test_->CollectGarbage();
    }
   private:
     RegExpInterruptTest* test_;
  };

  void CollectGarbage() {
    block_->Wait();
    while (gc_during_regexp_ < kRequiredGCs) {
      {
        v8::Locker lock;
        // TODO(lrn): Perhaps create some garbage before collecting.
        i::Heap::CollectAllGarbage();
        gc_count_++;
      }
      i::OS::Sleep(1);
    }
    gc_success_ = true;
  }

  void LongRunningRegExp() {
    block_->Signal();  // Enable garbage collection thread on next preemption.
    int rounds = 0;
    while (gc_during_regexp_ < kRequiredGCs) {
      int gc_before = gc_count_;
      {
        // Match 15-30 "a"'s against 14 and a "b".
        const char* c_source =
            "/a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaa/"
            ".exec('aaaaaaaaaaaaaaab') === null";
        Local<String> source = String::New(c_source);
        Local<Script> script = Script::Compile(source);
        Local<Value> result = script->Run();
        if (!result->BooleanValue()) {
          gc_during_regexp_ = kRequiredGCs;  // Allow gc thread to exit.
          return;
        }
      }
      {
        // Match 15-30 "a"'s against 15 and a "b".
        const char* c_source =
            "/a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaa/"
            ".exec('aaaaaaaaaaaaaaaab')[0] === 'aaaaaaaaaaaaaaaa'";
        Local<String> source = String::New(c_source);
        Local<Script> script = Script::Compile(source);
        Local<Value> result = script->Run();
        if (!result->BooleanValue()) {
          gc_during_regexp_ = kRequiredGCs;
          return;
        }
      }
      int gc_after = gc_count_;
      gc_during_regexp_ += gc_after - gc_before;
      rounds++;
      i::OS::Sleep(1);
    }
    regexp_success_ = true;
  }

  i::Semaphore* block_;
  int gc_count_;
  int gc_during_regexp_;
  bool regexp_success_;
  bool gc_success_;
};


// Test that a regular expression execution can be interrupted and
// survive a garbage collection.
TEST(RegExpInterruption) {
  v8::Locker lock;
  v8::V8::Initialize();
  v8::HandleScope scope;
  Local<Context> local_env;
  {
    LocalContext env;
    local_env = env.local();
  }

  // Local context should still be live.
  CHECK(!local_env.IsEmpty());
  local_env->Enter();

  // Should complete without problems.
  RegExpInterruptTest().RunTest();

  local_env->Exit();
}


// Verify that we can clone an object
TEST(ObjectClone) {
  v8::HandleScope scope;
  LocalContext env;

  const char* sample =
    "var rv = {};"      \
    "rv.alpha = 'hello';" \
    "rv.beta = 123;"     \
    "rv;";

  // Create an object, verify basics.
  Local<Value> val = CompileRun(sample);
  CHECK(val->IsObject());
  Local<v8::Object> obj = Local<v8::Object>::Cast(val);
  obj->Set(v8_str("gamma"), v8_str("cloneme"));

  CHECK_EQ(v8_str("hello"), obj->Get(v8_str("alpha")));
  CHECK_EQ(v8::Integer::New(123), obj->Get(v8_str("beta")));
  CHECK_EQ(v8_str("cloneme"), obj->Get(v8_str("gamma")));

  // Clone it.
  Local<v8::Object> clone = obj->Clone();
  CHECK_EQ(v8_str("hello"), clone->Get(v8_str("alpha")));
  CHECK_EQ(v8::Integer::New(123), clone->Get(v8_str("beta")));
  CHECK_EQ(v8_str("cloneme"), clone->Get(v8_str("gamma")));

  // Set a property on the clone, verify each object.
  clone->Set(v8_str("beta"), v8::Integer::New(456));
  CHECK_EQ(v8::Integer::New(123), obj->Get(v8_str("beta")));
  CHECK_EQ(v8::Integer::New(456), clone->Get(v8_str("beta")));
}


class RegExpStringModificationTest {
 public:
  RegExpStringModificationTest()
      : block_(i::OS::CreateSemaphore(0)),
        morphs_(0),
        morphs_during_regexp_(0),
        ascii_resource_(i::Vector<const char>("aaaaaaaaaaaaaab", 15)),
        uc16_resource_(i::Vector<const uint16_t>(two_byte_content_, 15)) {}
  ~RegExpStringModificationTest() { delete block_; }
  void RunTest() {
    regexp_success_ = false;
    morph_success_ = false;

    // Initialize the contents of two_byte_content_ to be a uc16 representation
    // of "aaaaaaaaaaaaaab".
    for (int i = 0; i < 14; i++) {
      two_byte_content_[i] = 'a';
    }
    two_byte_content_[14] = 'b';

    // Create the input string for the regexp - the one we are going to change
    // properties of.
    input_ = i::Factory::NewExternalStringFromAscii(&ascii_resource_);

    // Inject the input as a global variable.
    i::Handle<i::String> input_name =
        i::Factory::NewStringFromAscii(i::Vector<const char>("input", 5));
    i::Top::global_context()->global()->SetProperty(*input_name, *input_, NONE);


    MorphThread morph_thread(this);
    morph_thread.Start();
    v8::Locker::StartPreemption(1);
    LongRunningRegExp();
    {
      v8::Unlocker unlock;
      morph_thread.Join();
    }
    v8::Locker::StopPreemption();
    CHECK(regexp_success_);
    CHECK(morph_success_);
  }
 private:

  class AsciiVectorResource : public v8::String::ExternalAsciiStringResource {
   public:
    explicit AsciiVectorResource(i::Vector<const char> vector)
        : data_(vector) {}
    virtual ~AsciiVectorResource() {}
    virtual size_t length() const { return data_.length(); }
    virtual const char* data() const { return data_.start(); }
   private:
    i::Vector<const char> data_;
  };
  class UC16VectorResource : public v8::String::ExternalStringResource {
   public:
    explicit UC16VectorResource(i::Vector<const i::uc16> vector)
        : data_(vector) {}
    virtual ~UC16VectorResource() {}
    virtual size_t length() const { return data_.length(); }
    virtual const i::uc16* data() const { return data_.start(); }
   private:
    i::Vector<const i::uc16> data_;
  };
  // Number of string modifications required.
  static const int kRequiredModifications = 5;
  static const int kMaxModifications = 100;

  class MorphThread : public i::Thread {
   public:
    explicit MorphThread(RegExpStringModificationTest* test)
        : test_(test) {}
    virtual void Run() {
      test_->MorphString();
    }
   private:
     RegExpStringModificationTest* test_;
  };

  void MorphString() {
    block_->Wait();
    while (morphs_during_regexp_ < kRequiredModifications &&
           morphs_ < kMaxModifications) {
      {
        v8::Locker lock;
        // Swap string between ascii and two-byte representation.
        i::String* string = *input_;
        CHECK(i::StringShape(string).IsExternal());
        if (i::StringShape(string).IsAsciiRepresentation()) {
          // Morph external string to be TwoByte string.
          i::ExternalAsciiString* ext_string =
              i::ExternalAsciiString::cast(string);
          i::ExternalTwoByteString* morphed =
              reinterpret_cast<i::ExternalTwoByteString*>(ext_string);
          morphed->map()->set_instance_type(i::SHORT_EXTERNAL_STRING_TYPE);
          morphed->set_resource(&uc16_resource_);
        } else {
          // Morph external string to be ASCII string.
          i::ExternalTwoByteString* ext_string =
              i::ExternalTwoByteString::cast(string);
          i::ExternalAsciiString* morphed =
              reinterpret_cast<i::ExternalAsciiString*>(ext_string);
          morphed->map()->set_instance_type(
              i::SHORT_EXTERNAL_ASCII_STRING_TYPE);
          morphed->set_resource(&ascii_resource_);
        }
        morphs_++;
      }
      i::OS::Sleep(1);
    }
    morph_success_ = true;
  }

  void LongRunningRegExp() {
    block_->Signal();  // Enable morphing thread on next preemption.
    while (morphs_during_regexp_ < kRequiredModifications &&
           morphs_ < kMaxModifications) {
      int morphs_before = morphs_;
      {
        // Match 15-30 "a"'s against 14 and a "b".
        const char* c_source =
            "/a?a?a?a?a?a?a?a?a?a?a?a?a?a?aaaaaaaaaaaaaaaa/"
            ".exec(input) === null";
        Local<String> source = String::New(c_source);
        Local<Script> script = Script::Compile(source);
        Local<Value> result = script->Run();
        CHECK(result->IsTrue());
      }
      int morphs_after = morphs_;
      morphs_during_regexp_ += morphs_after - morphs_before;
    }
    regexp_success_ = true;
  }

  i::uc16 two_byte_content_[15];
  i::Semaphore* block_;
  int morphs_;
  int morphs_during_regexp_;
  bool regexp_success_;
  bool morph_success_;
  i::Handle<i::String> input_;
  AsciiVectorResource ascii_resource_;
  UC16VectorResource uc16_resource_;
};


// Test that a regular expression execution can be interrupted and
// the string changed without failing.
TEST(RegExpStringModification) {
  v8::Locker lock;
  v8::V8::Initialize();
  v8::HandleScope scope;
  Local<Context> local_env;
  {
    LocalContext env;
    local_env = env.local();
  }

  // Local context should still be live.
  CHECK(!local_env.IsEmpty());
  local_env->Enter();

  // Should complete without problems.
  RegExpStringModificationTest().RunTest();

  local_env->Exit();
}


// Test that we can set a property on the global object even if there
// is a read-only property in the prototype chain.
TEST(ReadOnlyPropertyInGlobalProto) {
  v8::HandleScope scope;
  v8::Handle<v8::ObjectTemplate> templ = v8::ObjectTemplate::New();
  LocalContext context(0, templ);
  v8::Handle<v8::Object> global = context->Global();
  v8::Handle<v8::Object> global_proto =
      v8::Handle<v8::Object>::Cast(global->Get(v8_str("__proto__")));
  global_proto->Set(v8_str("x"), v8::Integer::New(0), v8::ReadOnly);
  global_proto->Set(v8_str("y"), v8::Integer::New(0), v8::ReadOnly);
  // Check without 'eval' or 'with'.
  v8::Handle<v8::Value> res =
      CompileRun("function f() { x = 42; return x; }; f()");
  // Check with 'eval'.
  res = CompileRun("function f() { eval('1'); y = 42; return y; }; f()");
  CHECK_EQ(v8::Integer::New(42), res);
  // Check with 'with'.
  res = CompileRun("function f() { with (this) { y = 42 }; return y; }; f()");
  CHECK_EQ(v8::Integer::New(42), res);
}
