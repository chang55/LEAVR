#include "app/state_machine.h"
#include "media/eis/eis_processor.h"
#include "media/frame_utils.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

namespace {

void TestAnnexBSplit() {
    const uint8_t stream[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x11, 0x22,
        0x00, 0x00, 0x01, 0x68, 0x33,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x44, 0x55,
    };
    const auto nalus = leavr::SplitAnnexBNalus(stream, sizeof(stream));
    assert(nalus.size() == 3);
    assert(nalus[0].size == 3 && nalus[0].data[0] == 0x67);
    assert(nalus[1].size == 2 && nalus[1].data[0] == 0x68);
    assert(nalus[2].size == 3 && nalus[2].data[0] == 0x65);
    assert(leavr::SplitAnnexBNalus(nullptr, 0).empty());
}

void TestStateMachineKeepsRegisteredAction() {
    leavr::StateMachine machine;
    bool called = false;
    machine.RegisterTransition(
        DEVICE_STATE_BOOT, EVENT_INIT_DONE, DEVICE_STATE_STANDBY,
        [&called]() {
            called = true;
            return LEAVR_OK;
        });

    assert(machine.Init() == LEAVR_OK);
    assert(machine.PostEvent(EVENT_INIT_DONE) == LEAVR_OK);
    assert(called);
    assert(machine.GetState() == DEVICE_STATE_STANDBY);
}

void TestEisCropBounds() {
    leavr::EisProcessor eis;
    assert(eis.Init(2560, 1440, 2304, 1296) == LEAVR_OK);
    assert(eis.Start() == LEAVR_OK);

    EisFrameData sample = {};
    uint64_t timestamp = 1000000;
    for (int i = 0; i < 200; ++i) {
        sample.timestamp_us = timestamp;
        timestamp += 5000;
        assert(eis.PushGyroData(sample) == LEAVR_OK);
    }
    for (int i = 0; i < 120; ++i) {
        sample.gyro_x = 1.5f;
        sample.gyro_y = -1.5f;
        sample.timestamp_us = timestamp;
        timestamp += 5000;
        const int ret = eis.PushGyroData(sample);
        assert(ret == LEAVR_OK || ret == LEAVR_ERR_BUSY);
        usleep(500);
    }

    usleep(20000);
    const EisCropWindow crop = eis.GetCropWindow();
    assert(crop.width == 2304 && crop.height == 1296);
    assert(crop.x >= 0 && crop.x + crop.width <= 2560);
    assert(crop.y >= 0 && crop.y + crop.height <= 1440);
    assert(eis.Stop() == LEAVR_OK);
}

} // namespace

int main() {
    TestAnnexBSplit();
    TestStateMachineKeepsRegisteredAction();
    TestEisCropBounds();
    std::puts("core tests passed");
    return 0;
}
