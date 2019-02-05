/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <uapi/linux/ptrace.h>

BPF_PERF_OUTPUT(events);
BPF_PERCPU_ARRAY(perf_event_data, u64, EVENT_MAP_SIZE);
BPF_PERCPU_ARRAY(perf_cpu_index, u64, 1);

/// Saves the generic event header into the per-cpu map, returning the
/// initial index
static int saveEventHeader(u64 event_identifier,
                           u64 syscall_number,
                           bool save_exit_code,
                           int exit_code) {
  int index_key = 0U;
  u64 initial_slot = 0U;
  u64* index_ptr = perf_cpu_index.lookup_or_init(&index_key, &initial_slot);
  int event_index = (index_ptr != NULL ? *index_ptr : initial_slot);

  int index = event_index;
  perf_event_data.update(&index, &event_identifier);
  INCREMENT_EVENT_DATA_INDEX(index);

  perf_event_data.update(&index, &syscall_number);
  INCREMENT_EVENT_DATA_INDEX(index);

  u64 field = bpf_ktime_get_ns();
  perf_event_data.update(&index, &field);
  INCREMENT_EVENT_DATA_INDEX(index);

  field = bpf_get_current_pid_tgid();
  perf_event_data.update(&index, &field);
  INCREMENT_EVENT_DATA_INDEX(index);

  field = bpf_get_current_uid_gid();
  perf_event_data.update(&index, &field);
  INCREMENT_EVENT_DATA_INDEX(index);

  if (save_exit_code == true) {
    field = (u64)exit_code;
    perf_event_data.update(&index, &field);
    INCREMENT_EVENT_DATA_INDEX(index);
  }

  initial_slot = index; // re-use the same var to avoid wasting stack space
  perf_cpu_index.update(&index_key, &initial_slot);

  return event_index;
}

/// Saves the given string into the per-cpu map
static int saveStringBuffer(const char* buffer, const int buffer_size) {
  int index_key = 0U;
  u64 initial_slot = 0U;
  u64* index_ptr = perf_cpu_index.lookup_or_init(&index_key, &initial_slot);
  int index = (index_ptr != NULL ? *index_ptr : initial_slot);

#pragma unroll
  for (int i = 0; i < buffer_size / 8; i++) {
    perf_event_data.update(&index, (u64*)&buffer[i * 8]);
    INCREMENT_EVENT_DATA_INDEX(index);
  }

  initial_slot = index; // re-use the same var to avoid wasting stack space
  perf_cpu_index.update(&index_key, &initial_slot);

  return 0;
}

/// Saves the string pointed to by the given address into the per-cpu map
static bool saveString(char* buffer,
                       const int buffer_size,
                       const char* address) {
  if (address == NULL) {
    return false;
  }

  bpf_probe_read(buffer, buffer_size, address);
  saveStringBuffer(buffer, buffer_size);

  return true;
}

/// Saves the truncation identifier into the per-cpu map; used for varargs
/// functions likes execve
static int emitVarargsTerminator(bool truncated) {
  int index_key = 0U;
  u64 initial_slot = 0U;
  u64* index_ptr = perf_cpu_index.lookup_or_init(&index_key, &initial_slot);
  int index = (index_ptr != NULL ? *index_ptr : initial_slot);

  u64 terminator = truncated == true ? VARARGS_TRUNCATION : VARARGS_TERMINATOR;
  perf_event_data.update(&index, &terminator);
  INCREMENT_EVENT_DATA_INDEX(index);

  initial_slot = index; // re-use the same var to avoid wasting stack space
  perf_cpu_index.update(&index_key, &initial_slot);

  return 0;
}

/// Saves the given value to the per-cpu buffer; only use after the header
/// has been sent
#define saveSignedInteger saveEventValue
#define saveUnsignedInteger saveEventValue
static int saveEventValue(u64 value) {
  int index_key = 0U;
  u64 initial_slot = 0U;
  u64* index_ptr = perf_cpu_index.lookup_or_init(&index_key, &initial_slot);
  int index = (index_ptr != NULL ? *index_ptr : initial_slot);

  perf_event_data.update(&index, &value);
  INCREMENT_EVENT_DATA_INDEX(index);

  initial_slot = index; // re-use the same var to avoid wasting stack space
  perf_cpu_index.update(&index_key, &initial_slot);

  return 0;
}

static int saveStringList(char* buffer,
                          const int buffer_size,
                          const char* const* string_list,
                          const int string_list_size) {
  const char* argument_ptr = NULL;

#pragma unroll
  for (int i = 1; i < string_list_size; i++) {
    bpf_probe_read(&argument_ptr, sizeof(argument_ptr), &string_list[i]);
    if (saveString(buffer, buffer_size, argument_ptr) == false) {
      goto emit_terminator;
    }
  }

  goto emit_truncation;

emit_truncation:
  emitVarargsTerminator(true);
  return 0;

emit_terminator:
  emitVarargsTerminator(false);
  return 0;
}