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

#include <stdlib.h>
#include <string.h>
#include "v8.h"
#include "cctest.h"

using namespace v8::internal;

// Use a testing allocator that clears memory before deletion.
class ZeroingAllocationPolicy {
 public:
  static void* New(size_t size) {
    // Stash the size in the first word to use for Delete.
    size_t true_size = size + sizeof(size_t);
    size_t* result = reinterpret_cast<size_t*>(malloc(true_size));
    if (result == NULL) return result;
    *result = true_size;
    return result + 1;
  }

  static void Delete(void* ptr) {
    size_t* true_ptr = reinterpret_cast<size_t*>(ptr) - 1;
    memset(true_ptr, 0, *true_ptr);
    free(true_ptr);
  }
};

// Check that we can add (a reference to) an element of the list
// itself.
TEST(ListAdd) {
  // Add elements to the list to grow it to its capacity.
  List<int, ZeroingAllocationPolicy> list(4);
  list.Add(1);
  list.Add(2);
  list.Add(3);
  list.Add(4);

  // Add an existing element, the backing store should have to grow.
  list.Add(list[0]);
  ASSERT(list[4] == 1);
}
