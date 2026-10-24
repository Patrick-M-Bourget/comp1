// MeterLabel: a meter's label, named by its text in uppercase, and skipped by a screen reader, which reads the meter's
// own title instead.

#include "MeterLabel.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("A meter label is named by its text in uppercase")
{
    const comp1::MeterLabel label { "Gate" };
    CHECK (label.getName() == "GATE");
}

TEST_CASE ("A meter label is hidden from a screen reader")
{
    comp1::MeterLabel label { "Out" };
    CHECK_FALSE (label.isAccessible());
    CHECK (label.getAccessibilityHandler() == nullptr);
}
