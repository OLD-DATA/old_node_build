// Copyright 2006-2009 the V8 project authors. All rights reserved.
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

#ifndef V8_FUNC_NAME_INFERRER_H_
#define V8_FUNC_NAME_INFERRER_H_

namespace v8 { namespace internal {

// FuncNameInferrer is a stateful class that is used to perform name
// inference for anonymous functions during static analysis of source code.
// Inference is performed in cases when an anonymous function is assigned
// to a variable or a property (see test-func-name-inference.cc for examples.)

// The basic idea is that during AST traversal LHSs of expressions are
// always visited before RHSs. Thus, during visiting the LHS, a name can be
// collected, and during visiting the RHS, a function literal can be collected.
// Inference is performed while leaving the assignment node.

class FuncNameInferrer BASE_EMBEDDED {
 public:
  FuncNameInferrer() :
      entries_stack_(10),
      names_stack_(5),
      func_to_infer_(NULL),
      dot_(Factory::NewStringFromAscii(CStrVector("."))) {
  }

  bool IsOpen() const { return !entries_stack_.is_empty(); }

  void PushEnclosingName(Handle<String> name);

  void Enter() {
    entries_stack_.Add(names_stack_.length());
  }

  void Leave() {
    ASSERT(IsOpen());
    names_stack_.Rewind(entries_stack_.RemoveLast());
  }

  void PushName(Handle<String> name) {
    if (IsOpen()) {
      names_stack_.Add(name);
    }
  }

  void SetFuncToInfer(FunctionLiteral* func_to_infer) {
    if (IsOpen()) {
      // If we encounter another function literal after already having
      // encountered one, the second one replaces the first.
      func_to_infer_ = func_to_infer;
    }
  }

  void InferAndLeave() {
    ASSERT(IsOpen());
    MaybeInferFunctionName();
    Leave();
  }

 private:
  Handle<String> MakeNameFromStack();
  Handle<String> MakeNameFromStackHelper(int pos, Handle<String> prev);
  void MaybeInferFunctionName();

  List<int> entries_stack_;
  List<Handle<String> > names_stack_;
  FunctionLiteral* func_to_infer_;
  Handle<String> dot_;

  DISALLOW_COPY_AND_ASSIGN(FuncNameInferrer);
};


// A wrapper class that automatically calls InferAndLeave when
// leaving scope.
class ScopedFuncNameInferrer BASE_EMBEDDED {
 public:
  explicit ScopedFuncNameInferrer(FuncNameInferrer* inferrer) :
      inferrer_(inferrer),
      is_entered_(false) {}
  ~ScopedFuncNameInferrer() {
    if (is_entered_) {
      inferrer_->InferAndLeave();
    }
  }

  void Enter() {
    inferrer_->Enter();
    is_entered_ = true;
  }

 private:
  FuncNameInferrer* inferrer_;
  bool is_entered_;

  DISALLOW_COPY_AND_ASSIGN(ScopedFuncNameInferrer);
};


} }  // namespace v8::internal

#endif  // V8_FUNC_NAME_INFERRER_H_
