/*
 * Copyright (C) 2022 Open Source Robotics Foundation
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
 *
 */

#define CATCH_CONFIG_MAIN
#include <rmf_utils/catch.hpp>

#include "test/utils.hpp"
#include "SqliteDataSource.hpp"

#include <rmf_scheduler_msgs/msg/schedule.hpp>
#include <rmf_scheduler_msgs/msg/schedule_state.hpp>
#include <rmf_scheduler_msgs/msg/trigger.hpp>
#include <rmf_scheduler_msgs/msg/trigger_state.hpp>

#include <filesystem>

namespace rmf::scheduler::test {

std::string make_trigger_name(int8_t status)
{
  return "trigger_status_" + std::to_string(status);
}

std::string make_schedule_name(int8_t status)
{
  return "schedule_status_" + std::to_string(status);
}

TEST_CASE("all in one")
{
  std::string db_file{"SqliteDataSource_TEST.sqlite3"};
  std::filesystem::remove(db_file);
  SqliteDataSource store{db_file};

  /**
   * Populate database
   */
  std::array<int8_t, 4> trigger_statuses{
    rmf_scheduler_msgs::msg::TriggerState::STARTED,
    rmf_scheduler_msgs::msg::TriggerState::FINISHED,
    rmf_scheduler_msgs::msg::TriggerState::CANCELLED,
    rmf_scheduler_msgs::msg::TriggerState::FAILED
  };

  auto make_trigger = [&store](int8_t status)
    {
      std::string name = make_trigger_name(status);
      rmf_scheduler_msgs::msg::Trigger trigger;
      trigger.name = name;
      trigger.payload.data.push_back(1);

      rmf_scheduler_msgs::msg::TriggerState state;
      state.name = name;
      state.status = status;

      store.create_trigger(trigger, state);
    };

  for (auto status : trigger_statuses)
  {
    make_trigger(status);
  }

  std::array<int8_t, 5> schedule_statuses{
    rmf_scheduler_msgs::msg::ScheduleState::CREATED,
    rmf_scheduler_msgs::msg::ScheduleState::STARTED,
    rmf_scheduler_msgs::msg::ScheduleState::FINISHED,
    rmf_scheduler_msgs::msg::ScheduleState::CANCELLED,
    rmf_scheduler_msgs::msg::ScheduleState::FAILED
  };

  auto make_schedule = [&store](int8_t status)
    {
      std::string name = make_schedule_name(status);
      rmf_scheduler_msgs::msg::Schedule schedule;
      schedule.name = name;
      schedule.payload.data.push_back(1);

      rmf_scheduler_msgs::msg::ScheduleState state;
      state.name = name;
      state.status = status;

      store.create_schedule(schedule, state);
    };

  for (auto status : schedule_statuses)
  {
    make_schedule(status);
  }

  {
    std::string name = "trigger_late";
    rmf_scheduler_msgs::msg::Trigger trigger;
    trigger.name = name;
    trigger.created_at = 1000;
    trigger.payload.data.push_back(1);

    rmf_scheduler_msgs::msg::TriggerState state;
    state.name = name;
    state.last_modified = 1000;

    store.create_trigger(trigger, state);
  }

  {
    std::string name = "schedule_late";
    rmf_scheduler_msgs::msg::Schedule schedule;
    schedule.name = name;
    schedule.created_at = 1000;
    schedule.payload.data.push_back(1);

    rmf_scheduler_msgs::msg::ScheduleState state;
    state.name = name;
    state.last_modified = 1000;

    store.create_schedule(schedule, state);
  }

  /**
   * Fetch tests
   */
  {
    auto name =
      make_trigger_name(rmf_scheduler_msgs::msg::TriggerState::FAILED);
    auto trigger = store.fetch_trigger(name).value();
    CHECK(trigger.name == name);

    auto state = store.fetch_trigger_state(name).value();
    CHECK(state.name == name);
  }

  {
    auto name = make_schedule_name(
      rmf_scheduler_msgs::msg::ScheduleState::FAILED);
    auto schedule = store.fetch_schedule(name).value();
    CHECK(schedule.name == name);

    auto state = store.fetch_schedule_state(name).value();
    CHECK(state.name == name);
  }

  {
    auto triggers = store.fetch_active_triggers().to_vec();
    REQUIRE(triggers.size() == 1);
    auto trigger = triggers.front();
    CHECK(trigger.name ==
      make_trigger_name(rmf_scheduler_msgs::msg::TriggerState::STARTED));
  }

  {
    auto schedules = store.fetch_active_schedules().to_vec();
    REQUIRE(schedules.size() == 2);
    size_t started_count = 0;
    size_t created_count = 0;
    for (const auto& schedule : schedules)
    {
      auto state = store.fetch_schedule_state(schedule.name).value();
      CHECK((
          state.status == rmf_scheduler_msgs::msg::ScheduleState::STARTED ||
          state.status == rmf_scheduler_msgs::msg::ScheduleState::CREATED));
      switch (state.status)
      {
        case rmf_scheduler_msgs::msg::ScheduleState::STARTED:
          ++started_count;
          break;
        case rmf_scheduler_msgs::msg::ScheduleState::CREATED:
          ++created_count;
          break;
        default:
          break;
      }
    }
    CHECK(started_count == 1);
    CHECK(created_count == 1);
  }

  {
    auto triggers = store.fetch_triggers_created_after(999).to_vec();
    REQUIRE(triggers.size() == 1);
    CHECK(triggers.front().name == "trigger_late");
  }

  {
    auto states = store.fetch_trigger_states_modified_after(999).to_vec();
    REQUIRE(states.size() == 1);
    CHECK(states.front().name == "trigger_late");
  }

  {
    auto schedules = store.fetch_schedules_created_after(999).to_vec();
    REQUIRE(schedules.size() == 1);
    CHECK(schedules.front().name == "schedule_late");
  }

  {
    auto states = store.fetch_schedule_states_modified_after(999).to_vec();
    REQUIRE(states.size() == 1);
    CHECK(states.front().name == "schedule_late");
  }
}

TEST_CASE("transactions rollback on exception")
{
  SqliteDataSource store{":memory:"};

  CHECK_THROWS([&store]()
    {
      auto t = store.begin_transaction();
      rmf_scheduler_msgs::msg::Trigger trigger;
      trigger.name = "trigger";
      trigger.payload.data.push_back(1);
      rmf_scheduler_msgs::msg::TriggerState state;
      state.name = "trigger";
      store.create_trigger(trigger, state);
      throw std::runtime_error("test");
    } ());

  // transaction should be rolled back
  CHECK(!store.fetch_trigger("trigger").has_value());
}

TEST_CASE("rolling back root transaction should rollback all changes")
{
  SqliteDataSource store{":memory:"};

  {
    auto t1 = store.begin_transaction();

    {
      auto t2 = store.begin_transaction();
      rmf_scheduler_msgs::msg::Trigger trigger;
      trigger.name = "trigger";
      trigger.payload.data.push_back(1);
      rmf_scheduler_msgs::msg::TriggerState state;
      state.name = "trigger";
      store.create_trigger(trigger, state);
      t2.commit();
    }
    t1.rollback();

    // even though t2 is committed, because it is not the root transaction,
    // it should have no effect. Rolling back t1 should rollback the trigger as well.
    CHECK(!store.fetch_trigger("trigger").has_value());
  }
}

}
