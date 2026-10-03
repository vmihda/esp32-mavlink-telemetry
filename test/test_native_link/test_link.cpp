#include <unity.h>

#include "link_monitor.h"

void setUp() {}
void tearDown() {}

void test_starts_waiting() {
    LinkMonitor m(3000);
    TEST_ASSERT_EQUAL(LinkState::Waiting, m.state());
    TEST_ASSERT_EQUAL(LinkEvent::None, m.update(10000));
    TEST_ASSERT_EQUAL(LinkState::Waiting, m.state());
}

void test_first_heartbeat_connects() {
    LinkMonitor m(3000);
    TEST_ASSERT_EQUAL(LinkEvent::Connected, m.onHeartbeat(100));
    TEST_ASSERT_EQUAL(LinkState::Connected, m.state());
    TEST_ASSERT_EQUAL(LinkEvent::None, m.onHeartbeat(1100));
}

void test_lost_reported_once_after_timeout() {
    LinkMonitor m(3000);
    m.onHeartbeat(1000);
    TEST_ASSERT_EQUAL(LinkEvent::None, m.update(4000));
    TEST_ASSERT_EQUAL(LinkEvent::Lost, m.update(4001));
    TEST_ASSERT_EQUAL(LinkState::Lost, m.state());
    TEST_ASSERT_EQUAL(LinkEvent::None, m.update(9000));
    TEST_ASSERT_EQUAL_UINT32(1, m.lostCount());
}

void test_heartbeat_after_loss_restores() {
    LinkMonitor m(3000);
    m.onHeartbeat(1000);
    m.update(5000);
    TEST_ASSERT_EQUAL(LinkEvent::Restored, m.onHeartbeat(6000));
    TEST_ASSERT_EQUAL(LinkState::Connected, m.state());
    TEST_ASSERT_EQUAL(LinkEvent::None, m.update(8000));
}

void test_millis_wraparound() {
    LinkMonitor m(3000);
    m.onHeartbeat(0xFFFFFF00u);
    // 0x100 + 0x100 = 512 ms elapsed across the wrap: still connected
    TEST_ASSERT_EQUAL(LinkEvent::None, m.update(0x00000100u));
    TEST_ASSERT_EQUAL(LinkEvent::Lost, m.update(0x00000C00u));
}

void test_state_names() {
    TEST_ASSERT_EQUAL_STRING("WAITING", linkStateName(LinkState::Waiting));
    TEST_ASSERT_EQUAL_STRING("CONNECTED", linkStateName(LinkState::Connected));
    TEST_ASSERT_EQUAL_STRING("LOST", linkStateName(LinkState::Lost));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_waiting);
    RUN_TEST(test_first_heartbeat_connects);
    RUN_TEST(test_lost_reported_once_after_timeout);
    RUN_TEST(test_heartbeat_after_loss_restores);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
