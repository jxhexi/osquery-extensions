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

#pragma once

#include <BPF.h>

#include <bcc_probe_fork_events.h>
#include <fork_events/fork_events.h>

#include <bcc_probe_exec_events.h>
#include <exec_events/exec_events.h>

#include <boost/variant.hpp>
#include <osquery/sdk.h>

namespace trailofbits {
struct EventHeader final {
  enum class Type : std::uint32_t {
    SysEnterClone = EVENTID_SYSENTERCLONE,
    SysExitClone = EVENTID_SYSEXITCLONE,

    SysEnterFork = EVENTID_SYSENTERFORK,
    SysExitFork = EVENTID_SYSEXITFORK,

    SysEnterVfork = EVENTID_SYSENTERVFORK,
    SysExitVfork = EVENTID_SYSEXITVFORK,

    SysEnterExecve = EVENTID_SYSENTEREXECVE,
    SysEnterExecveat = EVENTID_SYSENTEREXECVEAT,

    KprobePidvnr = EVENTID_PIDVNR
  };

  Type type;
  std::uint64_t timestamp;
  pid_t pid;
  pid_t tgid;
  uid_t uid;
  gid_t gid;
};

struct Event final {
  struct ExecData final {
    std::string filename;
    std::vector<std::string> argv;
    bool argv_truncated{false};
  };

  struct PidVnrData final {
    std::size_t namespace_count;
    pid_t host_pid;
    std::vector<pid_t> namespaced_pid_list;
  };

  EventHeader header;
  boost::variant<PidVnrData, ExecData> data;
};

class BCCProcessEventsProgram;
using BCCProcessEventsProgramRef = std::unique_ptr<BCCProcessEventsProgram>;

class BCCProcessEventsProgram final {
  BCCProcessEventsProgram();

  void detachKprobes();
  void detachTracepoints();

 protected:
  struct PrivateData;
  std::unique_ptr<PrivateData> d;

  void processPerfEvent(
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      const std::uint32_t* event_identifiers,
      std::size_t event_identifier_count);

 public:
  static osquery::Status create(BCCProcessEventsProgramRef& object);
  ~BCCProcessEventsProgram();

  void update();

  template <typename T>
  static void readEventData(
      T& value,
      int& current_index,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::size_t cpu_index) {
    value = {};

    std::vector<std::uint64_t> table_data = {};
    auto status = event_data_table.get_value(current_index, table_data);
    if (status.code() != 0) {
      throw osquery::Status::failure(status.msg());
    }

    if (cpu_index >= table_data.size()) {
      throw osquery::Status::failure("Invalid CPU index");
    }

    value = static_cast<T>(table_data[cpu_index]);
    INCREMENT_EVENT_DATA_INDEX(current_index);
  }

  static osquery::Status readEventHeader(
      EventHeader& event_header,
      int& current_index,
      std::size_t& cpu_index,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::uint32_t event_identifier);

  static osquery::Status readStringEventData(
      std::string& string_data,
      int& current_index,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::size_t cpu_index);

  static osquery::Status readExecEventData(
      Event::ExecData& exec_data,
      int& current_index,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::size_t cpu_index);

  static osquery::Status readPidVnrEventData(
      Event::PidVnrData& pidvnr_data,
      int& current_index,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::size_t cpu_index);

  static osquery::Status readEvent(
      Event& event,
      ebpf::BPFPercpuArrayTable<std::uint64_t>& event_data_table,
      std::uint32_t event_identifier);

  static void forkPerfEventHandler(void* this_ptr, void* data, int data_size);
  static void execPerfEventHandler(void* this_ptr, void* data, int data_size);

  BCCProcessEventsProgram(const BCCProcessEventsProgram& other) = delete;
  BCCProcessEventsProgram& operator=(const BCCProcessEventsProgram& other) =
      delete;
};
} // namespace trailofbits