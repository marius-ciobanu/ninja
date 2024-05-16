/* Generated by re2c 1.3 */
// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "dynout_parser.h"

#include "metrics.h"
#include "string_piece.h"

bool DynoutParser::Parse(const std::string& content,
                         std::vector<StringPiece>& result, std::string* err) {
  METRIC_RECORD("dynout parse");
  /// Split `content` into a series of individual text lines, without trailing
  /// newlines, and remove empty lines
  const char* input = content.c_str();
  const char* start = input;
  const char* limit = input + content.size();
  while (start < limit) {
    const char* end = start;
    // Note: \r\n will be treated as a line end + an empty line,
    // the latter will be ignored. This simplifies the logic in this
    // loop to reduce its code and speed it up.
    while (end < limit && (*end != '\n' && *end != '\r'))
      ++end;
    if (end > start)
      result.push_back(StringPiece(start, end - start));
    start = end + 1;
  }
  return true;
}