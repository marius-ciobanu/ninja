// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_EXPLAIN_H_
#define NINJA_EXPLAIN_H_

#include <stdio.h>

#define EXPLAIN(node, fmt, ...) {                                       \
  if (g_explaining) {                                                   \
    char buf[1024];                                                     \
    snprintf(buf, 1024, fmt, __VA_ARGS__);                              \
    record_explanation(node, buf);                                      \
  }                                                                     \
}

struct Edge;
struct Node;

extern bool g_explaining;
void record_explanation(const Node* node, std::string reason);
void print_explanations(FILE *stream, const Edge* node);

extern bool g_keep_depfile;

extern bool g_keep_dynout;

extern bool g_keep_rsp;

extern bool g_experimental_statcache;

#endif // NINJA_EXPLAIN_H_
