//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"

#include "td/utils/Container.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <map>

namespace td {

class DcOptionsSet {
 public:
  void add_dc_options(DcOptions dc_options);

  DcOptions get_dc_options() const;

  struct Stat {
    double ok_at{-1000};
    double error_at{-1001};
    double check_at{-1002};
    enum class State : int32 { Ok, Error, Checking };

    void on_ok() {
      ok_at = Time::now_cached();
    }
    void on_error() {
      error_at = Time::now_cached();
    }
    void on_check() {
      check_at = Time::now_cached();
    }
    bool is_ok() const {
      return state() == State::Ok;
    }
    State state() const {
      if (ok_at > error_at && ok_at > check_at) {
        return State::Ok;
      }
      if (check_at > ok_at && check_at > error_at) {
        return State::Checking;
      }
      return State::Error;
    }
  };

  struct ConnectionInfo {
    DcOption *option{nullptr};
    bool use_http{false};
    size_t order{0};
    bool should_check{false};
    Stat *stat{nullptr};
  };

  vector<ConnectionInfo> find_all_connections(DcId dc_id, bool allow_media_only, bool use_static, bool prefer_ipv6,
                                              bool only_http);

  Result<ConnectionInfo> find_connection(DcId dc_id, bool allow_media_only, bool use_static, bool prefer_ipv6,
                                         bool only_http);
  void reset();

 private:
  enum class State : int32 { Error, Ok, Checking };

  struct OptionStat {
    Stat tcp_stat;
    Stat http_stat;
  };

  struct DcOptionInfo {
    DcOption option;
    int64 stat_id = -1;
    size_t pos;
    size_t order = 0;

    DcOptionInfo(DcOption &&option, size_t pos) : option(std::move(option)), pos(pos) {
    }
  };

  struct DcOptionId {
    size_t pos;
    auto as_tie() const {
      return pos;
    }
    bool operator==(const DcOptionId &other) const {
      return as_tie() == other.as_tie();
    }
    bool operator<(const DcOptionId &other) const {
      return as_tie() < other.as_tie();
    }
  };

  std::vector<unique_ptr<DcOptionInfo>> options_;
  std::vector<DcOptionId> ordered_options_;
  std::map<IPAddress, int64> option_to_stat_id_;
  Container<unique_ptr<OptionStat>> option_stats_;

  DcOptionInfo *register_dc_option(DcOption &&option);
  void init_option_stat(DcOptionInfo *option_info);
  OptionStat *get_option_stat(const DcOptionInfo *option_info);
};

}  // namespace td
